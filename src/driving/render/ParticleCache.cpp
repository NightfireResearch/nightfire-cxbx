#pragma fp_contract(off)

#include "ParticleCache.h"

#include "Materials.h"                  // UVolatileMaterial, VolatileRequests, the primitive types
#include "Particles.h"
#include "Renderer.h"                   // fgRenderer
#include "RSceneObj.hpp"
#include "TextureContext.h"
#include "../camera/Camera.h"           // RViewCamera, RCamera
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"

#include <bit>
#include <math.h>
#include <string.h>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------------------------------------------
// The particles: their start, the cache's update and its draws. See ParticleCache.h.
//
// The cache draws into three sets of vertex arrays of the game's, each sized for a buffer's worth: camera-facing
// quads blended normally, the same blended additively (both the sprite texture, "MAIN" of texture context 1), and
// streaks, three vertices a particle (the "sprk" texture of context 0), each with its own volatile material.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define GFXGallery_GET_StickyEffectPos ((bool (__fastcall *)(void *, int, int index, Coord4 *position))0x000d3ce0)

// ---- globals

#define GameTick U32_AT(0x001f2a4c)
#define Sim ((void *)0x00233ff0)                    // the Simulation
#define Gallery (*(void **)0x0023f320)              // the GFXGallery
#define ParticleDrag FLOAT_AT(0x00230600)           // UpdateBlock's copy of the particle's drag (name ours)
#define ParticleFovScale FLOAT_AT(0x001c8ed4)       // 33 / the camera's field of view (names ours)
#define SpriteFadeSize FLOAT_AT(0x001c8ed8)         // 0.3: sprites bigger than this on screen fade out

// The vertex arrays and their materials and textures (names ours)
constexpr int kStreakVertices = 3 * kParticleBlock;
#define NormalPositions ((Coord4 *)0x00222d30)
#define NormalColours ((uint32_t *)0x0022ad40)
#define NormalUVs ((float (*)[2])0x0021ed30)
#define AdditivePositions ((Coord4 *)0x00216d30)
#define AdditiveColours ((uint32_t *)0x0022cd40)
#define AdditiveUVs ((float (*)[2])0x00212d30)
#define StreakPositions ((Coord4 *)0x0020cd30)
#define StreakColours ((uint32_t *)0x0022ed40)
#define StreakUVs ((float (*)[2])0x00209d30)
#define SpriteTexture (*(EAGL::TAR **)0x0022ad30)
#define StreakTexture (*(EAGL::TAR **)0x0022ad34)
#define SpriteMaterial ((UVolatileMaterial *)0x00230540)
#define StreakMaterial ((UVolatileMaterial *)0x002305a0)
#define SpriteBlendState ((EAGL::GeoPrimStateExtension *)0x00230540)   // the sprite material's GeoPrimState

constexpr uint32_t kTextureMain = 0x4e49414d;       // "MAIN" in memory
constexpr uint32_t kTextureSpark = 0x6b727073;      // "sprk"
constexpr int kNoBody = 0xff;
constexpr int kShadingGouraud = 1;
constexpr int kAlphaCompare = 4;
constexpr int kTransparent = 1;
constexpr uint32_t kGLSrcAlpha = 0x302;             // SetAlphaBlend's arguments, in OpenGL's numbering
constexpr uint32_t kGLOne = 1;
constexpr uint32_t kGLFuncAdd = 0x8006;
constexpr uint32_t kMaxTicks = 3;
constexpr float kFovScale = 33.0f;

