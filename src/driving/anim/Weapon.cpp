#pragma fp_contract(off)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Weapon.h"

#include "AnimEngine.h"                  // Handle

#include "../../common/xbeOverload.h"
#include "../data/DebugVariables.h"
#include "../data/RCARPFile.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"     // Abs
#include "../physics/PhysicsObject.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/Renderer.h"
#include "../world/Trigger.h"             // gEventDynamicData
#include "../../helpers.h"

#include <bit>
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// ActWeapon, ActWeaponAux, ActWeaponDatabase, URefCounter<WeaponInfo>'s tree and FloatAbs. See Weapon.h.
// ---------------------------------------------------------------------------------------------------------------

// The warning beside a provisional port (code no shipped data reaches), once.
#define ANIM_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            printf("[anim] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check " \
                   "what it computes against the original.\n", what); \
            fflush(stdout); \
        } \
    } while (0)

// ---- originals called by address

#define RMuzzleFlash_Draw ((void (*)(const MATRIX4 *transform, const Coord4 *position, const Coord4 *direction, int unknown, float intensity))0x000a1870)
#define RMuzzleFlash_DrawPOV ((void (*)(const MATRIX4 *transform, const Coord4 *position, int type, float intensity))0x000a19d0)
#define RDebris_DrawShellCasings ((void (__fastcall *)(void *, int))0x000a9680)   // FUN_000a9680, named for its caller
#define Simulation_SpawnNewtonObject ((Newton *(__fastcall *)(void *, int, const Coord3 *direction, const Coord3 *position, const Coord3 *momentum, const Coord3 *spin, CARP::Instance *instances, int instanceCount, float mass, float lifetime))0x000b5e20)

// ---- globals

#define Sim ((void *)0x00233ff0)                            // the Simulation
#define SimStepCount I32_AT(0x00234e34)
#define PlayerPhysicsObject (*(PhysicsObject **)PTR_AT(0x00234e40))
#define IdentityMatrix (*(const MATRIX4 **)0x001c4654)
#define Debris PTR_AT(0x00202a80)
#define HenchmenMuzzleFlashSize FLOAT_AT(0x001b4e3c)

// The player's weapon manager (Ghidra: SWeaponManager; only the field read here)
struct WeaponManagerFields {
    uint8_t unknown00[0x18];
    int32_t unknown18;                // +0x18
};
#define WeaponManager (*(WeaponManagerFields **)0x0023923c)

// WeaponManager's unknown18 values the weapon code tests
enum WeaponManagerValue {
    kManagerValue18 = 0x18,
    kManagerValue1A = 0x1a,
    kManagerValue1B = 0x1b,
    kManagerValue1C = 0x1c,
    kManagerValue1D = 0x1d,
    kManagerValue1E = 0x1e,
};

RSceneObj_vtbl *const kActWeaponVtable = (RSceneObj_vtbl *)0x0018a440;

constexpr uint32_t kArticleTag = 0x41727469;      // 'Arti'
constexpr int kFlashTicks = 4;                    // a flash is drawn for four steps
constexpr int kMuzzleFlashTicks = 7;              // CurrentMuzzleFlashStrength's
constexpr float kOneSeventh = 1.0f / 7.0f;
constexpr float kSpawnMass = 3.14159012f;
constexpr float kSpawnLifetime = 4.0f;
constexpr double kFlashFade = 0.75;               // a flash's size, by each step it has been drawn
constexpr float kDistanceSize = 0.0025f;          // a flash's extra size, a metre from the player
constexpr float kMaxDistanceSize = 0.5f;
constexpr float kUnitHigh = 1.00000012f;          // w further from 1 than these is divided out
constexpr float kUnitLow = 0.999999881f;
static_assert(std::bit_cast<uint32_t>(kOneSeventh) == 0x3e124925 && std::bit_cast<uint32_t>(kSpawnMass) == 0x40490fd0 &&
              std::bit_cast<uint32_t>(kDistanceSize) == 0x3b23d70a && std::bit_cast<uint32_t>(kUnitHigh) == 0x3f800001 &&
              std::bit_cast<uint32_t>(kUnitLow) == 0x3f7ffffe, "the original's constants");

// The C runtime's pow (_CIpow at 0x00132a94, the game's own), which takes its operands (the base below the exponent)
// and answers on the x87 stack
__declspec(naked) static double CrtPow(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        mov eax, 0x00132a94
        call eax
        ret
    }
}

// ---- ActWeaponAux

