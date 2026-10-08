#pragma fp_contract(off)

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "CameraIniLoader.h"

#include "../../common/xbeOverload.h"
#include "../data/Dafi.h"
#include "../data/IniFiles.h"
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // RigidVehicle
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"      // MEM_copy
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RCameraIniLoader. See CameraIniLoader.h.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's, whose state or number parsing must stay the game's
#define Crt_sprintf ((int (__cdecl *)(char *, const char *, ...))0x00132767)
#define Crt_strtok ((char *(__cdecl *)(char *, const char *))0x0013282a)
#define Crt_atof ((double (__cdecl *)(const char *))0x00133e84)

#define PlayerCamera (*(RPlayerCamera **)0x001ebc8c)          // RCameraIniLoader's
#define playerPhysicsObject (*(RigidVehicle ***)0x00234e40)
#define OverrideCarType (*(const char **)0x001e7a8c)          // used for a car of class 0 when set (name ours)
#define TwoPi FLOAT_AT(0x001ebc90)                            // set by static initialisers
#define TurnsPerRadian FLOAT_AT(0x001ebc94)

namespace {

const char kCameraIni[] = "data/render/camera.ini";
const char kGlobal[] = "Global";

constexpr float kPi = 3.1415925f;       // the game's
static_assert(std::bit_cast<uint32_t>(kPi) == 0x40490fda, "the game's pi");
constexpr float kMaxYawLimit = 0.5f;
constexpr float kMaxPitchLimit = 0.999f;
constexpr int kMaxEllipseHeights = 30;
constexpr int kWeaponSlots = 32;
constexpr float kDefaultShakePeriod = 52.0f;   // kExplosionShakePeriod's default, over 2 pi

// The weapon manager's slots as ResolveWeaponNames reads them
struct WeaponSlotNames {
    int32_t weapon;             // +0x00 its number: its bit in the mask
    const char *name;           // +0x04
    uint8_t unknown08[0x4c];
};
static_assert(sizeof(WeaponSlotNames) == 0x54, "a weapon slot is 84 bytes");

struct WeaponManagerSlots {
    uint8_t unknown00[0x10];
    WeaponSlotNames *slots;     // +0x10 [32]
};

#define WeaponManager (*(WeaponManagerSlots **)0x0023923c)

// PBondCar::GetSecondaryType, vtable slot 0x144 / 4 of the player's car
const char *SecondaryType(RigidVehicle *car) {
    typedef const char *(RigidVehicle::*Method)();
    return (car->*XbeVirtual<Method>(car, 0x144 / 4))();
}

bool Has(const char *text, const char *word) {
    return strstr(text, word) != NULL;
}

// A float the file gives, as the game rounds it into its tables
float Float(IniFiles *ini, const char *section, const char *key, float fallback = 0.0f) {
    return float(ini->ReadFloat(section, key, fallback));
}

// MaxVertigoDownhill and MaxVertigoUphill, held to 0-1 on the double ReadFloat answers (the Heli modes')
float VertigoLimit(IniFiles *ini, const char *section, const char *key) {
    double value = ini->ReadFloat(section, key, 0.0f);
    if (value < 0.0f)
        value = 0.0f;
    else if (value > 1.0f)
        value = 1.0f;
    return float(value);
}

// ... the Dashboard modes' way: rounded first, then clamped
float ClampedFloat(IniFiles *ini, const char *section, const char *key, float min, float max) {
    return ClampCameraValue(Float(ini, section, key), min, max);
}

// An angle the file gives in radians, in turns
float Turns(IniFiles *ini, const char *section, const char *key, float fallback) {
    return float(ini->ReadFloat(section, key, fallback) * TurnsPerRadian);
}

// The cursor speeds: times pi / 2
float CursorSpeed(IniFiles *ini, const char *section, const char *key) {
    return float(ini->ReadFloat(section, key, 1.0f) * kPi * 0.5f);
}

// "maxYaw" for the first limit, "maxYaw1" ... for the others
const char *LimitKey(char *key, const char *name, int limit) {
    if (limit > 0)
        Crt_sprintf(key, "%s%d", name, limit);
    else
        Crt_sprintf(key, "%s", name);
    return key;
}

} // namespace

