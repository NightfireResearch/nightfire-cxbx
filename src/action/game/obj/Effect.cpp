#include "Effect.h"

#include <math.h>

#include "../../game.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"
#include "../mp/multiplayer.h"
#include "../view.h"
#include "bullet.h"
#include "control.h"
#include "Emitter.h"
#include "Gas.h"

#pragma fp_contract(off)

// By material
// XBE_GLOBAL(0x0015d798, 0xc38) readonly relocate
HEINFO_tag EffectInfo[EFFECT_INFO_COUNT] = {
    {"NONE", HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, 0xffff, 0xffff, 0xffff, 0x0000, 0.5f,
     {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f,
     0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0,
     0, 5, HASHCODE_NONE},
    {"CARPET", HT_Entity_BulletHole_scar, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right, HASHCODE_NONE,
        0x011e, 0x0083, 0x012a,
     0x0000, 0.5f, {{HT_Entity_smoke_1_dust, 9, 0x7f7f7f7f, 0x00000000, 0.005f, 0.02f, 32, 1, 0},
         {HT_Entity_smoke_1_dust,
     10, 0x5050507f, 0x00000000, 0.025f, 0.03f, 42, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f,
     2.5f, 4, 0, 1}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0, 0, 5, HASHCODE_NONE},
    {"PAPER", HT_Entity_BulletHole_scar, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x011f, 0x0083,
     0x012b, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0, 0, 5, HASHCODE_NONE},
    {"WOODTHIN", HT_Entity_WoodBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x000b,
     0x000e, 0x012c, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HT_Entity_spark_wood, 6.2f, 0.3926991f, 1, 55, 1, 5, HASHCODE_NONE},
    {"WOODTHICK", HT_Entity_WoodBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x000c,
     0x000f, 0x012d, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HT_Entity_spark_wood, 6.2f, 0.3926991f, 1, 55, 1, 5, HASHCODE_NONE},
    {"METALTHIN", HT_Entity_MetalBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0006,
     0x0010, 0x012e, 0x003f, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HT_Entity_spark_metal, 6.2f, 0.3926991f, 1, 55, 1, 5, HT_Emitter_MetalHit},
    {"METALTHICK", HT_Entity_MetalBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0007,
     0x0011, 0x012f, 0x003f, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HT_Entity_spark_metal, 6.2f, 0.3926991f, 1, 55, 1, 5, HT_Emitter_MetalHit},
    {"GRASS", HASHCODE_NONE, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right, HASHCODE_NONE, 0x0120, 0x0081,
        0x0130, 0x0000,
     0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000,
     0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}}, HT_Entity_spark_leaves,
     2.2f, 0.7853982f, 1, 55, 1, 5, HASHCODE_NONE},
    {"EARTH", HT_Entity_BulletHole_scar, HT_Entity_FootPrint_Left, HT_Entity_FootPrint_Right, HASHCODE_NONE, 0x0121,
        0xffff, 0x0131,
     0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000,
     0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}}, HASHCODE_NONE,
     0.0f, 0.0f, 0, 0, 0, 5, HT_Emitter_EarthHit},
    {"SAND", HT_Entity_BulletHole_scar, HT_Entity_FootPrint_Left, HT_Entity_FootPrint_Right, HASHCODE_NONE, 0x0122,
        0xffff, 0x0132,
     0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000,
     0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}},
     HT_Entity_spark_sand, 2.2f, 0.7853982f, 1, 55, 1, 5, HT_Emitter_SandHit},
    {"STONE", HT_Entity_DefaultBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0008, 0x000d,
     0x0133, 0x001f, 0.5f, {{HT_Entity_smoke_1_dust, 9, 0x7f7f7f7f, 0x00000000, 0.005f, 0.02f, 32, 1, 0},
     {HT_Entity_smoke_1_dust, 10, 0x5050507f, 0x00000000, 0.025f, 0.15f, 42, 1, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f,
     0x7f7f0040, 1.5f, 2.5f, 4, 0, 1}}, HT_Entity_spark_plaster, 6.2f, 0.7853982f, 1, 55, 1, 5, HT_Emitter_StoneHit},
    {"GRAVEL", HT_Entity_BulletHole_scar, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right, HASHCODE_NONE,
        0x0008, 0x0082, 0x0134,
     0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000,
     0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0, 1}},
     HT_Entity_spark_concrete, 2.2f, 0.7853982f, 1, 55, 1, 5, HT_Emitter_GravelHit},
    {"SNOW", HASHCODE_NONE, HT_Entity_FootPrint_Left, HT_Entity_FootPrint_Right, HASHCODE_NONE, 0x0124, 0x007b, 0x0135,
        0x0000,
     0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_smoke_1_dust, 10, 0xffffff7f,
     0x00000000, 0.025f, 0.03f, 15, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}},
     HT_Entity_spark_water, 6.2f, 0.3926991f, 5, 35, 0, 10, HT_Emitter_SnowHit},
    {"DEEPWATER", HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, 0x000a, 0xffff, 0x0136, 0x0000, 0.0f,
     {{HT_Entity_watereffect, 1, 0x7f7f7fb4, 0x00000000, 0.05f, 0.4f, 60, 0, 0}, {HASHCODE_NONE, 0, 0x00000000,
     0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}},
     HT_Entity_spark_water, 6.2f, 0.3926991f, 5, 35, 0, 10, HT_Emitter_WaterHit},
    {"SHALWATER", HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, 0x0009, 0x0028, 0x0137, 0x0000, 0.0f,
     {{HT_Entity_watereffect, 1, 0x7f7f7fb4, 0x00000000, 0.05f, 0.4f, 60, 0, 0}, {HASHCODE_NONE, 0, 0x00000000,
     0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}},
     HT_Entity_spark_water, 6.2f, 0.3926991f, 5, 35, 0, 10, HT_Emitter_WaterHit},
    {"ICE", HT_Entity_IceBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right, HT_Entity_lazerhotspot,
        0x00ed, 0x0082,
     0x0138, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}},
     HT_Entity_spark_glass, 2.2f, 0.7853982f, 1, 55, 1, 5, HT_Emitter_IceHit},
    {"GLASS", HT_Entity_GlassBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0005, 0x0084,
     0x0139, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HT_Entity_spark_glass, 6.2f, 3.1415927f, 1, 55, 1, 5, HASHCODE_NONE},
    {"CERAMICTHIN", HT_Entity_DefaultBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0125,
     0x0084, 0x013a, 0x000f, 0.5f, {{HT_Entity_smoke_1_dust, 9, 0x7f7f7f7f, 0x00000000, 0.005f, 0.02f, 32, 1, 0},
     {HT_Entity_smoke_1_dust, 10, 0x5050507f, 0x00000000, 0.025f, 0.15f, 42, 1, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f,
     0x7f7f0040, 1.5f, 2.5f, 4, 0, 1}}, HT_Entity_spark_plaster, 6.2f, 0.7853982f, 1, 55, 1, 5, HT_Emitter_CeramicHit},
    {"CERAMICTHICK", HT_Entity_DefaultBulletHole, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0126,
     0x0084, 0x013b, 0x0018, 0.5f, {{HT_Entity_smoke_1_dust, 9, 0x7f7f7f7f, 0x00000000, 0.005f, 0.02f, 32, 1, 0},
     {HT_Entity_smoke_1_dust, 10, 0x5050507f, 0x00000000, 0.025f, 0.15f, 42, 1, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f,
     0x7f7f0040, 1.5f, 2.5f, 4, 0, 1}}, HT_Entity_spark_plaster, 6.2f, 3.1415927f, 1, 55, 1, 5, HT_Emitter_CeramicHit},
    {"FOLLAGE", HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, 0x0127, 0x0081, 0x013c, 0x0000, 0.5f,
     {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f,
     0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}}, HT_Entity_spark_leaves, 2.2f,
     0.7853982f, 1, 55, 1, 5, HASHCODE_NONE},
    {"FABRIC", HT_Entity_BulletHole_scar, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right, HASHCODE_NONE,
        0x0128, 0x0083, 0x013d,
     0x0000, 0.5f, {{HT_Entity_smoke_1_dust, 9, 0x7f7f7f7f, 0x00000000, 0.005f, 0.02f, 32, 1, 0},
         {HT_Entity_smoke_1_dust,
     10, 0x5050507f, 0x00000000, 0.025f, 0.15f, 42, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f,
     2.5f, 4, 0, 1}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0, 0, 5, HASHCODE_NONE},
    {"GENERIC", HT_Entity_BulletHole_scar, HT_Entity_IR_FootPrint_Left, HT_Entity_IR_FootPrint_Right,
        HT_Entity_lazerhotspot, 0x0129, 0xffff,
     0x013e, 0x0000, 0.5f, {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0,
     0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HT_Entity_hit_floor, 5, 0x7f7f007f, 0x7f7f0040, 1.5f, 2.5f, 4, 0,
     1}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0, 0, 5, HASHCODE_NONE},
    {"LAST", HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, HASHCODE_NONE, 0xffff, 0xffff, 0xffff, 0x0000, 0.0f,
     {{HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f,
     0.0f, 0, 0, 0}, {HASHCODE_NONE, 0, 0x00000000, 0x00000000, 0.0f, 0.0f, 0, 0, 0}}, HASHCODE_NONE, 0.0f, 0.0f, 0, 0,
     0, 5, HASHCODE_NONE},
};

