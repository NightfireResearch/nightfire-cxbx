#include "Env.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <bit>

#include "../../engine/viewer.h"
#include "../../game.h"
#include "../view.h"
#include "Gas.h"
#include "RainBox.h"
#include "../../math/X87Ops.h"
#include "../../memory.h"
#include "../../engine/celglist.h"
#include "../../engine/parsemap.h"
#include "build.h"

// XBE_GLOBAL(0x001f64ac, 0x4)
ENV_tag *GlobalEnv;

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

// Env_Update's scale of its draw of 20000
static constexpr float WindDrawScale = 1.0f / 65000;
static_assert(std::bit_cast<uint32_t>(WindDrawScale) == 0x37810e36, "not the original's 1/65000");

// The water materials a step or a shot ripples
#define MATERIAL_WATER_A 13
#define MATERIAL_WATER_B 14

// The tree script that has a second version, and the level where trees do not loop
#define TREE_STILL_LEVEL HT_Level_AllCharacters

#define ScriptLoops 0x80000000u     // sAnimScript_tag::flags

// Degrees to radians
static constexpr float kDegrees = 0.017453292f; // the original's float at 0x0015d558
static_assert(std::bit_cast<uint32_t>(kDegrees) == 0x3c8efa35, "kDegrees");

// An unsigned key as a float (FILD, and 2^32 added to one read as negative)
static double UnsignedKey(uint key) {
    return (double)(int32_t)key + ((int32_t)key < 0 ? 4294967296.0f : 0.0f);
}

// The wind of the cel at pos, if it has none yet: its strength in tenths, its direction from rot, and the level's
// rain; and the sun's place in the sky, 1000 away, from its two angles in degrees (none without them)
// AUTOINJECT
void Env_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl) {
    _VECTOR up = {0.0f, 1.0f, 0.0f};
    cel_tag *cel = build_FindCel(pos, glb_world);
    if (cel == NULL || cel->env != NULL)
        return;
    ENV_tag *env = (ENV_tag *)Mem_Malloc(sizeof(ENV_tag), (MallocFlags)0x104, 0);
    if (env == NULL)
        return;
    EnvPlacement *keys = (EnvPlacement *)lvl;
    env->dropType = (uchar)keys->dropType;
    env->unknown40 = env->dropType ? 0 : (uchar)keys->unknown30;
    env->unknown42 = (uchar)keys->unknown34;
    env->windRising = 1;
    env->windStrength = (float)(UnsignedKey(keys->wind) * 0.1f);
    env->windGust = -1.0f;
    env->windMax = (float)(UnsignedKey(keys->wind) * 0.1f);
    env->unknown3c[0] = (uchar)keys->unknown3c;
    env->unknown3c[1] = (uchar)keys->unknown40;
    env->unknown3c[2] = (uchar)keys->unknown44;
    env->unknown30 = (float)UnsignedKey(keys->unknown48);
    env->unknown34 = (float)UnsignedKey(keys->unknown4c);
    env->windSpread = 1.0f / cel->glist->boundSphere.r;
    Vec_Copy(pos, &env->pos);
    cel->env = env;
    GlobalEnv = env;
    _MATRIX mtx;
    RotMatrix(rot, &mtx);
    Mat_GetDir(&env->windDir, &mtx);
    Vec_Cross(&env->windDir, &up, &env->windSide);
    InitDrops(env->dropType);
    Vec_Zero(&SkySunPos);
    if (keys->sun)
        Vec_Spherical_2_Cartesian(&SkySunPos.x, 1000.0f, (float)(UnsignedKey(keys->sunYaw) * kDegrees),
                                  (float)-(UnsignedKey(keys->sunPitch) * kDegrees));
}

