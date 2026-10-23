#include "Emitter.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../../engine/psiDraw.h"
#include "../../engine/psiGraphics.h"
#include "../../math/X87Ops.h"
#include "../view.h"
#include "control.h"
#include "../../memory.h"
#include "../../util/bin.h"
#include "../../util/hashtable.h"

#pragma fp_contract(off)

// ColourGetValue's answer and the keys and weights of its last interpolation, Emitter_Draw's particle counter and
// matrix: statics of the original's
// XBE_GLOBAL(0x001f6450, 0x4)
int32_t ColourKey;
// XBE_GLOBAL(0x001f6454, 0x4)
int32_t ColourNextKey;
// XBE_GLOBAL(0x001f6448, 0x4)
float ColourWeight;
// XBE_GLOBAL(0x001f644c, 0x4)
float ColourNextWeight;
// XBE_GLOBAL(0x001f6458, 0x14)
KEYED_CLR ParticleColour;
// XBE_GLOBAL(0x001f64a8, 0x2)
uint16_t DrawIndex;
// XBE_GLOBAL(0x001f646c, 0x3c)
_MATRIX DrawMatrix;

// A slot never used
#define PARTICLE_UNUSED 99999.0f

// An emitter of the definition hc at pos, its particles going out about dir: life frames (0 to start only the slots
// never used), emitting while onChannel is on and offChannel off, aliveChannel on while any particle lives, and
// on for half of each period. Not checked: the object.
// AUTOINJECT
obj_tag* Emitter_Create(HASHCODE hc, _VECTOR *pos, _VECTOR *dir, float life, uint onChannel, uint offChannel,
                        uint aliveChannel, float period) {
    if (hc == 0)
        return NULL;
    EMITTER_DEF *def = (EMITTER_DEF *)hashtable_getitem(hc);
    if (def == NULL)
        return NULL;

    uint size = def->count * 0x2c + 0x128;
    // The w column is uninitialised stack in the original
    _MATRIX mtx;
    memset(&mtx, 0, sizeof(mtx));
    Vec_Copy(pos, (_VECTOR *)&mtx.m[12]);
    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, dir);
    Mat_SetDir(&u, &mtx);
    Vec_Copy(&v, (_VECTOR *)&mtx.m[0]);
    Mat_SetUp(dir, &mtx);
    obj_tag *obj = Control_CreateObjEx(size, NULL, NULL, &mtx, NULL, NULL, 1, 0x104, 1.0f, 0, 0xff, 0xff, 0xff);

    EMITTER_tag *emitter = (EMITTER_tag *)obj->extraObjectData;
    obj->objectType = OBJECTTYPE_EMITTER;
    emitter->vel = (EMITTER_VEC *)(emitter + 1);
    emitter->pos = emitter->vel + def->count;
    emitter->life = (float *)(emitter->pos + def->count);
    emitter->maxLife = emitter->life + def->count;
    emitter->colours = (EMITTER_COLOUR *)(emitter->maxLife + def->count);
    emitter->lifeLeft = life;
    emitter->def = def;
    emitter->finishedFrames = 0;
    emitter->stopping = life == 0.0f;
    emitter->offChannel = offChannel;
    emitter->onChannel = onChannel;
    emitter->active = 1;
    emitter->aliveChannel = aliveChannel;
    emitter->period = period;
    emitter->timer = 0.0f;
    emitter->phaseOn = 1;
    Vec_Zero(&emitter->rot);
    for (ushort i = 0; i < emitter->def->count; i++) {
        emitter->life[i] = PARTICLE_UNUSED;
        emitter->maxLife[i] = 0.0f;
        memset(&emitter->colours[i], 0, sizeof(EMITTER_COLOUR));
    }

    Vec_Copy(pos, &obj->centrePoint);
    obj->effectFlags |= EFFECT_BACK_TO_FRONT | EFFECT_NO_COLLISION;
    obj->radius = (float)(((double)def->speedRange + def->speedBase) * ((double)def->lifeRange + def->lifeBase));
    return obj;
}

