
#include "nmea0183.hpp"

#include "character.h"
#include "monitor.h"
#include "osal.h"
#include "stopwatch.h"

#include <stdio.h>
#include <string.h>

#include <time.h>

static bool  latLongValid ;
static bool dateTimeValid ;

static LatLongString   latLongString ;
static struct tm dateTime

static char nmeaMessage [96];       // tbd - use a local variable instead?



static bool echo ;
void nmea0183_echoToMonitor (bool echoOrNot)
{
    echo = echoOrNot ;
}



bool nmea0183_isLatLongValid ()
{
    return latLongValid ;
}


bool nmea0183_isDateTimeValid ()
{
    return dateTimeValid ;
}



void nmea0183_getDateAndTime (struct tm * dateTimePtr)
{
    if (! dateTimeValid)
    {
        dateTime.tm_year =
        dateTime.tm_mon =
        dateTime.tm_mday =

        dateTime.tm_hour =
        dateTime.tm_min =
        dateTime.tm_sec = 0 ;
    }

    * dateTimePtr = dateTime ;
}



char * nmea0183_getLatLongString (void)
{
    if (! latLongValid)
        strcpy (latLongString, "");

    return latLongString ;
}



static bool checksumIsOk (string message)
{
    // *CS is checksum (8 bit exclusive OR of all data in the sentence, including ","
    // delimiters, between but not including the '$' and '*' delimiters.

    char * start = message ;

    // the first character must be '$'
    if (* start != '$')
        return FALSE ;

    // find the end of the message
    char * end = start + 1 ;
    while (1)
    {
        uint8_t endChar = * end ;

        if ((endChar == CarriageReturn) ||
            (endChar == Linefeed) ||
            (endChar == 0))
            break ;

        ++ end ;
    }

    // check minimum length
    uint8_t messageLength = end - start ;
    if (messageLength < 4)
        return FALSE ;

    // read the checksum
    end -= 2 ;
    unsigned int checksum ;
    uint8_t scanResult = sscanf (end, "%2x", & checksum);
    if (scanResult != 1)
        return FALSE ;

    // verify '*' character
    end -= 1 ;
    if (* end != '*')
        return FALSE ;

    // compute the checksum
    end -= 1 ;
    while (end != start)
        checksum ^= * end -- ;

    return (checksum == 0) ;
}



static char * strtok_save_ptr ;

static string getNextField (void)
{
    string tokenPtr = strtok_r (NULL, ",", & strtok_save_ptr);
    return tokenPtr != 0 ? tokenPtr : "" ;
}


