#ifndef _NMEA_H_
#define _NMEA_H_

#include "lat-long.hpp"
#include "serial-port.h"
#include <string>

void    nmea0183_getDateAndTime   (struct tm *);
char *  nmea0183_getLatLongString (void);

bool nmea0183_isLatLongValid  (void);
bool nmea0183_isDateTimeValid (void);

void nmea0183_updateFromStream (SerialPort *, uint16_t timeoutSeconds);
void nmea0183_updateFromString (string);

void nmea0183_echoToMonitor (bool echoOrNot) ;

void nmea0183_initialize (void);

#endif
