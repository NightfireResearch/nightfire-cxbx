#include "Env.h"

#include "../../engine/viewer.h"
#include "Gas.h"
#include "../../math/X87Ops.h"

#pragma fp_contract(off)

// The water materials a step or a shot ripples
#define MATERIAL_WATER_A 13
#define MATERIAL_WATER_B 14
#define ENV_RIPPLE_GAS ((HASHCODE)0x02000129)

// AUTOGEN
void Env_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);

// AUTOGEN
obj_tag* Env_Tree_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);

// The cel's wind at pos: its direction times its strength, growing along its way from the cel's centre; none without
// wind
// AUTOINJECT
void Env_GetWindVel(cel_tag *cel, _VECTOR *pos, _VECTOR *velOut) {
    if (velOut == NULL)
        return;
    if (cel == NULL || cel->env == NULL || cel->env->windOn == 0.0f) {
        Vec_Zero(velOut);
        return;
    }
    _VECTOR offset;
    Vec_Subtract(pos, &cel->sphere, &offset);
    ENV_tag *env = cel->env;
    double along = ((double)offset.y * env->windDir.y + (double)offset.z * env->windDir.z) + (double)offset.x * env->windDir.x;
    double gust = AbsFchs(env->windGust) * (along * env->windSpread + 1.0f);
    Vec_MulR32(velOut, &env->windDir, (float)(gust * 0.5f * env->windStrength));
}

// A step or a shot in water: a ripple of smoke where it hit
// AUTOINJECT
void Env_WaterRipple(HITDATA_tag *hit) {
    if (hit == NULL)
        return;
    int material = hit->materialType & 0x3f;
    if (material != MATERIAL_WATER_A && material != MATERIAL_WATER_B)
        return;
    cel_tag *cel = hit->hitCel;
    if (cel == NULL) {
        if (hit->hitObj == NULL || hit->hitObj->inCel == NULL)
            return;
        cel = hit->hitObj->inCel;
    }
    Gas_Init(cel, &hit->hitPosition, NULL, NULL, 0.5f, 1.01f, ENV_RIPPLE_GAS, 1, 0x7f7f7f7f, 2, 0x3f);
}
