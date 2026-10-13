#pragma fp_contract(off)

#include "Debris.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"
#include "../eagl/RenderContext.h"
#include "../eagl/EaglGlobals.h"            // ViewMatrix
#include "../eagl/View.h"                   // EAGL::ViewPort
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"                // RoundToInt
#include "../render/Renderer.h"             // fgRenderer
#include "../render/RSceneObj.hpp"          // RandomShort
#include "../render/SkyWater.h"             // ScaleDebrisTables
#include "../render/TextureContext.h"
#include "../render/TimeData.h"             // GameTick, SimStep

// XBE_GLOBAL(0x00202a80, 0x4)
RDebris *Debris;

// ---------------------------------------------------------------------------------------------------------------
// RDebris and RDebrisType (0x000a8f10-0x000a9870), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// A kind of debris (20 bytes; the names are ours): its piece's strip
struct DebrisKind {
    const Coord4 *positions;
    const void *texCoords;
    const uint32_t *colours;            // not read by the draws
    uint32_t vertexCount;
    uint32_t texture;                   // a texture name ("shel" ...)
};
static_assert(sizeof(DebrisKind) == 20, "a debris kind is 20 bytes");

constexpr float kOneIn65536 = 1.0f / 65536.0f;
constexpr double kTwoPi = std::bit_cast<double>(0x401921fb4d12d84aull);   // 2 pi, as the original rounds it
constexpr float kSpinPhase = 3.95f;
constexpr float kSpinScale = 0.2f;
constexpr float kTurnsPerRadian = 0.1592f;
// The shell casings DrawShellCasings throws
alignas(16) constexpr Coord4 kShellVelocity = { 0.0f, 0.006f, 0.0f, 0.0f };      // in the effect's frame
constexpr uint32_t kShellColour = 0xff7f7f7f;
constexpr float kShellGravity = 0.0005f;
constexpr uint32_t kShellLifetime = 50;
static_assert(std::bit_cast<uint32_t>(kSpinPhase) == 0x407ccccd && std::bit_cast<uint32_t>(kSpinScale) == 0x3e4ccccd &&
              std::bit_cast<uint32_t>(kTurnsPerRadian) == 0x3e230553 &&
              std::bit_cast<uint32_t>(kShellVelocity.y) == 0x3bc49ba6 &&
              std::bit_cast<uint32_t>(kShellGravity) == 0x3a03126f, "the original's constants");

alignas(16) const MATRIX4 kIdentity = { {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };

}  // namespace

// The kinds' strips, which ScaleDebrisTables (render/SkyWater.cpp) scales in place
#define DebrisKinds ((const DebrisKind *)0x001c8c80)            // [RDebris::kKinds]
// The pieces' colours as drawn: every draw points the request at the first
// XBE_GLOBAL(0x00202a90, 0x200)
uint32_t DebrisColours[RDebrisType::kMaxPieces];
// EAGL's pointer to its view matrix (gpViewMatrix; eagl/EaglGlobals.h's ViewMatrix)
// XBE_GLOBAL(0x001caf50, 0x4)
const MATRIX4 *ViewMatrixPointer = reinterpret_cast<const MATRIX4 *>(ViewMatrix);

// ---- RDebrisType

// FUNC_AT(0x000a8f30)
void RDebrisType::Init(uint32_t kind) {
    this->kind = kind;
    material.SetPrimitiveType(kTriangleStrip);
    material.SetTextureEnable(true);
    material.SetShading(1);
    material.SetTransparencyMethod(1);
    material.SetAlphaBlendMode(kBlendNormal);
    texture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(DebrisKinds[this->kind].texture, 0);
}

// FUNC_AT(0x000a8fa0)
void RDebrisType::Add(const Coord4 *position, const Coord4 *velocity, uint32_t colour, uint32_t tick, float gravity,
                      uint32_t lifetime) {
    if (count >= kMaxPieces)
        return;
    this->position[count] = *position;
    this->gravity = gravity;
    this->lifetime = lifetime;
    this->velocity[count] = *velocity;
    this->velocity[count].w = (float)tick;
    Coord4 &axis = spin[count];
    axis.x = (float)(double(int32_t(RandomShort())) * 2.0 * kOneIn65536 - 1.0f);
    axis.y = (float)(double(int32_t(RandomShort())) * 2.0 * kOneIn65536 - 1.0f);
    axis.z = (float)(double(int32_t(RandomShort())) * 2.0 * kOneIn65536 - 1.0f);
    axis.w = (float)(1.0f / double(lifetime) * kTwoPi * double(int32_t(RandomShort())) * kOneIn65536);
    VU0_v4unitxyz(&axis, &axis);
    this->colour[count] = colour;
    count++;
}

