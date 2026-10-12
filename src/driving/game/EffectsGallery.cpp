#pragma fp_contract(off)

#include "EffectsGallery.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>

#include "Debris.h"                       // Debris
#include "FrontEndSystem.h"               // GSystem::FILE_FullPathname
#include "Gfx.h"                          // CANVAS_GetPosQuat
#include "Hud.h"                          // GHud::dbMessage
#include "../audio/Bank.h"                // fgBanks
#include "../audio/Mix.h"                 // AMix
#include "../audio/Sound.h"               // kSoundViewShared
#include "../audio/SoundManager.h"        // fgActiveViews
#include "../audio/ZoomObj.h"             // ASound
#include "../engine/UFileLoader.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"         // VU0_, D3DX
#include "../platform/RealPrint.h"        // MEM_fill
#include "../platform/X87.h"              // Ftol
#include "../render/Particles.h"
#include "../render/RGlareManager.hpp"    // TheGlareManager
#include "../render/RSceneObj.hpp"
#include "../render/Renderer.h"           // fgRenderer
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// GFXGallery (0x000d3840, 0x000d38f0-0x000d57a0), ported from the listing.
//
// Random numbers: GLARE_Draw draws three for a jittered glare, DEBRIS_Draw three for each piece and three more for
// each piece's turn when the key has one - from the effects' own generator (GfxRandom; GSystem::CURATOR_Init seeds
// it), which the original inlines.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define DefaultVector (*(const Coord4 *)0x001d4c00)     // (0,0,0,1); AttributeSet's, handed out by address
#define ParticleCreatorVtable ((const RParticleCreatorVtable *)0x00193c64)  // RParticleSystem::RParticleCreator's

// XBE_GLOBAL(0x0023f320, 0x4)
GFXGallery *TheEffectsGallery;
// The effects' generator, as the XBE starts it (CURATOR_Init seeds it again)
// XBE_GLOBAL(0x001caf34, 0x4)
uint32_t GfxRandomState = 0xf874af01;
// XBE_GLOBAL(0x001caf38, 0x4)
uint32_t GfxRandomMultiplier = 1;
// The index of the particle library's last type added (a byte of it goes into the next type's unknown2C)
// XBE_GLOBAL(0x0023f328, 0x4)
int32_t LastParticleSystem;
// A sound part's patch name for none
// XBE_GLOBAL(0x001ca3e0, 0x4)
const char *NoPatchName = "(null)";

// ---- constants

constexpr float kRandomUnit = 1.0f / 65536.0f;
constexpr float kShellLift = 0.018f;                // a shell casing starts this far above the effect
constexpr float kShellGravity = 0.0049f;
constexpr uint32_t kShellLifetime = 100;
constexpr float kDebrisCountLimitSize = 15.0f;     // up to this size, at most kDebrisCountLimit pieces
constexpr int kDebrisCountLimit = 7;
constexpr float kCullRadius = 5.0f;
constexpr float kCullFarScale = 0.75f;
constexpr int32_t kKeepAlive = 30;                  // RParticleSystem::keepAlive's updates
constexpr float kMungeAlpha = 127.0f;               // a gallery type's gravity and its spread, as the library has them
constexpr float kMungeDegrees = 360.0f;             // ... and its spin
static_assert(std::bit_cast<uint32_t>(kRandomUnit) == 0x37800000 && std::bit_cast<uint32_t>(kShellLift) == 0x3c9374bc &&
              std::bit_cast<uint32_t>(kShellGravity) == 0x3ba0902e, "the effects gallery's constants");

const Coord4 kOrigin = { 0.0f, 0.0f, 0.0f, 1.0f };
const Coord4 kUp = { 0.0f, 1.0f, 0.0f, 0.0f };
const Coord4 kSide = { 1.0f, 0.0f, 0.0f, 0.0f };

// The debris kind of each debris key kind; 2 and 3 take turns between kinds instead
const int kDebrisKinds[20] = { 0, 3, 0, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22 };

// The next 16 bits of the effects' generator
static uint32_t GfxRandom() {
    uint32_t product = GfxRandomMultiplier * GfxRandomState;
    GfxRandomState = product & 0xffff;
    return (product >> 8) & 0xffff;
}

