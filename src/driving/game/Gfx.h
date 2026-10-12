#ifndef DRIVING_GAME_GFX_H_
#define DRIVING_GAME_GFX_H_

// ---------------------------------------------------------------------------------------------------------------
// GFX (0x000d32f0-0x000d3840): the effects gallery's entry points - GFXGallery (EffectsGallery.h) made and freed,
// effects started by their group or name and then handled by the slot CANVAS_Add answered - and a canvas's position
// and orientation, compiled beside them. Three inline methods of the sound classes sit there too: ABaseSound::SetMix,
// AVoice::IsFinished and ASound::ClearLoops (audio/). See Gfx.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "EffectsGallery.h"
#include "../data/CoordConvert.h"         // Coord3, Coord4

class RSceneObj;
class UGroup;

// The effect's 'Flgs' word as Trigger reads it (the names ours; EffectsGallery.h's GfxEffectFlags has the byte's
// sticky bit)
enum GfxTriggerFlags : uint32_t {
    kEffectFlag01 = 0x01,               // CANVAS_Add's flag800; Trigger then answers 0
    kEffectTakeVelocity = 0x10,         // the canvas copies the velocity given
};

class GFX {
public:
    // The effects gallery of the mission's gallery file
    static void Init();                                                                         // 0x000d33f0
    // The gallery freed (TheEffectsGallery is left as it was)
    static void Shutdown();                                                                     // 0x000d35d0
    static int CULL_GetNextCanvas(Coord4 *sphere, float *height, bool *checkFar, float *farScale); // 0x000d3450
    static void PurgeSceneObjEffects(RSceneObj *sceneObj);                                      // 0x000d3490
    static void PurgeAllEffects();                                                              // 0x000d34b0
    // The canvases drawn, unless the HUD is paused
    static void Update();                                                                       // 0x000d34c0

    // An effect started: at `position` and `worldOrientation`, offset by `offset` and `orientation` (in the frame of
    // the scene object's instance when there is one). Answers CANVAS_Add's slot plus 1, 0 for none.
    static int Trigger(UGroup *effect, const Coord3 *position, const Coord4 *worldOrientation, const Coord3 *offset,
                       const Coord4 *orientation, RSceneObj *sceneObj, int instance,
                       const Coord3 *velocity);                                                 // 0x000d34e0
    // ... for an incidental effect: one with kEffectFlag01 stops when culled
    static int TriggerIncidental(UGroup *effect, const Coord3 *position, const Coord4 *worldOrientation,
                                 const Coord3 *offset, const Coord4 *orientation, RSceneObj *sceneObj,
                                 int instance, const Coord3 *velocity);                         // 0x000d3520
    // ... by the effect's name
    static int Trigger(const char *name, const Coord3 *position, const Coord4 *worldOrientation, const Coord3 *offset,
                       const Coord4 *orientation, RSceneObj *sceneObj, int instance,
                       const Coord3 *velocity);                                                 // 0x000d3610
    // What the three call (0x000d32f0: the effect in ESI and the offset in EDI, the rest on the stack). It is not
    // patched - no tag can name it: AUTOLTCG matches by name and three functions are GFX::Trigger, and FUNC_AT
    // refuses register arguments - and nothing else calls it.
    static int Trigger(GFXGallery *gallery, bool incidental, UGroup *effect, const Coord3 *position,
                       const Coord4 *worldOrientation, const Coord3 *offset, const Coord4 *orientation,
                       RSceneObj *sceneObj, int instance, const Coord3 *velocity);

    // By the handle Trigger answered
    static void Purge(int handle);                                                              // 0x000d3560
    static void SuppressEffect(int handle);                                                     // 0x000d3590
    static void SetIntensity(int handle, float intensity);                                      // 0x000d35b0
    // The effect's group, NULL for none
    static UGroup* LookupReference(const char *name);                                           // 0x000d35f0
};

// A canvas's position (w 1) and orientation: its own, or on its scene object, the instance's transform applied to
// its offset and orientation (Ghidra: FUN_000d3730, in GFXGallery.cpp; the name ours)
void CANVAS_GetPosQuat(GFXGallery::Canvas *effectData, Coord4 *position, Coord4 *orient);      // 0x000d3730

#endif // DRIVING_GAME_GFX_H_
