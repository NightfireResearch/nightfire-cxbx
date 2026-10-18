#include "RainBox.h"

#include <string.h>

#include "../../game.h"
#include "../../engine/psiDraw.h"
#include "../../engine/psiGraphics.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "control.h"

#pragma fp_contract(off)

// The PS2's copy back from its scratchpad: the shared empty function on the Xbox
#define psiCopyFromSP ((void (__cdecl *)(uint offset, void *dst, uint size))0x000e0ec0)

// The levels whose drops barely drift, those with 0x80 model drops and their splashes, and those that start with
// every drop falling (GameState.CurrentLevelHashcode)
#define STILL_RAIN_FIRST 0x07000007
#define STILL_RAIN_LAST 0x07000008
#define HEAVY_RAIN_FIRST 0x07000009
#define HEAVY_RAIN_LAST 0x0700000b
#define BODGE_FIRST 0x07000005
#define BODGE_LAST 0x07000006

#define RAIN_TEXTURE ((HASHCODE)0x03000045)
#define RISING_TEXTURE ((HASHCODE)0x03000087)
#define DROP_MODEL ((HASHCODE)0x02000290)
#define SPLASH_MODEL ((HASHCODE)0x02000039)

// The drops moved in one batch
#define DROPS_A_BATCH 1000

static bool Between(uint value, uint first, uint last) {
    return value >= first && value <= last;
}

// The steps and the drops for a type; every drop dead, far below
// AUTOINJECT
void InitDrops(uchar type) {
    DropType = type;
    for (int i = 0; i < 32; i++) {
        _VECTOR *step = &RandTable1[i];
        if (Between(GameState.CurrentLevelHashcode, STILL_RAIN_FIRST, STILL_RAIN_LAST)) {
            step->x = (float)(Float_FRand(0.004f) - 0.002f);
            step->y = (float)-(Float_FRand(0.075f) + 0.025f);
            step->z = (float)(Float_FRand(0.004f) - 0.002f);
        } else {
            step->x = (float)(Float_FRand(0.04f) - 0.02f);
            step->y = (float)-(Float_FRand(0.075f) + 0.025f);
            step->z = (float)(Float_FRand(0.04f) - 0.02f);
        }
        if (DropType == DROP_RISING) {
            step->x = step->x * 0.25f;
            step->y = step->y * -0.25f;
            step->z = step->z * 0.25f;
        }
    }

    memset(DropState, 0, sizeof(DropState));
    for (int i = 0; i < MAX_DROPS; i++) {
        DropVector[i].pos.x = 0.0f;
        DropVector[i].pos.y = 0.0f;
        DropVector[i].pos.z = -1024.0f;
    }

    WindVector.x = 0.0f;
    WindVector.z = 0.0f;
    if (DropType == DROP_MODELS) {
        WindVector.y = -0.5f;
        CurrentMax = Between(GameState.CurrentLevelHashcode, HEAVY_RAIN_FIRST, HEAVY_RAIN_LAST) ? 0x80 : 0x40;
    } else {
        WindVector.y = 0.0f;
        CurrentMax = DropType == DROP_PARTICLES ? 0x1000 : 0x800;
    }
    PrevPos.z = -999.0f;
    PrevPos.y = -999.0f;
    PrevPos.x = -999.0f;
    InitBodge = Between(GameState.CurrentLevelHashcode, BODGE_FIRST, BODGE_LAST);
}

// A new drop at drops[index], somewhere in the cube above CubeCentre (about it, for rising drops), taken back along
// the wind to where it would have started. It lives only inside an active rain box, and ends at the box's floor or
// 7.5 below CubeCentre (rising: at its top or 7.5 above); outside every box it is put far below.
// AUTOINJECT
void AddDrop(short index, int first, uchar *state, DROP_tag *drops) {
    Added++;
    DROP_tag *drop = &drops[index];
    Vec_Copy(&CubeCentre, &drop->pos);
    drop->pos.x = (float)(Float_FRand(30.0f) - 15.0f + drop->pos.x);
    drop->pos.z = (float)(Float_FRand(30.0f) - 15.0f + drop->pos.z);
    double height;
    if (DropType == DROP_RISING)
        height = Float_FRand(30.0f) - 15.0f;
    else
        height = Float_FRand(7.5f) + 7.5f;
    drop->pos.y = (float)(height + drop->pos.y);

    bool inBox = false;
    for (obj_tag *box = RainBoxRoot; box != NULL && !inBox; box = box->maybeParent) {
        RAINBOX_tag *rain = (RAINBOX_tag *)box->extraObjectData;
        if (!rain->active)
            continue;
        if (!(drop->pos.x < rain->max[0] && drop->pos.x > rain->min[0] && drop->pos.z < rain->max[2] &&
              drop->pos.z > rain->min[2]))
            continue;
        if (DropType == DROP_RISING) {
            inBox = true;
            drop->pos.y = rain->min[1] > drop->pos.y ? rain->min[1] : drop->pos.y;
            double top = (double)CubeCentre.y + 7.5f;
            drop->end = top > rain->max[1] ? rain->max[1] : (float)top;
        } else if (drop->pos.y > rain->min[1]) {
            inBox = true;
            double bottom = (double)CubeCentre.y - 7.5f;
            drop->end = bottom < rain->min[1] ? rain->min[1] : (float)bottom;
        }
    }

    if (!inBox) {
        drop->pos.y = (float)(drop->pos.y - 1054.0f);
        return;
    }
    if (WindVector.y != 0.0f) {
        drop->pos.x = (float)(height / WindVector.y * WindVector.x + drop->pos.x);
        drop->pos.z = (float)(height / WindVector.y * WindVector.z + drop->pos.z);
    }
    state[first + index] = 1;
}