void nmea0183_updateFromString (string message)
{
    if (echo)
    {
      #if 0
        serialPort_txString (monitorPort, message) ;
        serialPort_txString (monitorPort, "\r\n") ;
      #endif
    }

    if (! checksumIsOk (message))
    {
        latLongValid = dateTimeValid = FALSE ;
        return;
    }

    // make a copy of the given string, but with a space after each comma (because
    // strtok() considers consecutive delimiters to be a single delimiter)
    char copy [90] ;
    uint8_t length = 0 ;
    while (length < ArrayLength (copy) - 2)
    {
        char aChar = * message ++ ;
        if (aChar == 0)
            break ;
        copy [length ++] = aChar ;
        if (aChar == ',')
            copy [length ++] = ' ' ;
    }
    copy [length] = 0 ;


    char activeOrVoid ;

    unsigned int hours, minutes, seconds ;
    unsigned int day, month, year ;

    struct
    {
        char degrees [ 4] ;
        char minutes [10] ;
        char direction ;
    } latitude, longitude ;


    uint8_t scanResult ;

    // the first field must be "$GxRMC"
    string field = strtok_r (copy, ",", & strtok_save_ptr) ;
    char aChar ;
    scanResult = sscanf (field, "$G%*cRM%c", & aChar) ;
    if ((scanResult != 1) || (aChar != 'C'))
    {
        latLongValid = dateTimeValid = FALSE ;
        return;
    }


    latLongValid = dateTimeValid = TRUE ;


    // the next field contains hours, minutes, seconds and maybe hundredths of seconds
    scanResult = sscanf (getNextField(), " %2d%2d%2d", & hours, & minutes, & seconds) ;
    if (scanResult != 3)
        dateTimeValid = FALSE ;

    // the next field is status A:active or V:void
    scanResult = sscanf (getNextField(), " %c", & activeOrVoid) ;
    if  (scanResult != 1)
    {
        latLongValid = dateTimeValid = FALSE ;
        return ;
    }

    if (activeOrVoid != 'A')
      #if 1
        latLongValid =                 FALSE ;      // don't invalidate the date/time
      #else
        latLongValid = dateTimeValid = FALSE ;
      #endif


    // the next field is latitude
    scanResult = sscanf (getNextField(), " %2s%s", latitude.degrees, latitude.minutes) ;
    if (scanResult != 2)
        latLongValid = FALSE ;

    // the next field is latitude direction
    scanResult = sscanf (getNextField(), " %c", & latitude.direction) ;
    if ((scanResult != 1) || ((latitude.direction != 'N') && (latitude.direction != 'S')))
        latLongValid = FALSE ;


    // the next field is longitude
    scanResult = sscanf (getNextField(), " %3s%s", longitude.degrees, longitude.minutes) ;
    if (scanResult != 2)
        latLongValid = FALSE ;

    // the next field is longitude direction
    scanResult = sscanf (getNextField(), " %c", & longitude.direction) ;
    if ((scanResult != 1) || ((longitude.direction != 'E') && (longitude.direction != 'W')))
        latLongValid = FALSE ;


    // skip ground speed
    getNextField () ;

    // skip track angle
    getNextField () ;

    // the next field contains day, month, year
    scanResult = sscanf (getNextField(), " %2d%2d%2d", & day, & month, & year) ;
    if (scanResult != 3)
    {
        dateTimeValid = FALSE ;
        return ;
    }


    if (dateTimeValid)
    {

        dateTime.tm_year = year; //+ 2000
        dateTime.tm_mon = month;
        dateTime.tm_mday = day;

        dateTime.tm_hour = hours;
        dateTime.tm_min = minutes;
        dateTime.tm_sec = seconds;
    }


    if (latLongValid)
    {
        // save latitude/longitude in "48 02.391740 N, 123 03.672452 W" format
        snprintf (latLongString, sizeof (latLongString), "%s %s %c, %s %s %c",
                   latitude.degrees,  latitude.minutes,  latitude.direction,
                  longitude.degrees, longitude.minutes, longitude.direction);
    }

}



void nmea0183_updateFromStream (SerialPort * serialStream, uint16_t timeoutSeconds)
{
     latLongValid =
    dateTimeValid = FALSE ;

    Stopwatch timer ;
    stopwatch_initialize (& timer);

    uint8_t length ;
    char in ;

    while (1)
    {

        // wait for the start character
        while (1)
        {
            task_yield ();

            if (stopwatch_elapsedSeconds (& timer) > timeoutSeconds)
                return ;

            if (! serialPort_rxReady (serialStream))
                continue ;

            in = serialPort_rxByte (serialStream) ;
            if (in != '$')
                continue ;

            length = 0 ;
            nmeaMessage [length ++] = in ;
            break ;
        }

        // get the rest of the message
        while (length < ArrayLength (nmeaMessage) - 1)
        {
            task_yield ();

            if (stopwatch_elapsedSeconds (& timer) > timeoutSeconds)
                return ;

            if (! serialPort_rxReady (serialStream))
                continue ;

            in = serialPort_rxByte (serialStream) ;
          #if 0
            serialPort_txByte (serialStream, in) ;
          #endif

            if ((in == CarriageReturn) || (in == Linefeed) || (in == 0))
            {
                nmeaMessage [length ++] = 0 ;       // make zero-terminated string

                nmea0183_updateFromString (nmeaMessage);

                if (dateTimeValid)
                    return;

                // try the next line
                break;
            }
            else
            {
                nmeaMessage [length ++] = in ;
            }
        }

    }

}



void nmea0183_initialize (void)
{
     latLongValid =
    dateTimeValid = FALSE ;

    echo = FALSE ;
}



