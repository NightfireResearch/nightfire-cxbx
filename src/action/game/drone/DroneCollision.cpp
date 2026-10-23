// A drone's body against the world. Each frame NDrone2_Collision sets up the drone's capsule (Drone_tag.collision)
// for the collision update: from the position up to the radius and down to the feet, a sphere at the feet for a
// stunned drone, or along the body of a dead one. Then Drone_CollisionHandler, the drone's collide function, applies
// the capsule's push, hands each hit to NDrone2_DealWithObjHit, marks the contacts in the anim flags, finds the floor
// (Drone_FeetOnPoint) and moves the drone by its velocity, falling (NDrone2_Fall) unless it stands on something. A
// flying drone only drifts by its velocity.

#include "DroneCollision.h"
#include "NDrone2_Damage.h"            // NDrone2_DealWithObjHit
#include "../../game.h"                // GameState
#include "../../engine/AINetwork.h"    // AINetwork_GetBoundsPushVectorForSphere
#include "../../engine/Anim.h"         // AnimGetBoneWorldTrans, ANIM_CONTACT_*
#include "../../engine/Collide.h"      // Collide_RayIntersect, Collide_FreeHitList
#include "../../engine/viewer.h"       // cel_tag
#include "../../math/math.h"
#include "../obj/Player.h"           // PlayerOrDrone_FeetOnPoint_DoCollision

#include <string.h>

#pragma fp_contract(off)

// Drone_tag.flags
#define DRONE_FLAG_FALLS_ANYWAY 0x4     // NDrone2_DoGravity: falls whether it collides or not
#define DRONE_FLAG_COLLIDES     0x20
#define DRONE_FLAG_DAMPED       0x40    // flying: its velocity loses 2% a frame
#define DRONE_FLAG_FALLS        0x80
#define DRONE_FLAG_DEAD         0x200
#define DRONE_FLAG_4000         0x4000  // cleared by Drone_CollisionHandler
#define DRONE_FLAG_BOUNDS       0x80000000  // the AI network's bounds push the drone back

// Drone_tag.unknown46c: an anim that does not collide
#define DRONE_ANIM_NO_COLLISION 2

// The bones a dead drone's capsule runs along
#define BONE_HEAD   0x80000005
#define BONE_37     0x80000037
#define BONE_33     0x80000033


// ---- when the drone collides and falls

// Whether any player is within 2 m (Drone_tag.playerDistance)
// AUTOINJECT
bool DroneFunc_NearPlayerCollision(Drone_tag *drone) {
    for (int i = 0; i < 4; i++) {
        if (glb_players[i] != NULL && drone->playerDistance[i] < 2.0f)
            return true;
    }
    return false;
}

// Whether the capsule collides this frame: a drone with DRONE_FLAG_COLLIDES, not dead (except in Tower2Elevator).
// Skin classes 0xc and 0x13 always; the others while an anim script runs, near a player, drawn lately, or unless the
// anim does not collide.
// AUTOINJECT
bool NDrone2_DoCollision(DCVars_tag *dcv) {
    Drone_tag *drone = dcv->drone;
    if ((drone->flags & DRONE_FLAG_DEAD) && GameState.CurrentLevelHashcode != HT_Level_Tower2Elevator)
        return false;
    if (!(drone->flags & DRONE_FLAG_COLLIDES))
        return false;
    if (drone->skinClass == SKINCLASS_ZOE || drone->skinClass == SKINCLASS_KIKO_COMBAT)
        return true;
    if (drone->unknown434 != NULL)
        return true;
    if (DroneFunc_NearPlayerCollision(drone))
        return true;
    if (dcv->gameObj->drawnCountdown != 0)
        return true;
    return !(drone->unknown46c & DRONE_ANIM_NO_COLLISION);
}

