#include "PCQWorm.h"

#include <cstdio>

#include "bullet.h"

// For KeyCodes
#include "../sp/Locks.h"

// For switch channels
#include "../sp/SwitchChannels.h"

#pragma pack(push, 1)

typedef struct {
    ushort keyCodeNum;
    short switchChannel;
    char mayBeActivated;
    char processHits;
} ObjData_PCQWorm;

static_assert(sizeof(ObjData_PCQWorm) == 6, "Size of PCQWorm incorrect");

#pragma pack(pop)

// obj_tag::curState
enum QWormState {
    QWORM_WAITING = 0,      // for a Q-Worm bullet's hit, or PCQWorm_Activate
    QWORM_TRIGGERED = 1,
    QWORM_DONE = 2,
};

// AUTOINJECT
obj_tag * PCQWorm_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl) {

    PCQWormPlacement *create = (PCQWormPlacement*) lvl;

    obj_tag *gameObj = Control_CreateObjEx(sizeof(ObjData_PCQWorm), pos, rot, NULL, celgl, NULL, '\x01', 0, 1.0, 0,
                                           0xff, 0xff, 0xff);
    gameObj->curState = QWORM_WAITING;
    gameObj->objectType = OBJECTTYPE_QWORM;
    ObjData_PCQWorm *qWorm = (ObjData_PCQWorm *)gameObj->extraObjectData;
    qWorm->keyCodeNum = (ushort)create->keyCode;
    qWorm->switchChannel = (short)create->channel;
    qWorm->mayBeActivated = (char)create->activatable;
    qWorm->processHits = (char)create->shootable;
    return gameObj;
}

// AUTOINJECT
void PCQWorm_Update(obj_tag *gameObj) {

  ObjData_PCQWorm *qWorm = (ObjData_PCQWorm *)gameObj->extraObjectData;

  if (gameObj->curState == QWORM_WAITING) {
    if (qWorm->processHits) {
        for(HITDATA_tag *hit = gameObj->hitList; hit != NULL; hit = hit->next) {

            if(hit->hitObj == NULL)
                continue;

            if(hit->hitObj->objectType != OBJECTTYPE_BULLET)
                continue;

            BU_tag *bullet = (BU_tag*)hit->hitObj->extraObjectData;
            if(bullet->wpnDef->weaponVariantNum != Weap_QWorm)
                continue;

            gameObj->curState = QWORM_TRIGGERED;
        }
    }
  }

  else if (gameObj->curState == QWORM_TRIGGERED) {

    if (qWorm->keyCodeNum != 0) {
      char* str = Txt_GetStringFromHeap('\0');
      Control_NextLOD(gameObj);
      sprintf(str, Txt_BindLabel(OBTAINED_LOCK_KEYCODE_XXXX, 0),
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[0],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[1],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[2],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[3]
                );
      Text_AddMsg('\0','\0',TXTMSG_INFO,str,0,0xb4);
      KeyCodes[qWorm->keyCodeNum].discovered = true;
    }

    SwitchChannel_SetActive(qWorm->switchChannel);

    gameObj->curState = QWORM_DONE;

  }

}

// AUTOINJECT
void PCQWorm_Activate(obj_tag *gameObj) {

    ObjData_PCQWorm *qWorm = (ObjData_PCQWorm*)gameObj->extraObjectData;

    if(qWorm->mayBeActivated && gameObj->curState == QWORM_WAITING)
        gameObj->curState = QWORM_TRIGGERED;

}