// A placed emitter, out along its matrix's up. Its life and period are in seconds.
// AUTOINJECT
obj_tag* Emitter_CreatePlist(_MATRIX *mtx, level_tag *lvl) {
    EmitterPlacement *keys = (EmitterPlacement *)lvl;
    _VECTOR up;
    Mat_GetUp(&up, mtx);
    float life;
    if (keys->life == 0)
        life = -1.0f;
    else if (keys->life == 1)
        life = 0.0f;
    else
        life = (float)((double)keys->life * 0.016666668f);
    float period = (float)((double)keys->period * 0.016666668f);
    return Emitter_Create(keys->def, (_VECTOR *)&mtx->m[12], &up, life, keys->onChannel, keys->offChannel,
                          keys->aliveChannel, period);
}

// The colour at t (0 to 1 of a life): between the last key at or before t + 0.001 and the one after it, which may be
// past the last key (the next bytes in memory); the first key before them all
// AUTOINJECT
void ColourGetValue(KEYED_CLR *out, float t, KEYED_CLR *keys, int count) {
    t = t + 0.001f;
    int key = -1;
    ColourKey = -1;
    for (int i = count - 1; i >= 0; i--) {
        if (keys[i].time <= t) {
            key = i;
            ColourKey = key;
            break;
        }
    }
    int next;
    if (key >= count) {
        key = next = count;
        ColourKey = key;
        ColourWeight = 1.0f;
        ColourNextKey = next;
        ColourNextWeight = 0.0f;
    } else if (key < 0) {
        key = next = 0;
        ColourKey = key;
        ColourWeight = 1.0f;
        ColourNextKey = next;
        ColourNextWeight = 0.0f;
    } else {
        next = key + 1;
        ColourNextKey = next;
        ColourNextWeight =
            (float)(((double)t - keys[key].time) / ((double)keys[next].time - keys[key].time));
        ColourWeight = 1.0f - ColourNextWeight;
    }
    out->r = (float)((double)ColourNextWeight * keys[next].r + (double)ColourWeight * keys[key].r);
    out->g = (float)((double)ColourNextWeight * keys[ColourNextKey].g + (double)ColourWeight * keys[ColourKey].g);
    out->b = (float)((double)ColourNextWeight * keys[ColourNextKey].b + (double)ColourWeight * keys[ColourKey].b);
    out->a = (float)((double)ColourNextWeight * keys[ColourNextKey].a + (double)ColourWeight * keys[ColourKey].a);
}