// Whether gravity acts: with DRONE_FLAG_FALLS, and then always with DRONE_FLAG_FALLS_ANYWAY, otherwise when it
// collides.
// AUTOINJECT
bool NDrone2_DoGravity(DCVars_tag *dcv) {
    uint flags = dcv->drone->flags;
    if (!(flags & DRONE_FLAG_FALLS))
        return false;
    if (flags & DRONE_FLAG_FALLS_ANYWAY)
        return true;
    return NDrone2_DoCollision(dcv);
}

// Moves the drone by its velocity, which gravity speeds up while it is off the floor; not more than 0.3 m under its
// cel's shear height. Without gravity its vertical velocity stops. Inline in their reading (our name).
// FUNC_AT(0x0003ef30)
void NDrone2_Fall(obj_tag *obj) {
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(obj, &dcv);
    if (!NDrone2_DoGravity(&dcv)) {
        drone->velocity.y = 0.0f;
        return;
    }
    if (obj->inCel != NULL && (double)obj->position.y + 0.3f < obj->inCel->shearHeight)
        return;
    if (!(obj->animState->animFlags & ANIM_CONTACT_FLOOR))
        auxVec_AddMulR32(&drone->velocity, &GRAVITY_VECTOR, REC_FRAME_RATE, &drone->velocity);
    auxVec_AddMulR32(&obj->position, &drone->velocity, REC_FRAME_RATE, &obj->position);
}

// ---- the capsule

// Sets up the capsule. Alive, radius 0.4 (0.55 for skin class 0xc) from the radius above the position down to the
// feet, colliding when NDrone2_DoCollision says; with smallCollision, a 0.3 sphere at the feet. Dead, along the body
// from the head (radius the root height, at most 0.4), always colliding. With DRONE_FLAG_BOUNDS the AI network's
// bounds push the drone back.
// AUTOINJECT
void NDrone2_Collision(obj_tag *obj) {
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(obj, &dcv);
    float root = drone->rootHeight;
    HITTEST_tag *capsule = &drone->collision;
    capsule->mask = 0;
    capsule->cel = obj->inCel;

    if (drone->flags & DRONE_FLAG_DEAD) {
        capsule->radius = 0.4f < root ? 0.4f : root;
        _VECTOR head, bone37, bone33, axis;
        AnimGetBoneWorldTrans(obj, BONE_HEAD, 0, &head, NULL);
        AnimGetBoneWorldTrans(obj, BONE_37, 0, &bone37, NULL);
        AnimGetBoneWorldTrans(obj, BONE_33, 0, &bone33, NULL);
        Vec_Subtract(&head, &bone37, &axis);
        float length = (float)(Vec_NormaliseLen(&axis, &axis) * 0.5f);
        double span = Vec_Dist3D(&head, &bone33) * 0.5f;
        if (!(length > span))
            length = (float)span;
        capsule->damage = 0;
        capsule->type = HITTEST_CYLINDER;

        auxVec_AddMulR32(&obj->position, &axis, 0.4f, &capsule->rayStart);
        auxVec_AddMulR32(&obj->position, &axis, (float)-((double)length - 0.4f), &capsule->rayEnd);
    } else if (drone->smallCollision) {
        bool collides = NDrone2_DoCollision(&dcv);
        capsule->radius = 0.3f;
        capsule->damage = 0;
        capsule->type = collides ? HITTEST_CYLINDER : 0;

        _VECTOR up;
        Mat_GetUp(&up, &obj->transformMatrix);
        auxVec_AddMulR32(&obj->position, &up, (float)-((double)root - 0.3f), &capsule->rayEnd);
        Vec_Copy(&capsule->rayEnd, &capsule->rayStart);
    } else {
        float radius = drone->skinClass == SKINCLASS_ZOE ? 0.55f : 0.4f;
        bool collides = NDrone2_DoCollision(&dcv);
        capsule->radius = radius;
        capsule->damage = 0;
        capsule->type = collides ? HITTEST_CYLINDER : 0;

        _VECTOR up;
        Mat_GetUp(&up, &obj->transformMatrix);
        auxVec_AddMulR32(&obj->position, &up, radius, &capsule->rayStart);
        auxVec_AddMulR32(&obj->position, &up, (float)-((double)root - radius), &capsule->rayEnd);
    }

    obj->unknown_0xd9 = 1;
    if (drone->flags & DRONE_FLAG_BOUNDS) {
        _VECTOR push;
        AINetwork_GetBoundsPushVectorForSphere(&obj->position, obj->inCel, 0.4f, &push, drone->boundsPushFlags);
        Vec_Add2(&obj->position, &push, &obj->position);
    }
}