/*

NMEA 0183 info

    $GPRMC      Recommended Minimum Navigation Information
    $GPGGA      Global Positioning System Fix Data, Time, Position and fix-related data for a GPS receiver
    $GPGSV      Satellites in view
    $GPGLL      Geographic Position - Latitude/Longitude
    $GPGSA      GPS DOP and active satellites
    $GPVTG      Track made good and Ground speed



GGA - essential fix data which provide 3D location and accuracy data.

 $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47

Where:
     GGA          Global Positioning System Fix Data
     123519       Fix taken at 12:35:19 UTC
     4807.038,N   Latitude 48 deg 07.038' N
     01131.000,E  Longitude 11 deg 31.000' E
     1            Fix quality: 0 = invalid
                               1 = GPS fix (SPS)
                               2 = DGPS fix
                               3 = PPS fix
                               4 = Real Time Kinematic
                               5 = Float RTK
                               6 = estimated (dead reckoning) (2.3 feature)
                               7 = Manual input mode
                               8 = Simulation mode
     08           Number of satellites being tracked
     0.9          Horizontal dilution of position
     545.4,M      Altitude, Meters, above mean sea level
     46.9,M       Height of geoid (mean sea level) above WGS84 ellipsoid
     (empty field) time in seconds since last DGPS update
     (empty field) DGPS station ID number
     *47          the checksum data, always begins with *

If the height of geoid is missing then the altitude should be suspect. Some non-standard
implementations report altitude with respect to the ellipsoid rather than geoid altitude.
Some units do not report negative altitudes at all. This is the only sentence that reports
altitude.



$GPRMC

RMC - NMEA has its own version of essential gps pvt (position, velocity, time) data.
It is called RMC, The Recommended Minimum, which will look similar to:

$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A

Where:
     RMC          Recommended Minimum sentence C
     123519       Fix taken at 12:35:19 UTC
     A            Status A=active or V=Void.
     4807.038,N   Latitude 48 deg 07.038' N
     01131.000,E  Longitude 11 deg 31.000' E
     022.4        Speed over the ground in knots
     084.4        Track angle in degrees True
     230394       Date - 23rd of March 1994
     003.1,W      Magnetic Variation
     *6A          The checksum data, always begins with *

Note that, as of the 2.3 release of NMEA, there is a new field in the RMC sentence at the
end just prior to the checksum. For more information on this field see here.



$GPGSV

GSV - Satellites in View shows data about the satellites that the unit might be able
to find based on its viewing mask and almanac data. It also shows current ability to
track this data. Note that one GSV sentence only can provide data for up to 4 satellites
and thus there may need to be 3 sentences for the full information. It is reasonable
for the GSV sentence to contain more satellites than GGA might indicate since GSV may
include satellites that are not used as part of the solution. It is not a requirment
that the GSV sentences all appear in sequence. To avoid overloading the data bandwidth
some receivers may place the various sentences in totally different samples since each
sentence identifies which one it is.

The field called SNR (Signal to Noise Ratio) in the NMEA standard is often referred to
as signal strength. SNR is an indirect but more useful value that raw signal strength.
It can range from 0 to 99 and has units of dB according to the NMEA standard, but the
various manufacturers send different ranges of numbers with different starting numbers
so the values themselves cannot necessarily be used to evaluate different units. The
range of working values in a given gps will usually show a difference of about 25 to 35
between the lowest and highest values, however 0 is a special case and may be shown on
satellites that are in view but not being tracked.

  $GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75

Where:
      GSV          Satellites in view
      2            Number of sentences for full data
      1            sentence 1 of 2
      08           Number of satellites in view

      01           Satellite PRN number
      40           Elevation, degrees
      083          Azimuth, degrees
      46           SNR - higher is better
           for up to 4 satellites per sentence
      *75          the checksum data, always begins with *



$GPGLL

GLL - Geographic Latitude and Longitude is a holdover from Loran data and some old units
may not send the time and data active information if they are emulating Loran data. If a
gps is emulating Loran data they may use the LC Loran prefix instead of GP.

  $GPGLL,4916.45,N,12311.12,W,225444,A,*1D

Where:
     GLL          Geographic position, Latitude and Longitude
     4916.46,N    Latitude 49 deg. 16.45 min. North
     12311.12,W   Longitude 123 deg. 11.12 min. West
     225444       Fix taken at 22:54:44 UTC
     A            Data Active or V (void)
     *1D          checksum data

Note that, as of the 2.3 release of NMEA, there is a new field in the GLL sentence at the
end just prior to the checksum. For more information on this field see here.



$GPGSA

GSA - GPS DOP and active satellites. This sentence provides details on the nature of the fix.
It includes the numbers of the satellites being used in the current solution and the DOP.
DOP (dilution of precision) is an indication of the effect of satellite geometry on the accuracy
of the fix. It is a unitless number where smaller is better. For 3D fixes using 4 satellites
a 1.0 would be considered to be a perfect number, however for overdetermined solutions it is
possible to see numbers below 1.0.

There are differences in the way the PRN's are presented which can effect the ability of some
programs to display this data. For example, in the example shown below there are 5 satellites
in the solution and the null fields are scattered indicating that the almanac would show
satellites in the null positions that are not being used as part of this solution. Other
receivers might output all of the satellites used at the beginning of the sentence with the
null field all stacked up at the end. This difference accounts for some satellite display
programs not always being able to display the satellites being tracked. Some units may show
all satellites that have ephemeris data without regard to their use as part of the solution
but this is non-standard.

  $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39

Where:
     GSA      Satellite status
     A        Auto selection of 2D or 3D fix (M = manual)
     3        3D fix - values include: 1 = no fix
                                       2 = 2D fix
                                       3 = 3D fix
     04,05... PRNs of satellites used for fix (space for 12)
     2.5      PDOP (dilution of precision)
     1.3      Horizontal dilution of precision (HDOP)
     2.1      Vertical dilution of precision (VDOP)
     *39      the checksum data, always begins with *



$GPVTG

VTG - Velocity made good. The gps receiver may use the LC prefix instead of GP if it is emulating Loran output.

  $GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48

where:
        VTG          Track made good and ground speed
        054.7,T      True track made good (degrees)
        034.4,M      Magnetic track made good
        005.5,N      Ground speed, knots
        010.2,K      Ground speed, Kilometers per hour
        *48          Checksum

Note that, as of the 2.3 release of NMEA, there is a new field in the VTG sentence
at the end just prior to the checksum. For more information on this field see here.

Receivers that don't have a magnetic deviation (variation) table built in will null
out the Magnetic track made good.


*/


