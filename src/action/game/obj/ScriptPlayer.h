#ifndef SCRIPTPLAYER_H_
#define SCRIPTPLAYER_H_

#include "../../actionhelpers.h"


void SP_RemoveObj(obj_tag* obj, void* scriptPlayer);
void SP_Delete(obj_tag *obj);
void SP_Update(obj_tag *obj);
SCRIPTINFO * SP_getScriptInfo(obj_tag *gameObj);
void SP_SetColour(obj_tag *gameObj, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);
void SP_SetPos(obj_tag *gameObj, _VECTOR *newPos);
void SP_SetPosRot(obj_tag *gameObj, _MATRIX *newMtx);

#endif // SCRIPTPLAYER_H_