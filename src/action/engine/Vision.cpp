#include "Vision.h"

#include "viewer.h"

 // AUTOINJECT
 bool Vision_InView(_VECTOR *point, float radius, viewer_tag *viewer) {
 
    // Measure the distance from the point to each of the 4 frustum planes
    for(int i = 0; i < 4; i++) {

        float distance = DistancePointToPlane(point, &viewer->frustumPlanes[i]);

        if(distance < -radius) {
            return false; // Point is outside the frustum
        }

    }

    return true;
 }