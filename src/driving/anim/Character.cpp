#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Character.h"

#include "Actor.h"
#include "Manager.h"
#include "Model.h"
#include "Weapon.h"
#include "../eagl/Model.h"
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"               // EAGL::Device
#include "../eagl/anim/FnAnim.h"        // AnimVCall
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <algorithm>
#include <bit>
#include <math.h>
#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// ActCharacter, ActCharacterInfo and the helpers between them (0x000143e0-0x000156c0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

// The textures' and the models' stop-using calls: one empty function (Ghidra: dummyNullFunction)
#define DummyNullFunction ((void (__fastcall *)(void *, int, const void *))0x00017550)
#define RSceneObj_SetTransform ((void (__fastcall *)(RSceneObj *, int, const MATRIX4 *))0x0008dcb0)
#define GetArticleViewDistance ((double (__cdecl *)(const MATRIX4 *, float))0x0007dc00)
#define RLightManager_SetLightingModel ((void (__fastcall *)(LightingView *, int, int))0x0007f470)
#define RLightManager_DisablePositionalLighting ((void (__fastcall *)(LightingView *, int, bool))0x0007e860)

// ---- the C runtime's (strtok keeps its place between calls; tolower as the game has it)

#define Crt_strtok ((char *(__cdecl *)(char *, const char *))0x0013282a)
#define Crt_tolower ((int (__cdecl *)(int))0x001327f9)

// ---- globals

#define PlayerPhysicsObject (*(PhysicsObject **)PTR_AT(0x00234e40))

// ---- constants

namespace {

constexpr float kInverse255 = 1.0f / 255.0f;
constexpr float kPerFrame = 1.0f / 60.0f;              // SetWeaponVelocity's
constexpr float kHalfInverse255 = 1.0f / 510.0f;       // the car's brightness, halved
constexpr float kScaleFactor = 0.1f;
constexpr float kInverseScaleFactor = 10.0f;
constexpr float kHundredth = 0.01f;
constexpr float kShadowColour = 0.05f;
constexpr float kShadowDepth = 1000000.0f;

// Draw's: the view distance squared beyond which nothing is drawn, the two detail levels' distances, the alpha
// below which the model is not drawn and above which its shadow is, and the shadow's projection
constexpr float kDrawDistance2 = 0.12f;
constexpr float kLod1Distance2 = 0.0011f;
constexpr float kLod2Distance2 = 0.003f;
constexpr float kDrawAlpha = 0.03f;
constexpr float kShadowAlpha = 0.95f;
constexpr float kShadowLightLift = 5.0f;               // added to the light's y before it is made a unit vector
constexpr float kShadowMinLightY = 0.1f;
constexpr float kShadowHeight = 0.02f;
constexpr int kFarLod = 2;                             // also Draw's answer when nothing is drawn
constexpr int kShadowLight = 0;
constexpr int kMuzzleFlashLight = 1;

static_assert(std::bit_cast<uint32_t>(kInverse255) == 0x3b808081 &&
              std::bit_cast<uint32_t>(kPerFrame) == 0x3c888889 &&
              std::bit_cast<uint32_t>(kHalfInverse255) == 0x3b008081 &&
              std::bit_cast<uint32_t>(kScaleFactor) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(kHundredth) == 0x3c23d70a &&
              std::bit_cast<uint32_t>(kShadowColour) == 0x3d4ccccd &&
              std::bit_cast<uint32_t>(kShadowDepth) == 0x49742400 &&
              std::bit_cast<uint32_t>(kDrawDistance2) == 0x3df5c28f &&
              std::bit_cast<uint32_t>(kLod1Distance2) == 0x3a902de0 &&
              std::bit_cast<uint32_t>(kLod2Distance2) == 0x3b449ba6 &&
              std::bit_cast<uint32_t>(kDrawAlpha) == 0x3cf5c28f &&
              std::bit_cast<uint32_t>(kShadowAlpha) == 0x3f733333 &&
              std::bit_cast<uint32_t>(kShadowHeight) == 0x3ca3d70a, "the original's constants");

constexpr char kSeparator[] = ".";
constexpr char kInfraredTexture[] = "hhhh";
constexpr char kNoWeapon[] = "NO WEAPON!";
constexpr char kModelDirectory[] = "data\\actors\\models\\";
constexpr char kTextureDirectory[] = "data\\actors\\textures\\";
constexpr char kLodPlaceholder[] = "y.xxx";            // five characters, rewritten by the file name getters
constexpr unsigned kPathLength = 100;
constexpr uint32_t kWeaponShown = 0x1;
constexpr int kWeaponSlotDraw = 19;                    // ActWeapon's vtable

// Xbox D3D's stencil operation and comparison values
constexpr uint32_t kStencilKeep = 0x1e00;
constexpr uint32_t kStencilReplace = 0x1e01;
constexpr uint32_t kCompareNotEqual = 0x205;

// RSceneObj::SetBrightness, inline in InheritWeaponLightingFromCar (where the level is not rounded to a float)
uint8_t BrightnessByte(double level) {
    if (level > 1.0)
        level = 1.0;
    else if (level < 0.0)
        level = 0.0;
    return uint8_t(Ftol(level * 255.0));
}

}  // namespace

