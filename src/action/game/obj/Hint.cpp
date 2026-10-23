#include "Hint.h"

#include "object.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../game.h"
#include "../../sound/Sound.h"

extern uint32_t HintsEnabled; // defined in game.cpp

// A hint placement's parameters, as the level data has them (after the common 0x2c-byte placement header).
typedef struct {
    PlacementHeader header; // 0x00
    uint label;          // 0x2c - the text shown
    uint sound;          // 0x30 - played with it; 0 for none
    uint difficulty;     // 0x34 - 0: any; 1-4: at most this difficulty; 5-7: only difficulty 1-3
    uint onChannel;      // 0x38 - 0, or the channel that must be set first
    uint offChannel;     // 0x3c - 0, or the channel that removes it
} HintPlacement;
static_assert(offsetof(HintPlacement, difficulty) == 0x34, "Bad offset of HintPlacement::difficulty");

// A hint object's data
typedef struct {
    uint sound;          // 0x00
    uint label;          // 0x04
    uint onChannel;      // 0x08
    uint offChannel;     // 0x0c
} HINT;
static_assert(sizeof(HINT) == 0x10, "Bad size for HINT");

// A hint object (object type 0x43): only when hints are on and the placement's difficulty rule allows it.
// AUTOINJECT
obj_tag * Hint_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    if (HintsEnabled == 0)
        return NULL;
    HintPlacement *p = (HintPlacement *)lvl;
    uint difficulty = GameState.difficultyModifier;
    if (p->difficulty != 0 && difficulty > p->difficulty)
        return NULL;
    if (p->difficulty == 5 && difficulty != 1)
        return NULL;
    if (p->difficulty == 6 && difficulty != 2)
        return NULL;
    if (p->difficulty == 7 && difficulty != 3)
        return NULL;

    obj_tag *obj = control_create_object(sizeof(HINT), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_HINT;
    Control_SetGList(obj, celgl);
    HINT *hint = (HINT *)obj->extraObjectData;
    hint->label = p->label;
    hint->sound = p->sound;
    hint->onChannel = p->onChannel;
    hint->offChannel = p->offChannel;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Once its on channel is set, the first player to touch it hears its sound and gets its text, and the hint is
// removed. It is removed untouched once its off channel is set.
// AUTOINJECT
void Hint_Update(obj_tag *obj) {
    HINT *hint = (HINT *)obj->extraObjectData;
    if (hint->onChannel != 0 && !switch_channels[hint->onChannel])
        return;
    if (hint->offChannel != 0 && switch_channels[hint->offChannel]) {
        obj->flags |= 1;
        return;
    }

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL || hit->hitObj->objectType != OBJECTTYPE_PLAYER)
            continue;
        if (hint->sound != 0)
            Sound_PlayExt((Action_SFX)hint->sound, 100.0f, 0, 0);
        BLData *bloke = (BLData *)hit->hitObj->extraObjectData;
        Text_AddMsg(bloke->playerNum, 0, 1, (char *)Txt_BindLabel((Action_TranslatedText)hint->label, 0), 0, 180);
        obj->flags |= 1;
        return;
    }
}
