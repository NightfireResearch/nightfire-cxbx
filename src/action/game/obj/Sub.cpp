#include "Sub.h"

#include "bullet.h"
#include "control.h"
#include "Light.h"
#include "Player.h"
#include "../Spline.h"
#include "../view.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Anim.h"
#include "../../engine/Camera.h"
#include "../../engine/Collide.h"
#include "../../math/X87Ops.h"
#include "../../sound/music.h"
#include "../../util/Random.h"

#pragma fp_contract(off)

// The sub (OBJECTTYPE_SUB). No level places one.

// Its weapons (weapon_data)
#define WEAPON_SUB_LASER    0x67
#define WEAPON_SUB_MISSILE  0x68
#define WEAPON_SUB_TORPEDO  0x30

// The datums its missiles and torpedoes leave from
// XBE_GLOBAL(0x0018cbec, 0x8)
static const uint SubMissileDatums[2] = {4, 5};
// XBE_GLOBAL(0x0018cbf4, 0x10)
static const uint SubTorpedoDatums[4] = {0, 1, 2, 3};

// Waits for its switch channel, if it has one
static bool Switched(SubInfo *sub) {
    return sub->channel == 0 || switch_channels[sub->channel];
}

// Along its path at the placement's rate, with a cyan light, its anim looping
// AUTOINJECT
obj_tag* Sub_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    obj_tag *obj = Control_CreateObjEx(sizeof(SubInfo), pos, rot, NULL, NULL, NULL, 1, 0x44, 1.0f, 0, 0xff, 0xff, 0xff);
    SubInfo *sub = (SubInfo *)obj->extraObjectData;
    obj->objectType = OBJECTTYPE_SUB;
    obj->curState = 0;
    sub->channel = (ushort)lvl->keys[0];
    sub->path = lvl->spline;
    sub->lastKey = lvl->splinePoints != 0 ? lvl->splinePoints - 1 : 0;
    sub->t = 0.0f;
    double keys = (double)sub->lastKey * REC_FRAME_RATE;
    sub->health = 300.0f;
    sub->beam = NULL;
    sub->speed = (float)((double)_FRAME_RATE / lvl->splineLength * keys);
    sub->light = Light_Create(pos, 0, 0xff, 0xff, 45.0f, 0, -1, 1, 1.0f, 0, 0, 0, 0xffff);
    AnimObjectNew(obj, SUB_SKIN);
    sAnimScript_tag *script = AnimScriptAdd(obj, SUB_ANIM_MOVE);
    script->flags |= ANIMSCRIPT_LOOP;
    sub->height = AnimScriptPeekSeq(SUB_ANIM_MOVE)->boundSphere[3];
    sub->seabedGap = -3.0f;
    return obj;
}

// Along the path while there is path left
void Sub_Follow(obj_tag *obj, SubInfo *sub) {
    AnimObjectUpdate(obj);
    if (!Switched(sub))
        return;
    ushort last = sub->lastKey;
    if (!((double)last >= sub->t))
        return;
    Spline_Interp(sub->path, 0, last, sub->t, &obj->transformMatrix, NULL, 2);
    obj->transformFlags |= TRANSFORM_MOVED;
    sub->t = (float)((double)FRAME_RATE_MUL * sub->speed + sub->t);
}

// Player 1 within 8 of it, between 0.9 behind and 0.25 ahead of its middle, is hurt by 1 a frame
void Sub_Ram(obj_tag *obj, SubInfo *sub) {
    if (!Switched(sub) || glb_players[0] == NULL)
        return;
    _VECTOR at, local;
    Vec_Copy(&glb_players[0]->position, &at);
    _MATRIX toSub;
    Mat_Copy(&obj->transformMatrix, &toSub);
    Mat_InverseSelf(&toSub);
    RotPreTransVec(&toSub, &at, &local);
    if (!(local.z > -0.9f) || !(local.z < 0.25f))
        return;
    if (!(Vec_SqMagnitude(&local) < 64.0f))
        return;
    Player_Hurt(glb_players[0], 1.0f, &at);
}