// ---- helpers

// FUNC_AT(0x000143e0)
void RSceneObjBrightness::SetBrightness(float level) {
    brightness = BrightnessByte(level);
}

// FUNC_AT(0x00014440)
Coord4* MakeCoord4(Coord4 *result, float x, float y, float z, float w) {
    result->x = x;
    result->y = y;
    result->z = z;
    result->w = w;
    return result;
}

// FUNC_AT(0x00014880)
const float* MinFloat(const float *a, const float *b) {
    return *b < *a ? b : a;
}

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

// ---- CharacterDrawOptions

CharacterDrawOptions* CharacterDrawOptions::Construct() {
    alpha = 255;
    shadowColour[0] = kShadowColour;
    shadowColour[1] = kShadowColour;
    shadowColour[2] = kShadowColour;
    shadowColour[3] = 1.0f;
    return this;
}

// FUNC_AT(0x00014940)
void CharacterDrawOptions::Set(const CharacterDrawOptions *from, int drawingShadow) {
    alpha = RoundToInt(float(from->alpha * double(kInverse255) * 255.0));
    shadowColour[0] = from->shadowColour[0];
    shadowColour[1] = from->shadowColour[0];
    shadowColour[2] = from->shadowColour[0];
    shadowColour[3] = 1.0f;
    for (int i = 0; i < 3; i++)
        shadowTriangle[i] = from->shadowTriangle[i];
    shadow = drawingShadow;
    alphaScale = float(alpha * double(kInverse255));
    unknown24 = shadowColour[0];
    unknown28 = shadow ? kShadowDepth : 0.0f;
    unknown2c = unknown28;
}

// ---- ActCharacter

// FUNC_AT(0x000150a0)
ActCharacter* ActCharacter::Construct(ActModelDatabase *modelDatabase, ActTextureDatabase *textureDatabase,
                                      ActWeaponDatabase *weapons, bool shadow, const char *description,
                                      const char *weapon1, const char *weapon2) {
    info = NULL;
    hasShadow = shadow;
    void *memory = OperatorNew(sizeof(CharacterDrawOptions));
    options = memory != NULL ? static_cast<CharacterDrawOptions *>(memory)->Construct() : NULL;
    models = modelDatabase;
    textures = textureDatabase;
    weaponDatabase = weapons;
    ChangeCharacter(description, weapon1, weapon2, 0);
    if (info->number == 5 || info->number == 6 || info->number == 7 || info->number == 8)
        ChangeCharacter(description, weapon1, weapon2, 2);
    return this;
}