// A frame of the drops, about view 4 when it is on, else view 0; none drawn without the sky. The wind wanders
// (not for model drops), the cube goes 7.5 ahead of the player in view 0, live drops move on by their step and die
// past their end, and up to 21 dead ones start again.
// AUTOINJECT
void UpdateDrops(void) {
    if (DropType == DROP_NONE)
        return;
    if (GS_IsPaused(-1))
        return;

    short view = glb_viewer[4]->enabled ? 4 : 0;
    viewer_tag *viewer = glb_viewer[view];
    if (!viewer->draw.skyVisible) {
        Added = 999;
        return;
    }

    if (InitBodge) {
        InitBodge = 0;
        for (short i = 0; i < CurrentMax; i++) {
            DropState[i] = 1;
            DROP_tag *drop = &DropVector[i];
            Vec_Copy(&viewer->pos, &drop->pos);
            drop->pos.x = (float)(Float_FRand(30.0f) - 15.0f + drop->pos.x);
            drop->pos.z = (float)(Float_FRand(30.0f) - 15.0f + drop->pos.z);
            drop->pos.y = (float)(Float_FRand(30.0f) - 15.0f + drop->pos.y);
            drop->end = (float)((double)viewer->pos.y - 7.5f);
        }
        return;
    }

    Added = 0;
    if (DropType != DROP_MODELS) {
        WindVector.x = (float)(Float_FRand(0.0004f) - 0.0002f + WindVector.x);
        WindVector.z = (float)(Float_FRand(0.0004f) - 0.0002f + WindVector.z);
        if (WindVector.x < -0.01f)
            WindVector.x = -0.01f;
        else if (WindVector.x > 0.01f)
            WindVector.x = 0.01f;
        if (WindVector.z < -0.01f)
            WindVector.z = -0.01f;
        else if (WindVector.z > 0.01f)
            WindVector.z = 0.01f;
    }

    if (glb_players[0] == NULL)
        return;
    Vec_Copy(&viewer->pos, &CubeCentre);
    if (view == 0) {
        float yaw = glb_players[0]->rotation.y;
        CubeCentre.x = (float)(FSin(yaw) * Extended(7.5f) + Extended(CubeCentre.x));
        CubeCentre.z = (float)(FCos(yaw) * Extended(7.5f) + Extended(CubeCentre.z));
    }
    if (RainBox_Clip() == 0) {
        Added = 999;
        return;
    }

    Vec_Scalef(&WindVector, FRAME_RATE_MUL, &WindVector2);
    for (int i = 0; i < 32; i++) {
        Vec_Scalef(&RandTable1[i], FRAME_RATE_MUL, &RandTable2[i]);
        Vec_Add2(&WindVector2, &RandTable2[i], &RandTable2[i]);
    }

    if (DropType == DROP_MODELS) {
        for (short i = 0; i < CurrentMax; i++) {
            if (DropState[i] == 1) {
                DROP_tag *drop = &DropVector[i];
                Vec_Add2(&WindVector2, &drop->pos, &drop->pos);
                if (drop->pos.y < drop->end) {
                    DropState[i] = 0;
                    drop->pos.y = (float)(drop->pos.y - 1024.0f);
                }
            } else if (Added <= 20) {
                AddDrop(i, 0, DropState, DropVector);
            }
        }
        return;
    }

    // In batches, as the PS2 moved them through its scratchpad; a drop takes the step of its index in its batch
    int left = CurrentMax;
    _VECTOR *steps = (_VECTOR *)psiCopyToSP(RandTable2, 0, sizeof(RandTable2));
    int first = 0;
    while (left > 0) {
        int count = left > DROPS_A_BATCH ? DROPS_A_BATCH : left;
        left -= count;
        DROP_tag *drops = (DROP_tag *)psiCopyToSP(&DropVector[first], sizeof(RandTable2), count * sizeof(DROP_tag));
        for (short i = 0; i < count; i++) {
            if (DropState[first + i] == 1) {
                DROP_tag *drop = &drops[i];
                Vec_Add2(&steps[i & 0x1f], &drop->pos, &drop->pos);
                bool done = DropType == DROP_RISING ? drop->pos.y > drop->end : drop->pos.y < drop->end;
                if (done) {
                    DropState[first + i] = 0;
                    drop->pos.y = (float)(drop->pos.y - 1024.0f);
                }
            } else if (Added <= 20) {
                AddDrop(i, first, DropState, drops);
            }
        }
        psiCopyFromSP(sizeof(RandTable2), &DropVector[first], count * sizeof(DROP_tag));
        first += count;
    }
}