// FUNC_AT(0x0001ab50)
void ActWeaponAux::Load(const char *name) {
    char file[36];
    sprintf(file, "%s.crp", name);
    RCARPFileLoader loader;
    loader.Construct();
    carp = loader.LoadCARPFile("data\\actors\\weapons\\", file, NULL, true);
    loader.Destruct();
}

// ---- ActWeapon

// FUNC_AT(0x0001ab20)
void ActWeapon::LoadAttributes() {
    dbattrib_float("Henchmen muzzle flash size", &HenchmenMuzzleFlashSize, 0.0f, 1.0f, 0, 1.0f, NULL);
}

// FUNC_AT(0x0001abd0)
ActWeapon* ActWeapon::Construct(ActWeaponAux *aux, uint32_t flags) {
    memcpy(&instance, IdentityMatrix, sizeof instance);
    RSceneObj::Construct(&instance);
    this->flags = flags;
    vtable = kActWeaponVtable;
    muzzleFlashEndTick = 0;
    worldTransforms[0] = NULL;
    worldTransforms[1] = NULL;
    owner = NULL;
    unknown15c = 0;
    unknown160 = 0;
    unknown164 = 0;
    velocity.z = 0.0f;
    velocity.y = 0.0f;
    velocity.x = 0.0f;
    this->aux = aux;
    UseArticle(aux->carp->root->GroupLocateTag(kArticleTag), 0);
    for (int i = 0; i < kFlashCount; i++)
        flashes[i].endTick = 0;
    this->flags |= kManualRender;
    return this;
}

// FUNC_AT(0x0001acc0)
void ActWeapon::Destruct() {
    vtable = kActWeaponVtable;
    if (worldTransforms[0] != NULL)
        OperatorDelete(worldTransforms[0]);
    if (worldTransforms[1] != NULL)
        OperatorDelete(worldTransforms[1]);
    RSceneObj::Destruct();
}

// FUNC_AT(0x0001b1b0)
ActWeapon* ActWeapon::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(ActWeapon));
    return this;
}

// FUNC_AT(0x0001ad00)
void ActWeapon::SetTransformToWorldSpace(const MATRIX4 *first, const MATRIX4 *second) {
    if (worldTransforms[0] == NULL)
        worldTransforms[0] = static_cast<MATRIX4 *>(OperatorNew(sizeof(MATRIX4)));
    if (worldTransforms[1] == NULL)
        worldTransforms[1] = static_cast<MATRIX4 *>(OperatorNew(sizeof(MATRIX4)));
    *worldTransforms[0] = *first;
    *worldTransforms[1] = *second;
}

// FUNC_AT(0x0001ad70)
void ActWeapon::SetOwner(PhysicsObject *owner) {
    this->owner = owner;
    if (owner->IsOwnedBy(Simulation_GetPlayerObject(Sim, 0)))
        flags |= kPlayerOwned;
    else
        flags &= ~kPlayerOwned;
}

// FUNC_AT(0x0001adc0)
void ActWeapon::SetEventDynamicData() {
    RSceneObj::SetEventDynamicData();
    gEventDynamicData.weapon = this;
}

// FUNC_AT(0x0001add0)
void ActWeapon::PlayEvent(int stimulus) {
    animHandle->ProcessStimuli(stimulus, SimStepCount, 1);
}

// FUNC_AT(0x0001adf0)
Newton* ActWeapon::SpawnWeapon(const Coord3 *position, const Coord3 *direction, const Coord3 *momentum,
                               const Coord3 *spin) {
    return Simulation_SpawnNewtonObject(Sim, 0, direction, position, momentum, spin, animHandle->Instances(),
                                        animHandle->instanceCount, kSpawnMass, kSpawnLifetime);
}

// FUNC_AT(0x0001ae30)
double ActWeapon::CurrentMuzzleFlashStrength() {
    int32_t remaining = muzzleFlashEndTick - SimStepCount;
    if (remaining > 0)
        return remaining * (double)kOneSeventh;
    return 0.0;
}

// FUNC_AT(0x0001ae60)
void ActWeapon::Render() {
    if (!(flags & kManualRender)) {
        typedef void (ActWeapon::*Method)();
        (this->*XbeVirtual<Method>(this, 19))();   // ManualRender
    }
}

