#include "ThirdIcon.h"
#include "Player.h"
#include "control.h"
#include "build.h"
#include "../sp/SwitchChannels.h"

// The placement's parameters as ThirdIcon_Create reads them
#pragma pack(push, 1)
typedef struct {
    uchar _unknown00[0x2c];
    uint param[3];      // 0x2c
} Create_ThirdIcon_Params;
#pragma pack(pop)
static_assert(offsetof(Create_ThirdIcon_Params, param) == 0x2c, "Bad offset of param");

// ThirdIcon's obj_tag::curState: 1 once its channel is set
#define THIRDICON_ON 1

// The text shown when the player touches the standing icon (not while creeping)
#define TXT_THIRDICON_STANDING ((Action_TranslatedText)0x01000291)

// AUTOINJECT
obj_tag * ThirdIcon_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_ThirdIcon_Params *params = (Create_ThirdIcon_Params *)lvl;
    obj_tag *obj = control_create_object(sizeof(THIRDICON), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    THIRDICON *icon = (THIRDICON *)obj->extraObjectData;
    icon->icon = params->param[0];
    icon->onChannel = (ushort)params->param[1];
    icon->offChannel = (ushort)params->param[2];
    icon->wait = 0;
    obj->objGraphics = celgl;
    obj->curState = icon->onChannel == 0;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_THIRDICON;
    obj->effectFlags |= EFFECT_MASKABLE;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Once on, a player touching it is shown its icon (and, for the standing icon, its text, at most every 4 frames)
// AUTOINJECT
void ThirdIcon_Update(obj_tag *obj) {
    THIRDICON *icon = (THIRDICON *)obj->extraObjectData;
    if (icon->wait > 0)
        icon->wait--;
    if (switch_channels[icon->onChannel])
        obj->curState = THIRDICON_ON;
    if (switch_channels[icon->offChannel])
        obj->flags |= 1;
    if (obj->curState != THIRDICON_ON)
        return;

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        obj_tag *player = hit->hitObj;
        if (player == NULL || player->objectType != OBJECTTYPE_PLAYER)
            continue;
        BLData *blData = (BLData *)player->extraObjectData;
        if (icon->wait == 0 && icon->icon == THIRDICON_STANDING && player->subState != MovementType_Creep)
            Text_AddMsg(0, 0, 1, (char *)Txt_BindLabel(TXT_THIRDICON_STANDING, 0), 0, 0xb4);
        blData->thirdIcon = (uchar)icon->icon;
        icon->wait = 4;
    }
}
