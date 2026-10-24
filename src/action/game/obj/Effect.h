#ifndef EFFECT_H_
#define EFFECT_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// An effect object's extra data (OBJECTTYPE_EFFECT, 0x20 bytes): debris, sparks and muzzle flashes. Debris_Update
// moves it until its life is out or it has faded away.
typedef struct {
    char unknown00[4];
    _VECTOR vel;                // 0x04 - a frame; gravity pulls it while curState is 0
    _VECTOR spin;               // 0x10 - a frame
    short life;                 // 0x1c - frames
    uchar fade;                 // 0x1e - taken from tweakA a frame
    char unknown1f;
} Effect_tag;

static_assert(sizeof(Effect_tag) == 0x20, "Bad size for Effect_tag");

// Smoke from a hit on a material (three a material)
typedef struct HEGAS_tag {
    HASHCODE entity;            // 0x00 - none for 0
    uint kind;                  // 0x04 - Gas_Init's
    uint colour0;               // 0x08
    uint colour1;               // 0x0c
    float scale0;               // 0x10
    float scale1;               // 0x14
    ushort life;                // 0x18
    uchar onSurface;            // 0x1a - 5 cm out from the surface, only on surfaces near upright, turned with it
    uchar faceOut;              // 0x1b - turned out from the surface
} HEGAS_tag;

static_assert(sizeof(HEGAS_tag) == 0x1c, "HEGAS_tag is 0x1c bytes");

// What a hit on a material (HITDATA_tag::materialType & 0x3f) leaves (0x88 bytes; 23 of them)
typedef struct HEINFO_tag {
    const char *name;           // 0x00 - the material's
    HASHCODE decal;             // 0x04 - a bullet's mark (Effect_Bullet)
    HASHCODE footprint4;        // 0x08 - the footprint of a step with flag 4 (bone 55; Effect_Player)
    HASHCODE footprint8;        // 0x0c - with flag 8 (bone 51)
    HASHCODE laserGas;          // 0x10 - the laser's smoke (Effect_Laser)
    ushort soundBullet;         // 0x14
    ushort soundStep;           // 0x16 - steps and the laser
    ushort soundBody;           // 0x18
    ushort ricochet;            // 0x1a - Effect_RicochetProb's range
    float bounce;               // 0x1c - what a bouncing projectile keeps of its speed (and its ricochet)
    HEGAS_tag gas[3];           // 0x20
    HASHCODE debris;            // 0x74 - Debris_Create's model, none for 0
    float debrisSpeed;          // 0x78
    float debrisSpin;           // 0x7c
    uchar unknown80;
    uchar debrisLife;           // 0x81 - frames: half of it fixed, half at random
    uchar debrisOn;             // 0x82 - Effect_Create throws debris
    uchar debrisMax;            // 0x83 - up to this many
    HASHCODE emitter;           // 0x84 - Effect_Create's sparks
} HEINFO_tag;

LAYOUT_32(sizeof(HEINFO_tag) == 0x88, "HEINFO_tag is 0x88 bytes");
LAYOUT_32(offsetof(HEINFO_tag, gas) == 0x20 && offsetof(HEINFO_tag, debris) == 0x74, "HEINFO_tag layout");

#define EFFECT_INFO_COUNT 23
extern HEINFO_tag EffectInfo[EFFECT_INFO_COUNT];

struct cel_tag;

// A mark left on a cel or an object (0x58 bytes, from EffectList): a bullet's or a footprint
typedef struct ATTACH_tag {
    LLNODE_tag node;            // 0x00 - EffectList
    ATTACH_tag *next;           // 0x08 - the cel's or the object's marks
    ATTACH_tag *prev;           // 0x0c
    celglist_tag *glist;        // 0x10
    obj_tag *parent;            // 0x14 - the object it is on, or NULL
    cel_tag *cel;               // 0x18 - the cel it is on, or NULL
    _MATRIX matrix;             // 0x1c - in the cel, or in the object's space
} ATTACH_tag;

LAYOUT_32(sizeof(ATTACH_tag) == 0x58, "ATTACH_tag is 0x58 bytes");

#pragma pack(pop)

// Effect_Create's flags
enum {
    EFFECT_BULLET = 0x1,
    EFFECT_STEP = 0x2,
    EFFECT_LEFT_FOOT = 0x4,     // the footprint of BONE_LEFT_FOOT
    EFFECT_RIGHT_FOOT = 0x8,    // of BONE_RIGHT_FOOT
    EFFECT_LASER = 0x10,
    EFFECT_SPARKS = 0x20,       // and debris
    EFFECT_SOUND = 0x40,
    EFFECT_BODY = 0x80,
};

void Effect_InitLists(void);
uchar Effect_RicochetProb(uchar material, ushort mask);
void Effect_Create(HITDATA_tag *hit, obj_tag *obj, uint flags, float volume);
void Effect_Bullet(obj_tag *obj, HITDATA_tag *hit, cel_tag *cel, uint flags);
void Effect_Player(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags, float volume);
void Effect_Laser(HITDATA_tag *hit, cel_tag *cel, obj_tag *obj, uint flags);
void Effect_Body(HITDATA_tag *hit, obj_tag *obj, cel_tag *cel, uint flags);
void Effect_UnLink(ATTACH_tag *mark);
ATTACH_tag* Effect_NewMark(void);
void Effect_LinkToCel(ATTACH_tag *mark, cel_tag *cel);
void Effect_LinkToObject(ATTACH_tag *mark, obj_tag *obj);
void Effect_RemoveSObjs(obj_tag *obj);
void Particles_Create(int kind, _VECTOR *pos, plane_equ_tag *plane);
void Particles_Update(obj_tag *obj);

#endif // EFFECT_H_