// FUNC_AT(0x00014700)
void ActCharacter::Destruct() {
    OperatorDelete(options);
    StopUsingResources();
    if (info != NULL) {
        info->Destruct();
        UMemory::FastFree(info, sizeof(ActCharacterInfo));
    }
}

// FUNC_AT(0x00014490)
void ActCharacter::LoadCharacter(const char *description, const char *weapon1, const char *weapon2) {
    ActWeaponDatabase *weapons = TheActManager->weapons;
    ActModelDatabase *models = TheActManager->models;
    ActTextureDatabase *textures = TheActManager->textures;
    // Zeroed here: the original leaves type as the stack had it when the model's letter is none it knows
    ActCharacterInfo info = {};
    info.Construct(description);
    models->LoadModel(&info);
    if (info.texture1 != NULL)
        textures->LoadTexture(info.texture1, info.texture1Path);
    if (info.texture2 != NULL)
        textures->LoadTexture(info.texture2, info.texture2Path);
    if (info.texture != NULL)
        textures->LoadTexture(info.texture, info.texturePath);
    if (weapon1 != NULL)
        weapons->LoadWeapon(weapon1);
    if (weapon2 != NULL)
        weapons->LoadWeapon(weapon2);
    info.Destruct();
}

// FUNC_AT(0x00014a50)
void ActCharacter::ChangeCharacter(const char *description, const char *weapon1, const char *weapon2,
                                   uint32_t weaponUse) {
    if (info != NULL) {
        StopUsingResources();
        info->Destruct();
        UMemory::FastFree(info, sizeof(ActCharacterInfo));
    }
    void *memory = UMemory::FastAlloc(sizeof(ActCharacterInfo), "ActPoser");
    info = memory != NULL ? static_cast<ActCharacterInfo *>(memory)->Construct(description) : NULL;
    model = models->UseModel(info);
    infraredSkin = NULL;
    skins[0] = NULL;
    skins[2] = NULL;
    skins[1] = NULL;
    infraredSkin = textures->UseTexture(kInfraredTexture);
    if (info->texture != NULL)
        skins[0] = textures->UseTexture(info->texture);
    if (info->texture1 != NULL)
        skins[1] = textures->UseTexture(info->texture1);
    if (info->texture2 != NULL)
        skins[2] = textures->UseTexture(info->texture2);
    const char *names[kWeapons] = { weapon1, weapon2 };
    for (int i = 0; i < kWeapons; i++) {
        if (names[i] == NULL) {
            strcpy(weaponNames[i], kNoWeapon);
            weapons[i] = NULL;
        } else {
            strcpy(weaponNames[i], names[i]);
            weapons[i] = weaponDatabase->UseWeapon(names[i], weaponUse);
        }
    }
}

// FUNC_AT(0x00014680)
void ActCharacter::StopUsingResources() {
    for (int i = 0; i < kWeapons; i++)
        if (weapons[i] != NULL)
            weaponDatabase->StopUsingWeapon(weaponNames[i], weapons[i]);
    if (info->texture1 != NULL)
        DummyNullFunction(textures, 0, info->texture1);
    if (info->texture2 != NULL)
        DummyNullFunction(textures, 0, info->texture2);
    if (info->texture != NULL)
        DummyNullFunction(textures, 0, info->texture);
    DummyNullFunction(textures, 0, kInfraredTexture);
    DummyNullFunction(models, 0, info);
}

// FUNC_AT(0x00014580)
void ActCharacter::ShowWeapon(int weapon) {
    ActWeapon *shown = weapons[weapon];
    if (shown != NULL) {
        shown->Show();
        shown->flags |= kWeaponShown;
    }
}

// FUNC_AT(0x000145b0)
void ActCharacter::HideWeapon(int weapon) {
    ActWeapon *hidden = weapons[weapon];
    if (hidden != NULL) {
        hidden->Hide();
        hidden->flags &= ~kWeaponShown;
    }
}

