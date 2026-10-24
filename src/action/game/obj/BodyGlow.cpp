#include "BodyGlow.h"

#include "build.h"
#include "control.h"
#include "Player.h"
#include "../drone/Drone.h"
#include "../view.h"
#include "../../engine/Anim.h"
#include "../../engine/viewer.h"
#include "../../ui/HUD.h"
#include "../../util/hashtable.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"

#include <math.h>
#include <stdint.h>

#pragma fp_contract(off)

// A glow around a drone or player, seen in the night vision or through the OICW's sight: a sprite at the owner's bone
// 5 and a beam along each of 15 bones, from the bone's parent. It fades with distance and goes once the owner has
// been dead for 0x100 frames. None while drones are switched off.

extern uint32_t switch_NO_DRONES;

// The bones the beams run along
// XBE_GLOBAL(0x0018cbdc, 0xf)
static const uchar BodyGlowBones[BODYGLOW_BEAMS] = {0, 19, 20, 21, 31, 32, 35, 49, 50, 51, 52, 53, 54, 55, 56};

// The glow and its beams start drawn in no view. The objects are used unchecked.
// AUTOINJECT
obj_tag* BodyGlow_Create(obj_tag *owner) {
    if (owner == NULL || switch_NO_DRONES)
        return NULL;
    obj_tag *glow = control_create_object(sizeof(BODYGLOW_tag), &owner->position, &owner->rotation, NULL);
    glow->objectType = OBJECTTYPE_BODYGLOW;
    hashtable_set_object_to_entity_gfx(glow, HT_Entity_lazer_muzzle);
    glow->effectFlags |= EFFECT_BILLBOARD | EFFECT_XRAY_ONLY | EFFECT_DRAW_LAST;
    glow->scale = 6.0f;
    build_LinkToRoom(glow, 0, glb_world);
    View_SetDrawInNoViews(glow);

    BODYGLOW_tag *data = (BODYGLOW_tag *)glow->extraObjectData;
    data->owner = owner;
    data->life = 0x100;
    for (int i = 0; i < BODYGLOW_BEAMS; i++) {
        obj_tag *beam = control_create_object(0, &owner->position, &owner->rotation, NULL);
        hashtable_set_object_to_entity_gfx(beam, HT_Entity_BoneGlow);
        data->beams[i] = beam;
        beam->effectFlags |= EFFECT_XRAY_ONLY | EFFECT_DRAW_LAST;
        beam->objectType = OBJECTTYPE_GFX;
        build_LinkToRoom(beam, 0, glb_world);
        View_SetDrawInNoViews(beam);
    }
    return glow;
}

// AUTOINJECT
void BodyGlow_Delete(obj_tag *glow) {
    glow->flags |= OBJFLAG_DELETE;
    BODYGLOW_tag *data = (BODYGLOW_tag *)glow->extraObjectData;
    for (int i = 0; i < BODYGLOW_BEAMS; i++)
        data->beams[i]->flags |= OBJFLAG_DELETE;
}

// Drawn in no view, hidden (effect flag 0x10)
static void Hide(obj_tag *glow, BODYGLOW_tag *data) {
    View_SetDrawInNoViews(glow);
    glow->effectFlags |= EFFECT_HIDDEN;
    for (int i = 0; i < BODYGLOW_BEAMS; i++) {
        View_SetDrawInNoViews(data->beams[i]);
        data->beams[i]->effectFlags |= EFFECT_HIDDEN;
    }
}

