#include "Creature.h"

#include "control.h"
#include "Player.h"
#include "../Spline.h"
#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../util/Random.h"
#include "../../math/X87Ops.h"
#include "../../math/math.h"

#pragma fp_contract(off)

// Creatures (OBJECTTYPE_CREATURE): things that swim between waypoints planned at random within range of where they
// were placed, turning towards a player close by, and fish, which keep below the water and are listed in FishList.

// XBE_GLOBAL(0x001df8b0, 0xc)
LLISTINFO_tag FishList;

#define CEL_KIND_WATER 0xc023   // the low word of cel_tag::bitsFromPlacementTag where fish live

// AUTOINJECT
void Creature_Init(void) {
    LList_Init(&FishList, sizeof(Creature), 0);
}

// AUTOINJECT
void Creature_Delete(obj_tag *obj) {
    Creature *creature = (Creature *)obj->extraObjectData;
    if (creature->kind == CREATURE_FISH)
        LList_Remove(&FishList, &creature->node);
}

// Hurts any player it touches, by 2
void Creature_HurtPlayers(obj_tag *obj) {
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitObj != NULL && hit->hitObj->objectType == OBJECTTYPE_PLAYER)
            Player_Hurt(hit->hitObj, 2.0f, &obj->position);
    }
}

// The next waypoint: dir turned by the angles (and kept so), then distance and size along it from the last, or where a
// ray that far first meets something, size out from it along its normal; there dir becomes the normal's.
void Creature_CalcPoint(_VECTOR *out, float distance, float size, _VECTOR *angles, _VECTOR *dir, _VECTOR *from,
                        obj_tag *obj) {
    HITDATA_tag *hits = NULL;
    _MATRIX turn;
    RotMatrix(angles, &turn);
    _VECTOR turned;
    ApplyMatrixLV(&turn, dir, &turned);
    Vec_Copy(&turned, dir);
    _VECTOR end, start;
    auxVec_AddMulR32(from, dir, (float)((double)distance + size), &end);
    Vec_Copy(from, &start);
    if (Collide_RayIntersect(&start, &end, obj->inCel, obj, NULL, &hits, 0, 0x20d, 0)) {
        Mat_Align2Dir(&turn, (_VECTOR *)&hits->surfaceNormal, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
        Mat_GetNorm(dir, &turn);
        auxVec_AddMulR32(&hits->hitPosition, (_VECTOR *)&hits->surfaceNormal, size, out);
        Collide_FreeHitList(&hits);
    }
    else {
        auxVec_AddMulR32(&end, dir, -size, out);
    }
}

// With seeks set and player 1 within 4 sizes and 1.1, dir becomes the way to him
bool Creature_SeekPlayer(Creature *creature, obj_tag *obj, _VECTOR *dir) {
    if (!creature->seeks || glb_players[0] == NULL)
        return false;
    float reach = (float)((double)creature->size * 4.0f + 1.1f);
    if (!((double)reach * reach > Vec_SqDist3D(&glb_players[0]->position, &obj->position)))
        return false;
    Vec_Subtract(&glb_players[0]->position, &obj->position, dir);
    Vec_Normalise(dir, dir);
    return true;
}

// The first waypoint is where it is, turned as the last was; each of the rest is planned from the one before along
// the way it faces, turned at random (up to a half turn either way, a fifth of that towards a player close by) and up
// to range out, but never further than range from home. A fish's turn a quarter either way across and a 16th up or
// down, size out, and below the water. Then a spline of fish's faces along its way.
// AUTOINJECT
void Creature_CalcWaypoints(obj_tag *obj, Creature *creature) {
    cel_tag *cel = obj->inCel;
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->transformMatrix);
    creature->waypoints[0].pos = obj->position;
    Quat_Copy(&creature->waypoints[0].quat, &creature->waypoints[CREATURE_WAYPOINTS - 1].quat);
    creature->t = 0.0f;
    double extra = Float_FRand(creature->speedRange * 0.5f);
    creature->speed = (float)((extra + (double)creature->speedRange * 0.5f) * REC_FRAME_RATE);

    for (ushort i = 1; i < CREATURE_WAYPOINTS; i++) {
        _VECTOR *at = &creature->waypoints[i].pos;
        float turnX = 1.0f, turnY = 1.0f;
        if (i > 1 && Creature_SeekPlayer(creature, obj, &dir)) {
            turnX = 0.2f;
            turnY = 0.2f;
        }
        _VECTOR angles;
        angles.z = 0.0f;
        if (creature->kind != CREATURE_FISH) {
            float distance = (float)(Float_FRand(creature->range * 0.5f) + (double)creature->range * 0.5f);
            angles.x = (float)(Rand_FRandHalf(PI) * turnX);
            angles.y = (float)(Rand_FRandHalf(PI) * turnY);
            Creature_CalcPoint(at, distance, creature->size, &angles, &dir, &creature->waypoints[i - 1].pos, obj);
            _VECTOR away;
            Vec_Subtract(at, &creature->home, &away);
            if ((double)creature->range * creature->range < Vec_SqMagnitude(&away)) {
                Vec_NormaliseLen(&away, &away);
                auxVec_AddMulR32(&creature->home, &away, creature->range, at);
            }
        }
        else {
            angles.y = (float)Rand_FRandHalf(HALF_PI);
            angles.x = (float)Rand_FRandHalf(0.19634955f);
            Creature_CalcPoint(at, creature->size, creature->size, &angles, &dir, &creature->waypoints[i - 1].pos,
                               obj);
            if ((double)cel->shearHeight - creature->size < at->y)
                at->y = (float)((double)cel->someMax.y - creature->size);
        }
    }

    if (creature->splineMode != 0) {
        for (int i = 1; i < CREATURE_WAYPOINTS; i++) {
            _VECTOR along, angles;
            Vec_Subtract(&creature->waypoints[i].pos, &creature->waypoints[i - 1].pos, &along);
            vecutil_cartesian_to_spherical_acc(&angles, along.x, -along.y, along.z);
            Quat_EulerToQuat(&angles, &creature->waypoints[i].quat);
        }
    }
}

