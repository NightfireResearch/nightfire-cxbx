#ifndef DRIVING_RENDER_LIGHTS_H_
#define DRIVING_RENDER_LIGHTS_H_

// ---------------------------------------------------------------------------------------------------------------
// The renderer's lighting (0x0007e430..0x0007f5a0):
//   RLightManager (0x3f0 bytes, a USingleton, vtable 0x00191628; one instance, fgLightManager): the light sets
//     loaded from the "Render:Lighting" tuning database, one per environment, the one in use copied into an EAGL
//     light block for the render methods; the specular matrix; and the positional lights gathered over a frame,
//     four at most, handed over to the next frame by FinishLights.
//   RHighLevelLightManager (0x118 bytes, the light manager's last member): the positional lights the game asks
//     for - effects' lights, explosions that fade by their type's decay, missiles - which Process passes on to the
//     light manager once a frame.
// See Lights.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../engine/USingleton.h"
#include "../world/CollisionTypes.h"    // MATRIX4

struct ArticleEffect;                   // world/World.h: a dynamic light is one of type kTypeLight

// A light set (EAGL::LightBlock, 0x70 bytes, "EAGL::LightBlock new"; Ghidra calls the light manager's RLightInfo):
// four lights' directions as rows of x, y and z, and their colours. Light 0 is the tuning file's "Light 1", 1 "Light
// 2", 2 the ground light ("Ambient Ground", pointing along the "Dual Hemi" angles), 3 the sky light ("Ambient Sky").
struct LightBlock {
    float directions[3][4];             // +0x00 [x, y, z][light]
    Coord4 colours[4];                  // +0x30

    // The directions' x, y and z only; the colour comes back as (w, x, y, z). The linker placed both among the
    // characters' code.
    void GetLight(int light, Coord4 *direction, Coord4 *colour);                                // 0x000148a0
    void SetLight(int light, const Coord4 *direction, const Coord4 *colour);                    // 0x000148f0
};
static_assert(sizeof(LightBlock) == 0x70, "a light set is 0x70 bytes");

enum LightIndex {
    kLight1 = 0,
    kLight2 = 1,
    kLightGround = 2,
    kLightSky = 3,
};

// The light sets, by the tuning file's "Current Environment" index (its names: "World", "Character", "Unused1")
enum LightEnvironment {
    kEnvironmentWorld = 0,
    kEnvironmentCharacter = 1,
    kEnvironmentUnused1 = 2,
    kEnvironmentCount = 4,
};

// SetLightingModel's models: the set each selects
enum LightingModel {
    kLightingModel0 = 0,                // the world's lights
    kLightingModel1 = 1,                // the world's lights
    kLightingModel2 = 2,                // the character lights (ActCharacter::Draw)
};

// A light set's angles, in degrees: the rotations about x, then y, that turn (0, 0, 1) into each of the first
// three lights' directions
struct RLightAngles {
    float x[3];                         // "Light1 Angle X", "Light2 Angle X", "Dual Hemi Angle X"
    float y[3];                         // "Light1 Angle Y", "Light2 Angle Y", "Dual Hemi Angle Y"
};
static_assert(sizeof(RLightAngles) == 0x18, "a set's angles are six floats");

// The positional lights of a frame, as the render methods read them (GAME::PositionalLights, 0x80 bytes): the
// four lights' positions by component (w: the light's size), then their colours (w not written)
struct RPositionalLights {
    float positions[4][4];              // +0x00 [x, y, z, w][light]
    Coord4 colours[4];                  // +0x40
};
static_assert(sizeof(RPositionalLights) == 0x80, "the positional lights are 0x80 bytes");

constexpr int kMaxPositionalLights = 4;

// The tuning file's explosion and missile types: a colour and a size, and for an explosion its decay
struct RLightType {
    Coord4 colour;                      // "Colour"
    float size;                         // "Size"
    float decay;                        // "Decay" (explosions only)
};
static_assert(sizeof(RLightType) == 0x18, "a light type is 0x18 bytes");

// ---- RHighLevelLightManager

class RHighLevelLightManager {
public:
    // A light an effect asked for this frame
    struct PositionalLight {
        Coord4 position;                // +0x00
        Coord3 colour;                  // +0x10
        int32_t priority;               // +0x1c a full table gives the slot to a higher one
    };

    PositionalLight lights[kMaxPositionalLights];   // +0x00
    int32_t lightCount;                             // +0x80
    Coord4 explosionPositions[4];                   // +0x84 w: the type's size, written by Process
    int32_t explosionTypes[4];                      // +0xc4 by size: "Tiny" .. "Huge"
    uint32_t explosionStartTicks[4];                // +0xd4 0: the slot is free
    int32_t missileCount;                           // +0xe4
    Coord4 missilePositions[2];                     // +0xe8 w: the type's size times the scale, written by Process
    int32_t missileTypes[2];                        // +0x108
    float missileScales[2];                         // +0x110

    void AddExplosion(const Coord4 *position, float size);                                      // 0x0007e620
    void AddMissile(const Coord4 *position, int type, float scale);                             // 0x0007e6f0
    void AddPositionalLight(const Coord4 *position, const Coord3 *colour, int priority);        // 0x0007e790
    void AddDynamicLightEffect(const ArticleEffect *effect, const MATRIX4 *transform);          // 0x0007ec40
    // Hands every light, explosion and missile of the frame to the light manager
    void Process();                                                                             // 0x0007ed10
};
static_assert(sizeof(RHighLevelLightManager) == 0x118, "RHighLevelLightManager is 0x118 bytes");
static_assert(offsetof(RHighLevelLightManager, lightCount) == 0x80 &&
              offsetof(RHighLevelLightManager, explosionStartTicks) == 0xd4 &&
              offsetof(RHighLevelLightManager, missileCount) == 0xe4 &&
              offsetof(RHighLevelLightManager, missileScales) == 0x110, "RHighLevelLightManager layout");