// The drops (Game_Draw): a particle list of them all, or a model at each live one and, on three levels, a splash
// turned to view 0 as well. Model drops are not drawn with view 0 under its cel's water.
// AUTOINJECT
void DrawDrops(void) {
    if (DropType == DROP_NONE || Added == 999)
        return;

    if (DropType != DROP_MODELS) {
        int texture = *(int *)hashtable_getitem(DropType == DROP_PARTICLES ? RAIN_TEXTURE : RISING_TEXTURE);
        psiDrawParticleList((const float *)DropVector, CurrentMax, 0xff808080, texture, NULL, 1, 0.2f);
        return;
    }

    cel_tag *cel = glb_viewer[0]->someCel;
    if (cel != NULL && cel->shearHeight > glb_viewer[0]->pos.y)
        return;

    // The w column is uninitialised stack in the original
    _MATRIX mtx;
    memset(&mtx, 0, sizeof(mtx));
    Mat_Identity(&mtx);
    celglist_tag *dropModel = hashtable_hashcode_to_celglist(DROP_MODEL);
    for (short i = 0; i < CurrentMax; i++) {
        if (DropState[i] != 1)
            continue;
        Vec_Copy(&DropVector[i].pos, (_VECTOR *)&mtx.m[12]);
        psiDrawObjectMatrix(dropModel, &mtx);
    }

    if (!Between(GameState.CurrentLevelHashcode, HEAVY_RAIN_FIRST, HEAVY_RAIN_LAST))
        return;
    Mat_CopyRot(&glb_viewer[0]->mtx, &mtx);
    psiSetTweakARGB(0x24, 0x20, 0x20, 0x20);
    celglist_tag *splash = hashtable_hashcode_to_celglist(SPLASH_MODEL);
    for (short i = 0; i < CurrentMax; i++) {
        if (DropState[i] != 1)
            continue;
        Vec_Copy(&DropVector[i].pos, (_VECTOR *)&mtx.m[12]);
        psiDrawObjectMatrix(splash, &mtx);
    }
}

// Each box cut to the cube about CubeCentre; how many have anything left in x and z
// AUTOINJECT
ushort RainBox_Clip(void) {
    ushort active = 0;
    for (obj_tag *box = RainBoxRoot; box != NULL; box = box->maybeParent) {
        RAINBOX_tag *rain = (RAINBOX_tag *)box->extraObjectData;
        celglist_tag *glist = box->objGraphics;
        // The x bounds are compared unrounded, as the x87 keeps them
        double minX = (double)glist->extentMin.x + box->position.x;
        float cube = (float)((double)CubeCentre.x - 15.0f);
        if (!(minX > cube))
            minX = cube;
        rain->min[0] = (float)minX;
        double bound = (double)glist->extentMin.y + box->position.y;
        cube = (float)((double)CubeCentre.y - 15.0f);
        rain->min[1] = bound > cube ? (float)bound : cube;
        bound = (double)glist->extentMin.z + box->position.z;
        cube = (float)((double)CubeCentre.z - 15.0f);
        float minZ = bound > cube ? (float)bound : cube;
        rain->min[2] = minZ;

        double maxX = (double)glist->extentMax.x + box->position.x;
        cube = (float)((double)CubeCentre.x + 15.0f);
        if (!(maxX < cube))
            maxX = cube;
        rain->max[0] = (float)maxX;
        bound = (double)glist->extentMax.y + box->position.y;
        cube = (float)((double)CubeCentre.y + 15.0f);
        rain->max[1] = bound < cube ? (float)bound : cube;
        bound = (double)glist->extentMax.z + box->position.z;
        cube = (float)((double)CubeCentre.z + 15.0f);
        float maxZ = bound < cube ? (float)bound : cube;
        rain->max[2] = maxZ;

        if (maxX > minX && maxZ > minZ) {
            rain->active = 1;
            active++;
        } else {
            rain->active = 0;
        }
    }
    return active;
}

// A rain box: not drawn, not in a cel, last on RainBoxRoot's list
// AUTOINJECT
obj_tag* RainBox_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(RAINBOX_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_GFX;
    if (RainBoxRoot == NULL) {
        RainBoxRoot = obj;
        return obj;
    }
    obj_tag *last = RainBoxRoot;
    while (last->maybeParent != NULL)
        last = last->maybeParent;
    last->maybeParent = obj;
    return obj;
}

// AUTOINJECT
void InitRainBox(void) {
    RainBoxRoot = NULL;
}