// With a beam: within 12 of a player, for 0.3 of every 1.8 seconds, a laser bolt at player 1's chest, a little off,
// the beam drawn along it. Otherwise the beam is not drawn.
void Sub_Laser(obj_tag *obj, SubInfo *sub) {
    if (sub->beam == NULL || !Switched(sub))
        return;
    sub->laserTime = (float)((double)REC_FRAME_RATE + sub->laserTime);
    _VECTOR closest;
    if (control_ClosestPlayerPnt(Mat_Position(obj->transformMatrix), &closest) < 144.0f && sub->laserTime > 1.5f) {
        if (sub->laserTime > 1.8f)
            sub->laserTime = 0.0f;
        _VECTOR target, dir;
        Vec_Copy(&glb_players[0]->position, &target);
        target.y = (float)((double)target.y - 0.5f);
        Vec_Subtract(&target, &obj->centrePoint, &dir);
        Rand_FRand_MVar2_Vec(&dir, 0.2f, 0.1f);
        sub->beam->scale = (float)Negate(Vec_NormaliseLen(&dir, &dir));
        Bullet_init(-1, obj, obj, &weapon_data[WEAPON_SUB_LASER], &obj->centrePoint, &dir, 0x12);
        Mat_Align2Dir(&sub->beam->transformMatrix, &dir, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
        Vec_Copy(&obj->centrePoint, Mat_Position(sub->beam->transformMatrix));
        sub->beam->transformFlags |= TRANSFORM_MOVED;
        View_SetDrawInAllViews(sub->beam);
        return;
    }
    View_SetDrawInNoViews(sub->beam);
}

// Within 15 of a player, the music is told, and every 10 seconds a missile leaves from one of two datums
void Sub_FireMissiles(obj_tag *obj, SubInfo *sub) {
    if (!Switched(sub))
        return;
    sub->missileTime = (float)((double)REC_FRAME_RATE + sub->missileTime);
    _VECTOR closest;
    if (control_ClosestPlayerPnt(Mat_Position(obj->transformMatrix), &closest) > 225.0f)
        return;
    Music_Event(2, 1);
    if (!(sub->missileTime > 10.0f))
        return;
    ushort which = (ushort)Rand_Rand(2);
    sub->missileTime = 0.0f;
    _VECTOR at, dir;
    _MATRIX datum;
    AnimGetBoneWorldTrans(obj, SubMissileDatums[which], 0, &at, &datum);
    Mat_GetDir(&dir, &datum);
    Bullet_init(-1, obj, obj, &weapon_data[WEAPON_SUB_MISSILE], &at, &dir, 0xffff);
}

// Within 25 of a player, the music is told, and every 5 seconds, if it can see him past its own bulk, a torpedo from
// one of four datums that faces him, a little off; half of them home on player 1
void Sub_DeployTorpedoes(obj_tag *obj, SubInfo *sub) {
    if (!Switched(sub))
        return;
    _VECTOR *at = Mat_Position(obj->transformMatrix);
    _VECTOR closest;
    float distance = control_ClosestPlayerPnt(at, &closest);
    sub->torpedoTime = (float)((double)REC_FRAME_RATE + sub->torpedoTime);
    if (distance > 625.0f)
        return;
    Music_Event(2, 1);
    if (!(sub->torpedoTime > 5.0f))
        return;

    _VECTOR towards, clear;
    Vec_Subtract(&closest, at, &towards);
    Vec_Normalise(&towards, &towards);
    auxVec_AddMulR32(at, &towards, (float)((double)sub->height + 1.0f), &clear);
    if (!Collide_LineOfSight(&clear, &closest, obj->inCel, obj, glb_players[0], 0x40d))
        return;
    sub->torpedoTime = 0.0f;
    ushort which = (ushort)Rand_Rand(4);
    _VECTOR from, dir;
    _MATRIX datum, toDatum;
    AnimGetBoneWorldTrans(obj, SubTorpedoDatums[which], 0, &from, &datum);
    Mat_GetDir(&dir, &datum);
    Mat_Copy(&datum, &toDatum);
    Mat_InverseSelf(&toDatum);
    RotPreTransVec(&toDatum, &closest, &towards);
    if (!(towards.z > 0.0f))
        return;
    auxVec_AddMulR32(&from, &dir, 0.8f, &from);
    dir.x = (float)(Rand_FRandHalf(0.25f) + dir.x);
    dir.y = (float)(Rand_FRandHalf(0.25f) + dir.y);
    Vec_Normalise(&dir, &dir);
    obj_tag *torpedo = Bullet_init(-1, obj, obj, &weapon_data[WEAPON_SUB_TORPEDO], &from, &dir, 0xffff);
    if (Rand_Rand(0x5b7f) & 1)
        ((BU_tag *)torpedo->extraObjectData)->heatseekTarget = glb_players[0];
}

// Afloat (state 0) it moves and attacks, its light with it; sinking (1, its light gone) it eases down to sinkY, a
// frame's worth of the gap a frame, and lies there (2)
// AUTOINJECT
void Sub_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    SubInfo *sub = (SubInfo *)obj->extraObjectData;
    switch ((short)obj->curState) {
        case 0:
            Sub_Follow(obj, sub);
            Sub_Ram(obj, sub);
            Sub_Laser(obj, sub);
            Sub_FireMissiles(obj, sub);
            Sub_DeployTorpedoes(obj, sub);
            Vec_Copy(&obj->centrePoint, &sub->light->position);
            break;
        case 1: {
            if (sub->light != NULL)
                Light_Delete(sub->light);
            sub->light = NULL;
            _VECTOR *at = Mat_Position(obj->transformMatrix);
            at->y = moveTowardsValue(at->y, sub->sinkY, REC_FRAME_RATE);
            obj->transformFlags |= TRANSFORM_MOVED;
            AnimObjectUpdate(obj);
            if (AbsFchs((double)at->y - sub->sinkY) < 0.02f)
                obj->curState = 2;
            break;
        }
    }
}