// A canvas's particle parts' emitters given back: their systems are let go
static void FreeEmitters(GFXGallery::Canvas *canvas) {
    for (int i = 0; i < canvas->emitterCount; i++) {
        RParticleSystem *system = canvas->emitters[i].system;
        if (system != NULL) {
            system->keepAlive = 0;
            system->emitter = NULL;
            canvas->emitters[i].system = NULL;
        }
    }
    UMemory::FastFree(canvas->emitters, canvas->emitterCount * sizeof(RParticleEmitter));
    canvas->emitters = NULL;
}

// ---- construction

// FUNC_AT(0x000d44b0)
GFXGallery* GFXGallery::Construct(const char *fileName) {
    gallery = NULL;
    galleryFile = NULL;
    particleSystems = NULL;
    canvases = NULL;
    particleSystemCount = 0;
    canvasCount = 0;
    freeCount = kGfxCanvases;
    cullIndex = 0;
    drawnCount = 0;
    cullCount = 0;
    particleSystems = static_cast<GfxParticleSystem *>(
        UMemory::Alloc(kGfxParticleSystems * sizeof(GfxParticleSystem), 0, "GFXParticleSystems"));
    MEM_fill(particleSystems, 0, UMemory::Size(particleSystems));
    canvases = static_cast<Canvas *>(UMemory::Alloc(kGfxCanvases * sizeof(Canvas), 0, "GFXCanvas"));
    MEM_fill(canvases, 0, UMemory::Size(canvases));

    char path[128];
    GSystem::FILE_FullPathname(fileName, path);
    void *data = UFileLoader::FileLoad(path, 0);
    if (data == NULL) {
        char fallback[128];
        GSystem::FILE_FullPathname("gall1.gal", fallback);
        data = UFileLoader::FileLoad(fallback, 0);
    }
    gallery = UGroup::Deserialize(data, true);
    galleryFile = data;

    for (int i = 0; i < kGfxCanvases; i++)
        freeCanvases[i] = kGfxCanvases - 1 - i;
    stickyUsed[0] = 0;
    stickyUsed[1] = 0;
    return this;
}

// FUNC_AT(0x000d5170)
void GFXGallery::Destruct() {
    PurgeAllEffects();
    stickyUsed[0] = 0;
    stickyUsed[1] = 0;
    if (galleryFile != NULL) {
        UMemory::Free(galleryFile);
        galleryFile = NULL;
    }
    if (particleSystems != NULL) {
        UMemory::Free(particleSystems);
        particleSystems = NULL;
    }
    if (canvases != NULL) {
        UMemory::Free(canvases);
        canvases = NULL;
    }
}

// FUNC_AT(0x000d3840)
UGroup* GFXGallery::GET_EffectsGallery() {
    return gallery;
}

// FUNC_AT(0x000d45f0)
void GFXGallery::PurgeAllEffects() {
    for (int i = 0; i < canvasCount; i++) {
        Canvas *canvas = &canvases[canvasSlots[i]];
        canvas->effect = NULL;
        if (canvas->emitters != NULL)
            FreeEmitters(canvas);
        for (ASound *&sound : canvas->sounds) {
            if (sound != NULL) {
                sound->CallDelete(1);
                sound = NULL;
            }
        }
    }
    for (int i = 0; i < kGfxCanvases; i++) {
        Canvas *canvas = &canvases[i];
        canvas->effect = NULL;
        canvas->sceneObj = NULL;
        canvas->frame = 0;
        canvas->emitters = NULL;
        for (ASound *&sound : canvas->sounds)
            sound = NULL;
        freeCanvases[i] = kGfxCanvases - 1 - i;
    }
    canvasCount = 0;
    freeCount = kGfxCanvases;
    stickyUsed[0] = 0;
    stickyUsed[1] = 0;
}

// ---- the canvases