constexpr float kRandom15 = 1.0f / 32768.0f;        // a 16-bit draw times this: 0 to 2
constexpr float kRandom16 = 1.0f / 65536.0f;        // ... 0 to 1
constexpr float kPerSecond = 0.016666668f;
constexpr float kGravityScale = -1.0f / 4096.0f;
constexpr float kSpinScale = 40.7f;
constexpr float kTwoPi = 6.2831855f;
constexpr double kHalfTurnBack = -0.5;
constexpr float kAlphaScale = 58.823532f;
constexpr float kSizeScale = 1.5f;
constexpr float kNibbleAlpha = 0.0625f;
constexpr float kNibbleSize = 0.6666667f;
constexpr float kSpinToRadians = 0.024570024f;
constexpr float kNegativeDegreesPerRadian = -57.295784f;
constexpr float kPull = -0.875f;                    // sprites are pulled towards the camera by this times their size
constexpr float kFadeRange = 0.05f;
constexpr float kFadeScale = 20.0f;
static_assert(std::bit_cast<uint32_t>(kPerSecond) == 0x3c888889 && std::bit_cast<uint32_t>(kSpinScale) == 0x4222cccd &&
              std::bit_cast<uint32_t>(kTwoPi) == 0x40c90fdb && std::bit_cast<uint32_t>(kAlphaScale) == 0x426b4b4c &&
              std::bit_cast<uint32_t>(kNibbleSize) == 0x3f2aaaab &&
              std::bit_cast<uint32_t>(kSpinToRadians) == 0x3cc94713 &&
              std::bit_cast<uint32_t>(kNegativeDegreesPerRadian) == 0xc2652ee2 &&
              std::bit_cast<uint32_t>(kFadeRange) == 0x3d4ccccd, "the original's constants");

// A sprite's corners in the order they are emitted: the multiples of up and right that place each (the binary's
// table at 0x001c8f00), and its texture coordinates (0x001c8ee0).
static const float kCorner[4][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };
static const float kCornerUV[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

// atan_turns answers in ST0 unrounded; read as the double it is rather than through its float declaration
static double AtanTurnsUnrounded(float y, float x) {
    typedef double (*UnroundedTurnsFn)(float, float);
    return reinterpret_cast<UnroundedTurnsFn>(&atan_turns)(y, x);
}

// a + t (b - a) in four lanes: the lerp the original inlines (SSE, so each step rounds to a float)
static Coord4 Lerp(const Coord4 *a, const Coord4 *b, float t) {
    __m128 from = _mm_loadu_ps(&a->x);
    __m128 step = _mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(_mm_loadu_ps(&b->x), from));
    Coord4 out;
    _mm_storeu_ps(&out.x, _mm_add_ps(step, from));
    return out;
}

// The particles in a block: a whole one, or what the last one holds
static int BlockSize(int block, int count) {
    if (block < count >> 9)
        return kParticleBlock;
    return ((count - 1) & (kParticleBlock - 1)) + 1;
}

// ---- RParticle