// FUNC_AT(0x0001ae70)
void ActWeapon::ManualRender() {
    SetNowVisible();
    RRenderSharedData::SendPerViewPort();
    RSceneObj::Render();
    for (int i = 0; i < kFlashCount; i++) {
        const ActWeaponFlash &flash = flashes[i];
        if (flash.endTick <= SimStepCount)
            continue;
        Coord4 position = {flash.position.x, flash.position.y, flash.position.z, 1.0f};
        // Steps drawn so far (unsigned: a flash set further ahead fades to nothing at once)
        uint32_t drawn = SimStepCount - flash.endTick + kFlashTicks;
        float fade = (float)CrtPow(kFlashFade, (double)drawn);
        alignas(16) MATRIX4 transform;
        GetTransform(&transform);
        if (flags & kPlayerOwned) {
            float size;
            int type;   // RMuzzleFlash::DrawPOV's
            switch (WeaponManager->unknown18) {
            case kManagerValue1A:
                size = 0.18f;
                type = 2;
                break;
            case kManagerValue1B:
                size = 0.06f;
                type = 6;
                break;
            case kManagerValue1C:
                size = 0.08f;
                type = 3;
                break;
            case kManagerValue1D:
                size = 0.1f;
                type = 7;
                break;
            case kManagerValue1E:
                size = 0.2f;
                type = 7;
                break;
            default:
                size = 0.078f;
                type = 0;
                break;
            }
            RMuzzleFlash_DrawPOV(&transform, &position, type, (float)((double)size * flash.size * fade));
        } else {
            Coord4 direction = {0.0f, 0.0f, 1.0f, 0.0f};
            RMuzzleFlash_Draw(&transform, &position, &direction, 0,
                              (float)((double)HenchmenMuzzleFlashSize * flash.size * fade));
        }
    }
}
static_assert(std::bit_cast<uint32_t>(0.18f) == 0x3e3851ec && std::bit_cast<uint32_t>(0.06f) == 0x3d75c28f &&
              std::bit_cast<uint32_t>(0.08f) == 0x3da3d70a && std::bit_cast<uint32_t>(0.1f) == 0x3dcccccd &&
              std::bit_cast<uint32_t>(0.2f) == 0x3e4ccccd && std::bit_cast<uint32_t>(0.078f) == 0x3d9fbe77,
              "ManualRender's flash sizes");

// FUNC_AT(0x0001b020)
void ActWeapon::RenderShellCasings() {
    if ((flags & kPlayerOwned) && WeaponManager->unknown18 == kManagerValue18)
        RDebris_DrawShellCasings(Debris, 0);
}

// FUNC_AT(0x0001b050)
void ActWeapon::TransformToWorldSpace(MATRIX4 *matrix) {
    if (worldTransforms[0] != NULL && worldTransforms[1] != NULL) {
        VU0_MATRIX4_mult(matrix, matrix, worldTransforms[0]);
        VU0_MATRIX4_mult(matrix, matrix, worldTransforms[1]);
    }
}

// FUNC_AT(0x0001b090)
void ActWeapon::TransformMatrixToWorldSpace(MATRIX4 *matrix) {
    if (worldTransforms[1] != NULL && worldTransforms[0] != NULL) {
        VU0_MATRIX4_mult(matrix, matrix, worldTransforms[0]);
        VU0_MATRIX4_mult(matrix, matrix, worldTransforms[1]);
        VU0_v4unitxyz(matrix->mtx[0], matrix->mtx[0]);
        VU0_v4unitxyz(matrix->mtx[1], matrix->mtx[1]);
        VU0_v4unitxyz(matrix->mtx[2], matrix->mtx[2]);
        float w = fabsf(matrix->mtx[3][3]);
        if (w > kUnitHigh || w < kUnitLow)
            VU0_v4scale4(matrix->mtx[3], 1.0f / matrix->mtx[3][3], matrix->mtx[3]);
    }
}

// FUNC_AT(0x0001b130)
Coord3* ActWeapon::GetVelocity() {
    return &velocity;
}

// FUNC_AT(0x0001b1e0)
void ActWeapon::TransformPointToWorldSpace(Coord4 *point) {
    if (worldTransforms[1] != NULL && worldTransforms[0] != NULL) {
        VU0_MATRIX4_vect4mult(point, worldTransforms[0], point);
        VU0_MATRIX4_vect4mult(point, worldTransforms[1], point);
        float w = Abs(point->w);
        if (w > kUnitHigh || w < kUnitLow)
            VU0_v4scale4(point, 1.0f / point->w, point);
    }
}