// FUNC_AT(0x000d47a0)
int GFXGallery::CANVAS_Add(UGroup *effect, RSceneObj *sceneObj, int instance, const Coord4 *position,
                           const Coord4 *worldOrientation, const Coord3 *offset, const Coord4 *orientation,
                           const Coord3 *velocity, bool flag800, bool takeVelocity, bool flag8000) {
    if (freeCount <= (flag8000 ? 64 : 0)) {
        GHud::dbMessage("EXCEEDED MAX GFX!", 60);
        return 0;
    }
    freeCount--;
    int index = freeCanvases[freeCount];
    canvasSlots[canvasCount] = index;
    Coord3 startVelocity = { 0.0f, 0.0f, 0.0f };
    if (velocity != NULL && takeVelocity)
        startVelocity = *velocity;

    const uint8_t *effectFlags = effect->DataLocateTag(kTagFlags)->Data();
    int sticky = 0xff;
    int count = *reinterpret_cast<int32_t *>(effect->DataLocateTag(kTagParticleCount)->Data());
    RParticleEmitter *emitters = NULL;
    if (count > 0) {
        emitters = static_cast<RParticleEmitter *>(UMemory::FastAlloc(count * sizeof(RParticleEmitter), "effect"));
        if (sceneObj != NULL && (*effectFlags & kEffectSticky))
            sticky = STICKY_GetNewIndex(index);
        for (int i = 0; i < count; i++) {
            emitters[i].body = sticky;
            emitters[i].systemTag = 0xffffffff;
            emitters[i].system = NULL;
        }
    }

    Coord3 where = { position->x, position->y, position->z };
    Coord4 world = *worldOrientation;
    alignas(16) Coord4 rotation = *orientation;
    if (((flag800 && !(*effectFlags & kEffectSticky)) || takeVelocity) && sceneObj != NULL) {
        alignas(16) MATRIX4 transform;
        VU0_quattom4(&transform, &rotation);
        transform.mtx[3][0] = offset->x;
        transform.mtx[3][1] = offset->y;
        transform.mtx[3][2] = offset->z;
        transform.mtx[3][3] = 1.0f;
        sceneObj->TransformMatrixByInstancePosition(instance, &transform, true);
        VU0_m4toquat(&world, &transform);
        where = { transform.mtx[3][0], transform.mtx[3][1], transform.mtx[3][2] };
    }
    int life = *reinterpret_cast<int32_t *>(effect->DataLocateTag(kTagLife)->Data());

    Canvas *canvas = &canvases[index];
    if (canvas != NULL) {               // as the original tests it
        canvas->orientation = rotation;
        canvas->offset = *offset;
        canvas->effect = effect;
        canvas->worldOrientation = world;
        canvas->position = where;
        canvas->life = life;
        canvas->velocity = startVelocity;
        canvas->velocitySource = velocity;
        canvas->emitters = emitters;
        canvas->sceneObj = sceneObj;
        canvas->instance = instance;
        canvas->flags = (canvas->flags & 0x03ff) | (flag8000 ? Canvas::kFlag8000 : 0) |
                        (flag800 ? Canvas::kFlag800 : 0);
        canvas->emitterCount = int8_t(count);
        canvas->frame = 0;
        canvas->unknown60 = 0.0f;
        canvas->unknown64 = 0;
        canvas->time = 0;
        canvas->sticky = uint8_t(sticky);
        for (ASound *&sound : canvas->sounds)
            sound = NULL;
    }
    canvasCount++;
    return index + 1;
}

// FUNC_AT(0x000d4010)
void GFXGallery::CANVAS_Purge(int handle) {
    if (handle - 1 >= 0)
        CANVAS_PurgeSceneObj(handle - 1);
}

// FUNC_AT(0x000d4030)
void GFXGallery::CANVAS_Suppress(int handle) {
    canvases[handle - 1].flags |= Canvas::kSuppressed;
}

// FUNC_AT(0x000d4050)
void GFXGallery::CANVAS_SetIntensity(int handle, float intensity) {
    Canvas *canvas = &canvases[handle - 1];
    int count = *reinterpret_cast<int32_t *>(canvas->effect->DataLocateTag(kTagParticleCount)->Data());
    for (int i = 0; i < count; i++) {
        RParticleSystem *system = canvas->emitters[i].system;
        if (system != NULL)
            system->rateScale = intensity;
    }
}

