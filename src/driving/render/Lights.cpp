#pragma fp_contract(off)

#include "Lights.h"

#include <math.h>
#include <bit>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../camera/Camera.h"           // RViewCamera, RCamera, AsCoord3
#include "../camera/PlayerCamera.h"
#include "../data/DebugVariables.h"
#include "../data/Tuning.h"
#include "../eagl/EaglGlobals.h"        // EaglMalloc, EaglFree
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"      // MEM_copy, MEM_fill
#include "../world/World.h"             // ArticleEffect
#include "Colorize.h"
#include "Renderer.h"
#include "RenderHigh.h"

// ---------------------------------------------------------------------------------------------------------------
// RLightManager and RHighLevelLightManager (0x0007e430..0x0007f5a0), ported from the listing. The x87 code keeps
// the original's order and roundings.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's globals

#define GameTick U32_AT(0x001f2a4c)
#define Launch (*(LaunchPage *)0x00243b90)
#define SavedPositionalLights (*(RPositionalLights *)0x001ec268)    // DisablePositionalLighting's

// The tuning file's types and the indices dbindex reads them through
#define ExplosionTypes ((RLightType *)0x001c4080)               // by explosion size, five
#define MissileTypes ((RLightType *)0x001c40f8)
#define EnvironmentIndex (*(uint32_t *)0x001ec2f8)
#define ExplosionTypeIndex (*(uint32_t *)0x001ec2f0)
#define MissileTypeIndex (*(uint32_t *)0x001ec2e8)
// The flag bytes dbattrib_floatrgb is handed (and does not use)
#define SkyColourFlag (*(uint8_t *)0x001ec2f7)
#define GroundColourFlag (*(uint8_t *)0x001ec2f6)
#define Light1ColourFlag (*(uint8_t *)0x001ec2f5)
#define Light2ColourFlag (*(uint8_t *)0x001ec2f4)
#define TypeColourFlag (*(uint8_t *)0x001ec2ec)
// The menu's names for the indices' values and for a bool
#define EnvironmentNames ((const char *const *)0x001c4048)      // "World", "Character", "Unused1"
#define ExplosionTypeNames ((const char *const *)0x001c4058)    // "Tiny", "Small", "Medium", "Large", "Huge"
#define MissileTypeNames ((const char *const *)0x001c406c)      // "Missile", "Rocket", "SnowmobileRocket", ...
#define OffOnNames ((const char *const *)0x001b6638)            // "Off", "On"

namespace {

const USingletonVtable *const kLightManagerVtable = reinterpret_cast<const USingletonVtable *>(0x00191628);

constexpr float kExplosionFadeOffset = 0.02f;
constexpr float kMaxExplosionScale = 16.0f;
constexpr float kColourByteScale = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kExplosionFadeOffset) == 0x3ca3d70a, "0.02");
static_assert(std::bit_cast<uint32_t>(kColourByteScale) == 0x3b808081, "1/255");

// A light's colour as AddPositionalLight takes it (it reads three floats; the fourth is the priority)
const Coord4 *ColourAndPriority(const RHighLevelLightManager::PositionalLight *light) {
    return reinterpret_cast<const Coord4 *>(&light->colour);
}

typedef RLightManager *(RLightManager::*DeleteMethod)(unsigned flags);

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The blink and the light directions (register-argument helpers)

// AUTOLTCG
__declspec(naked) void FUN_0007e520() {
    __asm {
        push esi
        call LightEffectBrightness
        add esp, 4
        ret
    }
}

// The blink is a phase in [0, 1) that advances at blinkRate per tick from blinkStartTick. The light is dark once
// the phase passes blinkDuty; before that its brightness is blinkBase + blinkAmplitude * shape(phase), where the
// shape is the phase itself (a sawtooth), a triangle wave, or a pulse that rises, dips at its peak and falls.
double LightEffectBrightness(const ArticleEffect *effect) {
    uint32_t elapsed = GameTick - effect->light.blinkStartTick;
    double cycles = double(elapsed) * effect->light.blinkRate;
    // The rounded cycles less the floor of the unrounded ones, as the original has them
    double phase = double(float(cycles)) - floor(cycles);
    if (!(phase <= effect->light.blinkDuty))
        return 0.0;
    double shape = phase;
    if (effect->flags & ArticleEffect::kBlinkShaped) {
        if (effect->flags & ArticleEffect::kBlinkPulse) {
            if (phase <= 0.4f)
                shape = phase * 2.5f;
            else if (phase >= 0.6f)
                shape = 1.0f - (phase - 0.6f) * 2.5f;
            else if (phase >= 0.5f)
                shape = (phase - 0.1f) + (phase - 0.1f);
            else
                shape = 2.0f - ((phase + 0.1f) + (phase + 0.1f));
        } else {
            shape = phase + phase;
            if (shape > 1.0f)
                shape = 2.0f - shape;
        }
    }
    return shape * effect->light.blinkAmplitude + effect->light.blinkBase;
}