// A frame of the particles: on and off by its channels and its period, the live ones moving, falling and changing
// colour along their keys, dead ones starting again, up to perFrame a frame, out from its matrix's up at a random
// speed, elevation and azimuth. Gone three frames after it has stopped with every particle dead.
// AUTOINJECT
void Emitter_Update(obj_tag *obj) {
    Vec_Copy((_VECTOR *)&obj->transformMatrix.m[12], &obj->centrePoint);
    EMITTER_tag *emitter = (EMITTER_tag *)obj->extraObjectData;
    if (emitter->onChannel != 0)
        emitter->active = switch_channels[emitter->onChannel];
    if (emitter->offChannel != 0)
        emitter->active = !switch_channels[emitter->offChannel];

    if (emitter->period != 0.0f) {
        double length = emitter->period;
        if (emitter->phaseOn)
            length = length * 0.5f;
        double timer = (double)REC_FRAME_RATE + emitter->timer;
        emitter->timer = (float)timer;
        if (timer > length) {
            emitter->phaseOn = !emitter->phaseOn;
            emitter->timer = 0.0f;
        }
    }
    if (emitter->lifeLeft > 0.0f) {
        emitter->lifeLeft = emitter->lifeLeft - REC_FRAME_RATE;
        if (emitter->lifeLeft < 0.0f)
            emitter->stopping = 1;
    }
    if (emitter->aliveChannel != 0)
        switch_channels[emitter->aliveChannel] = 0;

    uint dead = 0;
    for (uint i = 0; i < emitter->def->count; i++) {
        if (!(emitter->life[i] < emitter->maxLife[i])) {
            dead++;
            emitter->colours[i].a = 0;
            continue;
        }
        ColourGetValue(&ParticleColour, (float)(1.0f / (double)emitter->maxLife[i] * emitter->life[i]),
                       emitter->def->keys, emitter->def->keyCount);
        emitter->colours[i].r = (uchar)Ftol(ParticleColour.r);
        emitter->colours[i].g = (uchar)Ftol(ParticleColour.g);
        emitter->colours[i].b = (uchar)Ftol(ParticleColour.b);
        emitter->colours[i].a = (uchar)Ftol(ParticleColour.a);
        auxVec_AddMulR32(&emitter->pos[i].v, &emitter->vel[i].v, REC_FRAME_RATE, &emitter->pos[i].v);
        emitter->vel[i].v.y =
            (float)(emitter->vel[i].v.y - (double)REC_FRAME_RATE * emitter->def->gravity * 9.8f);
        emitter->life[i] = REC_FRAME_RATE + emitter->life[i];
        if (emitter->aliveChannel != 0) {
            switch_channels[emitter->aliveChannel] = 1;
            switch_channels_time[emitter->aliveChannel] = GameState.NumFramesUnpaused;
        }
    }

    int toStart = emitter->def->perFrame;
    for (uint i = 0; i < emitter->def->count; i++) {
        if (toStart <= 0 || emitter->finishedFrames || !emitter->active || !emitter->phaseOn)
            break;
        if (!(emitter->life[i] > emitter->maxLife[i]))
            continue;
        if (emitter->stopping && emitter->life[i] < PARTICLE_UNUSED)
            continue;

        emitter->life[i] = 0.0f;
        emitter->colours[i].r = (uchar)Ftol(emitter->def->keys->r);
        emitter->colours[i].g = (uchar)Ftol(emitter->def->keys->g);
        emitter->colours[i].b = (uchar)Ftol(emitter->def->keys->b);
        emitter->colours[i].a = (uchar)Ftol(emitter->def->keys->a);
        float azimuth = (float)(Rand_FRandHalf(1.0f) * emitter->def->azimuthRange + emitter->def->azimuthBase);
        double elevation = Rand_FRandHalf(1.0f) * emitter->def->elevationRange + emitter->def->elevationBase;
        _VECTOR dir;
        Extended sinElevation = FSin(elevation);
        dir.x = (float)(FCos(azimuth) * sinElevation);
        dir.y = (float)FCos(elevation);
        dir.z = (float)(FSin(azimuth) * sinElevation);
        float speed = (float)(Rand_FRandHalf(1.0f) * emitter->def->speedRange + emitter->def->speedBase);
        Vec_MulR32(&dir, &dir, speed);
        Vec_Copy((_VECTOR *)&obj->transformMatrix.m[12], &emitter->pos[i].v);
        emitter->maxLife[i] = (float)(Rand_FRandHalf(1.0f) * emitter->def->lifeRange + emitter->def->lifeBase);
        ApplyMatrixLV(&obj->transformMatrix, &dir, &emitter->vel[i].v);
        toStart--;
    }

    uint ended = 0;
    for (uint i = 0; i < emitter->def->count; i++) {
        if (emitter->life[i] >= emitter->maxLife[i])
            ended++;
    }
    uchar finished = emitter->finishedFrames;
    emitter->finished = (ended == emitter->def->count) | finished;
    if ((finished && dead == emitter->def->count) || (emitter->stopping && ended == emitter->def->count)) {
        emitter->finishedFrames = finished + 1;
        if (emitter->finishedFrames > 2)
            obj->flags |= 1;
    }
}