// FUNC_AT(0x000d3ed0)
void GFXGallery::CANVAS_PurgeSceneObj(int index) {
    Canvas *canvas = &canvases[index];
    if (canvas->flags & Canvas::kFlag800) {
        canvas->sceneObj = NULL;
        return;
    }
    Coord3 velocity = { 0.0f, 0.0f, 0.0f };
    if (canvas->sceneObj != NULL) {
        const Coord3 *current = canvas->sceneObj->GetVelocityVirtual();
        if (current != NULL)
            velocity = *current;
    }
    canvas->velocity = velocity;
    canvas->velocitySource = NULL;
    canvas->flags |= Canvas::kStopping;
    canvas->flags |= Canvas::kFlag800;
    canvas->frame = canvas->life;
    for (int i = 0; i < canvas->emitterCount; i++) {
        RParticleSystem *system = canvas->emitters[i].system;
        if (system != NULL)
            system->active = 0;
    }
    canvas->sceneObj = NULL;
}

// FUNC_AT(0x000d4b30)
void GFXGallery::CANVAS_PurgeSceneObjEffects(RSceneObj *sceneObj) {
    for (int i = 0; i < canvasCount; i++) {
        if (canvases[canvasSlots[i]].sceneObj == sceneObj)
            CANVAS_PurgeSceneObj(canvasSlots[i]);
    }
}

// FUNC_AT(0x000d3d90)
bool GFXGallery::CANVAS_HasStopped(Canvas *canvas) {
    if (canvas->emitters != NULL) {
        for (int i = 0; i < canvas->emitterCount; i++) {
            RParticleSystem *system = canvas->emitters[i].system;
            if (system != NULL && system->active)
                return false;
        }
    }
    bool waiting = false;
    for (ASound *&sound : canvas->sounds) {
        if (sound == NULL)
            continue;
        if (canvas->flags & Canvas::kFlag800) {
            if (!sound->voice.views[kSoundViewShared].finished) {
                waiting = true;
                continue;
            }
            if (!(fgActiveViews & (1 << kSoundViewShared))) {
                bool playing = false;
                for (int view = 0; view < 2; view++) {
                    if ((fgActiveViews & (1 << view)) && !sound->voice.views[view].finished) {
                        playing = true;
                        break;
                    }
                }
                if (playing) {
                    waiting = true;
                    continue;
                }
            }
        }
        sound->CallDelete(1);
        sound = NULL;
    }
    if (waiting)
        return false;
    if (canvas->emitters != NULL)
        FreeEmitters(canvas);
    return true;
}

// ---- culling

// FUNC_AT(0x000d3aa0)
void GFXGallery::CULL_Start() {
    cullIndex = 0;
    cullCount = 0;
}

// FUNC_AT(0x000d3ab0)
int GFXGallery::CULL_GetNextCanvas(Coord4 *sphere, float *height, bool *checkFar, float *farScale) {
    if (cullIndex >= canvasCount)
        return -1;
    alignas(16) Coord4 rotation;
    GET_EffectPosQuat(canvasSlots[cullIndex], sphere, &rotation);
    sphere->w = kCullRadius;
    *height = kCullRadius;
    *checkFar = true;
    *farScale = kCullFarScale;
    return cullIndex++;
}

// FUNC_AT(0x000d3b30)
void GFXGallery::CULL_SetCanvas(int item, bool culled, float distance) {
    (void)distance;
    Canvas *canvas = &canvases[canvasSlots[item]];
    if (culled)
        canvas->flags |= Canvas::kCulled;
    else
        canvas->flags &= ~Canvas::kCulled;
    if (culled) {
        if (canvas->flags & Canvas::kFlag8000)
            canvas->flags |= Canvas::kStopping;
        else if ((canvas->flags & Canvas::kFlag800) && canvas->sceneObj == NULL)
            canvas->flags |= Canvas::kStopping;
        cullCount++;
    }
}

// ---- the sticky entries

// FUNC_AT(0x000d3bc0)
int GFXGallery::STICKY_GetNewIndex(int index) {
    for (int word = 0; word < 2; word++) {
        uint32_t used = stickyUsed[word];
        if (used == 0xffffffff)
            continue;
        for (int bit = 0; bit < 32; bit++, used >>= 1) {
            if ((used & 1) == 0) {
                int sticky = word * 32 + bit;
                stickyUsed[word] |= 1u << bit;
                stickies[sticky].canvas = index;
                return sticky;
            }
        }
    }
    return -1;
}

