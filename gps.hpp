
#ifndef _GPS_H_
#define _GPS_H_

#include "lat-long.hpp"
#include "serial-port.h"

#include <string>
#include <time.h>


void    gps_initiateDateTimeAcquisition (bool includeRtcUpdate) ;
void    gps_initiateLatLongAcquisition  (void) ;

void    gps_updateAcquisition (void) ;

bool gps_dateTimeAcquisitionBusy (void) ;
bool gps_latLongAcquisitionBusy  (void) ;

bool gps_dateTimeAcquisitionSucceeded (void) ;
bool gps_latLongAcquisitionSucceeded  (void) ;

struct tm * gps_getDateTime      (void);
string            gps_getLatLongString (void);


// intended for use by the monitor
void gps_open    (void);
void gps_close   (void);
void gps_turnOff (void);
void txMessage_UBX_MGA_INI_TIME_UTC (SerialPort *) ;


void gps_initialize (void);


#endif