// FUNC_AT(0x000145e0)
void ActCharacter::SetWeaponsTransformToWorldSpace(const MATRIX4 *viewProjection, const MATRIX4 *inverse) {
    for (int i = 0; i < kWeapons; i++)
        if (weapons[i] != NULL)
            weapons[i]->SetTransformToWorldSpace(viewProjection, inverse);
}

// FUNC_AT(0x00014610)
void ActCharacter::SetWeaponVelocity(const Coord3 *velocity) {
    for (int i = 0; i < kWeapons; i++) {
        if (weapons[i] != NULL) {
            Coord3 &perFrame = weapons[i]->velocity;
            perFrame.x = velocity->x * kPerFrame;
            perFrame.y = velocity->y * kPerFrame;
            perFrame.z = velocity->z * kPerFrame;
        }
    }
}

// FUNC_AT(0x00014740)
void ActCharacter::PlayEvent(int weapon, uint32_t stimulus) {
    if (weapons[weapon] != NULL)
        weapons[weapon]->PlayEvent(stimulus);
}

// FUNC_AT(0x000147f0)
void ActCharacter::SpawnWeapon(int weapon, Coord3 *position, Coord3 *direction, Coord3 *velocity, Coord3 *spin) {
    weapons[weapon]->SpawnWeapon(position, direction, velocity, spin);
}

// FUNC_AT(0x00014820)
void ActCharacter::DrawWeapon(int weapon, EAGL::ViewPort *unused) {
    if (weapon == 0)
        weapons[0]->RenderShellCasings();
    if (weapons[weapon] != NULL)
        AnimVCall<void>(weapons[weapon], kWeaponSlotDraw);
}

// FUNC_AT(0x00014850)
void ActCharacter::SetWeaponBone(int weapon, const MATRIX4 *matrix) {
    if (weapons[weapon] != NULL)
        RSceneObj_SetTransform(weapons[weapon], 0, matrix);
}

// FUNC_AT(0x00014870)
Coord3* ActCharacter::GetShadowTriangle() {
    return options->shadowTriangle;
}

// FUNC_AT(0x00014bf0)
void ActCharacter::SetAlpha(float value) {
    options->alpha = RoundToInt(value * 255.0f);
}

// FUNC_AT(0x00014c20)
void ActCharacter::GetScaleFactors(float *scale, float *inverse) {
    *scale = info->scale * kScaleFactor;
    *inverse = info->inverseScale * kInverseScaleFactor;
}

// FUNC_AT(0x00015010)
void ActCharacter::CalculateMuzzleFlashIntensity(int weapon) {
    if (weapon == -1) {
        muzzleFlash = 0.0f;
        return;
    }
    if (weapons[weapon]->CurrentMuzzleFlashStrength() > 0.0) {
        muzzleFlash = float(weapons[weapon]->CurrentMuzzleFlashStrength() + muzzleFlash);
        muzzleFlashDirection = weapons[weapon]->muzzleDirection;
        muzzleFlash = std::min(1.0f, muzzleFlash);
    }
}

// FUNC_AT(0x00015180)
void ActCharacter::InheritWeaponLightingFromCar() {
    for (int i = 0; i < kWeapons; i++) {
        if (weapons[i] != NULL) {
            double level = PlayerPhysicsObject->renderObject->brightness * double(kHalfInverse255) + 0.5;
            weapons[i]->brightness = BrightnessByte(level);
        }
    }
}

// The render context's extension is the context itself (its first word points back at it).
// FUNC_AT(0x00014760)
void ActCharacter::StartShadow() {
    EAGL::RenderContextExtension *context =
        reinterpret_cast<EAGL::RenderContextExtension *>(EAGL::Device::Get()->GetCurrentRenderContext());
    context->SetStencilZFail(kStencilKeep);
    context->SetStencilZPass(kStencilReplace);
    context->SetStencilFail(kStencilKeep);
    context->SetStencilFunc(kCompareNotEqual);
    context->SetStencilRef(1);
    context->SetStencilMask(1);
    context->SetStencilWriteMask(1);
    context->SetStencilEnable(1);
}