// FUNC_AT(0x000d3c30)
void GFXGallery::STICKY_UpdateAllIndexes() {
    for (int first = 0; first < kGfxStickies; first += 32) {
        uint32_t used = stickyUsed[first / 32];
        for (int bit = 0; used != 0 && bit < 32; bit++, used >>= 1) {
            if (used & 1) {
                GfxSticky *sticky = &stickies[first + bit];
                alignas(16) Coord4 where;
                alignas(16) Coord4 rotation;
                CANVAS_GetPosQuat(&canvases[sticky->canvas], &where, &rotation);
                sticky->position = { where.x, where.y, where.z };
            }
        }
    }
}

// FUNC_AT(0x000d3ce0)
bool GFXGallery::GET_StickyEffectPos(unsigned index, Coord4 *where) {
    if (index < kGfxStickies && (stickyUsed[index >> 5] & (1u << (index & 31)))) {
        const Coord3 &position = stickies[index].position;
        *where = { position.x, position.y, position.z, 0.0f };
        return true;
    }
    *where = DefaultVector;
    return false;
}

// FUNC_AT(0x000d39f0)
bool GFXGallery::GET_EffectPosQuat(int index, Coord4 *where, Coord4 *rotation) {
    if (index >= 0 && index < kGfxCanvases) {
        CANVAS_GetPosQuat(&canvases[index], where, rotation);
        return true;
    }
    *where = DefaultVector;
    *rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    return false;
}

// ---- the keys

// FUNC_AT(0x000d38f0)
void GFXGallery::GET_Keys(GalleryKey *keys, int time, int count, GalleryKey **before, int *beforeTime,
                          GalleryKey **after, int *afterTime) {
    *before = keys;
    *beforeTime = 0;
    *after = keys;
    *afterTime = 0;
    if (time == 0)
        return;
    uint8_t *first = reinterpret_cast<uint8_t *>(keys);
    for (int i = 0; i < count - 1;) {
        GalleryKey *a = reinterpret_cast<GalleryKey *>(first + keys->KeyStride() * i);
        int aTime = a->time;
        i++;
        GalleryKey *b = reinterpret_cast<GalleryKey *>(first + keys->KeyStride() * i);
        int bTime = b->time;
        if (aTime <= time && time <= bTime) {
            *before = a;
            *beforeTime = aTime;
            *after = b;
            *afterTime = bTime;
            return;
        }
        *before = b;
        *beforeTime = bTime;
        *after = b;
        *afterTime = bTime;
    }
}

// FUNC_AT(0x000d39d0)
GalleryKey* GFXGallery::GET_FirstIdleKey(UGroup *part) {
    return reinterpret_cast<GalleryKey *>(part->DataLocateTag(kGalleryTagKeys)->Data());
}

// ---- drawing

// FUNC_AT(0x000d55f0)
void GFXGallery::FRAME_Draw() {
    STICKY_UpdateAllIndexes();
    drawnCount = 0;
    int i = 0;
    while (i < canvasCount) {
        Canvas *canvas = &canvases[canvasSlots[i]];
        if (!(canvas->flags & Canvas::kFlag800))
            canvas->frame = 0;
        if (canvas->frame <= canvas->life) {
            if (canvas->frame >= canvas->life)
                canvas->flags |= Canvas::kStopping;
            alignas(16) Coord4 where;
            alignas(16) Coord4 rotation;
            GET_EffectPosQuat(canvasSlots[i], &where, &rotation);
            canvas->position = { where.x, where.y, where.z };
            if (canvas->flags & Canvas::kSuppressed)
                canvas->flags &= ~Canvas::kSuppressed;
            else
                CANVAS_Draw(canvas, &where, &rotation);
            canvas->frame++;
            drawnCount++;
        }
        canvas->time++;
        if ((canvas->flags & Canvas::kStopping) && CANVAS_HasStopped(canvas)) {
            if (canvas->sticky < kGfxStickies)
                stickyUsed[canvas->sticky >> 5] &= ~(1u << (canvas->sticky & 31));
            freeCanvases[freeCount] = canvasSlots[i];
            canvasSlots[i] = canvasSlots[canvasCount - 1];
            freeCount++;
            canvasCount--;
        } else {
            i++;
        }
    }
}

