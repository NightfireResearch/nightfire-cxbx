#ifndef DRIVING_GAME_EFFECTSGALLERY_H_
#define DRIVING_GAME_EFFECTSGALLERY_H_

// ---------------------------------------------------------------------------------------------------------------
// GFXGallery (0x1430 bytes, one, made by GFX::Init from the mission's gallery file): the effects GFX::Trigger
// starts. An effect is a group of the gallery; each playing one is a canvas, of 512, kept in a slot list. A
// canvas follows its scene object (or stays where it was started) and draws its effect's parts each frame: particle
// systems ('gp'), sounds ('gs'), glares ('gg') and debris ('gd'), each part a puppet of keys (Gallery.h). A canvas
// whose effect has the sticky flag keeps a world position the particle cache reads (the sticky entries). See
// EffectsGallery.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Gallery.h"                      // GalleryKey
#include "../data/CoordConvert.h"         // Coord3, Coord4
#include "../../helpers.h"
#include "../../common/xbeClass.h"        // LAYOUT_32

class ASound;
class RSceneObj;
class UGroup;
struct RParticleEmitter;
struct RParticleSystemData;

constexpr int kGfxCanvases = 512;
constexpr int kGfxStickies = 64;
constexpr int kGfxCanvasSounds = 8;
constexpr int kGfxParticleSystems = 256;

// The tags of an effect's records and parts
enum GfxTag : uint32_t {
    kTagFlags = 0x466c6773,             // 'Flgs': a byte of GfxEffectFlags
    kTagParticleCount = 0x4e554d50,     // 'NUMP': the effect's particle parts
    kTagLife = 0x4c494645,              // 'LIFE': the frames it draws for
    kTagParticlePart = 0x67702020,      // 'gp  '
    kTagSoundPart = 0x67732020,         // 'gs  '
    kTagGlarePart = 0x67672020,         // 'gg  '
    kTagDebrisPart = 0x67642020,        // 'gd  '
};

enum GfxEffectFlags : uint8_t {
    kEffectSticky = 0x08,               // on a scene object, its canvas keeps a sticky entry
};

// A sticky entry (0x10 bytes, the names ours): a canvas's position, refreshed every frame
struct GfxSticky {
    Coord3 position;                    // +0x00
    int32_t canvas;                     // +0x0c
};

// What a gallery particle part was added to the particle library as (the names ours)
struct GfxParticleSystem {
    UGroup *part;                       // +0x00
    uint32_t tag;                       // +0x04 its library tag: the count of systems at the time, plus 5
};

class GFXGallery {
public:
    // A playing effect (Ghidra's PS2 symbols: GFXGallery::Canvas, 0x90 bytes; the field names ours)
    struct Canvas {
        enum Flags : uint16_t {
            kCulled = 0x0400,           // CULL_SetCanvas's
            kFlag800 = 0x0800,          // CANVAS_Add's ninth argument: stops once its parts have, waits for its
                                        // sounds; a canvas whose scene object is purged gets it
            kStopping = 0x1000,         // done: removed once its parts have stopped (CANVAS_HasStopped)
            kSuppressed = 0x2000,       // the next frame's draw is skipped
            kDebrisThrown = 0x4000,
            kFlag8000 = 0x8000,         // CANVAS_Add's eleventh argument: stops when culled, and cannot take the
                                        // last 64 canvases
        };

        Coord4 orientation;             // +0x00 relative to the scene object's instance
        Coord3 offset;                  // +0x10 ... and the position
        UGroup *effect;                 // +0x1c
        Coord4 worldOrientation;        // +0x20
        Coord3 position;                // +0x30 refreshed every frame
        int32_t life;                   // +0x3c frames to draw
        Coord3 velocity;                // +0x40 copied from velocitySource when started, or the scene object's
                                        //       when it is purged
        const Coord3 *velocitySource;   // +0x4c
        RSceneObj *sceneObj;            // +0x50
        RParticleEmitter *emitters;     // +0x54 one per particle part ("effect")
        int32_t instance;               // +0x58 the scene object's instance
        int32_t frame;                  // +0x5c frames drawn
        float unknown60;
        uint32_t unknown64;
        int32_t time;                   // +0x68 frames since started
        uint16_t flags;                 // +0x6c Flags
        int8_t emitterCount;            // +0x6e
        uint8_t sticky;                 // +0x6f its sticky entry, 0xff for none
        ASound *sounds[kGfxCanvasSounds];   // +0x70 one per sound part
    };

    GfxSticky stickies[kGfxStickies];   // +0x0000
    uint32_t stickyUsed[2];             // +0x0400 a bit per sticky entry
    int32_t canvasSlots[kGfxCanvases];  // +0x0408 the playing canvases
    int32_t freeCanvases[kGfxCanvases]; // +0x0c08 a stack of the others
    UGroup *gallery;                    // +0x1408
    void *galleryFile;                  // +0x140c
    GfxParticleSystem *particleSystems; // +0x1410 "GFXParticleSystems"
    Canvas *canvases;                   // +0x1414 "GFXCanvas"
    int32_t particleSystemCount;        // +0x1418
    int32_t canvasCount;                // +0x141c the playing ones, in canvasSlots
    int32_t freeCount;                  // +0x1420
    int32_t cullIndex;                  // +0x1424 CULL_GetNextCanvas's next slot
    int32_t drawnCount;                 // +0x1428 the canvases FRAME_Draw drew
    int32_t cullCount;                  // +0x142c the canvases culled since CULL_Start