// FUNC_AT(0x000aadc0)
void RParticle::ResetParticle(const Coord3 *start, const RParticleSystemData *data, const RParticleEmitter *emitter,
                              const Coord4 *axis, const Coord3 *velocityPerSecond, const Coord4 *motion,
                              float fraction, float ticks) {
    float elapsed = fraction * ticks;
    position = *start;
    position.x = float(double(RandomShort()) * data->spreadX * kRandom15 - data->spreadX + position.x);
    position.z = float(double(RandomShort()) * data->spreadZ * kRandom15 - data->spreadZ + position.z);

    float speed =
        float(double(RandomShort()) * data->speedSpread * kRandom15 + (double(data->speed) - data->speedSpread));
    Coord4 direction = { emitter->direction.x, emitter->direction.y, emitter->direction.z, 0.0f };
    VU0_v4scale(&direction, speed, &velocity);

    Coord4 scaled;
    float sideSpeed = float(double(RandomShort()) * data->sideSpeed * kRandom15 - data->sideSpeed);
    Coord4 side = { emitter->side.x, emitter->side.y, emitter->side.z, 0.0f };
    VU0_v4scale(&side, sideSpeed, &scaled);
    VU0_v3add(&velocity, &scaled, &velocity);

    float axisSpeed = float(double(RandomShort()) * data->axisSpeed * kRandom15 - data->axisSpeed);
    Coord4 across = { axis->x, axis->y, axis->z, 0.0f };
    VU0_v4scale(&across, axisSpeed, &scaled);
    VU0_v3add(&velocity, &scaled, &velocity);

    if (velocityPerSecond != NULL) {
        Coord4 inherited = { velocityPerSecond->x * kPerSecond, velocityPerSecond->y * kPerSecond,
                             velocityPerSecond->z * kPerSecond, 0.0f };
        VU0_v3add(&velocity, &inherited, &velocity);
    }

    life = uint8_t(data->life - RoundToInt(elapsed));
    if (life == 0)
        life = 1;
    gravity = int8_t(Ftol(double(RandomShort()) * data->gravitySpread * kRandom15 +
                          (double(data->gravity) - data->gravitySpread)));
    spin = int8_t(Ftol((double(RandomShort()) * data->spin * kRandom15 - data->spin) * kSpinScale));
    system = uint8_t(data->index);
    body = emitter->body;
    alphaSize = uint8_t(data->unknown2C);
    unknown1D = uint8_t(Ftol(double(data->drag) * 255.0f));
    flags = uint8_t(data->flags);
    if ((data->flags & kParticleFlag100) && alphaSize > 0)
        flags = uint8_t(flags | kParticleUsesAlphaByte);
    else
        flags = uint8_t(flags & ~kParticleUsesAlphaByte);

    // A fixed spin: the angle of the velocity as the camera sees it.
    if (data->flags & kParticleSpinFixed) {
        Coord4 moving = { velocity.x, velocity.y, velocity.z, 0.0f };
        Coord4 seen = { velocity.x, velocity.y, velocity.z, 0.0f };
        TransformPoint(fgRenderer->renderContext->GetCurrentViewPort()->GetViewMatrix(), &moving, &seen);
        double turns = AtanTurnsUnrounded(seen.x, seen.y);
        if (turns < kHalfTurnBack)
            turns += 1.0f;
        spin = int8_t(Ftol(turns * kTwoPi * kSpinScale));
    }

    // A random alpha between the first and last colours' into the high nibble ...
    if (data->alphaSpread > 0.0f) {
        flags = uint8_t(flags & ~kParticleUsesAlphaByte);
        double range = fabs(double(data->colours[0].x) - data->colours[2].x);
        double lowest = data->colours[0].x < data->colours[2].x ? data->colours[0].x : data->colours[2].x;
        double t = 1.0f - double(RandomShort()) * data->alphaSpread * kRandom16;
        alphaSize = uint8_t(Ftol((t * range + lowest) * kAlphaScale));
        alphaSize = uint8_t(alphaSize << 4);
        flags = uint8_t(flags & ~kParticleUsesAlphaByte);
    }
    // ... and a random size, at least 1, into the low one.
    if (data->unknownA0 > 0.0f) {
        flags = uint8_t(flags & ~kParticleUsesAlphaByte);
        double range = fabs(double(data->sizeChange));
        double atSpawn = double(data->size) + data->sizeChange;
        double t = 1.0f - double(RandomShort()) * data->unknownA0 * kRandom16;
        alphaSize = uint8_t(alphaSize | (Ftol((t * range + atSpawn) * kSizeScale) & 0xf));
        if (alphaSize <= 1)
            alphaSize = 1;
        flags = uint8_t(flags & ~kParticleUsesAlphaByte);
    }

    // Back to where it would be had it spawned when due.
    double back = double(fraction) - 1.0f;
    position.x = float((back * velocity.x * ticks + double(fraction) * motion->x) + position.x);
    position.y = float(((back * velocity.y * ticks + double(gravity) * kGravityScale * elapsed * elapsed) +
                        double(fraction) * motion->y) + position.y);
    position.z = float((back * velocity.z * ticks + double(fraction) * motion->z) + position.z);
}

// ---- the cache