// FUNC_AT(0x000d51e0)
void GFXGallery::CANVAS_Draw(Canvas *canvas, const Coord4 *where, const Coord4 *rotation) {
    UGroup *effect = canvas->effect;
    bool stopped = true;
    int soundIndex = 0;
    int emitterIndex = 0;
    ASound **sound = canvas->sounds;
    UGroup *end = effect->GetArray() + effect->GroupCount();
    for (UGroup *part = effect->GetArray(); part != end; part++) {
        uint32_t type = part->MatchTag();
        if (type == kTagParticlePart) {
            GalleryKey *keys = reinterpret_cast<GalleryKey *>(part->DataLocateTag(kGalleryTagKeys)->Data());
            if (keys != NULL) {
                const Coord4 *at = canvas->sticky == 0xff ? where : &kOrigin;
                RParticleEmitter *emitter = &canvas->emitters[emitterIndex];
                emitter->body = canvas->sticky;
                stopped &= PARTICLE_Draw(part, emitter, keys, at, rotation, &canvas->velocity, canvas->frame,
                                         (canvas->flags & Canvas::kStopping) ? -1 : 0,
                                         (canvas->flags & Canvas::kCulled) ? -1 : 0,
                                         (canvas->flags & Canvas::kFlag8000) ? -1 : 0);
                emitterIndex++;
            }
        } else if (type == kTagSoundPart) {
            stopped = true;
            if (soundIndex < kGfxCanvasSounds) {
                if (!(canvas->flags & Canvas::kStopping)) {
                    const GallerySound *key = NULL;
                    if (*sound == NULL) {
                        GalleryKey *keys = reinterpret_cast<GalleryKey *>(part->DataLocateTag(kGalleryTagKeys)->Data());
                        key = &keys->sound;
                        if (strcmp(key->patch, NoPatchName) != 0) {
                            ASound *memory = static_cast<ASound *>(ABaseSound::OperatorNew(sizeof(ASound), "ASound"));
                            *sound = memory != NULL ? memory->Construct("Object Sounds") : NULL;
                            if (canvas->flags & Canvas::kFlag800)
                                (*sound)->ClearLoops();
                        }
                    }
                    if (*sound != NULL)
                        PARTICLE_Add(part, key, *sound, where, &canvas->velocity, canvas->frame);
                }
                soundIndex++;
                sound++;
            }
        } else if (type == kTagGlarePart) {
            stopped = false;
            if (!(canvas->flags & (Canvas::kCulled | Canvas::kStopping))) {
                GalleryKey *keys = reinterpret_cast<GalleryKey *>(part->DataLocateTag(kGalleryTagKeys)->Data());
                int count = part->DataLocateTag(kGalleryTagKeys)->count;
                GalleryKey *before;
                GalleryKey *after;
                int beforeTime;
                int afterTime;
                GET_Keys(keys, canvas->time, count, &before, &beforeTime, &after, &afterTime);
                double span = (double)afterTime - beforeTime;
                double t = span == 0.0 ? 0.0 : ((double)canvas->time - beforeTime) / span;
                GalleryGlare glare;
                glare.type = before->glare.type;
                glare.intensity = (float)fabs(((double)after->glare.intensity - before->glare.intensity) * t +
                                              before->glare.intensity);
                glare.jitter = before->glare.jitter;
                Coord4 at = { where->x + before->position.x, before->position.y + where->y,
                              before->position.z + where->z, 1.0f };
                if (before->glare.jitter != 0) {
                    for (int i = 0; i < 3; i++)
                        GLARE_Draw(part, &glare, &at);
                } else {
                    GLARE_Draw(part, &glare, &at);
                }
            }
        } else if (type == kTagDebrisPart && !(canvas->flags & (Canvas::kCulled | Canvas::kStopping))) {
            if ((canvas->flags & Canvas::kDebrisThrown) && canvas->frame != 0) {
                stopped = false;
            } else {
                DEBRIS_Draw(GET_FirstIdleKey(part), where, rotation, NULL, canvas->frame);
                canvas->flags |= Canvas::kDebrisThrown;
            }
        }
    }
    if ((canvas->flags & Canvas::kFlag800) && stopped)
        canvas->flags |= Canvas::kStopping;
}