// The marks of bullets and feet
// XBE_GLOBAL(0x002237c0, 0x18)
DLISTINFO_tag EffectList;

// The platform's particle update, MSVC's shared `return false` on the Xbox
static bool psiParticleUpdate(void *particles) {
    return false;
}

// The platform's particle system: none on the Xbox (-1)
static int psiCreateParticles(int kind, _VECTOR *pos, plane_equ_tag *plane) {
    return -1;
}

// The laser's sparks
static const HASHCODE kLaserSparks[5] = {HT_Entity_Sparkler_1, HT_Entity_Sparkler_2, HT_Entity_Sparkler_3,
                                         HT_Entity_Sparkler_4, HT_Entity_Sparkler_5};

// AUTOINJECT
void Effect_InitLists(void) {
    DList_Init(&EffectList, sizeof(ATTACH_tag), 200);
}

// Whether a bullet glances off the material: a draw in its ricochet range, masked (0 without either). The material
// is held to the table here, as nowhere else.
// AUTOINJECT
uchar Effect_RicochetProb(uchar material, ushort mask) {
    int surface = (material & 0x3f) > 22 ? 22 : material & 0x3f;
    ushort range = EffectInfo[surface].ricochet;
    if (range == 0 || mask == 0)
        return 0;
    return Rand_Rand(range) & mask;
}