// FUNC_AT(0x000147d0)
void ActCharacter::EndShadow() {
    EAGL::RenderContextExtension *context =
        reinterpret_cast<EAGL::RenderContextExtension *>(EAGL::Device::Get()->GetCurrentRenderContext());
    context->SetStencilEnable(0);
}

// The shadow is the shadow level's model flattened onto the ground along light 0's direction (lifted), drawn
// through the stencil.
// FUNC_AT(0x00014c50)
int ActCharacter::Draw(const MATRIX4 *cullMatrix, const MATRIX4 *matrix) {
    double distance = GetArticleViewDistance(cullMatrix, 0.0f);
    float distance2 = float(distance * distance);
    if (distance2 < 0.0f || distance2 > 1.0f || distance2 > kDrawDistance2)
        return kFarLod;
    int lod = 0;
    if (distance2 > kLod1Distance2)
        lod = 1;
    if (distance2 > kLod2Distance2)
        lod = kFarLod;
    int drawLod = model->count == 1 ? 0 : lod;
    if (options->alpha * double(kInverse255) > kDrawAlpha) {
        CharacterDrawOptions *drawOptions = &TheActManager->drawOptions;
        drawOptions->Set(options, 0);
        if (IRModeOn) {
            for (int i = 0; i < 3; i++)
                if (skins[i] != NULL)
                    model->SetTexture(drawLod, i, infraredSkin);
        } else {
            for (int i = 0; i < 3; i++)
                model->SetTexture(drawLod, i, skins[i]);
        }
        RLightManager_SetLightingModel(Lighting, 0, 2);
        LightBlock saved = {};
        LightBlock *lights = &Lighting->lights;
        if (muzzleFlash > 0.0f) {
            RLightManager_DisablePositionalLighting(Lighting, 0, true);
            saved = *lights;
            Coord4 colour;
            MakeCoord4(&colour, muzzleFlash, muzzleFlash, muzzleFlash, 1.0f);
            lights->SetLight(kMuzzleFlashLight, &muzzleFlashDirection, &colour);
        }
        EAGL::Model *drawn = model->models[drawLod];
        drawn->SetModelMatrix(matrix->mtx[0]);
        drawn->Draw(drawn->matrix);
        if (muzzleFlash > 0.0f) {
            RLightManager_DisablePositionalLighting(Lighting, 0, false);
            *lights = saved;
        }

        if (hasShadow && drawLod < kFarLod && options->alpha * double(kInverse255) > kShadowAlpha) {
            drawOptions->Set(options, 1);
            int shadowLod = model->count == 1 ? 0 : kFarLod;
            for (int i = 0; i < 3; i++)
                model->SetTexture(shadowLod, i, skins[i]);
            Coord4 light;
            Coord4 colour;
            lights->GetLight(kShadowLight, &light, &colour);
            MATRIX4 flatten;
            VU0_MATRIX4Init(&flatten);
            light.y = light.y + kShadowLightLift;
            VU0_v4unitxyz(&light, &light);
            flatten.mtx[1][3] = 0.0f;
            flatten.mtx[1][1] = 0.0f;
            if (fabs(light.y) > kShadowMinLightY) {
                double inverse = 1.0 / light.y;
                flatten.mtx[1][0] = float(-(light.x * inverse));
                flatten.mtx[1][2] = float(-(inverse * light.z));
            } else {
                flatten.mtx[1][2] = 0.0f;
                flatten.mtx[1][0] = 0.0f;
            }
            VU0_MATRIX4_mult(&flatten, &flatten, matrix);
            flatten.mtx[3][1] = flatten.mtx[3][1] + kShadowHeight;
            EAGL::Model *shadow = model->models[shadowLod];
            shadow->SetModelMatrix(flatten.mtx[0]);
            StartShadow();
            shadow->Draw(shadow->matrix);
            EndShadow();
        }
    }
    return lod;
}

