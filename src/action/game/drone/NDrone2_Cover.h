#ifndef NDRONE2_COVER_H
#define NDRONE2_COVER_H

#include "NDrone2.h"

// Cover, doors and kicked objects, snipers, the grenade throw and the action anims (NDrone2_Cover.cpp)

// DroneAnim_CoverAnim's steps (Drone_tag.coverStep)
enum {
    COVER_STEP_ENTER = 0,
    COVER_STEP_IDLE = 1,
    COVER_STEP_AIM = 2,
    COVER_STEP_FIRE = 3,
    COVER_STEP_RETURN = 4,
    COVER_STEP_LEAVE = 5,
    COVER_STEP_ARRIVE = 6,      // as COVER_STEP_ENTER, choosing the mode at a corner
    COVER_STEP_CHANGE = 7,      // a corner: maybe another mode, through the old one's leave anim
};

// Drone_tag.coverMode at a corner
enum {
    COVER_MODE_CROUCH_LEAN = 0,
    COVER_MODE_CROUCH_STEP_OUT = 1,
    COVER_MODE_STAND_LEAN = 2,
    COVER_MODE_STAND_STEP_OUT = 3,
    COVER_MODE_AS_LOW = 4,      // the corner used as low cover
};

// Drone_tag.coverFlags
#define DRONE_COVER_SIDE_MASK 3     // the corner's side, 1 or 2
#define DRONE_COVER_LOST 4          // UnderCoverReturn found the node no longer good
#define DRONE_COVER_LEFT 8          // left: a low node's leave step, or UnderCoverLeaveNow

ushort DroneAnim_CoverModeAnim(DCVars_tag *dcv, uchar mode);
bool DroneAnim_CoverAnim(DCVars_tag *dcv, uchar step, short endState);
bool NDrone2_SetAngleToCover(DCVars_tag *dcv, float unused);
void NDrone2_SlideToPosition(_VECTOR *pos, float fraction, char withHeight, DCVars_tag *dcv);
void DroneFunc_RunForCoverTalk(DCVars_tag *dcv);
void DroneFunc_UnderCoverTalk(DCVars_tag *dcv);
void DroneFunc_UnderCoverAimTalk(DCVars_tag *dcv);
void NDrone2_KickObject(DCVars_tag *dcv);
void NDrone2_OpenDoor(DCVars_tag *dcv);
bool NDrone2_DoorIsOpen(DCVars_tag *dcv);
void NDrone2_FindElevatorDoor(DCVars_tag *dcv);
void NDrone2_MoveTo_RouteNode(DCVars_tag *dcv, short offset, AIRoute_tag *route);
bool NDrone2_ReachedDestNode(AIRoute_tag *route, DCVars_tag *dcv);
bool DroneAnim_SetScript(HASHCODE script, ushort animState, ushort nextRow, uchar blend, short endState, uint endMsg,
                         uint flags, DCVars_tag *dcv);

bool NDrone2_DSTATE_RunForCover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverInit(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverAim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverSniperFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverSniperReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverReturn(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverTypeChange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverLeave(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_UnderCoverLeaveNow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_GrenadeThrow(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SniperIdle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SniperAim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SniperFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_SniperReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_OpenDoor(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_KickObject(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_ActionAnim(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StandFiddle(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

// Register-argument original: the adaptor patched over it (DI the anim state, ESI dcv, the result in AX), and its body
void DroneAnim_ValidateCoverAnim(void);
ushort _DroneAnim_ValidateCoverAnim(DCVars_tag *dcv, ushort state);

#endif // NDRONE2_COVER_H