// FUN_0007e520: an effect light's brightness, blinking (the original takes the effect in ESI: FUN_0007e520 is the
// adaptor, LightEffectBrightness the C++ under it). Unrounded, as the original leaves it on the x87 stack.
void FUN_0007e520();
double LightEffectBrightness(const ArticleEffect *effect);

// ---- RLightManager

class RLightManager {
public:
    USingleton singleton;               // +0x00 the base, its vtable pointer: RLightManager's (0x00191628)
    uint8_t unknown04[0xc];
    Coord4 specularLight;               // +0x10 light 0's direction when AddSpecularLight last ran (w not written)
    MATRIX4 specularMatrix;             // +0x20 the transpose of a frame looking along light 0, from the eye
    int32_t unknown60;                  // +0x60 cleared by the constructor and FinishLights
    int32_t pendingLightCount;          // +0x64 positional lights gathered this frame
    int32_t positionalLightCount;       // +0x68 ... and last frame (FinishLights)
    bool forcePositionalLighting;       // +0x6c cleared by the constructor
    bool positionalLightingDisabled;    // +0x6d DisablePositionalLighting's
    uint8_t unknown6E[2];
    int32_t lightingModel;              // +0x70 LightingModel
    LightBlock *currentLightInfo;       // +0x74 the set the model selects
    LightBlock *lightBlock;             // +0x78 an EAGL::LightBlock: the set in use, as the render methods read it
    LightBlock lightInfos[kEnvironmentCount];   // +0x7c by LightEnvironment
    RLightAngles angles[kEnvironmentCount];     // +0x23c
    RPositionalLights *pendingLights;   // +0x29c gathered this frame
    RPositionalLights *positionalLights;    // +0x2a0 last frame's, which the render methods read
    Coord4 currentAmbient;              // +0x2a4 the sky light's colour as (a, r, g, b)
    Coord4 currentDiffuse;              // +0x2b4 light 1's colour as (?, r, g, b): see SetCurrentAmbientAndDiffuse
    bool verticalFalloff;               // +0x2c4 "Vertical Falloff"
    bool lightMapsEnabled;              // +0x2c5 "Enable light maps"
    uint8_t unknown2C6[2];
    float falloffCutin;                 // +0x2c8 "Falloff Cutin"
    float sunHeight;                    // +0x2cc "Sun height"
    int32_t moonSize;                   // +0x2d0 "Moon size"
    float lightMapLightingBias;         // +0x2d4 "Light map lighting bias": scene objects' brightness off the maps
    RHighLevelLightManager highLevel;   // +0x2d8

    RLightManager* Construct();                                                                 // 0x0007f010
    void Destruct();                                                                            // 0x0007e8c0
    RLightManager* Delete(unsigned flags);  // the scalar deleting destructor, vtable slot 0      // 0x0007f450
    // Vtable slot 2, the singleton's kill: deletes the instance
    static void Kill();                                                                         // 0x0007e930

    float VerticalFalloffMultiplier(const Coord3 *position);    // always 1                      // 0x0007e430
    void AddPositionalLight(const Coord4 *position, const Coord4 *colour);                      // 0x0007e440
    void DisablePositionalLighting(bool disable);                                               // 0x0007e860
    void AddSpecularLight(const Coord4 *eye);                                                   // 0x0007e950
    void SetCurrentAmbientAndDiffuse();                                                         // 0x0007eaa0
    // The light block's sky and ground colours scaled by ambient, light 1's by diffuse; the third is not used
    void SetSurfaceProperties(float ambient, float diffuse, float unused);                      // 0x0007eb30
    void RefreshLightBlocks();          // every set's directions from its angles               // 0x0007efb0
    void SetLightingModel(int model);                                                           // 0x0007f470
    void FinishLights();                // the frame's positional lights become last frame's     // 0x0007f540
};
static_assert(sizeof(RLightManager) == 0x3f0, "RLightManager is 0x3f0 bytes");
static_assert(offsetof(RLightManager, specularMatrix) == 0x20 && offsetof(RLightManager, lightBlock) == 0x78 &&
              offsetof(RLightManager, lightInfos) == 0x7c && offsetof(RLightManager, angles) == 0x23c &&
              offsetof(RLightManager, pendingLights) == 0x29c && offsetof(RLightManager, currentAmbient) == 0x2a4 &&
              offsetof(RLightManager, verticalFalloff) == 0x2c4 && offsetof(RLightManager, falloffCutin) == 0x2c8 &&
              offsetof(RLightManager, lightMapLightingBias) == 0x2d4 && offsetof(RLightManager, highLevel) == 0x2d8,
              "RLightManager layout");

// FUN_0007eed0: a light set's first three directions from its angles (the original takes the set in EAX and the
// angles in EBX: FUN_0007eed0 is the adaptor, SetLightDirections the C++ under it)
void FUN_0007eed0();
void SetLightDirections(LightBlock *info, const RLightAngles *angles);

// The instance (RLightManager::Init makes it)
#define fgLightManager (*(RLightManager **)0x001ec260)

#endif // DRIVING_RENDER_LIGHTS_H_