// ---- the floor and the hits

// Casts a ray from the position to 0.6 m below the capsule's feet. The first hit is the floor when the feet are less
// than 0.2 m above it (the player only by its joints 1 to 5); *height gets the feet's height above the first hit,
// *slope its normal's y. On the floor, the first hit that may be stood on gives the drone its movement; the floor's
// hit goes to the anim, with ANIM_CONTACT_FLOOR (also when the capsule says so). Returns what it stands on.
// AUTOINJECT
obj_tag* Drone_FeetOnPoint(obj_tag *obj, HITTEST_tag *test, float *slope, int unused, float *height) {
    if (obj == NULL)
        return NULL;
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    HITDATA_tag *hits = NULL;
    HITDATA_tag *floor = NULL;
    _VECTOR up, below;
    Mat_GetUp(&up, &obj->transformMatrix);
    if (slope != NULL)
        *slope = 1.0f;
    obj->animState->floor.materialType = 0;
    auxVec_AddMulR32(&test->rayEnd, &up, -(drone->rootHeight + 0.6f), &below);
    Collide_RayIntersect(&obj->position, &below, obj->inCel, obj, NULL, &hits, 0, 0xc, 8);

    if (hits != NULL) {
        double drop = ((double)obj->position.y - drone->rootHeight) - hits->hitPosition.y;
        if (height != NULL)
            *height = (float)drop;
        if (drop < 0.2f) {
            if (hits->hitObj == NULL || hits->hitObj != glb_players[0])
                floor = hits;
            else if (hits->hitBoneIdx >= 1 && hits->hitBoneIdx <= 5)
                floor = hits;
            if (slope != NULL)
                *slope = hits->surfaceNormal.b;
            if (floor != NULL && floor->hitObj != NULL) {
                for (HITDATA_tag *hit = hits; hit != NULL; hit = hit->next) {
                    if (hit->hitCel != NULL && (hit->hitCel->flags84 & CEL_NO_CONTACT))
                        continue;
                    if (hit->hitObj != NULL && (hit->hitObj->effectFlags & FLAG_UNKNOWN_40))
                        continue;
                    PlayerOrDrone_FeetOnPoint_DoCollision(obj, hit);
                    break;
                }
            }
        }
    }

    if (test->found != 0 || floor != NULL) {
        obj->animState->animFlags |= ANIM_CONTACT_FLOOR;
        if (floor != NULL)
            memcpy(&obj->animState->floor, floor, sizeof(HITDATA_tag));
    }
    Collide_FreeHitList(&hits);
    // Read from the hit after the list is freed, as the original does
    return floor != NULL ? floor->hitObj : NULL;
}

// Applies the capsule's push and hands each of the object's hits to NDrone2_DealWithObjHit. Returns the first hit
// that counts as a contact. Inline in Drone_CollisionHandler (our name).
static HITDATA_tag *Drone_DealWithHits(obj_tag *obj, Drone_tag *drone) {
    HITDATA_tag *contact = NULL;
    HITDATA_tag *hit = obj->hitList;
    if (hit == NULL)
        return NULL;
    Vec_Add2(&drone->collision.push, &obj->position, &obj->position);
    for (; hit != NULL; hit = hit->next) {
        NDrone2_DealWithObjHit(drone, obj, hit);
        if (!(hit->flags & HIT_NO_CONTACT) && contact == NULL)
            contact = hit;
    }
    return contact;
}