// Placed at the furthest z of its model's bounds from its centre, facing as placed at every waypoint. A fish
// anywhere but in water is not created.
// AUTOINJECT
obj_tag* Creature_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl) {
    obj_tag *obj = Control_CreateObjEx(sizeof(Creature), pos, rot, NULL, celgl, NULL, 1, 0x44, 1.0f, 0, 0xff, 0xff,
                                       0xff);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_CREATURE;
    Creature *creature = (Creature *)obj->extraObjectData;
    CreaturePlacement *keys = (CreaturePlacement *)lvl;
    creature->gameObj = obj;
    creature->state = CREATURE_PLAN;
    creature->kind = (ushort)keys->kind;
    creature->range = (float)keys->range;
    creature->speedRange = (float)((double)keys->speed * 0.01f);
    creature->seeks = (char)keys->seeks;
    creature->hurts = (char)keys->hurts;
    float furthest = AbsFchs((double)celgl->extentMax.z) > AbsFchs((double)celgl->extentMin.z) ? celgl->extentMax.z
                                                                                                 : celgl->extentMin.z;
    creature->size = (float)AbsFchs((double)furthest);
    Vec_Copy(pos, &creature->home);

    quaternion_tag facing;
    Quat_EulerToQuat(rot, &facing);
    for (int i = 0; i < CREATURE_WAYPOINTS; i++)
        Quat_Copy(&creature->waypoints[i].quat, &facing);

    switch (creature->kind) {
        case CREATURE_SWIMS:
            creature->splineMode = 0;
            break;
        case CREATURE_FISH:
            if ((obj->inCel->bitsFromPlacementTag & 0xffff) != CEL_KIND_WATER) {
                control_delete_object(obj);
                return NULL;
            }
            creature->splineMode = 2;
            LList_Add(&FishList, &creature->node);
            break;
    }
    creature->state = CREATURE_MOVE;
    Creature_CalcWaypoints(obj, creature);
    return obj;
}

// Planning, moving along the spline (planning again at its end), and dying: hidden (effect flag 0x20) the frame after
void Creature_Move(obj_tag *obj, Creature *creature) {
    switch ((ushort)creature->state) {
        case CREATURE_PLAN:
            Creature_CalcWaypoints(obj, creature);
            creature->state = CREATURE_MOVE;
            break;
        case CREATURE_MOVE: {
            obj->transformFlags |= TRANSFORM_MOVED;
            Spline_Interp(creature->waypoints, 0, 5, creature->t, &obj->transformMatrix, NULL, creature->splineMode);
            double t = (double)FRAME_RATE_MUL * creature->speed + creature->t;
            creature->t = (float)t;
            if (t > 5.0f)
                creature->state = CREATURE_PLAN;
            break;
        }
        case CREATURE_DIE:
            creature->state = CREATURE_DEAD;
            break;
        case CREATURE_DEAD:
            obj->effectFlags |= EFFECT_NO_COLLISION;
            break;
    }
}

// Not while its cel is out of sight
// AUTOINJECT
void Creature_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    Creature *creature = (Creature *)obj->extraObjectData;
    if (obj->inCel != NULL && !obj->inCel->addedToDraw)
        return;
    if (creature->hurts)
        Creature_HurtPlayers(obj);
    Creature_Move(obj, creature);
}
