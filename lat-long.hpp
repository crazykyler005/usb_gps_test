#ifndef _LATITIDE_LONGITUDE_H_
#define _LATITIDE_LONGITUDE_H_


typedef struct
{
    int  latitude_minutes_x1e5 ;    //  >= 0 is North ,  < 0 is South
    int longitude_minutes_x1e5 ;    //  >= 0 is East  ,  < 0 is West
} LatitudeLongitude ;


// string format is 
//      d m.m {N|S}, d m.m {E|W}
//      degrees and decimal minutes with N, S, E or W suffix for North, South, East, West
//      degrees and decimal minutes are always >= 0
//      decimal minutes is max 6 digits
// examples
//      48 02.391740 N, 123 03.672452 W
//       0 30.456789 S, 179 59.999999 E


typedef char LatLongString [36] ;

// convert latitude/longitude to string
void    latitudeLongitude_toString   (LatitudeLongitude *, LatLongString) ;

// set latitude/longitude from string
bool latitudeLongitude_fromString (LatitudeLongitude *, LatLongString) ;


#endif