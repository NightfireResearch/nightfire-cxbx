#ifndef SPACEMISSILE_H_
#define SPACEMISSILE_H_

#include "../../actionhelpers.h"

void SpaceMissile_Update(obj_tag *obj);
obj_tag * Create_SpaceMissile(_VECTOR* pos, _VECTOR* rot, level_tag* level);

// A missile's obj_tag::curState
enum SpaceMissileState {
    SPACEMISSILE_WAITING = 0,       // for its channel, then its delay
    SPACEMISSILE_DEPLOYING = 1,     // the deployment script
    SPACEMISSILE_DECIDING = 2,      // the launch script picked by the channel of its number
    SPACEMISSILE_LAUNCHING = 3,     // subState 0 with the coupler destroyed, 1 intact
    SPACEMISSILE_DONE = 4,
};

// MissileDeploy[], one a missile (by number), read by HUD_UpdateSpacePane
enum MissileDeployState {
    MISSILE_NOT_DEPLOYED = 0,
    MISSILE_DEPLOYED = 1,
    MISSILE_COUPLER_DESTROYED = 2,  // launched with the coupler-destroyed script
    MISSILE_COUPLER_INTACT = 3,     // launched with the coupler-intact script
};

extern uchar MissileDeploy[8];      // MissileDeployState

#endif // SPACEMISSILE_H_