// FUNC_AT(0x000d4f90)
bool GFXGallery::PARTICLE_Draw(UGroup *part, RParticleEmitter *emitter, GalleryKey *key, const Coord4 *where,
                               const Coord4 *rotation, const Coord3 *velocity, int frame, int stopping, int culled,
                               int flag8000) {
    const RParticleSystemData *data = &key->particle;
    if (data->index == 0 && frame < int32_t(data->unknownA8))
        return false;
    if (emitter->systemTag == 0xffffffff)
        emitter->systemTag = PARTICLE_GetSystem(part, data);
    RParticleSystem *system = emitter->system;
    if (!culled) {
        alignas(16) Coord4 local = { key->position.x, key->position.y, key->position.z, 1.0f };
        VU0_v4quatrotate_xlate(rotation, &local, where, &emitter->position);
        emitter->position.w = 1.0f;
        alignas(16) Coord4 sticky;
        GET_StickyEffectPos(emitter->body, &sticky);
        VU0_v4add4(&sticky, &emitter->position, &sticky);
        float distance = vec3distance(&sticky, &fgRenderer->cameraPosition);
        alignas(16) Coord4 turn = key->orientation;
        D3DXQuaternionMultiply(&turn, &turn, rotation);
        VU0_v4quatrotate(&turn, &kUp, &emitter->direction);
        VU0_v4quatrotate(&turn, &kSide, &emitter->side);
        if (emitter->system == NULL) {
            RParticleSystem::RParticleCreator creator = { ParticleCreatorVtable };
            system = *fgParticleSystems->AddOrRefresh(uint32_t(uintptr_t(emitter)), emitter, &creator, distance);
            emitter->system = system;
        }
        system->keepAlive = kKeepAlive;
        system->unknown34 = distance;
    }
    if (system == NULL)
        return flag8000 != 0;
    if (frame == 0 || frame == int32_t(data->unknownA8)) {
        if (data->flags & kParticleFlag02) {
            system->active = 1;
            system->time = 0;
        }
        system->velocityPerSecond = const_cast<Coord3 *>(velocity);
    }
    if (stopping && !(system->data->flags & kParticleFlag02))
        system->active = 0;
    return (system->data->flags & kParticleFlag02) != 0;
}

// FUNC_AT(0x000d42a0)
uint32_t GFXGallery::PARTICLE_GetSystem(UGroup *part, const RParticleSystemData *data) {
    int count = particleSystemCount;
    for (int i = 0; i < particleSystemCount; i++) {
        if (particleSystems[i].part == part)
            return particleSystems[i].tag;
    }
    RParticleSystemData type = *data;
    uint8_t *link = reinterpret_cast<uint8_t *>(&type.unknown2C);     // unknown2C's second and third bytes
    if (int8_t(link[2]) > 0)
        link[1] = uint8_t(LastParticleSystem);
    uint32_t tag = count + 5;
    type.gravity *= kMungeAlpha;
    type.gravitySpread *= kMungeAlpha;
    type.spin *= kMungeDegrees;
    type.index = tag;
    LastParticleSystem = fgParticleSystems->library.AddSystem(&type, tag);
    particleSystems[particleSystemCount].part = part;
    particleSystems[particleSystemCount].tag = tag;
    particleSystemCount++;
    return particleSystems[particleSystemCount - 1].tag;
}

// FUNC_AT(0x000d43a0)
void GFXGallery::PARTICLE_Add(UGroup *part, const GallerySound *key, ASound *sound, const Coord4 *where,
                              const Coord3 *velocity, int frame) {
    (void)part;
    (void)frame;
    sound->position = { where->x, where->y, where->z };
    sound->forward = { 0.0f, 1.0f, 0.0f };
    sound->velocity = *velocity;
    if (key == NULL)
        return;
    sound->mix->Remove();
    sound->mix = AMix::Add("Giotto");
    ABank *bank = fgBanks[key->bank];
    int handle = bank->handle;
    sound->voice.Set(handle, bank->index.Lookup(key->patch));
    sound->volume = (float)((double)key->volume * 0.01f);
    sound->pitch = (float)((double)key->pitch * 0.01f);
    sound->minDistance = (float)key->minDistance;
    double maxDistance = key->maxDistance;
    sound->maxDistance = (float)maxDistance;
    sound->maxDistanceSq = (float)(maxDistance * maxDistance);
}