// FUNC_AT(0x00078550)
RCameraIniLoader* RCameraIniLoader::Construct(RPlayerCamera *camera) {
    PlayerCamera = camera;
    return this;
}

// FUNC_AT(0x00078560)
float ClampCameraValue(float value, float min, float max) {
    if (value < min)
        value = min;
    if (value > max)
        return max;
    return value;
}

// AUTOLTCG
__declspec(naked) void FUN_00078590() {
    __asm {
        push dword ptr [esp + 4]
        push edi
        call CarListContains
        add esp, 8
        ret
    }
}

bool CarListContains(const char *list, const char *name) {
    size_t length = strlen(name);
    if (length > strlen(list))
        return false;
    const char *found = strstr(list, name);
    if (found == NULL)
        return false;
    char after = found[length];
    return after == ' ' || after == ',' || after == '\0';
}

// FUNC_AT(0x000785f0)
void RCameraIniLoader::ResolveWeaponNames(const char *names, uint32_t *mask) {
    mask[0] = 0;
    mask[1] = 0;
    if (names == NULL)
        return;
    for (int i = 0; i < kWeaponSlots; i++) {
        const WeaponSlotNames *slot = &WeaponManager->slots[i];
        if (slot->name == NULL || slot->name[0] == '\0' || strstr(names, slot->name) == NULL)
            continue;
        int weapon = slot->weapon;
        if (weapon / 32 >= 2)
            return;
        mask[weapon / 32] |= 1u << (weapon % 32 & 31);
    }
}

// FUNC_AT(0x00078670)
bool RCameraIniLoader::CheckCameraAgainstCar(char *cars) {
    if (cars == NULL)
        return true;
    for (size_t i = 0; i < strlen(cars); i++) {
        if (cars[i] >= 'A' && cars[i] <= 'Z')
            cars[i] += 'a' - 'A';
    }
    RigidVehicle *car = *playerPhysicsObject;
    if (OverrideCarType != NULL && car->GetCarClass() == 0)
        return CarListContains(cars, OverrideCarType);
    const char *type = car->GetCarType();
    const char *secondary = SecondaryType(*playerPhysicsObject);
    if (secondary != NULL && CarListContains(cars, secondary))
        return true;
    return CarListContains(cars, type);
}

