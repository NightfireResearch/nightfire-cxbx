#include "FuseBox.h"
#include "build.h"
#include "control.h"
#include "ScriptPlayer.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Collide.h"
#include "../../engine/celglist.h"
#include "../../game.h"

#include "Effect.h"
#include "Player.h"
// A fuse box: a script makes its fuses and hands each to one of two lists (FuseBox_Register). Once every fuse a list
// was given has been shot, the list's two switch channels are set.
#define FUSEBOX_LISTS 2
#define FUSEBOX_FUSES 10

#pragma pack(push, 1)

typedef struct {
    obj_tag *scriptPlayer;                          // 0x00 - the script that makes the fuses
    obj_tag *fuses[FUSEBOX_LISTS][FUSEBOX_FUSES];   // 0x04 - the fuses not yet shot
    ushort channelA[FUSEBOX_LISTS];                 // 0x54
    ushort numFuses[FUSEBOX_LISTS];                 // 0x58 - how many fuses each list was given
    ushort channelB[FUSEBOX_LISTS];                 // 0x5c
} FUSEBOX;

typedef struct {
    ObjectCreationData_Basic basicCreation; // 0x00
    HASHCODE scriptHashcode;                // 0x2c
    uint channelA[FUSEBOX_LISTS];           // 0x30
    uint channelB[FUSEBOX_LISTS];           // 0x38
} Create_FuseBox_Params;

#pragma pack(pop)

static_assert(sizeof(FUSEBOX) == 0x60, "Bad size for FUSEBOX");
static_assert(offsetof(FUSEBOX, channelA) == 0x54, "Bad offset of channelA");
static_assert(offsetof(FUSEBOX, channelB) == 0x5c, "Bad offset of channelB");
static_assert(offsetof(Create_FuseBox_Params, channelB) == 0x38, "Bad offset of channelB");

// The script's play mode: run a tick, then wait (SP_LoadScript)
#define FUSEBOX_SCRIPT_PLAY_MODE 0x40

// The originals, called by address where Ghidra's prototypes do not fit. Player_Hurt is given a third argument it
// does not read, as here in the original.

// Script event 15 of the fuse box's script: puts the stream's object in a list (0 or 1), once.
// AUTOINJECT
void FuseBox_Register(obj_tag *fuse, ushort list, void *fuseBox) {
    if (fuseBox == NULL || list >= FUSEBOX_LISTS)
        return;

    FUSEBOX *box = (FUSEBOX *)fuseBox;
    for (ushort i = 0; i < FUSEBOX_FUSES; i++) {
        if (box->fuses[list][i] == fuse)
            return;
        if (box->fuses[list][i] == NULL) {
            box->fuses[list][i] = fuse;
            box->numFuses[list]++;
            return;
        }
    }
}

// AUTOINJECT
obj_tag * FuseBox_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_FuseBox_Params *params = (Create_FuseBox_Params *)lvl;

    obj_tag *obj = control_create_object(sizeof(FUSEBOX), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    FUSEBOX *box = (FUSEBOX *)obj->extraObjectData;
    box->channelA[0] = params->channelA[0];
    box->channelA[1] = params->channelA[1];
    box->channelB[0] = params->channelB[0];
    box->channelB[1] = params->channelB[1];
    obj->curState = 0;
    obj->objectType = OBJECTTYPE_FUSEBOX;
    obj->objGraphics = NULL;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);
    box->scriptPlayer = SP_Create(pos, rot, params->scriptHashcode, (HASHCODE)0, FUSEBOX_SCRIPT_PLAY_MODE, 0, obj,
                                  FuseBox_Register, box, 0);
    return obj;
}

// A fuse hit by anything but a punch breaks (its next level of detail) and leaves its list; one in list 1 hurts the
// player unless that list has a first channel, and clears list 0's second channel. A list whose fuses have all gone
// sets its channels, once.
// AUTOINJECT
void FuseBox_Update(obj_tag *obj) {
    FUSEBOX *box = (FUSEBOX *)obj->extraObjectData;
    ushort fusesLeft[FUSEBOX_LISTS];

    for (ushort list = 0; list < FUSEBOX_LISTS; list++) {
        fusesLeft[list] = 0;
        for (int i = 0; i < FUSEBOX_FUSES; i++) {
            obj_tag *fuse = box->fuses[list][i];
            if (fuse == NULL)
                continue;

            Collide_FilterBullets(&fuse->hitList, 0xfffd);
            if (fuse->hitList == NULL) {
                fusesLeft[list]++;
                continue;
            }

            if (Control_NextLOD(fuse))
                Effect_RemoveSObjs(fuse);
            box->fuses[list][i] = NULL;
            if (list == 1) {
                if (box->channelA[1] == 0)
                    Player_Hurt(glb_players[0], 5.0f, &fuse->centrePoint);
                box->channelB[0] = 0;
            }
        }

        if (fusesLeft[list] != 0 || box->numFuses[list] == 0)
            continue;

        if (box->channelA[list] != 0) {
            switch_channels[box->channelA[list]] = 1;
            switch_channels_time[box->channelA[list]] = GameState.NumFramesUnpaused;
            box->channelA[list] = 0;
        }
        if (box->channelB[list] != 0) {
            switch_channels[box->channelB[list]] = 1;
            switch_channels_time[box->channelB[list]] = GameState.NumFramesUnpaused;
            box->channelB[list] = 0;
        }
    }
}