// FUNC_AT(0x000d4160)
void GFXGallery::GLARE_Draw(UGroup *part, const GalleryGlare *glare, const Coord4 *where) {
    (void)part;
    Glare queued = {};
    if (glare->jitter != 0) {
        queued.position.x = (float)(((double)where->x - 1.0f) + (double)(2 * GfxRandom()) * 1.0f * kRandomUnit);
        queued.position.y = (float)(((double)where->y - 1.0f) + (double)(2 * GfxRandom()) * 1.0f * kRandomUnit);
        queued.position.z = (float)(((double)where->z - 1.0f) + (double)(2 * GfxRandom()) * 1.0f * kRandomUnit);
    } else {
        queued.position.x = where->x;
        queued.position.y = where->y;
        queued.position.z = where->z;
    }
    queued.w = where->w;
    queued.type = glare->type;
    queued.rangeOrIntensity = glare->intensity;
    queued._pad_0x14[0] = 2;            // the bytes at +0x14 and +0x15 (no reader known)
    queued._pad_0x14[1] = 0;
    queued.flags = 0;
    TheGlareManager->AddGlare(&queued, 0);
}

// FUNC_AT(0x000d4b90)
void GFXGallery::DEBRIS_Draw(const GalleryKey *key, const Coord4 *where, const Coord4 *rotation,
                             const Coord3 *offset, int frame) {
    (void)frame;
    const GalleryDebris *debris = &key->debris;
    int count = Ftol((double)debris->size + debris->size);
    if (debris->size <= kDebrisCountLimitSize)
        count = count > kDebrisCountLimit ? kDebrisCountLimit : count;
    float spreadXZ = debris->size * debris->spreadXZ;
    float spreadY = debris->size * debris->spreadY;
    float lift = debris->size * debris->lift;
    alignas(16) Coord4 base = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (offset != NULL) {
        base.x = offset->x;
        base.y = offset->y;
        base.z = offset->z;
    }

    if (debris->kind == 0) {
        base.y = base.y + kShellLift;
        alignas(16) Coord4 turn = key->orientation;
        alignas(16) Coord4 velocity = { 0.0f, spreadXZ, 0.0f, 1.0f };
        D3DXQuaternionMultiply(&turn, &turn, rotation);
        VU0_v4quatrotate(&turn, &velocity, &velocity);
        VU0_v4add4(&velocity, &base, &velocity);
        Debris->Add(RDebris::kShellCasings, where, &velocity, debris->colour, kShellGravity, kShellLifetime);
        return;
    }

    float halfY = spreadY * 0.5f;
    float halfXZ = spreadXZ * 0.5f;
    for (int i = 0; i < count; i++) {
        alignas(16) Coord4 spread;
        spread.x = (float)((double)GfxRandom() * spreadXZ * kRandomUnit + base.x - halfXZ);
        spread.y = (float)((double)GfxRandom() * spreadY * kRandomUnit + base.y + lift - halfY);
        spread.z = (float)((double)GfxRandom() * spreadXZ * kRandomUnit + base.z - halfXZ);
        spread.w = 0.0f;                // the original leaves it unset: RDebrisType::Add overwrites it
        alignas(16) Coord4 placed = DefaultVector;
        if (debris->turnX != 0.0f || debris->turnZ != 0.0f || debris->turnY != 0.0f) {
            placed.x = (float)((double)GfxRandom() * debris->turnX * kRandomUnit + (double)debris->turnX * -0.5f);
            placed.y = (float)((double)GfxRandom() * debris->turnY * kRandomUnit + (double)debris->turnY * -0.5f);
            placed.z = (float)((double)GfxRandom() * debris->turnZ * kRandomUnit + (double)debris->turnZ * -0.5f);
            VU0_v4quatrotate(rotation, &placed, &placed);
        }
        VU0_v4add4(&placed, where, &placed);
        if (debris->kind != 0 && debris->kind < 20) {
            int kind;
            if (debris->kind == 2)
                kind = i % 3;
            else if (debris->kind == 3)
                kind = i % 2 + 5;
            else
                kind = kDebrisKinds[debris->kind];
            Debris->Add(kind, &placed, &spread, debris->colour, debris->gravity, debris->lifetime);
        }
    }
}
