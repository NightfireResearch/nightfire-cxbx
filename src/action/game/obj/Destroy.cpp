#include "Destroy.h"

#include "../../game.h"
#include "../mp/multiplayer.h"
#include "../sp/PlayerStats.h"
#include "../sp/SwitchChannels.h"
#include "build.h"
#include "bullet.h"
#include "control.h"
#include "Effect.h"
#include "Explode.h"
#include "Light.h"

#include <math.h>

#pragma fp_contract(off)

// The explosions of the kinds of key 2: 1, 2 and the rest
#define DESTROY_EXPLOSION_1 ((HASHCODE)0x06000027)
#define DESTROY_EXPLOSION_2 ((HASHCODE)0x0600004f)
#define DESTROY_EXPLOSION_OTHER ((HASHCODE)0x06000052)

// The model whose destruction counts as a shot at the scenery in 0x07000002
#define DESTROY_SCENERY_MODEL ((HASHCODE)0x020001d9)

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// In multiplayer, only with explosive scenery on
// AUTOINJECT
obj_tag * Destroy_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl, celglist_tag *someOtherGl) {

    if (MPSettings.isMultiplayer && !MPSettings.ExplosiveSceneryEnabled)
        return NULL;

    obj_tag *obj = control_create_object(sizeof(DESTROY_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    obj->objectType = OBJECTTYPE_DESTROY;
    Control_SetGList(obj, celgl);
    build_LinkToRoom(obj, 0, glb_world);

    DESTROY_tag *destroy = (DESTROY_tag *)obj->extraObjectData;
    DestroyPlacement *keys = (DestroyPlacement *)lvl;
    destroy->smashedGlist = celgl->lod != NULL ? celgl->lod : someOtherGl;
    destroy->channelIn = (ushort)keys->channelIn;
    destroy->channelOut = (ushort)keys->channelOut;
    destroy->explosion = (char)keys->explosion;
    destroy->health = (float)keys->health;
    destroy->mask = keys->mask != 0 ? (ushort)~keys->mask : 0;
    destroy->blastRadius = (float)((double)keys->blast * 0.01f);
    destroy->delay = Ftol(-((double)keys->delay * _FRAME_RATE));
    destroy->smashed = 0;
    if (obj->effectFlags & FLAG_HIDDEN)
        destroy->smashedGlist = NULL;
    return obj;
}

// Once smashed with a channel and a delay: counts the delay down, then switches the channel on. Before: smashed when
// its own channel comes on.
// AUTOINJECT
void Destroy_Update(obj_tag *obj) {

    if (obj == NULL)
        return;

    DESTROY_tag *destroy = (DESTROY_tag *)obj->extraObjectData;
    if (destroy->channelOut != 0 && destroy->delay > 0) {
        if (--destroy->delay == 0) {
            switch_channels[destroy->channelOut] = 1;
            obj->objectType = OBJECTTYPE_DELETED;
        }
        return;
    }

    if (obj->objGraphics != destroy->smashedGlist && destroy->channelIn != 0 && switch_channels[destroy->channelIn]) {
        destroy->channelIn = 0;
        Destroy_Smash(obj, NULL, 0);
    }
}

// Bullets of the classes it does not ignore take their damage from its health; below 0 it is smashed. The laser
// heats it: green and blue fall by 5 a frame it hits, and come back by 1 a frame it does not.
// AUTOINJECT
void Destroy_CollisionHandler(obj_tag *obj) {

    DESTROY_tag *destroy = (DESTROY_tag *)obj->extraObjectData;
    short heat = 0;

    Collide_FilterBullets(&obj->hitList, destroy->mask);

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {

        obj_tag *hitter = hit->hitObj;
        if (hitter == NULL || hitter->objectType != OBJECTTYPE_BULLET)
            continue;

        if (((BU_tag *)hitter->extraObjectData)->wpnDef->weaponBaseNum == Weap_Laser)
            heat = 5;

        destroy->health -= hit->dmgAmt;
        if (destroy->health < 0.0f) {
            if (destroy->channelOut != 0) {
                if (destroy->delay == 0)
                    switch_channels[destroy->channelOut] = 1;
                else
                    destroy->delay = -destroy->delay;
            }
            Destroy_Smash(obj, ((BU_tag *)hit->hitObj->extraObjectData)->firedByObj, 0);
            return;
        }
    }

    int change = heat != 0 ? -heat : 1;
    int blue = obj->tweakB + change;
    obj->tweakB = blue < 0 ? 0 : (blue > 0xff ? 0xff : blue);
    int green = obj->tweakG + change;
    obj->tweakG = green < 0 ? 0 : (green > 0xff ? 0xff : green);
}

// Once only: the broken model, and the delay started; without a channel, or with one and no delay (the channel is
// switched on), it is done with (type 1). An explosion of its kind, its script two to four times as wide as the object,
// the damage the kind times ten.
// AUTOINJECT
void Destroy_Smash(obj_tag *obj, obj_tag *from, ushort flags) {

    DESTROY_tag *destroy = (DESTROY_tag *)obj->extraObjectData;
    if (destroy->smashed)
        return;
    destroy->smashed = 1;

    Light_Delete(Light_FindMapLight(&obj->centrePoint, obj->radius));
    Effect_RemoveSObjs(obj);
    if (destroy->smashedGlist != NULL)
        Control_SetGList(obj, destroy->smashedGlist);

    destroy->delay = -destroy->delay;
    if (destroy->channelOut == 0) {
        obj->objectType = OBJECTTYPE_DELETED;
    } else if (destroy->delay == 0) {
        switch_channels[destroy->channelOut] = 1;
        obj->objectType = OBJECTTYPE_DELETED;
        if (GameState.CurrentLevelHashcode == (HASHCODE)0x07000002 && obj->objGraphics != NULL
            && obj->objGraphics == hashtable_getitem(DESTROY_SCENERY_MODEL)) {
            PlrStat_LogShotHitScenery(0);
        }
    }

    if (destroy->explosion == 0)
        return;

    int kind = destroy->explosion;
    HASHCODE script;
    uint size;
    if (kind == 1) {
        script = DESTROY_EXPLOSION_1;
        size = 4;
    } else if (kind == 2) {
        script = DESTROY_EXPLOSION_2;
        size = 2;
    } else {
        script = DESTROY_EXPLOSION_OTHER;
        size = 3;
    }
    Explode_Create(obj, &obj->centrePoint, &CONST_UP_VECTOR, (float)size * obj->radius, destroy->blastRadius, script,
                   (float)kind * 10.0f, 200, 200, 0, from, 0);
}