// ---- ActCharacterInfo

// "<number>.<name>.<texture>.<scale>" for types P and A, "<number>.<name>.<texture1>.<texture2>.<scale>" for H
// and C.
// FUNC_AT(0x00015260)
ActCharacterInfo* ActCharacterInfo::Construct(const char *text) {
    strcpy(description, text);
    numberToken = Crt_strtok(description, kSeparator);
    name = Crt_strtok(NULL, kSeparator);
    switch (Crt_tolower(*name)) {
    case 'h':
        type = kCharacterH;
        break;
    case 'p':
        type = kCharacterP;
        break;
    case 'c':
        type = kCharacterC;
        break;
    case 'a':
        type = kCharacterA;
        break;
    }
    if (strlen(numberToken) == 2)
        number = (numberToken[0] - '0') * 10 + (numberToken[1] - '0');
    else
        number = numberToken[0] - '0';

    strcpy(modelPath, kModelDirectory);
    strcat(modelPath, name);
    strcat(modelPath, kLodPlaceholder);
    lodSuffix = modelPath + strlen(modelPath) - 5;

    if (type == kCharacterH || type == kCharacterC) {
        texture = NULL;
        texture1 = Crt_strtok(NULL, kSeparator);
        texture2 = Crt_strtok(NULL, kSeparator);
        texturePath = NULL;
        texture1Path = static_cast<char *>(OperatorNewArray(kPathLength));
        texture2Path = static_cast<char *>(OperatorNewArray(kPathLength));
        strcpy(texture1Path, kTextureDirectory);
        strcat(texture1Path, texture1);
        strcat(texture1Path, ".xsh");
        strcpy(texture2Path, kTextureDirectory);
        strcat(texture2Path, texture2);
        strcat(texture2Path, ".xsh");
    } else {
        texture = Crt_strtok(NULL, kSeparator);
        texture1 = NULL;
        texture2 = NULL;
        texture1Path = NULL;
        texture2Path = NULL;
        texturePath = static_cast<char *>(OperatorNewArray(kPathLength));
        strcpy(texturePath, kTextureDirectory);
        strcat(texturePath, texture);
        strcat(texturePath, ".xsh");
    }

    scaleToken = Crt_strtok(NULL, kSeparator);
    int percent = 100;
    switch (strlen(scaleToken)) {
    case 1:
        percent = scaleToken[0] - '0';
        break;
    case 2:
        percent = (scaleToken[0] - '0') * 10 + (scaleToken[1] - '0');
        break;
    case 3:
        percent = (scaleToken[0] - '0') * 100 + (scaleToken[1] - '0') * 10 + (scaleToken[2] - '0');
        break;
    }
    // The inverse is of the unrounded product
    double scaled = percent * double(kHundredth);
    scale = float(scaled);
    inverseScale = float(1.0 / scaled);
    return this;
}

// FUNC_AT(0x00015600)
void ActCharacterInfo::Destruct() {
    if (texturePath != NULL)
        OperatorDelete(texturePath);
    if (texture1Path != NULL)
        OperatorDelete(texture1Path);
    if (texture2Path != NULL)
        OperatorDelete(texture2Path);
}

// FUNC_AT(0x00015640)
char* ActCharacterInfo::GetModelFileName(int lod) {
    lodSuffix[0] = char('1' + lod);
    lodSuffix[1] = '.';
    lodSuffix[2] = 'd';
    lodSuffix[3] = 'a';
    lodSuffix[4] = 't';
    return modelPath;
}

// FUNC_AT(0x00015680)
char* ActCharacterInfo::GetModelSymbolFileName(int lod) {
    lodSuffix[0] = char('1' + lod);
    lodSuffix[1] = '.';
    lodSuffix[2] = 'r';
    lodSuffix[3] = 'e';
    lodSuffix[4] = 'l';
    return modelPath;
}