// FUNC_AT(0x00078770)
void RCameraIniLoader::LoadFile() {
    CameraTables &tables = fgCameraTables;
    CameraModeIndices &indices = fgCameraModeIndices;
    if (tables.modes != NULL)
        return;

    IniFiles *ini = static_cast<IniFiles *>(OperatorNew(sizeof(IniFiles)));
    if (ini != NULL)
        ini = ini->Construct(kCameraIni, false);
    indices.tumble = -1;
    indices.worldAnimation = -1;
    indices.relativeAnimation = -1;
    indices.aiPathAnimation = -1;
    indices.collision = -1;
    indices.autoDrive = -1;
    indices.pause = -1;
    indices.missile = -1;
    indices.cinematicHeli = -1;
    indices.cinematic = -1;
    indices.defaultCamera = -1;
    tables.bumperCount = 0;
    tables.heliCount = 0;
    tables.fixedCount = 0;
    tables.splineCount = 0;
    tables.ellipseCount = 0;
    tables.dashboardCount = 0;
    tables.modeCount = 0;

    // The arms are read into these, then copied to the mode's own table (the original's are uninitialised stack:
    // the fields the file does not give are zero here).
    HeliArmInfo heliArms[kMaxCameraArms] = {};
    AutoDriveArmInfo autoDriveArms[kMaxCameraArms] = {};
    char armSection[256];
    char key[256];
    char message[256];

    // The first pass counts the modes of each type. An "AnyCar" Heli or Dashboard mode counts only while every
    // one before it was "AnyCar" too; only the first AutoDrive mode counts.
    int sectionCount = DAFI_getsectioncount(ini->dafi);
    bool autoDriveCounted = false;
    int anyCarHelis = 0;
    int anyCarDashboards = 0;
    for (int i = 0; i < sectionCount; i++) {
        const char *section = DAFI_getsectionbyindex(ini->dafi, i);
        if (Has(section, kGlobal) || Has(section, "Debug") || Has(section, ":Arm"))
            continue;
        // (the "car" value is the file's own text, lower-cased in place)
        if (!CheckCameraAgainstCar(const_cast<char *>(ini->ReadString(section, "car", NULL))))
            continue;
        if (Has(section, "Bumper")) {
            tables.bumperCount++;
            tables.modeCount++;
        } else if (Has(section, "Heli")) {
            if (Has(section, "AnyCar")) {
                if (tables.heliCount != anyCarHelis)
                    continue;
                anyCarHelis++;
            }
            tables.heliCount++;
            tables.modeCount++;
        } else if (Has(section, "Fixed")) {
            tables.modeCount++;
            tables.fixedCount++;
        } else if (Has(section, "Spline")) {
            tables.splineCount++;
            tables.modeCount++;
        } else if (Has(section, "Ellipse")) {
            tables.ellipseCount++;
            tables.modeCount++;
        } else if (Has(section, "Dashboard")) {
            if (Has(section, "AnyCar")) {
                if (tables.dashboardCount != anyCarDashboards)
                    continue;
                anyCarDashboards++;
            }
            tables.dashboardCount++;
            tables.modeCount++;
        } else if (Has(section, "Tumble") || Has(section, "WorldAnim") || Has(section, "RelativeAnim") ||
                   Has(section, "AIPathAnim") || Has(section, "Collision")) {
            tables.modeCount++;
        } else if (Has(section, "AutoDrive")) {
            if (!autoDriveCounted) {
                tables.modeCount++;
                autoDriveCounted = true;
            }
        } else {
            Crt_sprintf(message, "RCameraIniLoader::LoadFile: Unknown camera type loaded: %s - check naming "
                                 "convention\n", section);
        }
    }

    tables.bumpers = static_cast<BumperCamInfo *>(OperatorNewArray(tables.bumperCount * sizeof(BumperCamInfo)));
    tables.helis = static_cast<HeliCamInfo *>(OperatorNewArray(tables.heliCount * sizeof(HeliCamInfo)));
    tables.fixeds = static_cast<FixedCamInfo *>(OperatorNewArray(tables.fixedCount * sizeof(FixedCamInfo)));
    tables.splines = static_cast<SplineCamInfo *>(OperatorNewArray(tables.splineCount * sizeof(SplineCamInfo)));
    tables.ellipses = static_cast<EllipseCamInfo *>(OperatorNewArray(tables.ellipseCount * sizeof(EllipseCamInfo)));
    tables.dashboards =
        static_cast<DashboardCamInfo *>(OperatorNewArray(tables.dashboardCount * sizeof(DashboardCamInfo)));
    tables.modes = static_cast<CameraModeInfo *>(OperatorNewArray((tables.modeCount + 1) * sizeof(CameraModeInfo)));

    // The second pass reads them, in the same order and with the same tests.
    int camera = 0;
    int bumper = 0, heli = 0, spline = 0, ellipse = 0, fixed = 0, dashboard = 0;
    anyCarHelis = 0;
    anyCarDashboards = 0;
    for (int i = 0; i < sectionCount; i++) {
        const char *section = DAFI_getsectionbyindex(ini->dafi, i);
        if (Has(section, kGlobal) || Has(section, "Debug") || Has(section, ":Arm"))
            continue;
        if (!CheckCameraAgainstCar(const_cast<char *>(ini->ReadString(section, "car", NULL))))
            continue;

        CameraModeInfo *mode = &tables.modes[camera];
        mode->tumble = uint8_t(ini->ReadInteger(section, "tumble", 0));
        mode->shake = uint8_t(ini->ReadInteger(section, "shake", 0));
        mode->smoothTrans = int8_t(ini->ReadInteger(section, "smoothTrans", 0));
        mode->lookBack = uint8_t(ini->ReadInteger(section, "lookBack", 0));
        mode->selectable = uint8_t(ini->ReadInteger(section, "selectable", 0));
        mode->lerpRotation = uint8_t(ini->ReadInteger(section, "lerpRotation", 0));
        mode->defaultFov = Float(ini, section, "defaultFov", 33.0f);
        mode->explosionShakeScale = Float(ini, section, "explosionShakeScale", 1.0f);
        mode->interiorView = uint8_t(ini->ReadInteger(section, "interiorView", 0));
        mode->camID = int16_t(ini->ReadInteger(section, "camID", 0));
        if (indices.defaultCamera == -1 && ini->ReadInteger(section, "defaultCamera", 0) != 0)
            indices.defaultCamera = int8_t(camera);

        if (Has(section, "Heli")) {
            if (Has(section, "AnyCar")) {
                if (heli != anyCarHelis)
                    continue;
                anyCarHelis++;
            }
            mode->index = int8_t(heli);
            mode->type = kCameraHeli;
            mode->update = kUpdateMomentumHeliCam;
            HeliCamInfo *info = &tables.helis[heli++];
            info->minRate = Float(ini, section, "Min_Rate");
            info->maxRate = Float(ini, section, "Max_Rate");
            info->speedRateDiff = Float(ini, section, "Speed_Rate_Diff");
            info->heightFactor = Float(ini, section, "Height_Factor");
            info->fallbackFactor = Float(ini, section, "Fallback_Factor");
            info->maxFallback = Float(ini, section, "Max_Fallback");
            info->vertigoLerp = Float(ini, section, "Vertigo_Lerp");
            info->tumbleArmScale = Float(ini, section, "Tumble_Arm_Scale");
            info->rigidArm = uint8_t(ini->ReadInteger(section, "rigidArm", 0));
            info->checkCollisions = uint8_t(ini->ReadInteger(section, "checkCollisions", 1));
            info->maxVertigoDownhill = VertigoLimit(ini, section, "MaxVertigoDownhill");
            info->maxVertigoUphill = VertigoLimit(ini, section, "MaxVertigoUphill");
            info->noisePace = Float(ini, section, "noisePace");
            info->noiseAmount = Float(ini, section, "noiseAmount");
            info->noiseFrequency = Float(ini, section, "noiseFrequency");
            info->upRate = Float(ini, section, "upRate");
            info->lookUp = Float(ini, section, "lookUp");
            if (ini->ReadInteger(section, "Cinematic", 0) != 0) {
                indices.cinematicHeli = int8_t(heli);   // already counted past this one
                indices.cinematic = int8_t(camera);
            }
            int arm;
            for (arm = 0; arm < kMaxCameraArms; arm++) {
                Crt_sprintf(armSection, "%s:Arm%d", section, arm);
                if (!ini->FindSection(armSection))
                    break;
                HeliArmInfo *armInfo = &heliArms[arm];
                armInfo->sideways = Float(ini, armSection, "Heli_Sideways");
                armInfo->height = Float(ini, armSection, "Heli_Height");
                armInfo->distance = Float(ini, armSection, "Heli_Distance");
                armInfo->armTransition = ini->ReadInteger(armSection, "armTransition", mode->smoothTrans);
                RWorldCamera::ReadAnchorInfo(ini, armSection, &armInfo->anchor);
                ResolveWeaponNames(ini->ReadString(armSection, "weapons", NULL), armInfo->weapons);
            }
            info->armCount = arm;
            info->arms = static_cast<HeliArmInfo *>(OperatorNewArray(arm * sizeof(HeliArmInfo)));
            MEM_copy(info->arms, heliArms, arm * sizeof(HeliArmInfo));
        } else if (Has(section, "Spline")) {
            mode->index = int8_t(spline);
            mode->type = kCameraSpline;
            mode->update = kUpdateSplineCam;
            SplineCamInfo *info = &tables.splines[spline++];
            info->splineSpeed = Float(ini, section, "splineSpeed");
            info->heightOffset = Float(ini, section, "heightOffset");
            info->maxSplineCamDist = Float(ini, section, "maxSplineCamDist");
            info->expLifeTime = Float(ini, section, "expLifeTime");
            info->minZoomDist = Float(ini, section, "minZoomDist");
            info->minFov = Float(ini, section, "minFov");
            RWorldCamera::ReadAnchorInfo(ini, section, &info->anchor);
        } else if (Has(section, "Ellipse")) {
            mode->index = int8_t(ellipse);
            mode->type = kCameraEllipse;
            mode->update = kUpdateEllipseCam;
            EllipseCamInfo *info = &tables.ellipses[ellipse++];
            info->xRad = Float(ini, section, "xRad");
            info->zRad = Float(ini, section, "zRad");
            info->facets = ini->ReadInteger(section, "facets", 0);
            RWorldCamera::ReadAnchorInfo(ini, section, &info->anchor);
            info->facets -= info->facets & 3;
            // "heights": up to 30 numbers, doubled; the original's buffer is uninitialised past them, and strncpy
            // copies no further
            int8_t heights[kMaxEllipseHeights];
            int count = 0;
            char *token = Crt_strtok(const_cast<char *>(ini->ReadString(section, "heights", "2.0")), " ,");
            while (token != NULL) {
                heights[count] = int8_t(Ftol(Crt_atof(token) * 2.0));
                token = Crt_strtok(NULL, " ,");
                if (++count >= kMaxEllipseHeights)
                    break;
            }
            info->heightCount = count;
            info->heights = static_cast<int8_t *>(OperatorNewArray(count));
            strncpy(reinterpret_cast<char *>(info->heights), reinterpret_cast<const char *>(heights), count);
        } else if (Has(section, "Bumper")) {
            mode->index = int8_t(bumper);
            mode->type = kCameraBumper;
            mode->update = kUpdateBumperCam;
            BumperCamInfo *info = &tables.bumpers[bumper++];
            info->forwardArm.x = Float(ini, section, "forwardArm.x");
            info->forwardArm.y = Float(ini, section, "forwardArm.y");
            info->forwardArm.z = Float(ini, section, "forwardArm.z");
            info->forwardArm.w = Float(ini, section, "forwardArm.panUp");
            info->backwardsArm.x = Float(ini, section, "backwardsArm.x");
            info->backwardsArm.y = Float(ini, section, "backwardsArm.y");
            info->backwardsArm.z = Float(ini, section, "backwardsArm.z");
            info->backwardsArm.w = Float(ini, section, "backwardsArm.panUp");
            RWorldCamera::ReadAnchorInfo(ini, section, &info->anchor);
        } else if (Has(section, "Dashboard")) {
            if (Has(section, "AnyCar")) {
                if (dashboard != anyCarDashboards)
                    continue;
                anyCarDashboards++;
            }
            mode->index = int8_t(dashboard);
            mode->type = kCameraDashboard;
            mode->update = kUpdateDashboardCam;
            DashboardCamInfo *info = &tables.dashboards[dashboard++];
            info->forwardArm.x = Float(ini, section, "forwardArm.x");
            info->forwardArm.y = Float(ini, section, "forwardArm.y");
            info->forwardArm.z = Float(ini, section, "forwardArm.z");
            info->forwardArm.w = 0.0f;
            info->backwardsArm.x = Float(ini, section, "backwardsArm.x");
            info->backwardsArm.y = Float(ini, section, "backwardsArm.y");
            info->backwardsArm.z = Float(ini, section, "backwardsArm.z");
            info->backwardsArm.w = 0.0f;
            info->forwardPitch = Float(ini, section, "forwardPitch");
            info->forwardYaw = Float(ini, section, "forwardYaw");
            info->intertiaScale = Float(ini, section, "intertiaScale");
            info->intertiaMin = Float(ini, section, "intertiaMin");
            info->intertiaMax = Float(ini, section, "intertiaMax");
            info->steerScale = Float(ini, section, "steerScale");
            info->steerMax = Float(ini, section, "steerMax");
            info->steerPace = Float(ini, section, "steerPace");
            info->glanceScale = Float(ini, section, "glanceScale");
            info->glanceMax = Float(ini, section, "glanceMax");
            info->glancePace = Float(ini, section, "glancePace");
            info->forceScale.x = Float(ini, section, "forceScale.x");
            info->forceScale.y = Float(ini, section, "forceScale.y");
            info->forceScale.z = Float(ini, section, "forceScale.z");
            info->forceScale.w = 0.0f;
            info->forceMax.x = Float(ini, section, "forceMax.x");
            info->forceMax.y = Float(ini, section, "forceMax.y");
            info->forceMax.z = Float(ini, section, "forceMax.z");
            info->forceMax.w = 0.0f;
            info->forcePace = Float(ini, section, "forcePace");
            info->torqueScale.x = Float(ini, section, "torqueScale.x");
            info->torqueScale.y = Float(ini, section, "torqueScale.y");
            info->torqueScale.z = Float(ini, section, "torqueScale.z");
            info->torqueScale.w = 0.0f;
            info->torqueMax.x = Float(ini, section, "torqueMax.x");
            info->torqueMax.y = Float(ini, section, "torqueMax.y");
            info->torqueMax.z = Float(ini, section, "torqueMax.z");
            info->torqueMax.w = 0.0f;
            info->torquePace = Float(ini, section, "torquePace");
            info->noiseAmount = Float(ini, section, "noiseAmount");
            info->noiseFrequency = Float(ini, section, "noiseFrequency");
            mode->defaultFov = Float(ini, section, "defaultFov", 33.0f);
            info->vertigoLerp = Float(ini, section, "Vertigo_Lerp");
            info->maxVertigoDownhill = ClampedFloat(ini, section, "MaxVertigoDownhill", 0.0f, 1.0f);
            info->maxVertigoUphill = ClampedFloat(ini, section, "MaxVertigoUphill", 0.0f, 1.0f);
            RWorldCamera::ReadAnchorInfo(ini, section, &info->anchor);
        } else if (Has(section, "Fixed")) {
            mode->index = int8_t(fixed);
            mode->type = kCameraFixed;
            mode->update = kUpdateFixedCam;
            FixedCamInfo *info = &tables.fixeds[fixed++];
            info->minZoomDist = Float(ini, section, "minZoomDist");
            info->minFov = Float(ini, section, "minFov");
            info->maxZoomDist = Float(ini, section, "maxZoomDist");
            info->rotSpeed = Float(ini, section, "rotSpeed", 1.0f);
            RWorldCamera::ReadAnchorInfo(ini, section, &info->anchor);
        } else if (Has(section, "Tumble")) {
            indices.tumble = int8_t(camera);
            mode->type = kCameraTumble;
            mode->update = kUpdateTumbleCam;
            mode->index = 0;
            tables.tumbleRelPosLerp = Float(ini, section, "relPosLerp", 0.06f);
            tables.tumbleVectorLerp = Float(ini, section, "vectorLerp", 0.01f);
            RWorldCamera::ReadAnchorInfo(ini, section, &tables.tumbleAnchor);
        } else if (Has(section, "WorldAnim")) {
            indices.worldAnimation = int8_t(camera);
            mode->type = kCameraAnimation;
            mode->update = kUpdateWorldAnimationCam;
            mode->index = 0;
            RWorldCamera::ReadAnchorInfo(ini, section, &tables.animationAnchor);
        } else if (Has(section, "RelativeAnim")) {
            indices.relativeAnimation = int8_t(camera);
            mode->type = kCameraAnimation;
            mode->update = kUpdateRelativeAnimationCam;
            mode->index = 0;
            RWorldCamera::ReadAnchorInfo(ini, section, &tables.animationAnchor);
        } else if (Has(section, "AIPathAnim")) {
            indices.aiPathAnimation = int8_t(camera);
            mode->type = kCameraAIPathAnimation;
            mode->update = kUpdateAIPathAnimationCam;
            mode->index = 0;
            RWorldCamera::ReadAnchorInfo(ini, section, &tables.animationAnchor);
        } else if (Has(section, "Collision")) {
            indices.collision = int8_t(camera);
            mode->type = kCameraCollision;
            mode->update = kUpdateNothing;
            mode->index = 0;
        } else if (Has(section, "AutoDrive")) {
            if (indices.autoDrive >= 0)
                continue;
            indices.autoDrive = int8_t(camera);
            mode->type = kCameraAutoDrive;
            mode->update = kUpdateAutoDriveCam;
            mode->index = 0;
            tables.autoDriveInertia = Float(ini, section, "inertia", 0.2f);
            tables.autoDriveZoomFactor = Float(ini, section, "zoomFactor", 0.1f);
            tables.maxWeapTransTime = Float(ini, section, "MaxWeapTransTime", 30.0f);
            int arm;
            for (arm = 0; arm < kMaxCameraArms; arm++) {
                Crt_sprintf(armSection, "%s:Arm%d", section, arm);
                if (!ini->FindSection(armSection))
                    break;
                AutoDriveArmInfo *armInfo = &autoDriveArms[arm];
                armInfo->maxDeadzonePitch = Turns(ini, armSection, "maxDeadzonePitch", 0.1f);
                armInfo->maxDeadzoneYaw = Turns(ini, armSection, "maxDeadzoneYaw", 0.1f);
                armInfo->maxAutoaimPitch = Turns(ini, armSection, "maxAutoaimPitch", 0.1f);
                armInfo->maxAutoaimYaw = Turns(ini, armSection, "maxAutoaimYaw", 0.1f);
                armInfo->autoaimInterpolSpeed = Float(ini, armSection, "autoaimInterpolSpeed", 1.0f);
                armInfo->restInterpolFallScale = Float(ini, armSection, "restInterpolFallScale", 100.0f);
                armInfo->aimFov = Float(ini, armSection, "aimFov");
                armInfo->minFov = Float(ini, armSection, "minFov");
                armInfo->relPos.x = Float(ini, armSection, "relPos.x");
                armInfo->relPos.y = Float(ini, armSection, "relPos.y", 1.0f);
                armInfo->relPos.z = Float(ini, armSection, "relPos.z");
                for (int limit = 0; limit < kAutoDriveLimits; limit++) {
                    armInfo->maxYaw[limit] = ClampedFloat(ini, armSection, LimitKey(key, "maxYaw", limit),
                                                          -kMaxYawLimit, kMaxYawLimit);
                    armInfo->minYaw[limit] = ClampedFloat(ini, armSection, LimitKey(key, "minYaw", limit),
                                                          -kMaxYawLimit, kMaxYawLimit);
                    armInfo->maxPitch[limit] = ClampedFloat(ini, armSection, LimitKey(key, "maxPitch", limit),
                                                            -kMaxPitchLimit, kMaxPitchLimit);
                    armInfo->minPitch[limit] = ClampedFloat(ini, armSection, LimitKey(key, "minPitch", limit),
                                                            -kMaxPitchLimit, kMaxPitchLimit);
                }
                armInfo->allowDeadzone = ini->ReadInteger(armSection, "AllowDeadzone", 1) != 0;
                armInfo->preserveTransform = ini->ReadInteger(armSection, "PreserveTransform", 0) != 0;
                armInfo->hasRestPos = ini->ReadInteger(armSection, "HasRestPos", 0) != 0;
                armInfo->lockArmToCar = ini->ReadInteger(armSection, "LockArmToCar", 0) != 0;
                armInfo->normCursorMoveSpeed = CursorSpeed(ini, armSection, "NormCursorMoveSpeed");
                armInfo->normCursorEndMoveSpeed = CursorSpeed(ini, armSection, "NormCursorEndMoveSpeed");
                armInfo->targetCursorMoveSpeed = Float(ini, armSection, "TargetCursorMoveSpeed", 1.0f);
                armInfo->targetCursorEndMoveSpeed = Float(ini, armSection, "TargetCursorEndMoveSpeed", 1.0f);
                VU0_v4Init(&armInfo->rotation);
                VU0_v4Init(&armInfo->rotationFrom);
                VU0_v4Init(&armInfo->rotationTo);
                RWorldCamera::ReadAnchorInfo(ini, armSection, &armInfo->anchor);
                ResolveWeaponNames(ini->ReadString(armSection, "weapons", NULL), armInfo->weapons);
            }
            tables.autoDriveArmCount = arm;
            tables.autoDriveArms = static_cast<AutoDriveArmInfo *>(OperatorNewArray(arm * sizeof(AutoDriveArmInfo)));
            MEM_copy(tables.autoDriveArms, autoDriveArms, arm * sizeof(AutoDriveArmInfo));
            tables.previousAutoDriveArm = 0;
        } else {
            Crt_sprintf(message, "RCameraIniLoader::LoadFile: Unknown camera type loaded: %s - check naming "
                                 "convention\n", section);
        }

        if (Has(section, "Pause"))
            indices.pause = int8_t(camera);
        if (Has(section, "Missile"))
            indices.missile = int8_t(camera);
        camera++;
    }

    // The entry past the last is a copy of the first.
    MEM_copy(&tables.modes[tables.modeCount], tables.modes, sizeof(CameraModeInfo));

    CameraConstants &constants = fgCameraConstants;
    constants.kMaxTumble = ini->ReadInteger(kGlobal, "kMaxTumble", 100);
    constants.kMaxCollision = ini->ReadInteger(kGlobal, "kMaxCollision", 50);
    constants.kDefualtTransition = ini->ReadInteger(kGlobal, "kDefualtTransition", 60);
    constants.kTransRate = Float(ini, kGlobal, "kTransRate", 0.03f);
    constants.kTransRateLerpRate = Float(ini, kGlobal, "kTransRateLerpRate", 0.02f);
    constants.kBumperYLerpRate = Float(ini, kGlobal, "kBumperYLerpRate", 0.4f);
    constants.kCollideRadius = Float(ini, kGlobal, "kCollideRadius", 1.0f);
    constants.kSplineOffsetLerp = Float(ini, kGlobal, "kSplineOffsetLerp", 0.01f);
    constants.kExplosionScale = Float(ini, kGlobal, "kExplosionScale", 0.035f);
    constants.kExplosionMaxShake = Float(ini, kGlobal, "kExplosionMaxShake", 0.25f);
    float shakePeriod = kDefaultShakePeriod / TwoPi;
    kExplosionShakePeriod = ini->ReadInteger(kGlobal, "kExplosionShakePeriod", Truncate(shakePeriod));
    constants.kExplosionAfterShock = Float(ini, kGlobal, "kExplosionAfterShock", 0.1f);
    constants.kExplosionTimeScale = Float(ini, kGlobal, "kExplosionTimeScale", 1.0f);
    constants.kCameraObjectRadiusEx.x = Float(ini, kGlobal, "kCameraObjectRadiusEx.x", 1.675f);
    constants.kCameraObjectRadiusEx.y = Float(ini, kGlobal, "kCameraObjectRadiusEx.y", 1.925f);
    constants.kCameraObjectRadiusEx.z = Float(ini, kGlobal, "kCameraObjectRadiusEx.z", 1.325f);
    constants.kCameraObjectSphereRad = Float(ini, kGlobal, "kCameraObjectSphereRad", 2.875f);
    constants.kCollisionMinRate = Float(ini, kGlobal, "kCollisionMinRate", 0.04f);
    constants.kCollisionMaxRate = Float(ini, kGlobal, "kCollisionMaxRate", 0.13f);
    constants.kCollisionRateDiff = Float(ini, kGlobal, "kCollisionRateDiff", 0.008f);
    constants.kZoomIncSpeed = Float(ini, kGlobal, "kZoomIncSpeed", 1.5f);
    constants.kCenteringSpeed = Float(ini, kGlobal, "kCenteringSpeed", 0.5f);
    kAutoDriveLatency = ini->ReadInteger(kGlobal, "kAutoDriveLatency", 0);
    constants.kWeaponArmChangeLatency = ini->ReadInteger(kGlobal, "kWeaponArmChangeLatency", 60);
    constants.kMissileCamLatency = ini->ReadInteger(kGlobal, "kMissileCamLatency", 40);
    constants.kfWeaponAnimationLength = Float(ini, kGlobal, "kfWeaponAnimationLength", 60.0f);
    constants.kfWeaponAnimationAmplitude = Float(ini, kGlobal, "kfWeaponAnimationAmplitude", 1.0f);
    constants.kfMaxAutoaimDistance = Float(ini, kGlobal, "kfMaxAutoaimDistance", 300.0f);

    // its deleting destructor, through the vtable
    (ini->*XbeVirtual<decltype(&IniFiles::Delete)>(ini, 0))(1);
}

// FUNC_AT(0x00081880)
void RCameraIniLoader::LoadFileThunk() {
    LoadFile();
}
