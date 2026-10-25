#include "Gas.h"

#include <math.h>

#include "../../game.h"
#include "../../engine/viewer.h"
#include "build.h"
#include "control.h"
#include "Effect.h"
#include "Env.h"
#include "../../math/X87Ops.h"
#include "../../math/math.h"

#pragma fp_contract(off)

// GAS_SOURCE and GAS_WIND_SOURCE stay put, give off smoke and are hidden; GAS_WIND goes with the wind; the rest rise
// at a unit a second unless given a velocity (a second's)
// AUTOINJECT
obj_tag* Gas_Init(cel_tag *cel, _VECTOR *pos, _VECTOR *rot, _VECTOR *vel, float scale0, float scale1, HASHCODE entity,
                  uint kind, uint colour0, uint colour1, ushort life) {

    if (cel == NULL || life == 0)
        return NULL;

    bool source = kind >= GAS_SOURCE && kind <= GAS_WIND_SOURCE;
    obj_tag *obj = control_create_object(source ? sizeof(GAS_tag) : offsetof(GAS_tag, colour0), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    GAS_tag *gas = (GAS_tag *)obj->extraObjectData;
    if (source) {
        gas->entity = entity;
        gas->scale0 = scale0;
        gas->life0 = life;
        gas->colour0 = colour0;
        gas->colour1 = colour1;
        gas->scale1 = scale1;
    }

    gas->kind = (uchar)kind;
    gas->life = life;
    double perFrame = 1.0f / (double)life;
    obj->effectFlags |= EFFECT_BIT9;
    obj->scale = scale0;
    obj->tweakR = colour0 >> 24;
    obj->tweakB = colour0 >> 8;
    obj->objectType = OBJECTTYPE_GAS;
    obj->tweakG = colour0 >> 16;
    obj->tweakA = colour0;

    gas->scaleStep = (float)(((double)scale1 - scale0) * perFrame);
    gas->colourStep[0] = (float)(((double)(colour1 >> 24) - (double)(colour0 >> 24)) * perFrame);
    gas->colourStep[1] = (float)(((double)((colour1 >> 16) & 0xff) - (double)((colour0 >> 16) & 0xff)) * perFrame);
    gas->colourStep[2] = (float)(((double)((colour1 >> 8) & 0xff) - (double)((colour0 >> 8) & 0xff)) * perFrame);
    gas->colourStep[3] = (float)(((double)(colour1 & 0xff) - (double)(colour0 & 0xff)) * perFrame);
    gas->colour[0] = obj->tweakR;
    gas->colour[1] = obj->tweakG;
    gas->colour[2] = obj->tweakB;
    gas->colour[3] = obj->tweakA;

    double spin = Float_FRand(REC_FRAME_RATE * HALF_PI);
    gas->spin = (float)spin;
    if (kind == GAS_FADING)
        gas->spin = (float)(spin * 0.25f);
    if (Rand_Rand(0x100) > 0x7f)
        gas->spin = gas->spin * -1.0f;

    hashtable_set_object_to_entity_gfx(obj, entity);
    obj->effectFlags |= EFFECT_NO_COLLISION | EFFECT_NO_TRANSIENT_LIGHTS | EFFECT_DIM_IN_NIGHT_VISION | EFFECT_ALWAYS_FORCED;
    if (!build_LinkToRoom(obj, 0, glb_world))
        control_link_object_to_cel(obj, cel);

    if (vel != NULL) {
        if (kind == GAS_FALLING) {
            gas->vel.x = (float)(Float_FRand(0.05f) + (double)REC_FRAME_RATE * vel->x);
            gas->vel.y = REC_FRAME_RATE * vel->y;
            gas->vel.z = (float)(Float_FRand(0.05f) + (double)REC_FRAME_RATE * vel->z);
        } else {
            gas->vel.x = REC_FRAME_RATE * vel->x;
            gas->vel.y = REC_FRAME_RATE * vel->y;
            gas->vel.z = REC_FRAME_RATE * vel->z;
        }
        return obj;
    }

    switch (kind) {
    case GAS_WIND:
        Env_GetWindVel(cel, pos, &gas->vel);
        gas->vel.x = (float)((Float_FRand(0.2f) - 0.1f + gas->vel.x) * REC_FRAME_RATE);
        gas->vel.y = (float)((Float_FRand(0.2f) + gas->vel.y) * REC_FRAME_RATE);
        gas->vel.z = (float)((Float_FRand(0.2f) - 0.1f + gas->vel.z) * REC_FRAME_RATE);
        break;
    case GAS_WIND_SOURCE:
        gas->life *= 0.5f;
        // fall through
    case GAS_SOURCE:
        Vec_Copy(pos, &gas->vel);
        obj->effectFlags |= EFFECT_HIDDEN;
        break;
    default:
        gas->vel.x = 0.0f;
        gas->vel.z = 0.0f;
        gas->vel.y = REC_FRAME_RATE;
        break;
    }
    return obj;
}

// Gravity's pull (k of it, a frame; negative to rise), and the smoke's own velocity
static void Gas_Drift(obj_tag *obj, GAS_tag *gas, float k) {
    auxVec_AddMulR32(&obj->position, &GRAVITY_VECTOR, (float)((double)FRAME_RATE_MUL * k * REC_FRAME_RATE),
                     &obj->position);
    Vec_Add2(&obj->position, &gas->vel, &obj->position);
}

static void Gas_Grow(obj_tag *obj, GAS_tag *gas) {
    obj->scale = (float)((double)FRAME_RATE_MUL * gas->scaleStep + obj->scale);
}

// A colour of the smoke as a tweak byte
static uchar Gas_ColourByte(double colour) {
    if (colour < 0.0f)
        colour = 0.0f;
    else if (colour > 255.0f)
        colour = 255.0f;
    return (uchar)Ftol(colour);
}

// Moves the smoke by its kind, and changes its colour; deleted when its life is out
// AUTOINJECT
void gases_smoke(obj_tag *obj) {

    GAS_tag *gas = (GAS_tag *)obj->extraObjectData;

    switch (gas->kind) {

    case GAS_SLOWING:
        Vec_MulR32(&gas->vel, &gas->vel, FRAME_RATE_DIV * 0.95f);
        // fall through
    case GAS_WIND:
        obj->rotation.z = (float)((double)FRAME_RATE_MUL * gas->spin + obj->rotation.z);
        Gas_Drift(obj, gas, -0.02f);
        if (obj->position.y > obj->inCel->boxMax.y)
            obj->position.y = obj->inCel->boxMax.y;
        Gas_Grow(obj, gas);
        break;

    case GAS_RISING:
    case GAS_RISING6:
        obj->rotation.z = gas->spin + obj->rotation.z;
        Gas_Drift(obj, gas, -0.1f);
        if (obj->position.y > obj->inCel->boxMax.y) {
            obj->position.y = obj->inCel->boxMax.y;
            break;
        }
        Gas_Grow(obj, gas);
        break;

    case GAS_FADING:
        obj->rotation.z = gas->spin + obj->rotation.z;
        Gas_Drift(obj, gas, -0.02f);
        if (obj->position.y > obj->inCel->boxMax.y)
            obj->position.y = obj->inCel->boxMax.y;
        Gas_Grow(obj, gas);
        if (gas->life < 30.0f)
            gas->colour[3] = gas->life * 8.5f;
        break;

    case GAS_FALLING:
        auxVec_AddMulR32(&gas->vel, &GRAVITY_VECTOR, (float)((double)FRAME_RATE_MUL * 0.01f * REC_FRAME_RATE),
                         &gas->vel);
        Vec_Add2(&obj->position, &gas->vel, &obj->position);
        gas->vel.x = (float)(((double)FRAME_RATE_MUL * 0.95f - REC_FRAME_RATE) * gas->vel.x);
        gas->vel.z = (float)(((double)FRAME_RATE_MUL * 0.95f - REC_FRAME_RATE) * gas->vel.z);
        Gas_Grow(obj, gas);
        break;

    case GAS_GROWING:
    case GAS_GROWING5:
        Gas_Grow(obj, gas);
        break;

    case GAS_SINKING:
        Gas_Grow(obj, gas);
        auxVec_AddMulR32(&obj->position, &GRAVITY_VECTOR, (float)((double)FRAME_RATE_MUL * 0.05f * REC_FRAME_RATE),
                         &obj->position);
        break;

    case GAS_SOURCE:
        obj->effectFlags |= EFFECT_HIDDEN;
        if ((double)(uint)(GameState.NumFramesUnpaused - obj->creationTimeFrames) < (double)FRAME_RATE_MUL * 20.0f)
            return;
        if ((GameState.NumFramesUnpaused & 3) == 3) {
            ushort life = Ftol(gas->life0);
            Gas_Init(obj->inCel, &gas->vel, &obj->rotation, NULL, gas->scale0, gas->scale1, gas->entity, GAS_SINKING,
                     (gas->colour0 & 0xffffff00) | obj->tweakA, gas->colour1, life);
        }
        break;

    case GAS_WIND_SOURCE: {
        obj->effectFlags |= EFFECT_HIDDEN;
        uint every = (ushort)Ftol((double)FRAME_RATE_MUL * 7.0f);
        if (every != (every & GameState.NumFramesUnpaused))
            break;
        _VECTOR up;
        up.x = 0.0f;
        up.z = 0.0f;
        up.y = REC_FRAME_RATE + REC_FRAME_RATE;
        ushort life = Ftol(gas->life0);
        Gas_Init(obj->inCel, &gas->vel, &obj->rotation, &up, gas->scale0, gas->scale1, gas->entity, GAS_WIND,
                 (gas->colour0 & 0xffffff00) | obj->tweakA, gas->colour1, life);
        break;
    }
    }

    // Red is taken to its byte unrounded
    double red = (double)gas->colourStep[0] + gas->colour[0];
    gas->colour[0] = (float)red;
    gas->colour[1] = gas->colourStep[1] + gas->colour[1];
    gas->colour[2] = gas->colourStep[2] + gas->colour[2];
    gas->colour[3] = gas->colourStep[3] + gas->colour[3];
    obj->tweakR = Gas_ColourByte(red);
    obj->tweakG = Gas_ColourByte(gas->colour[1]);
    obj->tweakB = Gas_ColourByte(gas->colour[2]);
    obj->tweakA = Gas_ColourByte(gas->colour[3]);

    double left = (double)gas->life - FRAME_RATE_MUL;
    gas->life = (float)left;
    if (left <= 0.0)
        obj->flags |= OBJFLAG_DELETE;
    else
        obj->transformFlags |= TRANSFORM_MOVED;
}

// Debris of the material from the hit: count pieces of its model, off along the plane's normal at half its speed and
// at random along the hit's direction on the surface, spinning
// AUTOINJECT
void Debris_Create(HITDATA_tag *hit, struct HEINFO_tag *info, cel_tag *cel, ushort count) {

    if (info->debris == 0)
        return;

    float speed = info->debrisSpeed;
    float half = speed * 0.5f;
    for (uint left = count; left != 0; left--) {

        obj_tag *obj = control_create_object(sizeof(Effect_tag), &hit->hitPosition, NULL, NULL);
        if (obj == NULL)
            continue;

        Effect_tag *debris = (Effect_tag *)obj->extraObjectData;
        obj->objectType = OBJECTTYPE_EFFECT;
        obj->scale = (float)(Float_FRand(0.1f) + 0.4f);
        obj->tweakA = 0xff;
        obj->curState = 0;
        uint extra = Rand_Rand(info->debrisLife >> 1);
        debris->fade = 1;
        debris->life = (short)(extra + (info->debrisLife >> 1));

        debris->vel.x = (float)((Rand_FRandHalf(speed) * hit->tangent.x + (double)half * hit->surfaceNormal.a)
                                * REC_FRAME_RATE);
        debris->vel.y = (float)((Rand_FRandHalf(speed) * hit->tangent.y + (double)half * hit->surfaceNormal.b)
                                * REC_FRAME_RATE);
        debris->vel.z = (float)((Rand_FRandHalf(speed) * hit->tangent.z + (double)half * hit->surfaceNormal.c)
                                * REC_FRAME_RATE);
        Rand_FRandHalf_Vec(&debris->spin, info->debrisSpin);

        hashtable_set_object_to_entity_gfx(obj, info->debris);
        obj->effectFlags |= EFFECT_NO_COLLISION | EFFECT_ALWAYS_FORCED;
        if (!build_LinkToRoom(obj, 0, glb_world))
            control_link_object_to_cel(obj, cel);
    }
}

// An explosion's debris: count pieces of models drawn from entities, off along dir and at random along side, spinning
// AUTOINJECT
void Debris_CreateEx(_VECTOR *pos, _VECTOR *dir, _VECTOR *side, float speed, uint life, uint count, cel_tag *cel,
                     HASHCODE *entities, uint numEntities) {

    if (entities == NULL)
        return;

    for (ushort i = 0; i < count; i++) {

        obj_tag *obj = control_create_object(sizeof(Effect_tag), pos, NULL, NULL);
        if (obj == NULL)
            continue;

        Effect_tag *debris = (Effect_tag *)obj->extraObjectData;
        obj->objectType = OBJECTTYPE_EFFECT;
        obj->scale = (float)(Float_FRand(0.1f) + 0.4f);
        obj->tweakA = 0xff;
        obj->curState = 0;
        debris->life = (short)(Rand_Rand(life >> 1) + (life >> 1));
        debris->fade = 1;

        float along = (float)((Float_FRand(1.5f) + 0.1f) * (speed * 0.5f));
        debris->vel.x = (float)((Rand_FRandHalf(speed) * side->x + (double)along * dir->x) * REC_FRAME_RATE);
        debris->vel.y = (float)((Rand_FRandHalf(speed) * side->y + (double)along * dir->y) * REC_FRAME_RATE);
        debris->vel.z = (float)((Rand_FRandHalf(speed) * side->z + (double)along * dir->z) * REC_FRAME_RATE);
        Rand_FRandHalf_Vec(&debris->spin, 0.23561947f);

        hashtable_set_object_to_entity_gfx(obj, entities[Rand_Rand(numEntities - 1)]);
        obj->effectFlags |= EFFECT_NO_COLLISION | EFFECT_ALWAYS_FORCED;
        if (!build_LinkToRoom(obj, 0, glb_world))
            control_link_object_to_cel(obj, cel);
    }
}

// Flies, falls while curState is 0, spins and fades; deleted when its life is out or it has faded away
// AUTOINJECT
void Debris_Update(obj_tag *obj) {

    if (obj == NULL)
        return;

    Effect_tag *debris = (Effect_tag *)obj->extraObjectData;
    if (debris->life > 0 && obj->tweakA - debris->fade > 0) {
        obj->tweakA -= debris->fade;
        debris->life--;
        Vec_Add2(&obj->position, &debris->vel, &obj->position);
        if (obj->curState == 0)
            auxVec_AddMulR32(&debris->vel, &GRAVITY_VECTOR, REC_FRAME_RATE * REC_FRAME_RATE, &debris->vel);
        Vec_Add2(&obj->rotation, &debris->spin, &obj->rotation);
        obj->transformFlags |= TRANSFORM_MOVED;
    } else {
        obj->flags |= OBJFLAG_DELETE;
    }
}
