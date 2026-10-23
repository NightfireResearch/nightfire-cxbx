#ifndef EMITTER_H_
#define EMITTER_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// A colour of an emitter's particles at a time of their life (0 to 1); the keys of an emitter run in time
typedef struct KEYED_CLR {
    float r;
    float g;
    float b;
    float a;
    float time;                 // 0x10
} KEYED_CLR;

static_assert(sizeof(KEYED_CLR) == 0x14, "KEYED_CLR is 0x14 bytes");

// An emitter's definition, from the level (hashtable_getitem). Each "base, range" pair is a base value and the range
// a random half either side of it is drawn from.
typedef struct EMITTER_DEF {
    KEYED_CLR *keys;            // 0x00
    int texture;                // 0x04 - a particle list's
    celglist_tag *model;        // 0x08 - a model for each particle instead, if any
    float lifeBase;             // 0x0c - frames at 60 Hz
    float lifeRange;            // 0x10
    float gravity;              // 0x14 - its share of 9.8
    float speedBase;            // 0x18
    float speedRange;           // 0x1c
    float elevationBase;        // 0x20 - from up
    float elevationRange;       // 0x24
    float azimuthBase;          // 0x28
    float azimuthRange;         // 0x2c
    float size;                 // 0x30 - a particle list's
    int keyCount;               // 0x34
    ushort count;               // 0x38 - particles
    ushort perFrame;            // 0x3a - started at most a frame
    ushort additive;            // 0x3c - 0 blends the list additively (psiDrawParticleList's blend)
    char _pad3e[2];
} EMITTER_DEF;

LAYOUT_32(sizeof(EMITTER_DEF) == 0x40, "EMITTER_DEF is 0x40 bytes");

LAYOUT_32(offsetof(EMITTER_DEF, perFrame) == 0x3a, "Bad offset of EMITTER_DEF.perFrame");

// A particle's colour
typedef struct EMITTER_COLOUR {
    uchar b;
    uchar g;
    uchar r;
    uchar a;
} EMITTER_COLOUR;

// A particle's position or velocity: four floats, of which the w is not used
typedef struct EMITTER_VEC {
    _VECTOR v;
    float w;
} EMITTER_VEC;

static_assert(sizeof(EMITTER_VEC) == 0x10, "EMITTER_VEC is 0x10 bytes");

// An emitter's extra data, its particles' arrays after it
typedef struct EMITTER_tag {
    EMITTER_VEC *vel;           // 0x00 - a frame at 60 Hz
    EMITTER_VEC *pos;           // 0x04
    EMITTER_COLOUR *colours;    // 0x08
    float *life;                // 0x0c - frames lived; 99999 for a slot never used
    float *maxLife;             // 0x10
    EMITTER_DEF *def;           // 0x14
    void *unknown18;            // 0x18 - handed to psiDrawParticleList, which does not read it
    _VECTOR rot;                // 0x1c - the models', turning
    uint onChannel;             // 0x28 - it emits while it is on
    uint offChannel;            // 0x2c - while it is off
    uint aliveChannel;          // 0x30 - on while any particle lives
    float lifeLeft;             // 0x34 - frames; past it, only slots never used start
    float period;               // 0x38 - on for half of it, off for the whole, over and over; 0 for always on
    float timer;                // 0x3c
    uchar phaseOn;              // 0x40
    uchar finishedFrames;       // 0x41 - frames all particles have been dead once it was stopping; gone after 3
    uchar active;               // 0x42
    uchar stopping;             // 0x43
    uchar finished;             // 0x44 - nothing to draw
    char _pad45[0x4c - 0x45];
} EMITTER_tag;

LAYOUT_32(sizeof(EMITTER_tag) == 0x4c, "EMITTER_tag is 0x4c bytes");

// An emitter's placement keys (level_tag)
typedef struct EmitterPlacement {
    char _unknown00[0x2c];
    HASHCODE def;               // 0x2c
    uint life;                  // 0x30 - seconds; 0 for for ever, 1 for once
    uint onChannel;             // 0x34
    uint offChannel;            // 0x38
    uint aliveChannel;          // 0x3c
    uint period;                // 0x40 - seconds
} EmitterPlacement;

static_assert(offsetof(EmitterPlacement, period) == 0x40, "Bad offset of EmitterPlacement.period");

#pragma pack(pop)

obj_tag * Emitter_Create(HASHCODE hc, _VECTOR *pos, _VECTOR *dir, float life, uint onChannel, uint offChannel,
                         uint aliveChannel, float period);
obj_tag * Emitter_CreatePlist(_MATRIX *mtx, level_tag *lvl);
void Emitter_Update(obj_tag *obj);
void Emitter_Draw(obj_tag *obj, viewer_tag *viewer);
void ColourGetValue(KEYED_CLR *out, float t, KEYED_CLR *keys, int count);
void parsemap_block_particles(uchar *data);

#endif // EMITTER_H_