// What a hit leaves, by its material: in the hit, a direction along the surface (glass's normal turned round for
// it); with EFFECT_SPARKS, sparks and debris; in single player, the bullet's, step's, laser's or body's effects
// AUTOINJECT
void Effect_Create(HITDATA_tag *hit, obj_tag *obj, uint flags, float volume) {

    plane_equ_tag plane = hit->surfaceNormal;
    if (obj == NULL || (hit->materialType & 0x3f) == 0)
        return;

    cel_tag *cel = hit->hitCel;
    if (cel == NULL)
        cel = hit->hitObj != NULL ? hit->hitObj->inCel : NULL;

    if ((hit->materialType & 0x3f) == 0x10) {
        hit->surfaceNormal.a = -hit->surfaceNormal.a;
        hit->surfaceNormal.b = -hit->surfaceNormal.b;
        hit->surfaceNormal.c = -hit->surfaceNormal.c;
    }
    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &hit->surfaceNormal.normal);
    Vec_Add2(&u, &v, &hit->unknown1);

    if (flags & EFFECT_SPARKS) {
        Emitter_Create(EffectInfo[hit->materialType & 0x3f].emitter, &hit->hitPosition, &hit->surfaceNormal.normal,
                       0.0f, 0, 0, 0, 0.0f);
        if (EffectInfo[hit->materialType & 0x3f].debrisOn) {
            ushort count = Rand_Rand(EffectInfo[hit->materialType & 0x3f].debrisMax) + 1;
            Debris_Create(hit, &EffectInfo[hit->materialType & 0x3f], cel, count);
        }
    }

    if (MPSettings.isMultiplayer)
        return;

    hit->surfaceNormal = plane;
    if (flags & EFFECT_BULLET)
        Effect_Bullet(obj, hit, cel, flags);
    else if (flags & EFFECT_STEP)
        Effect_Player(hit, obj, cel, flags, volume);
    else if (flags & EFFECT_LASER)
        Effect_Laser(hit, cel, obj, flags);
    else if (flags & EFFECT_BODY)
        Effect_Body(hit, obj, cel, flags);
}