// The cel's wind at pos: its direction times its strength, growing along its way from the cel's centre; none without
// wind
// AUTOINJECT
void Env_GetWindVel(cel_tag *cel, _VECTOR *pos, _VECTOR *velOut) {
    if (velOut == NULL)
        return;
    if (cel == NULL || cel->env == NULL || cel->env->windMax == 0.0f) {
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
    Gas_Init(cel, &hit->hitPosition, NULL, NULL, 0.5f, 1.01f, HT_Entity_RippleGas, 1, 0x7f7f7f7f, 2, 0x3f);
}

// A breath from the mouth, on the first third of every two seconds, a puff each tenth of one, in a cel seen this
// frame: white in the rain outdoors (drop type 0, a cel open to the sky), else only seen in the night vision (viewer
// 0's mode 2), turned at random. The second's Gas_Init is used unchecked.
// AUTOINJECT
void Env_Breath(_VECTOR *mouth, _VECTOR *dir, cel_tag *cel, int created) {
    if (cel == NULL || !cel->addedToDraw)
        return;
    uint age = GameState.NumFramesUnpaused - created;
    if (age % (FRAME_RATE_INT / 10) != 0)
        return;
    uint phase = age % (FRAME_RATE_INT * 2);
    if ((double)_FRAME_RATE * 0.6666667f < (double)phase)
        return;

    if (GlobalEnv != NULL && GlobalEnv->dropType == 0 && !(cel->bitsFromPlacementTag & CEL_NO_SKY)) {
        _VECTOR pos;
        auxVec_AddMulR32(mouth, dir, 0.1f, &pos);
        Gas_Init(cel, &pos, NULL, dir, 0.05f, 0.1f, HT_Entity_EnvBreathGas, 0, 0xffffff80, 0xffffff01, 40);
        return;
    }

    if (glb_viewer[0]->nightVisionRelated != 2)
        return;
    _VECTOR rot;
    rot.y = 0.0f;
    rot.x = 0.0f;
    rot.z = (float)Float_FRand(6.2831855f);
    _VECTOR pos;
    auxVec_AddMulR32(mouth, dir, 0.1f, &pos);
    obj_tag *gas = Gas_Init(cel, &pos, &rot, dir, 0.05f, 0.2f, HT_Entity_EnvBreathGas, 0, 0xffffffc8, 0xffffff01, 40);
    gas->effectFlags |= 0x400;
}

// A tree swaying to its script, looping from a random frame but on one level. Neither the object nor the script is
// checked.
// AUTOINJECT
obj_tag* Env_Tree_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    build_LinkToRoom(obj, 0, glb_world);
    obj->objectType = OBJECTTYPE_TREE;
    TreePlacement *keys = (TreePlacement *)lvl;
    AnimObjectNew(obj, keys->skin);

    HASHCODE script = keys->script;
    if (script == HT_Script_TreeScriptA && (Rand_Random() & 1))
        script = HT_Script_TreeScriptB;
    sAnimScript_tag *anim = AnimScriptAdd(obj, script);
    if (GameState.NextLevelHashcode == TREE_STILL_LEVEL) {
        anim->flags &= ~ScriptLoops;
    } else {
        anim->flags |= ScriptLoops;
        anim->curFrame = (float)Ftol(Float_FRand(anim->numFrames));
    }

    obj->effectFlags = (obj->effectFlags & ~0x20) | 0x80;
    return obj;
}

// Not while a script holds the camera
// AUTOINJECT
void Env_Tree_Update(obj_tag *obj) {
    if (ScriptCam == 0)
        AnimObjectUpdate(obj);
}

// A frame of the cels' wind - one draw for every cel's gusts and swells - then the rain
// AUTOINJECT
void Env_Update(void) {
    if (glb_world == NULL)
        return;
    cel_tag *cel = glb_world->firstCel;
    float step = (float)((double)Rand_Rand(20000) * REC_FRAME_RATE * WindDrawScale);
    for (; cel != NULL; cel = cel->nextCel) {
        ENV_tag *env = cel->env;
        if (env == NULL || env->windMax == 0.0f)
            continue;
        env->windGust = (float)((double)REC_FRAME_RATE * step + env->windGust);
        if (cel->env->windGust > 1.0f)
            cel->env->windGust = -1.0f;
        env = cel->env;
        if (env->windRising) {
            env->windStrength = (float)((double)env->windStrength + env->windStrength + step);
            if (cel->env->windStrength > cel->env->windMax)
                cel->env->windRising = 0;
        } else {
            env->windStrength = (float)((1.0 - step) * env->windStrength);
            if ((double)cel->env->windMax * 0.25f > cel->env->windStrength)
                cel->env->windRising = 1;
        }
    }
    UpdateDrops();
}

// For a new level: no sky models, no rain, no rain boxes, no sun, no level wind
// AUTOINJECT
void Env_Reset(void) {
    memset(&SkyObjs, 0, sizeof(SkyObjs));
    InitDrops(0xff);
    InitRainBox();
    Vec_Zero(&SkySunPos);
    GlobalEnv = NULL;
}