// FUNC_AT(0x0001b270)
void ActWeapon::StartMuzzleFlash(const Coord3 *position) {
    int slot = 0;
    while (flashes[slot].endTick > SimStepCount) {
        if (++slot >= kFlashCount)
            return;
    }
    Coord3 player = *PlayerPhysicsObject->GetPosition();
    Coord3 at = *position;
    double extra = vec3distance(&player, &at) * (double)kDistanceSize;
    if (kMaxDistanceSize < extra)
        extra = kMaxDistanceSize;
    ActWeaponFlash &flash = flashes[slot];
    flash.position = *position;
    flash.endTick = SimStepCount + kFlashTicks;
    flash.size = (float)(RandomShort() * 0.25 * (1.0 / 65536) + extra + 0.75);
    alignas(16) MATRIX4 transform;
    GetTransform(&transform);
    muzzleDirection.x = transform.mtx[2][0];
    muzzleDirection.y = transform.mtx[2][1];
    muzzleDirection.z = transform.mtx[2][2];
    muzzleDirection.w = transform.mtx[2][3];
    muzzleFlashEndTick = SimStepCount + kMuzzleFlashTicks;
}

// ---- ActWeaponDatabase

// FUNC_AT(0x0001bf80)
ActWeaponDatabase* ActWeaponDatabase::Construct() {
    memset(weapons, 0, sizeof weapons);
    count = 0;
    references = WeaponInfoRefCounter::Get();
    return this;
}

// FUNC_AT(0x0001bcb0)
void ActWeaponDatabase::Destruct() {
    for (int i = 0; i < count; i++) {
        ActWeaponInfo *info = references->GetReference(weapons[i]->name);
        references->RemoveReference(info);
        ActWeaponAux *aux = info->aux;
        if (aux != NULL) {
            RCARPFile *carp = aux->carp;
            if (carp != NULL) {
                carp->Destruct();
                UMemory::FastFree(carp, sizeof(RCARPFile));
            }
            UMemory::FastFree(aux, sizeof(ActWeaponAux));
        }
        OperatorDelete(info);
        weapons[i] = NULL;
    }
    count = 0;
}

// FUNC_AT(0x0001bd60)
void ActWeaponDatabase::LoadWeapon(const char *name) {
    if (references->GetReference(name) != NULL)
        return;
    weapons[count] = static_cast<ActWeaponInfo *>(OperatorNew(sizeof(ActWeaponInfo)));
    strcpy(weapons[count]->name, name);
    ActWeaponAux *aux = static_cast<ActWeaponAux *>(UMemory::FastAlloc(sizeof(ActWeaponAux), "ActWeaponAux"));
    if (aux != NULL)
        aux->Load(name);
    weapons[count]->aux = aux;
    references->AddReference(name, weapons[count]);
    count++;
}

// FUNC_AT(0x0001b3d0)
ActWeapon* ActWeaponDatabase::UseWeapon(const char *name, uint32_t flags) {
    ActWeaponInfo *info = references->GetReference(name);
    ActWeapon *weapon = static_cast<ActWeapon *>(UMemory::FastAlloc(sizeof(ActWeapon), "ActWeapon"));
    if (weapon == NULL)
        return NULL;
    return weapon->Construct(info->aux, flags);
}

// FUNC_AT(0x0001b140)
void ActWeaponDatabase::StopUsingWeapon(const char *, ActWeapon *weapon) {
    if (weapon != NULL) {
        typedef ActWeapon *(ActWeapon::*Method)(unsigned);
        (weapon->*XbeVirtual<Method>(weapon, 0))(1);   // the scalar deleting destructor
    }
}

// ---- URefCounter<WeaponInfo>'s tree

// FUNC_AT(0x0001b450)
void WeaponInfoRefTree::EraseSubtree(RefCounterNode *node) {
    RefCounterTree::EraseSubtree(node);
}

// FUNC_AT(0x0001b4a0)
RefCounterNode** WeaponInfoRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x0001b810)
RefCounterNode** WeaponInfoRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where, const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x0001ba00)
RefCounterNode** WeaponInfoRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x0001baf0)
RefCounterInsertResult* WeaponInfoRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x0001be20)
void WeaponInfoRefTree::DestroyRange() {
    ANIM_UNTESTED("URefCounter<WeaponInfo>'s tree, destroyed by an exception unwind");
    RefCounterTree::DestroyRange();
}

// ---- FloatAbs

// FUNC_AT(0x0001b160)
float FloatAbs(float x) {
    return Abs(x);
}