    // The gallery through FILE_FullPathname ("gall1.gal" if it is missing), every canvas free
    GFXGallery* Construct(const char *file);                                                   // 0x000d44b0
    void Destruct();                                                                            // 0x000d5170
    // Ghidra files it under GGallery
    UGroup* GET_EffectsGallery();                                                               // 0x000d3840
    void PurgeAllEffects();                                                                     // 0x000d45f0

    // A playing effect: its slot, plus 1, or 0 when no canvas is free ("EXCEEDED MAX GFX!")
    int CANVAS_Add(UGroup *effect, RSceneObj *sceneObj, int instance, const Coord4 *position,
                   const Coord4 *worldOrientation, const Coord3 *offset, const Coord4 *orientation,
                   const Coord3 *velocity, bool flag800, bool takeVelocity, bool flag8000);     // 0x000d47a0
    // By the slot CANVAS_Add answered
    void CANVAS_Purge(int handle);                                                              // 0x000d4010
    void CANVAS_Suppress(int handle);                                                           // 0x000d4030
    void CANVAS_SetIntensity(int handle, float intensity);                                      // 0x000d4050
    void CANVAS_PurgeSceneObj(int slot);                                                        // 0x000d3ed0
    void CANVAS_PurgeSceneObjEffects(RSceneObj *sceneObj);                                      // 0x000d4b30
    // Its parts done: the sounds deleted (a kFlag800 canvas waits for them to finish) and the emitters freed
    bool CANVAS_HasStopped(Canvas *canvas);                     // does not read `this`         // 0x000d3d90

    // The canvases one by one for the world camera's culling: their bounding spheres
    void CULL_Start();                                                                          // 0x000d3aa0
    int CULL_GetNextCanvas(Coord4 *sphere, float *height, bool *checkFar, float *farScale);    // 0x000d3ab0
    void CULL_SetCanvas(int item, bool culled, float distance);                                 // 0x000d3b30

    int STICKY_GetNewIndex(int slot);                                                           // 0x000d3bc0
    void STICKY_UpdateAllIndexes();                                                             // 0x000d3c30
    bool GET_StickyEffectPos(unsigned index, Coord4 *position);                                 // 0x000d3ce0
    // A slot's position and orientation, or the defaults when the slot is out of range
    bool GET_EffectPosQuat(int slot, Coord4 *position, Coord4 *orientation);                    // 0x000d39f0

    // The keys either side of `time` (does not read `this`)
    void GET_Keys(GalleryKey *keys, int time, int count, GalleryKey **before, int *beforeTime,
                  GalleryKey **after, int *afterTime);                                          // 0x000d38f0
    GalleryKey* GET_FirstIdleKey(UGroup *part);                 // does not read `this`         // 0x000d39d0

    // Each frame: every playing canvas drawn, the stopped ones freed
    void FRAME_Draw();                                                                          // 0x000d55f0
    void CANVAS_Draw(Canvas *canvas, const Coord4 *position, const Coord4 *orientation);       // 0x000d51e0
    // A particle part's system kept alive and placed; whether the system has stopped (the name ours; Ghidra:
    // FUN_000d4f90)
    bool PARTICLE_Draw(UGroup *part, RParticleEmitter *emitter, GalleryKey *key, const Coord4 *position,
                       const Coord4 *orientation, const Coord3 *velocity, int frame, int stopping, int culled,
                       int flag8000);                                                           // 0x000d4f90
    // A particle part's library tag, adding its type to the library the first time (the name ours; Ghidra:
    // FUN_000d42a0)
    uint32_t PARTICLE_GetSystem(UGroup *part, const RParticleSystemData *data);                 // 0x000d42a0
    // A sound part's sound placed, and given its patch when `key` is not NULL (does not read `this`)
    void PARTICLE_Add(UGroup *part, const GallerySound *key, ASound *sound, const Coord4 *position,
                      const Coord3 *velocity, int frame);                                       // 0x000d43a0
    void GLARE_Draw(UGroup *part, const GalleryGlare *glare, const Coord4 *position);           // 0x000d4160
    void DEBRIS_Draw(const GalleryKey *key, const Coord4 *position, const Coord4 *orientation,
                     const Coord3 *offset, int frame);          // does not read `this`         // 0x000d4b90
};
LAYOUT_32(sizeof(GFXGallery::Canvas) == 0x90, "a canvas is 0x90 bytes");
LAYOUT_32(offsetof(GFXGallery::Canvas, effect) == 0x1c && offsetof(GFXGallery::Canvas, life) == 0x3c &&
          offsetof(GFXGallery::Canvas, sceneObj) == 0x50 && offsetof(GFXGallery::Canvas, frame) == 0x5c &&
          offsetof(GFXGallery::Canvas, flags) == 0x6c && offsetof(GFXGallery::Canvas, sounds) == 0x70,
          "GFXGallery::Canvas layout");
LAYOUT_32(sizeof(GFXGallery) == 0x1430, "GFXGallery is 0x1430 bytes");
LAYOUT_32(offsetof(GFXGallery, stickyUsed) == 0x400 && offsetof(GFXGallery, canvasSlots) == 0x408 &&
          offsetof(GFXGallery, freeCanvases) == 0xc08 && offsetof(GFXGallery, gallery) == 0x1408 &&
          offsetof(GFXGallery, cullCount) == 0x142c, "GFXGallery layout");

extern GFXGallery *TheEffectsGallery;   // GFX::Init's

extern int32_t LastParticleSystem;      // the particle library's last type added
extern const char *NoPatchName;         // "(null)": a sound part without a patch

// The effects' own random generator (Ghidra: fastRandom, randSeed): GSystem::CURATOR_Init seeds it
extern uint32_t GfxRandomState;
extern uint32_t GfxRandomMultiplier;

#endif // DRIVING_GAME_EFFECTSGALLERY_H_
