#include "Flicker.h"

#include <math.h>
#include <stdint.h>
#include <bit>

#include "../view.h"
#include "../../engine/psiLight.h"
#include "../../math/X87Ops.h"
#include "../../engine/viewer.h"
#include "build.h"
#include "control.h"
#include "Effect.h"
#include "Gas.h"
#include "Light.h"

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

// A spark's Gas_Init(cel, &centre, NULL, &up, 1.2f, 0.8f, model, 4, 0xffffffff, 0x0a460003, 85), inline in
// Flicker_Update with the compiler's constants
static constexpr float SparkLife = 85.0f;
static constexpr float SparkScale = 1.2f;
static constexpr float SparkScaleStep = (0.8f - 1.2f) / 85.0f;
static constexpr float SparkRedStep = (10.0f - 255.0f) / 85.0f;
static constexpr float SparkGreenStep = (70.0f - 255.0f) / 85.0f;
static constexpr float SparkBlueStep = (0.0f - 255.0f) / 85.0f;
static constexpr float SparkAlphaStep = (3.0f - 255.0f) / 85.0f;
static_assert(std::bit_cast<uint32_t>(SparkScaleStep) == 0xbb9a33ce, "not the original's scale step");
static_assert(std::bit_cast<uint32_t>(SparkRedStep) == 0xc0387878, "not the original's red step");
static_assert(std::bit_cast<uint32_t>(SparkGreenStep) == 0xc00b4b4b, "not the original's green step");
static_assert(std::bit_cast<uint32_t>(SparkBlueStep) == 0xc0400000, "not the original's blue step");
static_assert(std::bit_cast<uint32_t>(SparkAlphaStep) == 0xc03dbdbe, "not the original's alpha step");

static const HASHCODE SparkModels[5] = {HT_Entity_0002d1, HT_Entity_0002d2, HT_Entity_0002d3,
                                        HT_Entity_0002d4, HT_Entity_0002d5};

// AUTOINJECT
obj_tag* Flicker_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(FLICKER_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_FLICKER;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(obj, 0, glb_world);

    FLICKER_tag *flicker = (FLICKER_tag *)obj->extraObjectData;
    FlickerPlacement *keys = (FlickerPlacement *)lvl;
    flicker->light = 0;
    flicker->sparks = (short)keys->sparks;
    flicker->channel = keys->channel;
    _MATRIX mtx;
    View_RotTransMatrix(rot, pos, &mtx);
    Mat_GetUp(&flicker->up, &mtx);
    return obj;
}

// The colour steps a tenth of the way, at least 1, to its end
static uchar ColourStep(uchar now, double towards) {
    double step = towards * 0.1f;
    if (!(AbsFchs(step) < 1.0f))
        return (uchar)(Ftol(step) + now);
    return (uchar)((step < 0.0 ? -1 : 1) + now);
}

// In a cel seen this frame: dark and its channel on while its light is off (always, on the Xbox, which makes no
// lights), white and the channel off while it is on; dark with sparks, every other frame, a spark falling along its
// up, and the platform's particles
// AUTOINJECT
void Flicker_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    if (obj->inCel != NULL && !obj->inCel->addedToDraw)
        return;

    FLICKER_tag *flicker = (FLICKER_tag *)obj->extraObjectData;
    bool dark;
    if (flicker->light == 0) {
        // The original hands it the centre and the radius, which the Xbox's does not read
        flicker->light = psiLight_Create();
        dark = false;
        if (flicker->light != 0)
            dark = !((light_tag *)(uintptr_t)flicker->light)->enabled;
    } else {
        dark = !((light_tag *)(uintptr_t)flicker->light)->enabled;
    }
    uchar target;
    if (dark) {
        target = 0;
        if (flicker->channel != 0)
            switch_channels[flicker->channel] = 1;
    } else {
        target = 0xff;
        if (flicker->channel != 0)
            switch_channels[flicker->channel] = 0;
    }

    uchar red = obj->tweakR, green = obj->tweakG;
    double toRed = target - red;
    float toGreen = (float)(target - green);
    float toBlue = (float)(target - obj->tweakB);
    if (toRed != 0.0)
        obj->tweakR = ColourStep(red, toRed);
    if (toGreen != 0.0f)
        obj->tweakG = ColourStep(green, toGreen);
    if (toBlue != 0.0f)
        obj->tweakB = (uchar)(Ftol((double)toBlue * 0.1f) + obj->tweakB);

    if (!dark || flicker->sparks == 0 || (GameState.NumFramesUnpaused & 1))
        return;

    HASHCODE model = SparkModels[Rand_Rand(5)];
    cel_tag *cel = obj->inCel;
    _VECTOR *centre = &obj->centrePoint;
    // Gas_Init, inline: its vel is never NULL here
    if (cel != NULL) {
        obj_tag *spark = control_create_object(offsetof(GAS_tag, colour0), centre, NULL, NULL);
        if (spark != NULL) {
            GAS_tag *gas = (GAS_tag *)spark->extraObjectData;
            gas->life = SparkLife;
            gas->kind = 4;
            spark->objectType = OBJECTTYPE_GAS;
            spark->scale = SparkScale;
            spark->effectFlags |= 0x200;
            spark->tweakG = 0xff;
            spark->tweakB = 0xff;
            spark->tweakR = 0xff;
            spark->tweakA = 0xff;
            gas->scaleStep = SparkScaleStep;
            gas->colourStep[0] = SparkRedStep;
            gas->colourStep[1] = SparkGreenStep;
            gas->colourStep[2] = SparkBlueStep;
            gas->colourStep[3] = SparkAlphaStep;
            gas->colour[0] = spark->tweakR;
            gas->colour[1] = spark->tweakG;
            gas->colour[2] = spark->tweakB;
            gas->colour[3] = spark->tweakA;
            gas->spin = (float)Float_FRand(REC_FRAME_RATE * 1.5707964f);
            if (Rand_Rand(0x100) > 0x7f)
                gas->spin = gas->spin * -1.0f;
            hashtable_set_object_to_entity_gfx(spark, model);
            spark->effectFlags |= 0x20020120;
            if (!build_LinkToRoom(spark, 0, glb_world))
                control_link_object_to_cel(spark, cel);
            _VECTOR *up = &flicker->up;
            gas->vel.x = (float)(Float_FRand(0.05f) + (double)REC_FRAME_RATE * up->x);
            gas->vel.y = REC_FRAME_RATE * up->y;
            gas->vel.z = (float)(Float_FRand(0.05f) + (double)REC_FRAME_RATE * up->z);
        }
    }

    plane_equ_tag down;
    down.a = 0.0f;
    down.b = -1.0f;
    down.c = 0.0f;
    down.d = 0.0f;
    Particles_Create(5, centre, &down);
}
