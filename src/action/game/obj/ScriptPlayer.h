#ifndef SCRIPTPLAYER_H_
#define SCRIPTPLAYER_H_

#include "../../actionhelpers.h"

// Bits of SCRIPTPLAYER::triggerMask, and the values SP_Hit computes/returns each frame
enum SP_TriggerFlags {
    SP_Trigger_SwitchChannel = 0x1,  // someSwitchChannel is currently on
    SP_Trigger_BulletHit     = 0x2,  // A bullet matching requiredBulletType hit a linked entity
    SP_Trigger_Unpause       = 0x4,  // unpauseRequested was set, or the countdown/damage timer elapsed
    SP_Trigger_PlayerTouch   = 0x8,  // A player touched a linked entity
    SP_Trigger_DroneTouch    = 0x10, // A drone touched a linked entity
};

void SP_RemoveObj(obj_tag* obj, void* scriptPlayer);
void SP_Delete(obj_tag *obj);
void SP_Update(obj_tag *obj);
SCRIPTINFO * SP_getScriptInfo(obj_tag *gameObj);
void SP_SetColour(obj_tag *gameObj, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
void SP_SetPos(obj_tag *gameObj, _VECTOR *newPos);
void SP_SetPosRot(obj_tag *gameObj, _MATRIX *newMtx);
obj_tag* SP_CreateScriptPlayer(_VECTOR *pos, _VECTOR *rot, level_tag *placementData, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext);
obj_tag* SP_Create(_VECTOR *pos, _VECTOR *rot, HASHCODE scriptHashcode0, HASHCODE scriptHashcode1, short playMode, char playOnce, obj_tag *ownerObj, ScriptPlayerCallback *funcPtr, void *callbackContext, short triggerMask);
ushort SP_GetState(obj_tag *obj);
void SP_HideObj(obj_tag *obj, char hide);
void SP_UnPause(obj_tag *obj);
void SP_Activate(obj_tag *activatorObj, obj_tag *param_2);
void SP_SwitchToScript(obj_tag *obj, short newScriptPlayMode, ScriptPlayerCallback *callback);
float SP_GetHitDamage(obj_tag *obj, obj_tag **outHitObjects, ushort maxHitObjects, ushort *outHitObjectCount, uint bulletFilterFlags, bool *outPlayerWasHit);

#endif // SCRIPTPLAYER_H_