// A piece is dropped once it was thrown before SimStep less the lifetime (the last one moved into its place);
// the rest are drawn where they have flown, turned about their axis, faded by how old they are.
// FUNC_AT(0x000a92a0)
void RDebrisType::Draw() {
    if (count == 0)
        return;
    float oldest = (float)(double(SimStep.step) - double(lifetime));
    for (uint32_t i = 0; i < count; i++) {
        if (oldest > velocity[i].w) {
            count--;
            position[i] = position[count];
            velocity[i] = velocity[count];
            spin[i] = spin[count];
            colour[i] = colour[count];
            i--;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        double age = double(SimStep.step) - velocity[i].w;
        Coord3 at = { position[i].x, position[i].y, position[i].z };
        at.x = (float)(age * velocity[i].x + at.x);
        double rise = age * velocity[i].y + at.y;
        at.z = (float)(age * velocity[i].z + at.z);
        at.y = (float)(rise - age * age * gravity);

        alignas(16) Coord4 turn = spin[i];
        turn.w = (float)((turn.w * age - kSpinPhase) * kSpinScale);
        float sine = sin_fractionalangle(turn.w * kTurnsPerRadian);
        float cosine = cos_fractionalangle(turn.w * kTurnsPerRadian);
        turn.x = turn.x * sine;
        turn.y = turn.y * sine;
        turn.z = turn.z * sine;
        turn.w = cosine;
        alignas(16) MATRIX4 transform;
        VU0_MATRIX4Init(&transform);
        VU0_quattom4(&transform, &turn);
        transform.mtx[3][0] = at.x;
        transform.mtx[3][1] = at.y;
        transform.mtx[3][2] = at.z;

        float alpha = (float)(double(oldest) / velocity[i].w * double(colour[i] >> 24));
        DebrisColours[i] = (colour[i] & 0xffffff) | uint32_t(RoundToInt(alpha)) << 24;
        VolatileRequests[VolatileRequestIndex]->positions.SetData(DebrisKinds[kind].positions);
        VolatileRequests[VolatileRequestIndex]->texCoords.SetData(DebrisKinds[kind].texCoords);
        VolatileRequests[VolatileRequestIndex]->texture.SetData(texture);
        VolatileRequests[VolatileRequestIndex]->colours.SetData(DebrisColours);
        material.Draw(kTriangleStrip, DebrisKinds[kind].vertexCount, &transform);
    }
}

// ---- RDebris

// FUNC_AT(0x000a91b0)
RDebris* RDebris::Construct() {
    shellCount = 0;
    for (int i = 0; i < kKinds; i++) {
        RDebrisType *type = static_cast<RDebrisType *>(UMemory::FastAlloc(sizeof(RDebrisType), "RDebrisType"));
        if (type != NULL)
            type->Construct();
        types[i] = type;
        type->Init(i);
    }
    ScaleDebrisTables();
    return this;
}

// FUNC_AT(0x000a8f10)
void RDebris::Reset() {
    shellCount = 0;
    for (int i = 0; i < kKinds; i++)
        types[i]->count = 0;
}

// FUNC_AT(0x000a9260)
void RDebris::Add(int kind, const Coord4 *position, const Coord4 *velocity, uint32_t colour, float gravity,
                  uint32_t lifetime) {
    if (lifetime > 0)
        types[kind]->Add(position, velocity, colour, GameTick, gravity, lifetime);
}

// FUNC_AT(0x000a9660)
void RDebris::Draw() {
    for (int i = 0; i < kKinds; i++) {
        if (i != kShellCasings)
            types[i]->Draw();
    }
}

// Each queued casing leaves its effect's place with kShellVelocity turned by the effect's frame, both taken into
// the view's frame; the kind is then drawn with the view's matrix the identity.
// FUNC_AT(0x000a9680)
void RDebris::DrawShellCasings() {
    alignas(16) MATRIX4 view = *ViewMatrixPointer;
    alignas(16) MATRIX4 inverse = view;
    Inverse(&inverse);
    for (uint32_t i = 0; i < shellCount; i++) {
        alignas(16) MATRIX4 frame;
        shellObject[i]->LocateFX(shellEffect[i], &frame, false);
        alignas(16) Coord4 position = { frame.mtx[3][0], frame.mtx[3][1], frame.mtx[3][2], frame.mtx[3][3] };
        shellObject[i]->TransformPointByInstancePosition(uint32_t(-1), &position, true);
        alignas(16) MATRIX4 transform;
        shellObject[i]->GetTransform(&transform);
        VU0_MATRIX4_mult(&frame, &frame, &transform);
        alignas(16) Coord4 velocity;
        VU0_MATRIX4_vect4mult(&kShellVelocity, &frame, &velocity);
        VU0_MATRIX4_vect4mult(&velocity, &view, &velocity);
        VU0_MATRIX4_vect4mult(&position, &view, &position);
        types[kShellCasings]->Add(&position, &velocity, kShellColour, shellTick[i], kShellGravity, kShellLifetime);
    }
    shellCount = 0;
    if (types[kShellCasings]->count > 0) {
        fgRenderer->renderContext->GetCurrentViewPort()->EndView();
        fgRenderer->renderContext->GetCurrentViewPort()->SetViewMatrix(kIdentity.mtx[0]);
        fgRenderer->renderContext->GetCurrentViewPort()->BeginView();
        types[kShellCasings]->Draw();
        fgRenderer->renderContext->GetCurrentViewPort()->EndView();
        fgRenderer->renderContext->GetCurrentViewPort()->SetViewMatrix(view.mtx[0]);
        fgRenderer->renderContext->GetCurrentViewPort()->BeginView();
    }
}
