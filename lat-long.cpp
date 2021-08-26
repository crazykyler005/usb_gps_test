#include "lat-long.hpp"

#include <stdio.h>
#include <string.h>
#include <ctype.h>



void latitudeLongitude_toString (LatitudeLongitude * latLong, LatLongString outputString)
{
    // convert latitude/longitude to "48 02.391740 N, 123 03.672452 W" format

    int latMinutes_x1e5 = latLong ->  latitude_minutes_x1e5 ;
    int lonMinutes_x1e5 = latLong -> longitude_minutes_x1e5 ;

    enum { LatitudeNorth = 'N',  LatitudeSouth = 'S',  LatitudeNone  = ' ' } northSouth ;
    enum { LongitudeEast = 'E',  LongitudeWest = 'W',  LongitudeNone = ' ' }  eastWest ;

    if      (latMinutes_x1e5 > 0)   northSouth = LatitudeNorth ;
    else if (latMinutes_x1e5 < 0)   northSouth = LatitudeSouth ;
    else                            northSouth = LatitudeNone  ;

    if      (lonMinutes_x1e5 > 0)   eastWest = LongitudeEast ;
    else if (lonMinutes_x1e5 < 0)   eastWest = LongitudeWest ;
    else                            eastWest = LongitudeNone ;

    if (northSouth == LatitudeSouth)    latMinutes_x1e5 *= -1 ;
    if (  eastWest == LongitudeWest)    lonMinutes_x1e5 *= -1 ;

    unsigned int lat = latMinutes_x1e5 ;
    unsigned int lon = lonMinutes_x1e5 ;

    unsigned int latDeg = lat / (100000 * 60) ;
    unsigned int lonDeg = lon / (100000 * 60) ;

    unsigned int latMin = lat % (100000 * 60) ;
    unsigned int lonMin = lon % (100000 * 60) ;

    sprintf (outputString, "%2d %2d.%05d %c, %3d %2d.%05d %c",
            latDeg, latMin / 100000, latMin % 100000, northSouth,
            lonDeg, lonMin / 100000, lonMin % 100000, eastWest ) ;

/*
    2013/03/03 18:08:12  48 02.391740,N, 123 03.672452
    48  2.391744 N, 123  3.672576 W
*/
}



static bool decimalMinutes_x1e6_fromString (char * decimalString, unsigned int * decimalMinutes_x1e6)
{
    // on success, decimalMinutes_x1e6 will be set to a number from 0 to 999999

    uint8_t numDigits = strlen (decimalString) ;

    bool fault = numDigits > 6 ;

    unsigned int decimalMinutes ;
    uint8_t scanResult = sscanf (decimalString, "%u", & decimalMinutes) ;

    fault |= (scanResult != 1) ;

    if (! fault)
    {
        // for each digit short of 6, multiply the result by 10
        while (numDigits ++ < 6)
            decimalMinutes *= 10 ;

        * decimalMinutes_x1e6 = decimalMinutes ;
    }

    return ! fault ;
}



bool latitudeLongitude_fromString (LatitudeLongitude * latLon, LatLongString latLongString)
{
    // lat/lon string format is
    //      d m.m {N|S}, d m.m {E|W}
    //      degrees and decimal minutes with North, South, East or West suffix
    //      degrees and decimal minutes are always >= 0
    //      decimal minutes is max 6 digits

    struct {
        unsigned int degrees ;
        unsigned int minutes ;
        char         decimalDigits [7] ;    // max 6 decimal digits
        unsigned int decimalMinutes ;
        char         direction ;
    } lat, lon ;

    uint8_t scanResult;

    scanResult = sscanf (latLongString, "%u %u.%6s %c, %u %u.%6s %c",
                         & lat.degrees, & lat.minutes, lat.decimalDigits, & lat.direction,
                         & lon.degrees, & lon.minutes, lon.decimalDigits, & lon.direction) ;

    lat.direction = toupper (lat.direction) ;
    lon.direction = toupper (lon.direction) ;

    bool fault = (scanResult != 8) ;

    fault |= (lat.degrees >  90) || (lat.minutes > 60) ||
             (lon.degrees > 180) || (lon.minutes > 60) ;

    fault |= ! decimalMinutes_x1e6_fromString (lat.decimalDigits, & lat.decimalMinutes) ||
             ! decimalMinutes_x1e6_fromString (lon.decimalDigits, & lon.decimalMinutes) ;

    fault |= ! ((lat.direction == 'N') || (lat.direction == 'S')) ||
             ! ((lon.direction == 'E') || (lon.direction == 'W')) ;

    if (! fault)
    {
        // make decimal minutes range 0 .. (100000 - 1)
        lat.decimalMinutes /= 10 ;
        lon.decimalMinutes /= 10 ;

        int latMinutes_x1e5 = lat.minutes * 100000 + lat.decimalMinutes ;
        int lonMinutes_x1e5 = lon.minutes * 100000 + lon.decimalMinutes ;

        fault |= (latMinutes_x1e5 > 60 * 100000) ||
                 (lonMinutes_x1e5 > 60 * 100000) ;

        latMinutes_x1e5 += lat.degrees * 60 * 100000 ;
        lonMinutes_x1e5 += lon.degrees * 60 * 100000 ;

        fault |= (latMinutes_x1e5 >  90 * 60 * 100000) ||
                 (lonMinutes_x1e5 > 180 * 60 * 100000) ;

        if (! fault)
        {
            if (lat.direction == 'S')   latMinutes_x1e5 *= -1 ;
            if (lon.direction == 'W')   lonMinutes_x1e5 *= -1 ;

            latLon-> latitude_minutes_x1e5 = latMinutes_x1e5 ;
            latLon->longitude_minutes_x1e5 = lonMinutes_x1e5 ;
        }
    }

    return ! fault ;
}