// Its particles: a model for each live one, turning, or the list
// AUTOINJECT
void Emitter_Draw(obj_tag *obj, viewer_tag *viewer) {
    EMITTER_tag *emitter = (EMITTER_tag *)obj->extraObjectData;
    if (emitter->finished)
        return;
    EMITTER_DEF *def = emitter->def;
    if (def->model == NULL) {
        psiDrawParticleList(&emitter->pos[0].v.x, def->count, 0xffffffff, def->texture,
                            (const uint32_t *)emitter->colours, def->additive == 0, def->size);
        return;
    }
    for (DrawIndex = 0; DrawIndex < emitter->def->count; DrawIndex++) {
        EMITTER_COLOUR *colour = &emitter->colours[DrawIndex];
        if (colour->a < 1)
            continue;
        psiSetTweakARGB(colour->a, colour->r, colour->g, colour->b);
        emitter->rot.x = REC_FRAME_RATE + emitter->rot.x;
        emitter->rot.y = REC_FRAME_RATE + emitter->rot.y;
        View_RotTransMatrix(&emitter->rot, &emitter->pos[DrawIndex].v, &DrawMatrix);
        psiDrawObjectMatrix(emitter->def->model, &DrawMatrix);
    }
}

// The map's emitter definitions (block 0x30; nightfire-port's Emitter_LoadDefs): each, of version 4, is read into an
// EMITTER_DEF with its colour keys after it and listed under its hashcode. A definition of another version ends the
// block. The keys' red, green and blue are halved.
// AUTOINJECT
void parsemap_block_particles(uchar *data) {
    uint count = BIN_GetDWord(&data);
    for (ushort i = 0; i < count; i++) {
        uint version = BIN_GetDWord(&data);
        if ((uchar)version != 4)
            return;
        HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&data);
        EMITTER_DEF def = {};
        def.additive = (ushort)(version >> 8);
        HASHCODE texture = (HASHCODE)BIN_GetDWord(&data);
        HASHCODE model = (HASHCODE)BIN_GetDWord(&data);
        def.count = BIN_GetWord(&data);
        def.perFrame = BIN_GetWord(&data);
        def.size = BIN_GetFloat(&data);
        def.lifeBase = BIN_GetFloat(&data);
        def.lifeRange = BIN_GetFloat(&data);
        def.gravity = BIN_GetFloat(&data);
        def.speedBase = BIN_GetFloat(&data);
        def.speedRange = BIN_GetFloat(&data);
        def.elevationBase = BIN_GetFloat(&data);
        def.elevationRange = BIN_GetFloat(&data);
        def.azimuthBase = BIN_GetFloat(&data);
        def.azimuthRange = BIN_GetFloat(&data);
        def.keyCount = BIN_GetDWord(&data);
        // GC check (0x800c9358): a definition with no texture
        NF_WARN_IF(texture == 0xffffffff, "EMITTER TEXTURE MISSING\n");
        int *textureItem = texture != 0xffffffff ? (int *)hashtable_getitem(texture) : NULL;
        def.texture = textureItem != NULL ? *textureItem : 0;
        def.model = model != 0xffffffff ? (celglist_tag *)hashtable_getitem(model) : NULL;

        EMITTER_DEF *made = (EMITTER_DEF *)Mem_Malloc(sizeof(EMITTER_DEF) + def.keyCount * sizeof(KEYED_CLR),
                                                    (MallocFlags)0x4204, 0);
        if (made == NULL)
            continue;
        *made = def;
        made->keys = (KEYED_CLR *)(made + 1);
        for (ushort k = 0; k < (uint)def.keyCount; k++) {
            KEYED_CLR *key = &made->keys[k];
            key->r = BIN_GetFloat(&data) * 0.5f;
            key->g = BIN_GetFloat(&data) * 0.5f;
            key->b = BIN_GetFloat(&data) * 0.5f;
            key->a = BIN_GetFloat(&data);
            key->time = BIN_GetFloat(&data);
        }
        hashtable_additem(hashcode, made);
    }
}
