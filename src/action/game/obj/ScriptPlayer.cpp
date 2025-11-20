#include "ScriptPlayer.h"
#include "../../math/math.h"
#include "../../engine/Script.h"

#pragma pack(push, 1)

typedef struct SCRIPTPLAYER {
    char unknown1[0xc];
    SCRIPTINFO* scriptInfos[4];
    HASHCODE someHashcode;
    char unknown2[10];
    ushort nthScript;
} SCRIPTPLAYER;

static_assert(offsetof(SCRIPTPLAYER, scriptInfos) == 0xc, "Bad offset of scriptInfos");
static_assert(offsetof(SCRIPTPLAYER, nthScript) == 0x2a, "Bad offset of nthScript");

#pragma pack(pop)

// AUTOGEN
void SP_RemoveObj(obj_tag* obj, void* scriptPlayer);

// NOINJECT
void SP_Update(obj_tag* obj) {

    // printf("Object at 0x%08x is a SCRIPTPLAYER\n");
    SCRIPTPLAYER* sp = (SCRIPTPLAYER*)obj->extraObjectData;



}

// AUTOINJECT
void SP_Delete(obj_tag *obj) {
    SCRIPTPLAYER *sp = (SCRIPTPLAYER*)obj->extraObjectData;
    Script_Free(sp->scriptInfos[0]);
    sp->scriptInfos[0] = NULL;
    Script_Free(sp->scriptInfos[1]);
    sp->scriptInfos[1] = NULL;
    obj->flags |= 1;
}

// AUTOINJECT
SCRIPTINFO * SP_getScriptInfo(obj_tag *gameObj) {

  if(gameObj == NULL)
    return NULL;
  
  SCRIPTPLAYER* sp = (SCRIPTPLAYER *)gameObj->extraObjectData;
  
  if(sp == NULL)
    return NULL;

  return sp->scriptInfos[sp->nthScript];

}

// AUTOINJECT
void SP_SetColour(obj_tag *gameObj, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Script_SetColour(scriptPlayer->scriptInfos[scriptPlayer->nthScript], clr_r, clr_g, clr_b);
    
}
// AUTOINJECT
void SP_SetPos(obj_tag *gameObj, _VECTOR *newPos) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Vec_Copy(newPos, Mat_Position(scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix));

  Script_SetPosRot(scriptPlayer->scriptInfos[scriptPlayer->nthScript], &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);
    
}

// AUTOINJECT
void SP_SetPosRot(obj_tag *gameObj, _MATRIX *newMtx) {
  
  if (gameObj == NULL)
    return;

  SCRIPTPLAYER* scriptPlayer = (SCRIPTPLAYER *)gameObj->extraObjectData;

  if (scriptPlayer->scriptInfos[scriptPlayer->nthScript] == NULL)
    return;

  Mat_Copy(newMtx, &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);

  Script_SetPosRot(scriptPlayer->scriptInfos[scriptPlayer->nthScript], &scriptPlayer->scriptInfos[scriptPlayer->nthScript]->maybeMatrix);
    
}

// AUTOGEN
obj_tag* SP_CreateScriptPlayer(_VECTOR *pos, _VECTOR *rot, level_tag *param_3, obj_tag *param_4, ScriptPlayerCallback *funcPtr, void *param_6);