// FUNC_AT(0x000aaa60)
RParticleParticleCache* RParticleParticleCache::Construct() {
    unknown00 = -1;
    count = 0;
    block = -1;
    buffer = 0;
    spawned = 0;
    SpriteTexture = RTextureContextManager::GetContext(1)->FindOrCreateTexture(kTextureMain, 0);
    StreakTexture = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kTextureSpark, 0);
    lastTick = 0;

    SpriteMaterial->SetTextureEnable(true);
    SpriteMaterial->SetShading(kShadingGouraud);
    SpriteMaterial->SetAlphaCompareValue(kAlphaCompare);
    SpriteMaterial->SetAlphaTestEnable(false);
    SpriteMaterial->SetTransparencyMethod(kTransparent);
    SpriteMaterial->SetAlphaBlendMode(kBlendNormal);

    StreakMaterial->SetTextureEnable(true);
    StreakMaterial->SetShading(kShadingGouraud);
    StreakMaterial->SetTransparencyMethod(kTransparent);
    StreakMaterial->SetAlphaBlendMode(kBlendAdditive);
    StreakMaterial->SetAlphaTestEnable(false);

    for (int i = 0; i < kStreakVertices; i++) {
        switch (i % 3) {
        case 0:
            StreakUVs[i][0] = 0.0f;
            StreakUVs[i][1] = 0.0f;
            break;
        case 1:
            StreakUVs[i][0] = 0.0f;
            StreakUVs[i][1] = 1.0f;
            break;
        case 2:
            StreakUVs[i][0] = 1.0f;
            StreakUVs[i][1] = 0.0f;
            break;
        }
    }
    return this;
}

// FUNC_AT(0x000aabd0)
RParticle* RParticleParticleCache::Spawn() {
    if (count + spawned >= kParticleBufferSize)
        return NULL;
    return &particles[1 - buffer][spawned++];
}

// FUNC_AT(0x000aac20)
void RParticleParticleCache::UpdateBlock(int block) {
    int size = BlockSize(block, count);
    for (int i = 0; i < size; i++) {
        RParticle *particle = &particles[buffer][this->block * kParticleBlock + i];
        float fall = float(double(particle->gravity) * kGravityScale);
        ParticleDrag = fgParticleSystems->library.FindSystemViaIndex(particle->system)->drag;
        particle->position.x = particle->position.x + particle->velocity.x;
        particle->position.y = particle->position.y + particle->velocity.y;
        particle->position.z = particle->position.z + particle->velocity.z;
        particle->velocity.x = ParticleDrag * particle->velocity.x;
        particle->velocity.y = ParticleDrag * particle->velocity.y;
        particle->life--;
        particle->velocity.z = ParticleDrag * particle->velocity.z;
        particle->velocity.y = fall + particle->velocity.y;
        if (particle->life != 0)
            particles[1 - buffer][spawned++] = *particle;
    }
}

// FUNC_AT(0x000aad60)
void RParticleParticleCache::JustUpdate() {
    if (count > 0) {
        int blocks = (uint32_t(count - 1) >> 9) + 1;
        for (int i = 0; i < blocks; i++) {
            block = i;
            UpdateBlock(i);
            block = -1;
        }
    }
    count = spawned;
    buffer = 1 - buffer;
    spawned = 0;
}

// FUNC_AT(0x000abf20)
void RParticleParticleCache::UpdateAndRender() {
    ParticleFovScale = kFovScale / fgRenderer->currentView->camera->fieldOfView;
    uint32_t ticks = GameTick - lastTick;
    if (ticks > kMaxTicks)
        ticks = kMaxTicks;
    for (uint32_t i = 0; i < ticks; i++)
        JustUpdate();

    fgRenderer->renderContext->SetZWritesEnable(0);
    if (count > 0) {
        int blocks = (uint32_t(count - 1) >> 9) + 1;
        for (int i = 0; i < blocks; i++) {
            block = i;
            RenderBlock(i);
            block = -1;
        }
    }
    fgRenderer->renderContext->SetZWritesEnable(1);
    lastTick = GameTick;
}

// The current volatile request pointed at a set of arrays and a texture, then drawn with `material`
static void DrawVertices(UVolatileMaterial *material, int primitive, int vertices, Coord4 *positions,
                         uint32_t *colours, float (*uvs)[2], EAGL::TAR *texture) {
    VolatileRequests[VolatileRequestIndex]->positions.SetData(positions);
    VolatileRequests[VolatileRequestIndex]->colours.SetData(colours);
    VolatileRequests[VolatileRequestIndex]->texCoords.SetData(uvs);
    VolatileRequests[VolatileRequestIndex]->texture.SetData(texture);
    material->Draw(primitive, vertices, NULL);
}