// Shown to each other player's view in the night vision or with the OICW's sight up. With any OICW sight it is drawn
// through walls at full strength; otherwise it fades over 10 m from the first view and is hidden past that.
// AUTOINJECT
void BodyGlow_Update(obj_tag *glow) {
    if (switch_NO_DRONES)
        return;
    BODYGLOW_tag *data = (BODYGLOW_tag *)glow->extraObjectData;
    obj_tag *owner = data->owner;
    AnimState *anim = owner->animState;
    if (anim == NULL)
        return;
    AnimObj *animObj = &anim->animObj;

    Drone_tag *drone = NULL;
    if (owner->objectType == OBJECTTYPE_DRONE && !(owner->flags & OBJFLAG_DELETE)) {
        drone = (Drone_tag *)owner->extraObjectData;
        if (drone->health < 0.0f || drone->health == 0.0f)
            data->life--;
    }
    BLData *blData = NULL;
    if (owner->objectType == OBJECTTYPE_PLAYER && !(owner->flags & OBJFLAG_DELETE)) {
        blData = (BLData *)owner->extraObjectData;
        if (blData->health < 0.0f || blData->health == 0.0f)
            data->life--;
    }
    if (data->life <= 0) {
        BodyGlow_Delete(glow);
        if (drone != NULL)
            drone->bodyGlow = NULL;
        if (blData != NULL)
            blData->bodyGlow = NULL;
        return;
    }

    uint views = 0;
    bool throughWalls = false;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        if (glb_viewer[i] == NULL || blData == glb_blokes[i])
            continue;
        if (glb_viewer[i]->visionMode == VISION_XRAY)
            views |= 1 << i;
        if (glb_blokes[i]->hudInfo->pane[OICW].enabled) {
            views |= 1 << i;
            throughWalls = true;
        }
    }
    if (views == 0 || (drone != NULL && owner->curState == 1)) {
        Hide(glow, data);
        return;
    }

    double distance = Vec_Dist3D(&owner->position, &glb_viewer[0]->pos);
    if (throughWalls) {
        distance = 0.0f;
        for (int i = 0; i < BODYGLOW_BEAMS; i++)
            data->beams[i]->effectFlags &= ~(EFFECT_XRAY_ONLY | EFFECT_DRAW_LAST);
    }
    else {
        for (int i = 0; i < BODYGLOW_BEAMS; i++)
            data->beams[i]->effectFlags |= EFFECT_XRAY_ONLY | EFFECT_DRAW_LAST;
        if (distance > 10.0f) {
            Hide(glow, data);
            return;
        }
    }
    uchar alpha = (uchar)Ftol((1.0f - distance * 0.1f) * 128.0f * data->life * 0.00390625f);

    sAnimSkin_tag *skin = animObj->skin;
    psiBuildMatrixPalette(owner, animObj, 0);
    Vec_Copy(Mat_Position(animObj->boneMtxs[5]), &glow->position);
    views |= 0x10;
    View_SetDrawInViews(glow, (short)views);
    glow->transformFlags |= TRANSFORM_MOVED;
    glow->tweakA = alpha;
    glow->effectFlags &= ~EFFECT_HIDDEN;
    control_unlink_object(glow);
    control_link_object_to_cel(glow, data->owner->inCel);

    for (uint i = 0; i < BODYGLOW_BEAMS; i++) {
        obj_tag *beam = data->beams[i];
        uint bone = BodyGlowBones[i];
        uint parent = skin->bones[bone] & 0x7f;
        if (bone == 0)
            parent = 4;
        _VECTOR *at = Mat_Position(animObj->boneMtxs[bone]);
        _VECTOR *from = Mat_Position(animObj->boneMtxs[parent]);
        _VECTOR along, toView;
        Vec_Subtract(at, from, &along);
        Vec_Subtract(&glb_viewer[0]->pos, at, &toView);
        Mat_Align2Dir(&beam->transformMatrix, &along, &toView, &toView);
        Matrix_SetTrans(at, &beam->transformMatrix);
        beam->scale = Vec_Dist3D(at, from);
        Mat_Scale3f(&beam->transformMatrix, &beam->transformMatrix, 1.5f, 1.0f, beam->scale);
        beam->transformFlags |= TRANSFORM_MOVED | TRANSFORM_MATRIX_PLACED | TRANSFORM_NO_SCALE;
        View_SetDrawInViews(beam, (short)views);
        beam->tweakA = alpha;
        beam->effectFlags &= ~EFFECT_HIDDEN;
        control_unlink_object(beam);
        control_link_object_to_cel(beam, data->owner->inCel);
    }
}
