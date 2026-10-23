#include "Ripples.h"

#include <math.h>
#include <stdint.h>
#include <bit>

#include "build.h"
#include "control.h"
#include "Gas.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../../math/math.h"

#pragma fp_contract(off)

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

// A ring's Gas_Init(cel, &pos, NULL, NULL, size, size * 2, HT_Entity_RippleGas, 1, 0x7f7f7fff, 0x00000006, 42), inline
// in
// Ripples_Update with the compiler's constants: a frame's share of the life is 1/42 as a float
static constexpr float RippleLife = 42.0f;
static constexpr float RipplePerFrame = 1.0f / 42.0f;
static constexpr float RippleColourStep = -127.0f / 42.0f;
static constexpr float RippleAlphaStep = (6.0f - 255.0f) / 42.0f;
static_assert(std::bit_cast<uint32_t>(RipplePerFrame) == 0x3cc30c31, "not the original's 1/42");
static_assert(std::bit_cast<uint32_t>(RippleColourStep) == 0xc0418618, "not the original's colour step");
static_assert(std::bit_cast<uint32_t>(RippleAlphaStep) == 0xc0bdb6db, "not the original's alpha step");

// Without a radius or a rate: a ring of radius 1 every 30 frames
// AUTOINJECT
obj_tag* Ripples_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(RIPPLES_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    RipplesPlacement *keys = (RipplesPlacement *)lvl;
    RIPPLES_tag *ripples = (RIPPLES_tag *)obj->extraObjectData;
    ripples->radius = (short)keys->radius;
    ripples->rate = (short)keys->rate;
    if (ripples->rate == 0 && ripples->radius == 0) {
        ripples->rate = 30;
        ripples->radius = 1;
    }

    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_RIPPLES;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Now and then a ring of smoke somewhere within the radius
// AUTOINJECT
void Ripples_Update(obj_tag *obj) {
    RIPPLES_tag *ripples = (RIPPLES_tag *)obj->extraObjectData;
    if (Rand_Rand(Ftol(ripples->rate / (double)FRAME_RATE_MUL)) != 0)
        return;

    _VECTOR pos;
    Vec_Copy(&obj->position, &pos);
    float radius = ripples->radius;
    pos.x = (float)(Float_FRand(radius + radius) - radius + pos.x);
    pos.z = (float)(Float_FRand(radius + radius) - radius + pos.z);
    double size = Float_FRand(0.05f) + 0.05f;
    float scale0 = (float)size;
    float scale1 = (float)(size + size);

    cel_tag *cel = obj->inCel;
    if (cel == NULL)
        return;
    obj_tag *ring = control_create_object(offsetof(GAS_tag, colour0), &pos, NULL, NULL);
    if (ring == NULL)
        return;

    GAS_tag *gas = (GAS_tag *)ring->extraObjectData;
    gas->life = RippleLife;
    gas->kind = 1;
    ring->scale = scale0;
    ring->tweakR = 0x7f;
    ring->tweakG = 0x7f;
    ring->tweakB = 0x7f;
    ring->tweakA = 0xff;
    ring->objectType = OBJECTTYPE_GAS;
    ring->effectFlags |= 0x200;
    gas->colourStep[0] = RippleColourStep;
    gas->scaleStep = (float)(((double)scale1 - scale0) * RipplePerFrame);
    gas->colourStep[1] = RippleColourStep;
    gas->colourStep[2] = RippleColourStep;
    gas->colourStep[3] = RippleAlphaStep;
    gas->colour[0] = ring->tweakR;
    gas->colour[1] = ring->tweakG;
    gas->colour[2] = ring->tweakB;
    gas->colour[3] = ring->tweakA;

    gas->spin = (float)Float_FRand(REC_FRAME_RATE * 1.5707964f);
    if (Rand_Rand(0x100) > 0x7f)
        gas->spin = gas->spin * -1.0f;

    hashtable_set_object_to_entity_gfx(ring, HT_Entity_RippleGas);
    ring->effectFlags |= 0x20020120;
    if (!build_LinkToRoom(ring, 0, glb_world))
        control_link_object_to_cel(ring, cel);

    gas->vel.x = 0.0f;
    gas->vel.y = REC_FRAME_RATE;
    gas->vel.z = 0.0f;
}

// A bubble in a cel: grows by 0.001 a frame and moves by its velocity, which slows by 5% a frame across, until its
// time is up or the top of its model is past the cel's ceiling (someMax.y), and it is deleted. Out of any cel it
// waits.
// AUTOINJECT
void Bubble_Update(obj_tag *obj) {
    BUBBLE_tag *bubble = (BUBBLE_tag *)obj->extraObjectData;
    cel_tag *cel = obj->inCel;
    if (cel == NULL)
        return;
    obj->scale = (float)((double)obj->scale + 0.001f);
    bubble->framesLeft--;
    if (bubble->framesLeft > 0 && !((double)obj->objGraphics->extentMax.y + obj->position.y > cel->someMax.y)) {
        Vec_Add2(&bubble->velocity, &obj->position, &obj->position);
        bubble->velocity.x = (float)((double)bubble->velocity.x * 0.95f);
        bubble->velocity.z = (float)((double)bubble->velocity.z * 0.95f);
        obj->transformFlags |= TRANSFORM_MOVED;
        return;
    }
    obj->flags |= 1;
}