// FUNC_AT(0x000aba10)
void RParticleParticleCache::RenderBlock(int block) {
    if (count <= block * kParticleBlock)
        return;
    int size = BlockSize(block, count);
    int normalCount = 0;
    int additiveCount = 0;
    int streakCount = 0;
    for (int i = 0; i < size; i++) {
        RParticle *particle = &particles[buffer][this->block * kParticleBlock + i];
        const RParticleSystemData *data = fgParticleSystems->library.FindSystemViaIndex(particle->system);

        // The colour from the life left: start to middle colour in the first half, middle to end in the second
        // (the colours are stored the other way round). The doubling is of the unrounded product.
        double scaledLife = double(particle->life) * data->invLife;
        float lifeLeft = float(scaledLife);
        double half = scaledLife + scaledLife;
        int from = 0;
        if (half > 1.0f) {
            half -= 1.0f;
            from = 1;
        }
        Coord4 colour = Lerp(&data->colours[from], &data->colours[from + 1], float(half));
        if (data->flags & kParticleUsesAlphaByte)
            colour.x = float(particle->alphaSize >> 4) * kNibbleAlpha;

        if (!(data->flags & kParticleStreak)) {
            RenderParticle(block, &normalCount, &additiveCount, data, particle, colour, lifeLeft);
            continue;
        }

        // A streak: two vertices at its tail, one raised by the size, and one at the particle.
        Coord4 tail;
        VU0_v4copy(particle, &tail);
        tail.x = float(double(particle->velocity.x) * data->sizeChange + tail.x);
        tail.y = float(double(particle->velocity.y) * data->sizeChange + tail.y);
        tail.z = float(double(particle->velocity.z) * data->sizeChange + tail.z);
        VU0_v4tocolour(&colour, &StreakColours[streakCount]);
        VU0_v4copy(&tail, &StreakPositions[streakCount]);
        StreakPositions[streakCount].y = data->size + StreakPositions[streakCount].y;
        StreakPositions[streakCount].w = 1.0f;
        streakCount++;
        VU0_v4tocolour(&colour, &StreakColours[streakCount]);
        VU0_v4copy(&tail, &StreakPositions[streakCount]);
        StreakPositions[streakCount].w = 1.0f;
        streakCount++;
        VU0_v4tocolour(&colour, &StreakColours[streakCount]);
        VU0_v4copy(particle, &StreakPositions[streakCount]);
        StreakPositions[streakCount].w = 1.0f;
        streakCount++;
        if (particle->body != kNoBody) {
            const Coord3 *offset = &Simulation_GetRigidBody(Sim, 0, particle->body)->position;
            VU0_v3add(&StreakPositions[streakCount - 1], offset, &StreakPositions[streakCount - 1]);
            VU0_v3add(&StreakPositions[streakCount - 2], offset, &StreakPositions[streakCount - 2]);
            VU0_v3add(&StreakPositions[streakCount - 3], offset, &StreakPositions[streakCount - 3]);
        }
    }

    fgRenderer->renderContext->SetZWritesEnable(0);
    if (normalCount > 0) {
        SpriteMaterial->SetAlphaBlendMode(kBlendNormal);
        DrawVertices(SpriteMaterial, kQuadList, normalCount, NormalPositions, NormalColours, NormalUVs, SpriteTexture);
    }
    if (additiveCount > 0) {
        SpriteBlendState->SetAlphaBlend(kGLSrcAlpha, kGLOne, kGLFuncAdd);
        DrawVertices(SpriteMaterial, kQuadList, additiveCount, AdditivePositions, AdditiveColours, AdditiveUVs,
                     SpriteTexture);
    }
    if (streakCount > 0)
        DrawVertices(StreakMaterial, kTriangleList, streakCount, StreakPositions, StreakColours, StreakUVs,
                     StreakTexture);
    fgRenderer->renderContext->SetZWritesEnable(1);
}