// A bullet's sound; its material's smoke (always for the player's bullets, half the time for others'); and its mark,
// a centimetre out from the surface, facing out
void Effect_Bullet(obj_tag *obj, HITDATA_tag *hit, cel_tag *cel, uint flags) {

    BU_tag *bullet = (BU_tag *)obj->extraObjectData;
    _VECTOR *normal = &hit->surfaceNormal.normal;

    _VECTOR surfaceRot;
    Vec_NormToRot(normal, &surfaceRot);
    _VECTOR faceRot;
    double x = -normal->x, z = -normal->z;
    faceRot.x = -Atan2(-normal->y, (float)sqrt(x * x + z * z));
    faceRot.y = Atan2(-normal->x, -normal->z);
    faceRot.z = 0.0f;

    HEINFO_tag *info = &EffectInfo[hit->materialType & 0x3f];
    if (flags & EFFECT_SOUND)
        Sound_Play3D((Action_SFX)info->soundBullet, &hit->hitPosition, 100.0f, -1.0f, -1.0f, 0, 0, 0);

    obj_tag *shooter = bullet->firedByObj;
    if ((shooter != NULL && shooter->objectType == OBJECTTYPE_PLAYER) || Rand_Rand(2) == 0) {
        for (int i = 0; i < 3; i++) {
            HEGAS_tag *gas = &info->gas[i];
            if (gas->entity == 0)
                continue;
            _VECTOR at;
            if (gas->onSurface) {
                if (fabsf(normal->y) > 0.3f)
                    continue;
                auxVec_AddMulR32(&hit->hitPosition, normal, 0.05f, &at);
            } else {
                Vec_Copy(&hit->hitPosition, &at);
            }
            _VECTOR *rot = gas->onSurface ? &surfaceRot : gas->faceOut ? &faceRot : &CONST_ZERO_VECTOR;
            Gas_Init(cel, &at, rot, NULL, gas->scale0, gas->scale1, gas->entity, gas->kind, gas->colour0, gas->colour1,
                     gas->life);
        }
    }

    if (EffectInfo[hit->materialType & 0x3f].decal == 0)
        return;
    ATTACH_tag *mark = Effect_NewMark();
    if (mark == NULL)
        return;
    Mat_Align2Dir(&mark->matrix, normal, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    auxVec_AddMulR32(&hit->hitPosition, normal, 0.01f, (_VECTOR *)&mark->matrix.m[12]);
    mark->parent = hit->hitObj;
    hashtable_set_sub_object_to_entity_gfx(mark, EffectInfo[hit->materialType & 0x3f].decal);
    if (hit->hitCel != NULL)
        Effect_LinkToCel(mark, cel);
    else
        Effect_LinkToObject(mark, hit->hitObj);
}

// A step's sound, and with EFFECT_LEFT_FOOT or EFFECT_RIGHT_FOOT the footprint under that foot, facing where the
// player faces (the foot, for anything else), if the foot is within 20 cm of the floor
void Effect_Player(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags, float volume) {

    if (flags & EFFECT_SOUND) {
        DYNAMICSOUNDS *sound = Sound_Play3D((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundStep,
                                            &hit->hitPosition, volume, -1.0f, -1.0f, 0, 0, 0);
        Sound_SetAlertness(sound, 0.0f);
    }

    HASHCODE footprint = (flags & EFFECT_LEFT_FOOT) ? EffectInfo[hit->materialType & 0x3f].footprint4
                                                    : EffectInfo[hit->materialType & 0x3f].footprint8;
    if (!(flags & (EFFECT_LEFT_FOOT | EFFECT_RIGHT_FOOT)) || footprint == 0)
        return;

    _MATRIX *foot = &obj->transformMatrix;
    if (flags & EFFECT_LEFT_FOOT)
        foot = &obj->animState->animObj.boneMtxs[55];
    else if (flags & EFFECT_RIGHT_FOOT)
        foot = &obj->animState->animObj.boneMtxs[51];

    _VECTOR dir;
    if (ScriptCam == 0 && !MPSettings.isMultiplayer && obj->objectType == OBJECTTYPE_PLAYER)
        Mat_GetDir(&dir, &obj->transformMatrix);
    else
        Mat_GetDir(&dir, foot);

    ATTACH_tag *mark = Effect_NewMark();
    if (mark == NULL)
        return;
    Mat_Align2Up((float *)&mark->matrix, (float *)&hit->surfaceNormal, (float *)&dir);
    Vec_Copy((_VECTOR *)&foot->m[12], (_VECTOR *)&mark->matrix.m[12]);
    // Too far from the floor, the mark stays taken but is never shown
    if (fabs((double)mark->matrix.m[13] - hit->hitPosition.y) > 0.2f)
        return;
    mark->parent = obj;
    mark->matrix.m[13] = hit->hitPosition.y + 0.05f;
    hashtable_set_sub_object_to_entity_gfx(mark, footprint);
    if (hit->hitCel != NULL)
        Effect_LinkToCel(mark, cel);
    else
        Effect_LinkToObject(mark, hit->hitObj);
}

// The laser's sound and its material's smoke, with a spark six times in ten; every other frame a little dust near
// the point
void Effect_Laser(HITDATA_tag *hit, cel_tag *cel, obj_tag *obj, uint flags) {

    if (flags & EFFECT_SOUND)
        Sound_PlayExt((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundStep, 100.0f, 0, 0);

    _VECTOR *normal = &hit->surfaceNormal.normal;
    if (EffectInfo[hit->materialType & 0x3f].laserGas != 0) {

        _VECTOR rot;
        double x = normal->x, z = normal->z;
        rot.x = -Atan2(normal->y, (float)sqrt(x * x + z * z));
        rot.y = Atan2(normal->x, normal->z);
        rot.z = 0.0f;
        _VECTOR at;
        auxVec_AddMulR32(&hit->hitPosition, normal, 0.005f, &at);
        Gas_Init(cel, &at, &rot, NULL, 2.0f, 3.0f, EffectInfo[hit->materialType & 0x3f].laserGas, 5, 0x7f7f64ff,
                 0x643c0018, 10);

        if (Rand_Rand(100) > 40) {
            _VECTOR out = *normal;
            obj_tag *spark = control_create_object(sizeof(Effect_tag), &hit->hitPosition, NULL, NULL);
            ((Effect_tag *)spark->extraObjectData)->life = 1;
            spark->objectType = OBJECTTYPE_EFFECT;
            spark->scale = (float)(Float_FRand(2.0f) + 2.0f);
            spark->rotation.z = (float)Float_FRand(6.2831855f);
            spark->position.x = (float)((double)out.x * 0.01f + spark->position.x);
            spark->position.y = (float)((double)out.y * 0.01f + spark->position.y);
            spark->position.z = (float)((double)out.z * 0.01f + spark->position.z);
            hashtable_set_object_to_entity_gfx(spark, kLaserSparks[Rand_Rand(5)]);
            spark->effectFlags |= 0x20;
            control_link_object_to_cel(spark, cel);
        }
    }

    if (GameState.NumFramesUnpaused & 1) {
        _VECTOR dust;
        Rand_FRandHalf_Vec(&dust, 0.1f);
        Vec_Mul(&dust, &hit->unknown1);
        Vec_Add2(&dust, &hit->hitPosition, &dust);
        Gas_Init(cel, &dust, &CONST_ZERO_VECTOR, NULL, 0.005f, 0.02f, HT_Entity_smoke_1_dust, 0, 0xc8c8c87f, 0x20202006,
                 0x15);
    }
}

// Only the sound, which makes the drones more watchful
void Effect_Body(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags) {
    if (flags & EFFECT_SOUND) {
        DYNAMICSOUNDS *sound = Sound_Play3D((Action_SFX)EffectInfo[hit->materialType & 0x3f].soundBody,
                                            &hit->hitPosition, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Sound_ModAlertness(sound, 0.5f);
    }
}

// Off its cel's or object's marks (it stays in use)
void Effect_UnLink(ATTACH_tag *mark) {
    if (mark == NULL)
        return;
    ATTACH_tag *next = mark->next;
    ATTACH_tag *prev = mark->prev;
    if (next != NULL)
        next->prev = prev;
    if (prev != NULL)
        prev->next = next;
    if (mark->cel != NULL && mark->cel->attachments == mark)
        mark->cel->attachments = next;
    if (mark->parent != NULL && mark->parent->attachments == mark)
        mark->parent->attachments = next;
    mark->next = NULL;
    mark->prev = NULL;
}

// A mark from EffectList; when they are all in use, the oldest is taken back
// AUTOINJECT
ATTACH_tag* Effect_NewMark(void) {
    ATTACH_tag *mark = (ATTACH_tag *)DList_MoveFromFree2InUse(&EffectList);
    if (mark == NULL) {
        ATTACH_tag *oldest = (ATTACH_tag *)EffectList.activeList.head;
        Effect_UnLink(oldest);
        DList_RemoveFromInUse2Free(&EffectList, &oldest->node);
        mark = (ATTACH_tag *)DList_MoveFromFree2InUse(&EffectList);
    }
    return mark;
}

void Effect_LinkToCel(ATTACH_tag *mark, cel_tag *cel) {
    ATTACH_tag *first = cel->attachments;
    mark->next = first;
    mark->prev = NULL;
    if (first != NULL)
        first->prev = mark;
    cel->attachments = mark;
    mark->parent = NULL;
    mark->cel = cel;
}

// On the object, in its space
void Effect_LinkToObject(ATTACH_tag *mark, obj_tag *obj) {
    if (mark == NULL || obj == NULL)
        return;
    _MATRIX inverse;
    _VECTOR world;
    Mat_Copy(&obj->transformMatrix, &inverse);
    Mat_InverseSelf(&inverse);
    Vec_Copy((_VECTOR *)&mark->matrix.m[12], &world);
    Concat(&inverse, &mark->matrix);
    RotPreTransVec(&inverse, &world, (_VECTOR *)&mark->matrix.m[12]);
    ATTACH_tag *first = obj->attachments;
    mark->next = first;
    mark->prev = NULL;
    if (first != NULL)
        first->prev = mark;
    obj->attachments = mark;
    mark->parent = obj;
    mark->cel = NULL;
}

// Every mark on the object back to EffectList (it is deleted or broken)
// AUTOINJECT
void Effect_RemoveSObjs(obj_tag *obj) {
    ATTACH_tag *mark = obj->attachments;
    obj->attachments = NULL;
    while (mark != NULL) {
        ATTACH_tag *next = mark->next;
        Effect_UnLink(mark);
        DList_RemoveFromInUse2Free(&EffectList, &mark->node);
        mark = next;
    }
}

// A particle system of the platform's at pos, as an object drawn in every view (never on the Xbox, whose platform has
// none)
// AUTOINJECT
void Particles_Create(int kind, _VECTOR *pos, plane_equ_tag *plane) {
    int system = psiCreateParticles(kind, pos, plane);
    if (system == -1)
        return;
    obj_tag *obj = control_create_object(0, pos, NULL, NULL);
    if (obj == NULL)
        return;
    obj->transformFlags |= 0x10;
    obj->effectFlags |= 0x21;
    obj->objectType = OBJECTTYPE_PARTICLES;
    obj->radius = 10.0f;
    Vec_Copy(pos, &obj->centrePoint);
    build_LinkToRoom(obj, 0, glb_world);
    View_SetDrawInAllViews(obj);
    obj->extraObjectData = (void *)(intptr_t)system;
}

// A particle system's object goes once the platform's system has run out (on the Xbox, at once)
// AUTOINJECT
void Particles_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    if (!psiParticleUpdate(obj->extraObjectData))
        obj->flags |= 1;
}
