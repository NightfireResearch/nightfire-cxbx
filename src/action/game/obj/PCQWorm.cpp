#include "PCQWorm.h"

#include <cstdio>

#include "bullet.h"

// For KeyCodes
#include "../sp/Locks.h"

// For switch channels
#include "../sp/SwitchChannels.h"

typedef struct {
    ObjectCreationData_Basic baseData;
    uint extraData[4];

} PCQWormCreationData;

#pragma pack(push, 1)

typedef struct {
    ushort keyCodeNum;
    short switchChannel;
    char mayBeActivated;
    char processHits;
} ObjData_PCQWorm;

static_assert(sizeof(ObjData_PCQWorm) == 6, "Size of PCQWorm incorrect");

#pragma pack(pop)




// AUTOINJECT
obj_tag * PCQWorm_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl) {
  
    PCQWormCreationData *create = (PCQWormCreationData*) lvl;
    
    obj_tag *gameObj = Control_CreateObjEx(6, pos, rot, NULL, celgl, NULL, '\x01', 0, 1.0, 0, 0xff, 0xff, 0xff);
    gameObj->curState = 0;
    gameObj->objectType = OBJECTTYPE_QWORM;
    ObjData_PCQWorm *qWorm = (ObjData_PCQWorm *)gameObj->extraObjectData;
    qWorm->keyCodeNum = (ushort)create->extraData[0];
    qWorm->switchChannel = (short)create->extraData[1];
    qWorm->mayBeActivated = (char)create->extraData[2];
    qWorm->processHits = (char)create->extraData[3];
    return gameObj;
}

// AUTOINJECT
void PCQWorm_Update(obj_tag *gameObj) {

  ObjData_PCQWorm *qWorm = (ObjData_PCQWorm *)gameObj->extraObjectData;
  
  if (gameObj->curState == 0) { // Awaiting trigger
    if (qWorm->processHits) {
        for(HITDATA_tag *hit = gameObj->hitList; hit != NULL; hit = hit->next) {
            
            if(hit->hitObj == NULL) // Linked to a game object?
                continue;
                
            if(hit->hitObj->objectType != OBJECTTYPE_BULLET) // Right hit type?
                continue;

            BU_tag *bullet = (BU_tag*)hit->hitObj->extraObjectData;
            if(bullet->wpnDef->weaponVariantNum != Weap_QWorm) // Right weapon ID?
                continue;
            
            // We've been QWormed
            gameObj->curState = 1;
        }
    }
  }

  else if (gameObj->curState == 1) { // Triggered

    if (qWorm->keyCodeNum != 0) {
      char* str = Txt_GetStringFromHeap('\0');
      Control_NextLOD(gameObj);
      sprintf(str, Txt_BindLabel(OBTAINED_LOCK_KEYCODE_XXXX, 0), 
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[0],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[1],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[2],
                    KeyCodes[qWorm->keyCodeNum].asciiDigit[3]
                );
      Text_AddMsg('\0','\0',1,str,0,0xb4);
      KeyCodes[qWorm->keyCodeNum].discovered = true;
    }
    
    SwitchChannel_SetActive(qWorm->switchChannel);

    gameObj->curState = 2;

  }

}

// AUTOINJECT
void PCQWorm_Activate(obj_tag *gameObj) {

    ObjData_PCQWorm *qWorm = (ObjData_PCQWorm*)gameObj->extraObjectData; 

    if(qWorm->mayBeActivated && gameObj->curState == 0)
        gameObj->curState = 1;

}