// Once a frame: past its health it plays its sinking anim and sinks to 3 above whatever is below it (down to 25,
// passing other subs and anim objects), or stays where it is
// AUTOINJECT
void Sub_ApplyDamage(obj_tag *obj, float damage) {
    SubInfo *sub = (SubInfo *)obj->extraObjectData;
    if (sub->lastHitFrame == GameState.NumFramesUnpaused)
        return;
    double health = (double)sub->health - damage;
    sub->health = (float)health;
    sub->lastHitFrame = GameState.NumFramesUnpaused;
    if (!(health < 0.0f))
        return;

    AnimSetDeleteAll(obj);
    AnimScriptAdd(obj, SUB_ANIM_SINK)->flags &= ~ANIMSCRIPT_LOOP;
    sub->sinkY = Mat_Position(obj->transformMatrix)->y;
    _VECTOR below;
    Vec_Copy(&obj->centrePoint, &below);
    below.y = (float)((double)below.y - 25.0f);
    HITDATA_tag *hits;
    Collide_RayIntersect(&obj->centrePoint, &below, obj->inCel, obj, NULL, &hits, 0, 0, 0);
    for (HITDATA_tag *hit = hits; hit != NULL; hit = hit->next) {
        obj_tag *what = hit->hitObj;
        if (what != NULL && (what->objectType == OBJECTTYPE_SUB || what->objectType == OBJECTTYPE_ANIMOBJECT))
            continue;
        sub->sinkY = (float)(AbsFchs((double)sub->seabedGap) + hit->hitPosition.y);
        break;
    }
    Collide_FreeHitList(&hits);
    obj->curState = 1;
}