// AUTOLTCG
__declspec(naked) void FUN_0007eed0() {
    __asm {
        push ebx
        push eax
        call SetLightDirections
        add esp, 8
        ret
    }
}

void SetLightDirections(LightBlock *info, const RLightAngles *angles) {
    const Coord4 forward = { 0.0f, 0.0f, 1.0f, 0.0f };
    for (int light = 0; light < 3; light++) {
        alignas(16) MATRIX4 rotation;
        alignas(16) Coord4 direction = forward;
        BuildRotate(&rotation, angles->x[light], 1.0f, 0.0f, 0.0f);
        TransformPoint(&rotation, &direction, &direction);
        BuildRotate(&rotation, angles->y[light], 0.0f, 1.0f, 0.0f);
        TransformPoint(&rotation, &direction, &direction);
        info->directions[0][light] = direction.x;
        info->directions[1][light] = direction.y;
        info->directions[2][light] = direction.z;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RHighLevelLightManager

// FUNC_AT(0x0007e620)
void RHighLevelLightManager::AddExplosion(const Coord4 *position, float size) {
    int type;
    if (size > 800.0f)
        type = 4;
    else if (size > 400.0f)
        type = 3;
    else if (size > 200.0f)
        type = 2;
    else if (size > 100.0f)
        type = 1;
    else
        type = 0;

    // The first free slot, else the oldest explosion's
    int slot = 0;
    uint32_t oldest = 0xffffffff;
    for (int i = 0; i < 4 && oldest != 0; i++) {
        if (explosionStartTicks[i] < oldest) {
            slot = i;
            oldest = explosionStartTicks[i];
        }
    }
    if (oldest == 0xffffffff)
        return;
    explosionPositions[slot] = *position;
    explosionTypes[slot] = type;
    explosionStartTicks[slot] = GameTick;
}

// FUNC_AT(0x0007e6f0)
void RHighLevelLightManager::AddMissile(const Coord4 *position, int type, float scale) {
    int slot;
    if (missileCount < 2) {
        slot = missileCount++;
    } else {
        // A full table gives up the slot with the greater scale, if any is above zero
        float greatest = 0.0f;
        slot = 2;
        if (greatest < missileScales[0]) {
            slot = 0;
            greatest = missileScales[0];
        }
        if (greatest < missileScales[1])
            slot = 1;
    }
    if (slot >= 2)
        return;
    missilePositions[slot] = *position;
    missileScales[slot] = scale;
    missileTypes[slot] = type;
}

// FUNC_AT(0x0007e790)
void RHighLevelLightManager::AddPositionalLight(const Coord4 *position, const Coord3 *colour, int priority) {
    if (lightCount < kMaxPositionalLights) {
        PositionalLight &light = lights[lightCount];
        light.position = *position;
        light.colour = *colour;
        light.priority = priority;
        lightCount++;
        return;
    }
    for (int i = 0; i < kMaxPositionalLights; i++) {
        if (priority > lights[i].priority) {
            lights[i].position = *position;
            lights[i].colour = *colour;
            lights[i].priority = priority;
            return;
        }
    }
}

// FUNC_AT(0x0007ec40)
void RHighLevelLightManager::AddDynamicLightEffect(const ArticleEffect *effect, const MATRIX4 *transform) {
    double brightness = LightEffectBrightness(effect);
    float lit = float(brightness);
    if (!(brightness > 0.0))
        return;
    alignas(16) Coord4 position;
    VU0_MATRIX4_vect4mult(&effect->position, transform, &position);
    position.w = lit;
    const uint8_t *bytes = effect->light.colour;
    alignas(16) Coord4 colour = { float(bytes[3]), float(bytes[2]), float(bytes[1]), float(bytes[0]) };
    VU0_v4scale4(&colour, kColourByteScale, &colour);
    AddPositionalLight(&position, AsCoord3(&colour), effect->light.priority);
}

// FUNC_AT(0x0007ed10)
void RHighLevelLightManager::Process() {
    for (int i = 0; i < lightCount; i++)
        fgLightManager->AddPositionalLight(&lights[i].position, ColourAndPriority(&lights[i]));

    // An explosion's light fades as 1 / (elapsed / 2) - 0.02, times its type's decay
    for (int i = 0; i < 4; i++) {
        if (explosionStartTicks[i] == 0)
            continue;
        int32_t elapsed = GameTick - explosionStartTicks[i];
        if (elapsed <= 0) {
            explosionStartTicks[i] = 0;
            continue;
        }
        double fade = 1.0 / (double(elapsed) * 0.5f) - kExplosionFadeOffset;
        if (fade <= 0.0) {
            explosionStartTicks[i] = 0;
            continue;
        }
        const RLightType &type = ExplosionTypes[explosionTypes[i]];
        fade *= type.decay;
        float scale = fade < kMaxExplosionScale ? float(fade) : kMaxExplosionScale;
        alignas(16) Coord4 colour = type.colour;
        VU0_v4scale(&colour, scale, &colour);
        explosionPositions[i].w = type.size;
        fgLightManager->AddPositionalLight(&explosionPositions[i], &colour);
    }

    for (int i = 0; i < missileCount; i++) {
        const RLightType &type = MissileTypes[missileTypes[i]];
        float scale = 1.0f < missileScales[i] ? missileScales[i] : 1.0f;
        missilePositions[i].w = type.size * scale;
        fgLightManager->AddPositionalLight(&missilePositions[i], &type.colour);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// RLightManager

// FUNC_AT(0x0007e430)
float RLightManager::VerticalFalloffMultiplier(const Coord3 *position) {
    (void)position;
    return 1.0f;
}

// FUNC_AT(0x0007e440)
void RLightManager::AddPositionalLight(const Coord4 *position, const Coord4 *colour) {
    if (pendingLightCount >= kMaxPositionalLights)
        return;
    Coord4 &slot = pendingLights->colours[pendingLightCount];
    if (Colorize->mode == RColorize::kModeInfrared) {     // red dropped, blue halved
        slot.x = 0.0f;
        slot.y = colour->y;
        slot.z = colour->z * 0.5f;
    } else {
        slot.x = colour->x;
        slot.y = colour->y;
        slot.z = colour->z;
    }
    pendingLights->positions[0][pendingLightCount] = position->x;
    pendingLights->positions[1][pendingLightCount] = position->y;
    pendingLights->positions[2][pendingLightCount] = position->z;
    pendingLights->positions[3][pendingLightCount] = position->w;
    pendingLightCount++;
}

// FUNC_AT(0x0007e860)
void RLightManager::DisablePositionalLighting(bool disable) {
    positionalLightingDisabled = disable;
    if (disable) {
        MEM_copy(&SavedPositionalLights, positionalLights, sizeof(RPositionalLights));
        MEM_fill(positionalLights, 0, sizeof(RPositionalLights));
    } else {
        MEM_copy(positionalLights, &SavedPositionalLights, sizeof(RPositionalLights));
    }
}

// FUNC_AT(0x0007e8c0)
void RLightManager::Destruct() {
    singleton.vtable = kLightManagerVtable;
    OperatorDelete(pendingLights);
    OperatorDelete(positionalLights);
    EaglFree(lightBlock, sizeof(LightBlock));
    singleton.Destruct();
}

// FUNC_AT(0x0007e930)
void RLightManager::Kill() {
    RLightManager *manager = fgLightManager;
    if (manager != NULL)
        (manager->*XbeVirtual<DeleteMethod>(manager, 0))(1);
}

// The light block's light 0 seen from `from`: a frame whose z axis points from there to the light, x and y made
// square to it about the world's y, transposed into the specular matrix
// FUNC_AT(0x0007e950)
void RLightManager::AddSpecularLight(const Coord4 *from) {
    const LightBlock *block = lightBlock;
    specularLight.x = block->directions[0][kLight1];
    specularLight.y = block->directions[1][kLight1];
    specularLight.z = block->directions[2][kLight1];
    // The original copies an uninitialised stack word into w
    specularLight.w = 0.0f;

    alignas(16) Coord4 towards;
    towards.x = specularLight.x - from->x;
    towards.y = specularLight.y - from->y;
    towards.z = specularLight.z - from->z;
    towards.w = 0.0f;
    VU0_v4unitxyz(&towards, &towards);

    alignas(16) MATRIX4 frame = { {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { towards.x, towards.y, towards.z, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    VU0_v4unitxyz(MatrixRow(&frame, 2), MatrixRow(&frame, 2));
    VU0_v4unitcrossprodxyz(MatrixRow(&frame, 1), MatrixRow(&frame, 2), MatrixRow(&frame, 0));
    VU0_v4unitcrossprodxyz(MatrixRow(&frame, 2), MatrixRow(&frame, 0), MatrixRow(&frame, 1));
    VU0_MATRIX4_transpose(&specularMatrix, &frame);
}

// FUNC_AT(0x0007eaa0)
void RLightManager::SetCurrentAmbientAndDiffuse() {
    const Coord4 &sky = lightBlock->colours[kLightSky];
    const Coord4 &light1 = lightBlock->colours[kLight1];
    currentAmbient = Coord4{ sky.w, sky.x, sky.y, sky.z };
    // The diffuse's first word is the positional light count's, copied as it is
    currentDiffuse = Coord4{ std::bit_cast<float>(positionalLightCount), light1.x, light1.y, light1.z };
}

// FUNC_AT(0x0007eb30)
void RLightManager::SetSurfaceProperties(float ambient, float diffuse, float unused) {
    (void)unused;
    const LightBlock *info = currentLightInfo;
    const Coord4 &sky = info->colours[kLightSky];
    const Coord4 &ground = info->colours[kLightGround];
    const Coord4 &light1 = info->colours[kLight1];
    lightBlock->colours[kLightSky] = Coord4{ sky.x * ambient, sky.y * ambient, sky.z * ambient, 1.0f };
    lightBlock->colours[kLightGround] = Coord4{ ground.x * ambient, ground.y * ambient, ground.z * ambient,
                                                ground.w };
    lightBlock->colours[kLight1] = Coord4{ light1.x * diffuse, light1.y * diffuse, light1.z * diffuse, light1.w };
    SetCurrentAmbientAndDiffuse();
}

// FUNC_AT(0x0007efb0)
void RLightManager::RefreshLightBlocks() {
    for (int environment = 0; environment < kEnvironmentCount; environment++)
        SetLightDirections(&lightInfos[environment], &angles[environment]);
    // The world's and the characters' ground lights point straight up
    for (int environment = kEnvironmentWorld; environment <= kEnvironmentCharacter; environment++) {
        lightInfos[environment].directions[0][kLightGround] = 0.0f;
        lightInfos[environment].directions[1][kLightGround] = 1.0f;
        lightInfos[environment].directions[2][kLightGround] = 0.0f;
    }
}

// FUNC_AT(0x0007f010)
RLightManager* RLightManager::Construct() {
    singleton.vtable = kLightManagerVtable;
    unknown60 = 0;
    pendingLightCount = 0;
    positionalLightCount = 0;
    forcePositionalLighting = false;
    positionalLightingDisabled = false;
    pendingLights = static_cast<RPositionalLights *>(OperatorNew(sizeof(RPositionalLights)));
    positionalLights = static_cast<RPositionalLights *>(OperatorNew(sizeof(RPositionalLights)));
    LightBlock *block = static_cast<LightBlock *>(EaglMalloc(sizeof(LightBlock), "EAGL::LightBlock new"));
    if (block != NULL)
        *block = LightBlock();
    lightBlock = block;
    falloffCutin = 0.0f;
    verticalFalloff = false;
    sunHeight = 500.0f;
    moonSize = 43;
    lightMapsEnabled = true;
    lightMapLightingBias = 1.0f;
    MEM_fill(pendingLights, 0, sizeof(RPositionalLights));
    MEM_fill(positionalLights, 0, sizeof(RPositionalLights));
    MEM_fill(lightInfos, 0, sizeof(lightInfos));
    for (int i = 0; i < 4; i++)
        highLevel.explosionStartTicks[i] = 0;
    highLevel.missileCount = 0;
    highLevel.lightCount = 0;

    TuningDBMgr->LoadDatabase("Render:Lighting", Launch.missionName, 0, false);
    dbindex("Current Environment", &EnvironmentIndex, 0, 3, EnvironmentNames);
    const uint32_t setStride = sizeof(LightBlock);
    const uint32_t angleStride = sizeof(RLightAngles);
    LightBlock &set = lightInfos[0];
    RLightAngles &setAngles = angles[0];
    dbattrib_floatrgb("Ambient Sky", &set.colours[kLightSky].x, &SkyColourFlag, setStride);
    dbattrib_floatrgb("Ambient Ground", &set.colours[kLightGround].x, &GroundColourFlag, setStride);
    dbattrib_floatrgb("Light 1", &set.colours[kLight1].x, &Light1ColourFlag, setStride);
    dbattrib_float("Light1 Angle Y", &setAngles.y[kLight1], 0.0f, 360.0f, angleStride, 1.0f, NULL);
    dbattrib_float("Light1 Angle X", &setAngles.x[kLight1], -90.0f, 0.0f, angleStride, 1.0f, NULL);
    dbattrib_floatrgb("Light 2", &set.colours[kLight2].x, &Light2ColourFlag, setStride);
    dbattrib_float("Light2 Angle Y", &setAngles.y[kLight2], 0.0f, 360.0f, angleStride, 1.0f, NULL);
    dbattrib_float("Light2 Angle X", &setAngles.x[kLight2], -90.0f, 0.0f, angleStride, 1.0f, NULL);
    dbattrib_float("Dual Hemi Angle Y", &setAngles.y[kLightGround], 0.0f, 360.0f, angleStride, 1.0f, NULL);
    dbattrib_float("Dual Hemi Angle X", &setAngles.x[kLightGround], -90.0f, 90.0f, angleStride, 1.0f, NULL);
    dbendindex();

    dbattrib_bool("Vertical Falloff", &verticalFalloff, 0, 1, 0, -1.0f, OffOnNames);
    dbattrib_float("Falloff Cutin", &falloffCutin, -100.0f, 300.0f, 0, 1.0f, NULL);
    dbattrib_float("Sun height", &sunHeight, 0.0f, 2000.0f, 0, 1.0f, NULL);
    dbattrib_s8("Moon size", &moonSize, 0, 128, 0, 1.0f, NULL);
    dbattrib_bool("Enable light maps", &lightMapsEnabled, 0, 1, 0, -1.0f, OffOnNames);
    dbattrib_float("Light map lighting bias", &lightMapLightingBias, 0.0f, 4.0f, 0, 1.0f, NULL);

    const uint32_t typeStride = sizeof(RLightType);
    dbindex("Explosion Type", &ExplosionTypeIndex, 0, 4, ExplosionTypeNames);
    dbattrib_floatrgb("Colour", &ExplosionTypes[0].colour.x, &TypeColourFlag, typeStride);
    dbattrib_float("Size", &ExplosionTypes[0].size, 0.05f, 10.0f, typeStride, 0.2f, NULL);
    dbattrib_float("Decay", &ExplosionTypes[0].decay, 0.0f, 500.0f, typeStride, 1.0f, NULL);
    dbendindex();
    dbindex("Missile Type", &MissileTypeIndex, 0, 4, MissileTypeNames);
    dbattrib_floatrgb("Colour", &MissileTypes[0].colour.x, &TypeColourFlag, typeStride);
    dbattrib_float("Size", &MissileTypes[0].size, 0.05f, 10.0f, typeStride, 0.2f, NULL);
    dbendindex();
    TuningDBMgr->CloseCurrent();

    RefreshLightBlocks();
    return this;
}

// FUNC_AT(0x0007f450)
RLightManager* RLightManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x0007f470)
void RLightManager::SetLightingModel(int model) {
    if (lightingModel == model && currentLightInfo != NULL)
        return;
    lightingModel = model;
    switch (model) {
    case kLightingModel0:
        currentLightInfo = &lightInfos[kEnvironmentWorld];
        break;
    case kLightingModel1:
        currentLightInfo = &lightInfos[kEnvironmentWorld];
        break;
    case kLightingModel2:
        currentLightInfo = &lightInfos[kEnvironmentCharacter];
        break;
    }
    MEM_copy(lightBlock, currentLightInfo, sizeof(LightBlock));
    SetCurrentAmbientAndDiffuse();

    // The specular light from the current view's camera's z axis, else the first camera view's
    const RViewCamera *view = fgRenderer->currentView;
    if (view == NULL)
        view = fgRenderHigh->views[0].view;
    alignas(16) Coord4 axis = *MatrixRow(&view->camera->matrix, 2);
    fgLightManager->AddSpecularLight(&axis);
}

// FUNC_AT(0x0007f540)
void RLightManager::FinishLights() {
    highLevel.Process();
    highLevel.missileCount = 0;
    highLevel.lightCount = 0;
    MEM_copy(positionalLights, pendingLights, sizeof(RPositionalLights));
    MEM_fill(pendingLights, 0, sizeof(RPositionalLights));
    unknown60 = 0;
    positionalLightCount = pendingLightCount;
    pendingLightCount = 0;
}

// ---- LightBlock

// FUNC_AT(0x000148a0)
void LightBlock::GetLight(int light, Coord4 *direction, Coord4 *colour) {
    direction->x = directions[0][light];
    direction->y = directions[1][light];
    direction->z = directions[2][light];
    colour->y = colours[light].x;
    colour->z = colours[light].y;
    colour->w = colours[light].z;
    colour->x = colours[light].w;
}

// FUNC_AT(0x000148f0)
void LightBlock::SetLight(int light, const Coord4 *direction, const Coord4 *colour) {
    directions[0][light] = direction->x;
    directions[1][light] = direction->y;
    directions[2][light] = direction->z;
    colours[light] = *colour;
}
