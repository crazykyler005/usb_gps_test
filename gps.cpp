#include "gps.hpp"
#include "character.h"
#include "lat-long.hpp"
#include "main-cm4-task.h"
#include "nmea0183.hpp"
#include "osal.h"
#include "serial-port.h"
#include <time.h>

#include <stdio.h>
#include <string.h>
#include <mutex>
#include <thread>
using namespace std;

/*
    Note:
        gps can be enabled only when the 5V supply is off
*/


#define CHIP_ON     0
#define CHIP_OFF    1


static const uint16_t    MaxMinutesOn             = 180 ;// 3 hours                                              tbd
static const uint16_t    MinutesUpdateInterval =  10 ;   // (longer interval allows for better lat/long fix)     tbd

static uint16_t minutesOn ;
static uint8_t lastUpdateMinutes ;


typedef enum { GpsBusy, GpsSucceeded, GpsFailed, GpsStopped } Status ;

static struct {
    Status          status ;
    struct tm       data ;
    bool            includeRtcUpdate ;
} dateTime ;


static struct {
    Status          status ;
    LatLongString   data ;
} latLong ;

static std::mutex m;
static Mutex            busy ;

static void get_local_time(){

    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);
    
    printf("now: %d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static uint16_t cksum_FletcherAlgorithm (uint8_t * data, uint8_t length)
{
    uint8_t ck_a = 0 ;
    uint8_t ck_b = 0 ;

    while (length --)
    {
        ck_a += * data ++ ;
        ck_b += ck_a ;
    }

    return ck_b * 256 + ck_a ;
}


void txMessage_UBX_MGA_INI_TIME_UTC (SerialPort * port)
{
    uint8_t message [32] ;
    uint8_t * msg = message ;

    time_t t = time(NULL);
    struct tm tm = *gmtime(&t);

    * msg ++ = 0xb5 ;   // sync
    * msg ++ = 0x62 ;   // sync

    * msg ++ = 0x13 ;   // class
    * msg ++ = 0x40 ;   // id

    * msg ++ = 24 ;     // payload length lsb
    * msg ++ =  0 ;     // payload length msb


    // payload (24 bytes) ...

    * msg ++ = 0x10 ;   // type
    * msg ++ = 0x00 ;   // version

    * msg ++ = 0x00 ;   // time reference

    * msg ++ = 0x80 ;   // leap seconds unknown

    uint16_t year = tm.tm_year ;
    * msg ++ = year  & 0xff ;   // lsb
    * msg ++ = year >>    8 ;   // msb

    * msg ++ = tm.tm_mon ;
    * msg ++ = tm.tm_mday ;

    * msg ++ = tm.tm_hour ;
    * msg ++ = tm.tm_min ;
    * msg ++ = tm.tm_sec ;

    * msg ++ = 0 ;      // reserved

    // nanoseconds (0 .. 999999999)
    * msg ++ = 0 ;
    * msg ++ = 0 ;
    * msg ++ = 0 ;
    * msg ++ = 0 ;

    // seconds part of time accuracy
    * msg ++ = 0 ;
    * msg ++ = 0 ;

    * msg ++ = 0 ;      // reserved
    * msg ++ = 0 ;      // reserved

    // nanoseconds part of time accuracy
    * msg ++ = 0 ;
    * msg ++ = 0 ;
    * msg ++ = 0 ;
    * msg ++ = 0 ;


    // cksum ...

    uint16_t cksum = cksum_FletcherAlgorithm (& message [2], sizeof (message) - 4) ;

    * msg ++ = cksum  & 0xff ;      // lsb
    * msg ++ = cksum >>    8 ;      // msb

    // transmit the packet to the gps chip
    msg = message ;
    uint8_t length = sizeof (message) ;
    while (length --)
        serialPort_txByte (port, * msg ++) ;
}


static void initiateAcquisition (void)
{
    lastUpdateMinutes = tm.tm_min % 60 ;

    minutesOn = 0 ;

    gps_open ();

    printf ("gps started") ;
}


static void terminateAcquisition (void)
{
    gps_close ();

    printf ("gps stopped") ;
}



void gps_turnOff (void)
{
    m.lock();
    mutex_get (& busy, OSAL_WAIT_FOREVER) ;

    if ((dateTime.status == GpsBusy) || (latLong.status == GpsBusy))
    {
        terminateAcquisition () ;

        if (dateTime.status == GpsBusy)
            dateTime.status  = GpsStopped ;

        if (latLong.status == GpsBusy)
            latLong.status  = GpsStopped ;
    }

    mutex_release (& busy) ;
    m.unlock()
}



void gps_updateAcquisition (void)
{

    if ((dateTime.status != GpsBusy) && (latLong.status != GpsBusy))
        return ;


    time_t t = time(NULL);
    struct tm timeNow = *gmtime(&t);

    timeNow.tm_min %= 60 ;


    // determine if it is time to do an update

    uint8_t minutesDelta = (timeNow.tm_min - lastUpdateMinutes + 60) % 60 ;  // handle rollover

    if (minutesDelta > MinutesUpdateInterval)
        // somebody must have messed with the RTC
        minutesDelta = MinutesUpdateInterval ;

    if (minutesDelta < MinutesUpdateInterval)
        return ;


    // do an update ...

    mutex_get (& busy, OSAL_WAIT_FOREVER) ;

    lastUpdateMinutes = timeNow.tm_min ;
    minutesOn += minutesDelta ;


    {
        // must set a long timeout, since the clock may be altered and that would affect the timeout
///     peripheralClocks_setSecondsTimeout (180, PeripheralClock_Dependent_GPS) ;

        SerialPort * gps_port = serialPort_open (SerialPort_GPS) ;
        serialPort_setBaudRate (gps_port, 9600);

        uint8_t maxSeconds = 5 ;
        nmea0183_updateFromStream (gps_port, maxSeconds);

        serialPort_close (SerialPort_GPS) ;

///     peripheralClocks_setSecondsTimeout (0, PeripheralClock_Dependent_GPS) ;
    }

    if ((dateTime.status == GpsBusy) && nmea0183_isDateTimeValid ())
    {
        dateTime.status = GpsSucceeded ;
        nmea0183_getDateAndTime (& dateTime.data);

        if (dateTime.includeRtcUpdate)
        {
            // update the rtc
            printf ("setting rtc from gps...");
            // k_sleep(2000)
            // alarmClock_setDateAndTime (& dateTime.data) ;
            // main_resetAlarm ();
        }

        printf ("gps date/time acquired after %d minutes", minutesOn) ;
    }


    if ((latLong.status == GpsBusy) && nmea0183_isLatLongValid () && nmea0183_isDateTimeValid ())
    {
        latLong.status = GpsSucceeded ;
        nmea0183_getDateAndTime (& dateTime.data);
        strncpy (latLong.data, nmea0183_getLatLongString (), sizeof (latLong.data));

        printf ("gps lat/long acquired after %d minutes", minutesOn);
    }


    if ((dateTime.status != GpsBusy) && (latLong.status != GpsBusy))
    {
        // success
        terminateAcquisition ();
        mutex_release (& busy) ;
        return ;
    }


    if (minutesOn >= MaxMinutesOn)
    {
        // timeout

        printf ("gps acquisition timed out after %d minutes", minutesOn) ;

        terminateAcquisition ();

        if (dateTime.status == GpsBusy)
        {
            dateTime.status =  GpsFailed ;
            printf ("gps date/time acquisition failed") ;
        }

        if (latLong.status == GpsBusy)
        {
            latLong.status =  GpsFailed ;
            printf ("gps lat/long acquisition failed") ;
        }
    }


    mutex_release (& busy) ;
}



void gps_initiateDateTimeAcquisition (bool includeRtcUpdate)
{
    if (dateTime.status == GpsBusy)
        return ;

    dateTime.includeRtcUpdate = includeRtcUpdate ;

    dateTime.status = GpsBusy ;
    memset ((uint8_t *) & dateTime.data, 0, sizeof (dateTime.data)) ;

    initiateAcquisition () ;
}


void gps_initiateLatLongAcquisition (void)
{
    if (latLong.status == GpsBusy)
        return ;

    latLong.status = GpsBusy ;
    memset ((uint8_t *) &  latLong.data, 0, sizeof ( latLong.data)) ;

    initiateAcquisition () ;
}


bool gps_dateTimeAcquisitionBusy      (void) { return dateTime.status == GpsBusy ; }
bool gps_latLongAcquisitionBusy       (void) { return  latLong.status == GpsBusy ; }

bool gps_dateTimeAcquisitionSucceeded (void) { return dateTime.status == GpsSucceeded ; }
bool gps_latLongAcquisitionSucceeded  (void) { return  latLong.status == GpsSucceeded ; }

struct tm * gps_getDateTime        (void) { return & dateTime.data ; }
string      gps_getLatLongString   (void) { return    latLong.data ; }


void gps_close (void)
{

    serialPort_close (SerialPort_GPS) ;

    // power it down
    gpio_set (GPS_EN_N, CHIP_OFF) ;
}



void gps_open (void)
{

    // power it up and take it out of reset
    // gpio_set (GPS_RESET_N, 0) ;
    // time_delayMilliseconds (5) ;
    // gpio_set (GPS_EN_N, CHIP_ON) ;
    // time_delayMilliseconds (50) ;
    // gpio_set (GPS_RESET_N, 1) ;

    // gpio_set (SONIC_EN, 0) ;
}


void gps_initialize (void)
{
    dateTime.status = latLong.status = GpsFailed ;

    memset ((uint8_t *) & dateTime.data, 0, sizeof (dateTime.data)) ;
    memset ((uint8_t *) &  latLong.data, 0, sizeof ( latLong.data)) ;

    mutex_initialize (& busy);

    // gpio_set (GPS_EN_N, CHIP_OFF) ;
    // gpio_set (GPS_RESET_N, 0) ;
}

void main(){
    gps_initialize()
    gps_

}


#if 0

> gps-location
2018/05/10 15:57:07 41 53.38568 N, 087 46.34652 W

19:03:58 gps lat/long acquired after 160 minutes

> gps-location
2018/05/10 19:03:57 41 53.39354 N, 087 46.35360 W

> gps-location
2018/05/10 19:44:01  41 53.38663 N, 087 46.34140 W




[

p. 37 of data sheet


11 Multiple GNSS Assistance (MGA)

11.1 Introduction

Users would ideally like GNSS receivers to provide accurate position
information the moment they are turned on. With standard GNSS receivers there
can be a significant delay in providing the first position fix, principally
because the receiver needs to obtain data from several satellites and the
satellites transmit that data slowly.  Under adverse signal conditions, data
downloads from the satellites to the receiver can take minutes, hours or even
fail altogether.

Assisted GNSS (A-GNSS) is a common solution to this problem and involves some
form of reference network of receivers that collect data such as ephemeris,
almanac, accurate time and satellite status and pass this onto to the target
receiver via any suitable communications link. Such assistance data enables the
receiver to compute a position within a few seconds, even under poor signal
conditions.

The UBX-MGA message class provides the means for delivering assistance data to
u-blox receivers and customers can obtain it from the u-blox AssistNow Online
or AssistNow Offline Services. Alternatively they can obtain assistance data
from third-party sources (e.g. SUPL/RRLP) and generate the appropriate UBX-MGA
messages to send this data to the receiver.



11.2 Assistance Data

u-blox receivers currently accept the following types of assistance data:


â€¢ Time: The current time can either be supplied as an inexact value via the
standard communication interfaces, suffering from latency depending on the baud
rate, or using hardware time synchronization where an accurate time pulse is
connected to an external interrupt.

The preferred option is to supply UTC time using the UBX-MGA-INI-TIME_UTC
message, but times referenced to some GNSS can be delivered with the
UBX-MGA-INI-TIME_GNSS message.


â€¢ Position: Estimated receiver position can be submitted to the receiver using
the UBX-MGA-INI-POS_XYZ or UBX-MGA-INI-POS_LLH messages.



UBX-MGA-INI-TIME_UTC

sync
    0xB5
    0x62
class
    0x13
id
    0x40
payload length
    24  lsb
     0  msb

payload
    byte
    offset
    0       0x10    type
    1       0x00    version
    2       0x00    reference (none)
    3       0x80    leap seconds unknown
    4       0xXXXX  4-digit year year   (0..65535)
    6       0xXX    month               (1..12)
    7       0xXX    day                 (1..31)
    8       0xXX    hour                (0..23)
    9       0xXX    minute              (0..59)
   10       0xXX    second              (0..59)
   11       0x00    reserved
   12       4-byte  nanoseconds (0 .. 999999999)
   16       2-byte  seconds part of time accuracy ??
   18       2-byte  reserved
   20       4-byte  nanoseconds part of time accuracy ??


payload checksum
    CK_A
    CK_B



ck_a = 0
ck_b = 0
for (i = 0 ; i < n ; i ++)
{
    ck_a = ck_a + buffer [i]
    ck_b = ck_b + ck_a
}

]






$GNRMC,165947.00,A,4153.38633,N,08746.35785,W,0.114,,120520,,,A*7B

$GNVTG,,T,,M,0.114,N,0.211,K,A*3B

$GNGGA,165947.00,4153.38633,N,08746.35785,W,1,12,0.89,203.4,M,-33.8,M,,*75

$GNGSA,A,3,08,11,13,07,28,01,30,,,,,,1.75,0.89,1.50*1F
$GNGSA,A,3,88,78,81,80,79,82,,,,,,,1.75,0.89,1.50*10

$GPGSV,3,1,10,01,43,120,34,07,60,165,26,08,29,050,19,11,51,072,27*75
$GPGSV,3,2,10,13,24,301,25,15,05,323,,17,28,228,14,19,04,226,*70
$GPGSV,3,3,10,28,48,296,26,30,74,266,29*7E

$GLGSV,2,1,08,70,07,288,20,71,07,334,18,78,27,115,26,79,71,053,33*67
$GLGSV,2,2,08,80,34,320,29,81,73,073,30,82,44,190,20,88,23,032,23*63

$GNGLL,4153.38633,N,08746.35785,W,165947.00,A,A*62


#endif