/*
sample messages

> gps-rx

$PTIGCD,1,1,CFWVER,P,CC4000,R,01.11.00,Jan 10 2012,14:21:23*19
$PTIGCD,1,1,GPSVER,C,02.00,5f,R,01.09.01.00,12/26/2008,P,31.85,01/19/2012*53

$GPGLL,4802.391740,N,12303.672452,W,180812.00,A,A*7C
$GPRMC,180812.00,A,4802.391740,N,12303.672452,W,0.0,0.0,030313,18.6,W,A*0F
$GPGGA,180812.00,4802.391740,N,12303.672452,W,1,04,5.8,187.2,M,-18.5,M,,*5D
$GPVTG,,T,,M,0.0,N,0.0,K,A*23
$GPGSA,A,2,03,06,19,11,,,,,,,,,5.9,5.8,0.8*37
$GPGSV,3,1,09,14,74,155,,03,21,227,33,18,33,083,,32,,,27*44
$GPGSV,3,2,09,06,18,211,36,19,40,250,33,21,07,137,,24,20,047,*74
$GPGSV,3,3,09,11,33,302,37*45

$GPGLL,             4802.391740,N,  12303.672452,W,   180812.00,A,A*7C
$GPRMC,180812.00,A, 4802.391740,N,  12303.672452,W,   0.0,0.0,          030313,     18.6,W,A*0F
$GPGGA,180812.00,   4802.391740,N,  12303.672452,W,   1,04,5.8,187.2,M,-18.5,M,,*5D


$GPRMC 180812.00    4802.391740,N,  12303.672452,W,   030313


*CS is checksum (8 bit exclusive OR of all data in the sentence, including ","
delimiters, between but not including the "$" and "*" delimiters.

$GPRTE,1,1,c,0*07

'GPRTE,1,1,c,0' asOrderedCollection inject: 0 into:
 [:sum :each | sum bitXor: each asciiValue ]


[
date/time only:
$GNRMC,205845.00,V,,,,,,,010518,,,N*60
]


*/

