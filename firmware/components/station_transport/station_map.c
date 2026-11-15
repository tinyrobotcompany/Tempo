#include "station_map.h"
#include <math.h>
void station_map_project(double lat,double lon,unsigned z,double *x,double *y){
 double scale=256.0*(1u<<z);lat=fmax(-85.05112878,fmin(85.05112878,lat));double sine=sin(lat*0.0174532925199433);
 *x=(lon+180)/360*scale;*y=(0.5-log((1+sine)/(1-sine))/(4*3.141592653589793))*scale;
}
void station_map_unproject(double x,double y,unsigned z,double *lat,double *lon){
 double scale=256.0*(1u<<z);*lat=atan(sinh(3.141592653589793*(1-2*fmax(0,fmin(scale,y))/scale)))*57.29577951308232;
 *lon=fmod(fmod(x/scale*360,360)+360,360)-180;
}