// After the collision update (the drone's collide function). Clears the contacts, takes the hits and the floor, and
// moves the drone. A contact sets ANIM_CONTACT_HIT, ANIM_CONTACT_ABOVE when above the capsule's top, and
// ANIM_CONTACT_FLOOR when less than 0.2 m below its bottom; on the floor the vertical velocity stops and the drone
// sinks an eighth of the way to it.
// AUTOINJECT
void Drone_CollisionHandler(obj_tag *obj) {
    if (!NPCGlobals.enabled || Drone_bDisableSystem) {
        Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
        drone->collision.type = 0;
        return;
    }
    Drone_tag *drone = (Drone_tag *)obj->extraObjectData;
    DCVars_tag dcv;
    Drone_DCVfromOBJ(obj, &dcv);
    drone->flags &= ~DRONE_FLAG_4000;
    obj->animState->animFlags &= ~(ANIM_CONTACT_FLOOR | ANIM_CONTACT_HIT | ANIM_CONTACT_ABOVE);
    obj->animState->animFlags &= ~(ANIM_CONTACT_HIT | ANIM_CONTACT_ABOVE | ANIM_CONTACT_MOVER);
    float height = 0.0f;
    float belowEnd = 0.0f;
    drone->touchingPlayer = 0;
    _VECTOR off;

    if (drone->flies) {
        Drone_FeetOnPoint(obj, &drone->collision, NULL, 0, NULL);
        HITDATA_tag *contact = Drone_DealWithHits(obj, drone);
        if (contact != NULL) {
            if (drone->dtype == DTYPE_Astronaut) {
                obj->animState->animFlags |= ANIM_CONTACT_HIT;
            } else {
                Vec_Subtract(&contact->hitPosition, &drone->collision.rayStart, &off);
                obj->animState->animFlags |= ANIM_CONTACT_HIT;
                if (off.y > 0.0f)
                    obj->animState->animFlags |= ANIM_CONTACT_ABOVE;
                Vec_Subtract(&contact->hitPosition, &drone->collision.rayEnd, &off);
                if (off.y < 0.0f)
                    obj->animState->animFlags |= ANIM_CONTACT_FLOOR;
            }
        }
        auxVec_AddMulR32(&obj->position, &drone->velocity, REC_FRAME_RATE, &obj->position);
        if (drone->flags & DRONE_FLAG_DAMPED)
            Vec_MulR32(&drone->velocity, &drone->velocity, 0.98f);
        obj->transformFlags |= TRANSFORM_MOVED;
        if ((obj->animState->animFlags & ANIM_CONTACT_FLOOR) && drone->dtype != DTYPE_Astronaut)
            drone->velocity.y = 0.0f;
        return;
    }

    if (NDrone2_DoCollision(&dcv)) {
        Drone_FeetOnPoint(obj, &drone->collision, NULL, 0, &height);
        HITDATA_tag *contact = Drone_DealWithHits(obj, drone);
        if (contact != NULL) {
            Vec_Subtract(&contact->hitPosition, &drone->collision.rayStart, &off);
            obj->animState->animFlags |= ANIM_CONTACT_HIT;
            if (off.y > 0.0f)
                obj->animState->animFlags |= ANIM_CONTACT_ABOVE;
            Vec_Subtract(&contact->hitPosition, &drone->collision.rayEnd, &off);
            belowEnd = off.y;
            if (off.y > -0.2f && off.y < 0.0f)
                obj->animState->animFlags |= ANIM_CONTACT_FLOOR;
        }
        NDrone2_Fall(obj);
    } else if (NDrone2_DoGravity(&dcv)) {
        NDrone2_Fall(obj);
    } else {
        drone->velocity.y = 0.0f;
    }
    obj->transformFlags |= TRANSFORM_MOVED;
    if (obj->animState->animFlags & ANIM_CONTACT_FLOOR) {
        drone->velocity.y = 0.0f;
        if (belowEnd > -0.1f)
            obj->position.y = (float)(obj->position.y - (double)height * 0.125f);
    }
}