// FUNC_AT(0x000ab650)
void RParticleParticleCache::RenderParticle(int block, int *normalCount, int *additiveCount,
                                            const RParticleSystemData *data, RParticle *particle, Coord4 colour,
                                            float lifeLeft) {
    (void)block;
    Coord4 centre = { particle->position.x, particle->position.y, particle->position.z, 1.0f };
    if (particle->body != kNoBody) {
        Coord4 offset;
        if (GFXGallery_GET_StickyEffectPos(Gallery, 0, particle->body, &offset))
            VU0_v4add4(&centre, &offset, &centre);
        else
            particle->body = kNoBody;
    }

    float size;
    if (data->flags & kParticleUsesSizeByte)
        size = float(double(particle->alphaSize & 0xf) * kNibbleSize);
    else
        size = float(double(lifeLeft) * data->sizeChange + data->size);
    double turn = 0.0f;
    if (particle->flags & kParticleRotates) {
        if (particle->flags & kParticleSpinFixed)
            turn = double(particle->spin) * kSpinToRadians;
        else
            turn = double(particle->spin) * lifeLeft * kSpinToRadians;
    }

    // The camera's right and up, turned about its view axis and scaled to the size
    const RCamera *camera = fgRenderer->currentView->camera;
    const float *viewAxis = camera->matrix.mtx[2];
    MATRIX4 turning;
    BuildRotate(&turning, float(turn * kNegativeDegreesPerRadian), viewAxis[0], viewAxis[1], viewAxis[2]);
    Coord4 up = {};
    Coord4 right = {};
    VU0_MATRIX4_vect3mult(camera->matrix.mtx[1], &turning, &up);
    VU0_MATRIX4_vect3mult(camera->matrix.mtx[0], &turning, &right);
    VU0_v4scale(&up, size, &up);
    VU0_v4scale(&right, size, &right);

    // Bigger than SpriteFadeSize on screen it fades, gone past it by kFadeRange; the first test is of the
    // unrounded size.
    float pull = size * kPull;
    Coord4 toCentre;
    VU0_v4sub(&centre, camera->matrix.mtx[3], &toCentre);
    float inverseDistance = VU0_rsqrt(VU0_v3lengthsquare(&toCentre));
    double screenSize = double(ParticleFovScale) * inverseDistance * size;
    float onScreen = float(screenSize);
    if (screenSize > SpriteFadeSize) {
        if (double(SpriteFadeSize) + kFadeRange < onScreen)
            return;
        colour.x = float((kFadeRange - (double(onScreen) - SpriteFadeSize)) * colour.x * kFadeScale);
    }
    VU0_v4scale(&toCentre, inverseDistance, &toCentre);
    VU0_v4scaleadd(&toCentre, pull, &centre, &centre);
    uint32_t argb;
    VU0_v4tocolour(&colour, &argb);

    bool additive = (data->flags & kParticleAdditive) != 0;
    int *vertexCount = additive ? additiveCount : normalCount;
    Coord4 *positions = additive ? AdditivePositions : NormalPositions;
    uint32_t *colours = additive ? AdditiveColours : NormalColours;
    float (*uvs)[2] = additive ? AdditiveUVs : NormalUVs;
    for (int corner = 0; corner < 4; corner++) {
        VU0_v4scaleadd(&up, kCorner[corner][0], &centre, &positions[*vertexCount]);
        VU0_v4scaleadd(&right, kCorner[corner][1], &positions[*vertexCount], &positions[*vertexCount]);
        positions[*vertexCount].w = 1.0f;
        colours[*vertexCount] = argb;
        uvs[*vertexCount][0] = float(double(kCornerUV[corner][0]) * data->uvSize + data->u);
        uvs[*vertexCount][1] = float(double(kCornerUV[corner][1]) * data->uvSize + data->v);
        (*vertexCount)++;
    }
}
