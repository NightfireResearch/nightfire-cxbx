#ifndef DRONE_TRACKING_H
#define DRONE_TRACKING_H

#include "NDrone2.h"

// Where a drone looks and aims each frame (NDrone2_DoTracking, from NDrone2_ControlSTANDARD): its arms at the
// opponent while its anim aims (NDrone2_AimTest), otherwise its head at whoever NDrone2_DoHeadTrackingBehaviour
// picks.

uchar NDrone2_AimTest(DCVars_tag *dcv);
void NDrone2_DoAiming(char aim, DCVars_tag *dcv);
void NDrone2_DoHeadTracking(char on, obj_tag *target, DCVars_tag *dcv);
obj_tag* NDrone2_HeadTrackNearest(DCVars_tag *dcv);
void NDrone2_DoHeadTrackingBehaviour(DCVars_tag *dcv);
void NDrone2_DoTracking(char on, DCVars_tag *dcv);

#endif // DRONE_TRACKING_H
