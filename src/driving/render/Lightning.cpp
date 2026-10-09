#pragma fp_contract(off)

#include "Lightning.h"

#include <math.h>
#include <string.h>

#include "Materials.h"
#include "Renderer.h"                     // fgRenderer
#include "RSceneObj.hpp"
#include "TextureContext.h"
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../data/DebugVariables.h"
#include "../eagl/RenderContext.h"
#include "../engine/CoreFoundation.h"     // ThrowLengthError
#include "../engine/PhysicsUtil.h"        // Util_GaussRandom, Util_ApplyVariance, Util_PerturbVector
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../world/CollisionInstance.h"   // ColStl
#include "../world/World.h"

// ---------------------------------------------------------------------------------------------------------------
// RLightning and its points, segments and bolts (0x0009e8e0-0x000a0a50), ported from the listing.
//
// Every random number comes from the lightning's own tables, stepped by fixed strides (5 for the directions, 7 for
// the colours, 11 for the numbers) and set back at each tick from the tick itself, from the game's 16-bit
// generator (CalcRandomTables), or from the C runtime's rand through Util_GaussRandom; the draws are made in the
// original's order.
// ---------------------------------------------------------------------------------------------------------------

class RTextureContext;

// ---- the game's code not ported here
// The C runtime's array iterators: `eh vector constructor iterator` and `vector destructor iterator`
#define CRT_VectorConstructorIterator ((void (__stdcall *)(void *array, uint32_t size, int32_t count, void *constructor, void *destructor))0x0013326e)
#define CRT_VectorDestructorIterator ((void (__stdcall *)(void *array, uint32_t size, int32_t count, void *destructor))0x0013332e)

// ---- globals
#define SimTimeStep FLOAT_AT(0x00234e30)                // the simulation's step, in seconds
#define SimStepCount I32_AT(0x00234e34)
#define SimStepsPerSecond I32_AT(0x00234e2c)
#define ZeroVector ((const Coord3 *)0x00243030)         // the game's zero vector, never written

static const USingletonVtable *const kLightningVtable = (const USingletonVtable *)0x00192d20;
static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;

// The segment constructor and destructor the C runtime's iterators run
static void *const kSegmentConstructor = (void *)XbeAddress(&RLightningSegment::Construct);
static void *const kSegmentDestructor = (void *)XbeAddress(&RLightningSegment::Destruct);

// The bolt textures, drawn in turn
static const uint32_t kBoltTextures[4] = {
    0x3074696c,   // 'lit0'
    0x3174696c,   // 'lit1'
    0x3274696c,   // 'lit2'
    0x3374696c,   // 'lit3'
};

// The strides the tables are stepped by
constexpr int kCoordStride = 5;
constexpr int kColourStride = 7;
constexpr int kNumStride = 11;
constexpr int kRandomMask = kLightningRandomCount - 1;

constexpr double kRandomScale = 1.0 / 65536.0;    // 16 random bits to [0, 1)
constexpr uint32_t kMaxBatch = 512;                // strip vertices a draw
constexpr uint32_t kMaxBolts = 0x3fffffff;

// GeoPrimState's depth comparison, in OpenGL's numbering
constexpr uint32_t kDepthLess = 0x201;

// ---- the points

// FUNC_AT(0x0009eae0)
void RLightningPoint::Update(int tick) {
    int elapsed = tick - startTick;
    if (elapsed > 0) {
        Coord4 travel = {}, shake = {};
        VU0_v4scale(&drift, (float)elapsed, &travel);
        RLightning *lightning = TheLightning;
        lightning->randomCoordIndex = (lightning->randomCoordIndex + kCoordStride) & kRandomMask;
        VU0_v4scale(&lightning->randomCoords[lightning->randomCoordIndex], jitter, &shake);
        VU0_v3add(&base, &travel, &current);
        VU0_v3add(&current, &shake, &current);
    } else {
        current = base;
    }
}

// FUNC_AT(0x0009ec70)
void RLightningMidPoint::Respawn(float fraction, float lift) {
    base.x = (float)(Util_GaussRandom() * spread + fraction);
    base.y = (float)((Util_GaussRandom() + lift) * spread + fraction);
    base.z = (float)(Util_GaussRandom() * spread + fraction);

    RLightning *lightning = TheLightning;
    lightning->randomCoordIndex = (lightning->randomCoordIndex + kCoordStride) & kRandomMask;
    const Coord4 &random = lightning->randomCoords[lightning->randomCoordIndex];
    Coord4 direction = { random.x, random.y, random.z, 0.0f };
    double speed = Util_ApplyVariance(driftSpeed, lightning->driftVariance);
    VU0_v4scale(&direction, (float)(SimTimeStep * speed), &drift);

    startTick = SimStepCount;
    deathTick = Ftol(Util_ApplyVariance((float)life, lightning->lifeVariance)) + startTick;
}

// FUNC_AT(0x0009eb90)
void RLightningControlPoint::Init(const Coord3 *position, RSceneObj *object, const ArticleEffect *fxLocator) {
    base = *position;
    startTick = SimStepCount;
    drift = *ZeroVector;
    life = 0;
    jitter = 0.0f;
    spread = 0.0f;
    driftSpeed = 0.0f;
    sceneObj = object;
    locator = fxLocator;
}

// FUNC_AT(0x0009ebf0)
void RLightningControlPoint::CalcWorldPos() {
    if (sceneObj == NULL) {
        worldPos = current;
        return;
    }
    Coord4 local = { current.x, current.y, current.z, 0.0f };
    alignas(16) MATRIX4 transform;
    if (locator != NULL)
        sceneObj->LocateFX(locator, &transform, true);
    else
        sceneObj->GetTransform(&transform);
    VU0_MATRIX4_vect3mult(&local, &transform, &worldPos);
}

// ---- segments

// FUNC_AT(0x0009f7c0)
RLightningSegment* RLightningSegment::Construct() {
    midpoints.data = NULL;
    midpoints.count = 0;
    parts.data = NULL;
    parts.count = 0;
    return this;
}

// FUNC_AT(0x0009f7e0)
void RLightningSegment::Destruct() {
    parts.Destruct();
    if (midpoints.data != NULL)
        OperatorDelete(midpoints.data);
    midpoints.data = NULL;
    midpoints.count = 0;
}

// FUNC_AT(0x0009f8b0)
void RLightningSegmentArray::Allocate(uint32_t newCount) {
    if (data != NULL) {
        int32_t *cookie = (int32_t *)data - 1;   // new[]'s count of elements
        CRT_VectorDestructorIterator(data, sizeof(RLightningSegment), *cookie, kSegmentDestructor);
        OperatorDelete(cookie);
    }
    data = NULL;
    count = newCount;
    int32_t *block = static_cast<int32_t *>(OperatorNewArray(newCount * sizeof(RLightningSegment) + 4));
    if (block == NULL) {
        LIGHTNING_UNTESTED("RLightning::Segment array allocation failing");
        data = NULL;
        return;
    }
    *block = newCount;
    CRT_VectorConstructorIterator(block + 1, sizeof(RLightningSegment), newCount, kSegmentConstructor,
                                  kSegmentDestructor);
    data = reinterpret_cast<RLightningSegment *>(block + 1);
}

// FUNC_AT(0x0009f250)
void RLightningSegment::Update(int tick) {
    for (int i = 0; i < int(midpoints.count); i++) {
        RLightningMidPoint *midpoint = &midpoints.data[i];
        if (tick >= midpoint->deathTick)
            midpoint->Respawn((float)((double)(i + 1) / (int)(midpoints.count + 1)), 0.0f);
        midpoint->Update(tick);
    }
    for (int i = 0; i < int(parts.count); i++)
        parts.data[i].Update(tick);
}

// A midpoint's place: its fractions of the way from start to end
static void PlaceMidPoint(RLightningMidPoint *midpoint, const RLightningPoint *start, const RLightningPoint *end) {
    Coord4 span = {};
    VU0_v4sub(&end->worldPos, &start->worldPos, &span);
    VU0_v4multxyz(&midpoint->current, &span, &midpoint->worldPos);
    VU0_v3add(&midpoint->worldPos, &start->worldPos, &midpoint->worldPos);
}

// FUNC_AT(0x0009f2f0)
void RLightningSegment::Draw(RLightning *lightning) {
    if (int(parts.count) > 0) {
        for (int i = 0; i < int(midpoints.count); i++)
            PlaceMidPoint(&midpoints.data[i], start, end);
        parts.data[0].Draw(lightning);
        for (int i = 0; i < int(midpoints.count); i++) {
            RLightningStrip *strip = lightning->strip;
            lightning->JitterStripPoints(&midpoints.data[i].worldPos, &strip->positions[lightning->stripCount],
                                         &strip->colours[lightning->stripCount]);
            lightning->stripCount += 2;
            parts.data[i + 1].Draw(lightning);
        }
    } else {
        for (int i = 0; i < int(midpoints.count); i++) {
            PlaceMidPoint(&midpoints.data[i], start, end);
            RLightningStrip *strip = lightning->strip;
            lightning->JitterStripPoints(&midpoints.data[i].worldPos, &strip->positions[lightning->stripCount],
                                         &strip->colours[lightning->stripCount]);
            lightning->stripCount += 2;
        }
    }
}

// ---- bolts

// FUNC_AT(0x0009f4c0)
void RLightningBolt::Destruct() {
    segments.Destruct();
    if (points.data != NULL)
        OperatorDelete(points.data);
    points.data = NULL;
    points.count = 0;
}

// FUNC_AT(0x0009f6b0)
void RLightningBolt::Draw(RLightning *lightning) {
    int elapsed = SimStepCount - startTick;
    lightning->randomNumIndex = (lightning->randomNumIndex + kNumStride) & kRandomMask;
    double fade = alpha - (double)elapsed * SimTimeStep * alphaDecay +
                  (double)lightning->randomNums[lightning->randomNumIndex] * lightning->alphaNoise;
    float drawAlpha = fade < 0.0 ? 0.0f : (float)fade;

    for (int i = 0; i < int(points.count); i++)
        points.data[i].CalcWorldPos();
    lightning->BeginBoltDraw(&points.data[0].worldPos, drawAlpha);
    for (int i = 0; i < int(segments.count); i++) {
        segments.data[i].Draw(lightning);
        RLightningStrip *strip = lightning->strip;
        lightning->JitterStripPoints(&points.data[i + 1].worldPos, &strip->positions[lightning->stripCount],
                                     &strip->colours[lightning->stripCount]);
        lightning->stripCount += 2;
    }
    lightning->joinNext = true;
}

// ---- the bolt list

// FUNC_AT(0x0009ff80)
void RLightningBoltVector::Xlen() {
    LIGHTNING_UNTESTED("RLightning's bolt vector _Xlen");
    ThrowLengthError("vector<T> too long");
}

// FUNC_AT(0x000a0020)
void RLightningBoltVector::InsertN(RLightningBolt **where, uint32_t count, RLightningBolt *const *value) {
    ColStl::InsertN(this, where, count, value, kMaxBolts, Xlen);
}

// FUNC_AT(0x000a0390)
void RLightningBoltVector::PushBack(RLightningBolt *const *bolt) {
    ColStl::PushBack(this, bolt, [this](RLightningBolt **where, uint32_t count, RLightningBolt *const *value) {
        InsertN(where, count, value);
    });
}

// ---- RLightning

// FUNC_AT(0x0009e8e0)
void RLightning::LoadAttributes() {
    width = 0.18f;
    squiggle = 0.05f;
    spread = 0.05f;
    driftRate = 0.1f;
    driftVariance = 0.5f;
    lifeVariance = 0.5f;
    alphaInitial = 0.7f;
    alphaDecay = 0.0f;
    alphaNoise = 0.3f;
    segments = 4;
    levels = 4;
    passes = 1;
    dbattrib_float("Lightning width", &width, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning squiggle", &squiggle, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning spread", &spread, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning drift rate", &driftRate, 0.0f, 3.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning drift variance", &driftVariance, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning life variance", &lifeVariance, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning alpha (initial)", &alphaInitial, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning alpha decay", &alphaDecay, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_float("Lightning alpha noise", &alphaNoise, 0.0f, 1.0f, 0, 1.0f, NULL);
    dbattrib_s8("Lightning segments", &segments, 2, 10, 0, 1.0f, NULL);
    dbattrib_s8("Lightning levels", &levels, 2, 5, 0, 1.0f, NULL);
    dbattrib_s8("Lightning passes", &passes, 1, 5, 0, 1.0f, NULL);
}

// FUNC_AT(0x0009ed50)
void RLightning::CalcRandomTables() {
    randomCoordIndex = 0;
    randomColourIndex = 0;
    randomNumIndex = 0;
    for (int i = 0; i < kLightningRandomCount; i++) {
        // A direction from three random numbers in [-1, 1), drawn again while all three are zero, made unit
        Coord4 direction;
        do {
            direction.x = (float)(RandomShort() * 2 * kRandomScale - 1.0);
            direction.y = (float)(RandomShort() * 2 * kRandomScale - 1.0);
            direction.z = (float)(RandomShort() * 2 * kRandomScale - 1.0);
        } while (direction.x == 0.0f && direction.y == 0.0f && direction.z == 0.0f);
        direction.w = 0.0f;
        VU0_v4unitxyz(&direction, &direction);
        randomCoords[i].x = direction.x;
        randomCoords[i].y = direction.y;
        randomCoords[i].z = direction.z;
        randomCoords[i].w = 1.0f;
        randomColours[i] = uint32_t(RandomShort() * 0xff >> 16) << 24 | 0xffffff;
        randomNums[i] = (float)Util_GaussRandom();
    }
}

// FUNC_AT(0x0009ef00)
void RLightning::JitterStripPoints(const Coord3 *point, Coord4 *positions, uint32_t *colours) {
    Coord4 centre = { point->x, point->y, point->z, 1.0f };
    randomCoordIndex = (randomCoordIndex + kCoordStride) & kRandomMask;
    Coord4 offset = randomCoords[randomCoordIndex];
    randomColourIndex = (randomColourIndex + kColourStride) & kRandomMask;
    uint32_t colour = randomColours[randomColourIndex];

    VU0_v4scale4(&offset, width, &offset);
    VU0_v4add4(&centre, &offset, &positions[0]);
    VU0_v4sub4(&centre, &offset, &positions[1]);
    positions[0].w = 1.0f;
    positions[1].w = 1.0f;

    uint32_t alpha = uint32_t(Ftol((double)(colour >> 24) * alphaScale));
    if (alpha > 0xff)
        alpha = 0xff;
    colour = (colour & 0xffffff) | alpha << 24;
    colours[0] = colour;
    colours[1] = colour;
}

// FUNC_AT(0x0009f520)
void RLightning::BeginBoltDraw(const Coord3 *start, float alpha) {
    alphaScale = alpha;
    if (strip == NULL) {
        // new RLightningStrip: the colours start cleared
        RLightningStrip *buffer = static_cast<RLightningStrip *>(OperatorNew(sizeof(RLightningStrip)));
        if (buffer != NULL)
            memset(buffer->colours, 0, sizeof(buffer->colours));
        else
            LIGHTNING_UNTESTED("RLightning's strip allocation failing");
        strip = buffer;
    }
    if (joinNext) {
        // Two degenerate triangles from the last bolt's end: its last vertex repeated, then this point's first
        // repeated. The first pair of points jittered here is thrown away.
        Coord4 unusedPositions[2];
        uint32_t unusedColours[2] = { 0, 0 };
        JitterStripPoints(start, unusedPositions, unusedColours);
        strip->positions[stripCount] = strip->positions[stripCount - 1];
        strip->colours[stripCount] = strip->colours[stripCount - 1];
        JitterStripPoints(start, &strip->positions[stripCount + 2], &strip->colours[stripCount + 2]);
        strip->positions[stripCount + 1] = strip->positions[stripCount + 2];
        strip->colours[stripCount + 1] = strip->colours[stripCount + 2];
        joinNext = false;
        stripCount += 4;
    } else {
        JitterStripPoints(start, &strip->positions[stripCount], &strip->colours[stripCount]);
        stripCount += 2;
    }
}

// FUNC_AT(0x0009f020)
void RLightning::RenderBolts() {
    if (stripCount == 0)
        return;
    lastStripCount = stripCount;

    // The texture runs across the strip: (0, 0), (0, 1), (1, 0), (1, 1) and round again
    const float uvCycle[4][2] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } };
    for (uint32_t i = 0; i < stripCount; i++) {
        strip->uvs[i][0] = uvCycle[i & 3][0];
        strip->uvs[i][1] = uvCycle[i & 3][1];
    }
    textureCycle = (textureCycle + 1) & 3;

    // Batches of up to 512 vertices. A batch that is not the last ends four vertices or more short of the end;
    // the next starts on its last two vertices, on a multiple of four (the texture's cycle).
    uint32_t first = 0;
    while (first < stripCount) {
        TexturedGeoPrim *request = VolatileRequests[VolatileRequestIndex];
        request->positions.SetData(&strip->positions[first]);
        request->colours.SetData(&strip->colours[first]);
        request->texCoords.SetData(strip->uvs[first]);
        request->texture.SetData(textures[textureCycle]);

        uint32_t count = stripCount - first;
        if (count >= kMaxBatch)
            count = kMaxBatch;
        count &= ~1u;
        uint32_t next = first + count;
        if (next < stripCount) {
            while (stripCount - next < 4) {
                next -= 2;
                count -= 2;
            }
            next -= 2;
            while ((next & 3) != 0) {
                next -= 2;
                count -= 2;
            }
        }

        material.SetTextureEnable(true);
        material.SetAlphaTestEnable(true);
        material.SetAlphaCompareValue(8);
        material.SetAlphaBlendMode(2);
        material.SetTransparencyMethod(1);
        material.SetDepthTestMethod(kDepthLess);
        material.SetShading(1);
        fgRenderer->renderContext->SetZWritesEnable(0);
        material.Draw(kTriangleStrip, count, NULL);
        fgRenderer->renderContext->SetZWritesEnable(1);
        first = next;
    }
}

// FUNC_AT(0x0009f840)
bool RLightning::IsBoltAlive(int handle) {
    for (RLightningBolt **bolt = bolts.first; bolt != bolts.last; bolt++)
        if ((*bolt)->handle == handle)
            return true;
    return false;
}

// FUNC_AT(0x0009f970)
void RLightning::BuildNewSegment(RLightningSegment *segment, RLightningPoint *start, RLightningPoint *end, int life,
                                 int segmentCount, int levels, float driftSpeed, float jitter, float spread,
                                 float bulge) {
    segment->start = start;
    segment->end = end;
    if (levels <= 0)
        return;

    // The midpoints, lifted most at the middle: bulge times their distance from the middle in half-lengths
    int midCount = segmentCount - 1;
    if (segment->midpoints.data != NULL)
        OperatorDelete(segment->midpoints.data);
    segment->midpoints.data = NULL;
    segment->midpoints.count = midCount;
    segment->midpoints.data =
        static_cast<RLightningMidPoint *>(OperatorNewArray(midCount * sizeof(RLightningMidPoint)));
    if (midCount > 0) {
        float half = (float)(midCount * 0.5);
        float inverseHalf = (float)(1.0 / half);
        float total = (float)segmentCount;
        for (int i = 0; i < midCount; i++) {
            RLightningMidPoint *midpoint = &segment->midpoints.data[i];
            midpoint->life = life;
            midpoint->driftSpeed = driftSpeed;
            midpoint->jitter = jitter;
            midpoint->spread = spread;
            float lift = (float)(fabs(i - (double)half) * inverseHalf * bulge);
            midpoint->Respawn((float)((i + 1) / (double)total), lift);
        }
    }

    // The parts between them, a level down: shorter lives, everything else scaled to a part's length
    double inverse = 1.0 / segmentCount;
    int partLife = SimStepsPerSecond >> 3;
    if (partLife <= life >> 1)
        partLife = life >> 1;
    float partDrift = (float)(driftSpeed * inverse);
    float partJitter = (float)(jitter * inverse);
    float partSpread = (float)(spread * inverse * 2.0);
    segment->parts.Allocate(segmentCount);
    RLightningMidPoint *midpoints = segment->midpoints.data;
    BuildNewSegment(&segment->parts.data[0], start, &midpoints[0], partLife, segmentCount, levels - 1, partDrift,
                    partJitter, partSpread, 0.0f);
    for (int i = 1; i < segmentCount; i++) {
        RLightningPoint *partEnd = i < int(segment->midpoints.count) ? &midpoints[i] : end;
        BuildNewSegment(&segment->parts.data[i], &midpoints[i - 1], partEnd, partLife, segmentCount, levels - 1,
                        partDrift, partJitter, partSpread, 0.0f);
    }
}

// FUNC_AT(0x0009fb90)
void RLightning::Update() {
    int now = SimStepCount;
    int steps = now - lastSimTick;
    if (steps <= 0) {
        lastSimTick = now;
        return;
    }
    if (steps > 3)
        steps = 1;
    for (int tick = now - steps + 1; tick <= SimStepCount; tick++) {
        randomCoordIndex = (tick * kCoordStride) & kRandomMask;
        randomColourIndex = (tick * kColourStride) & kRandomMask;
        randomNumIndex = (tick * kNumStride) & kRandomMask;
        RLightningBolt **at = bolts.first;
        while (at != bolts.last) {
            RLightningBolt *bolt = *at;
            if (tick > bolt->startTick + bolt->life) {
                for (RLightningBolt **next = at + 1; next != bolts.last; next++)
                    next[-1] = *next;
                bolts.last--;
                bolt->Destruct();
                OperatorDelete(bolt);
                continue;
            }
            for (int i = 0; i < int(bolt->points.count); i++)
                bolt->points.data[i].Update(tick);
            for (int i = 0; i < int(bolt->segments.count); i++)
                bolt->segments.data[i].Update(tick);
            at++;
        }
    }
    lastSimTick = SimStepCount;
}

// FUNC_AT(0x0009fd20)
void RLightning::Draw() {
    Update();
    int tick = SimStepCount;
    randomNumIndex = (tick * kNumStride) & kRandomMask;
    randomCoordIndex = (tick * kCoordStride) & kRandomMask;
    randomColourIndex = (tick * kColourStride) & kRandomMask;
    stripCount = 0;
    joinNext = false;
    textureCycle = 0;
    alphaScale = 1.0f;
    for (RLightningBolt **bolt = bolts.first; bolt != bolts.last; bolt++)
        for (int pass = 0; pass < passes; pass++)
            (*bolt)->Draw(this);
    RenderBolts();
}

// FUNC_AT(0x0009fdc0)
void RLightning::ClearAllBolts() {
    for (RLightningBolt **bolt = bolts.first; bolt != bolts.last; bolt++) {
        if (*bolt != NULL) {
            (*bolt)->Destruct();
            OperatorDelete(*bolt);
        }
    }
    ColStl::Tidy(&bolts);
}

// FUNC_AT(0x0009fe30)
void RLightning::Reset() {
    lastSimTick = SimStepCount;
    nextBoltHandle = 1;
    unknown6E4 = false;
    metricsInterval = SimStepsPerSecond;
    CalcRandomTables();
    ClearAllBolts();
    stripCount = 0;
    joinNext = false;
    textureCycle = 0;
    alphaScale = 1.0f;
    nextMetricsTick = metricsInterval + SimStepCount;
    lastStripCount = 0;
    unknown6F4 = 0;
    if (strip != NULL) {
        OperatorDelete(strip);
        strip = NULL;
    }
}

// FUNC_AT(0x0009fec0)
void RLightning::Destruct() {
    singleton.vtable = kLightningVtable;
    Reset();
    for (int i = 0; i < 4; i++)
        textures[i] = NULL;
    material.Destruct();
    ColStl::Tidy(&bolts);
    singleton.vtable = kSingletonVtable;
}

// FUNC_AT(0x0009ff60)
void RLightning::Kill() {
    RLightning *lightning = TheLightning;
    if (lightning != NULL) {
        typedef RLightning *(RLightning::*DeletingDestructor)(unsigned flags);
        (lightning->*XbeVirtual<DeletingDestructor>(lightning, 0))(1);
    }
}

// FUNC_AT(0x000a0000)
RLightning* RLightning::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x000a02e0)
RLightning* RLightning::Construct() {
    singleton.vtable = kLightningVtable;
    bolts.first = NULL;
    bolts.last = NULL;
    bolts.end = NULL;
    memset(randomColours, 0, sizeof(randomColours));
    material.Construct();
    strip = NULL;
    Reset();
    for (int i = 0; i < 4; i++)
        textures[i] = RTextureContextManager::GetContext(0)->FindOrCreateTexture(kBoltTextures[i], 0);
    return this;
}

// FUNC_AT(0x000a0400)
int RLightning::AddBolt(const Coord3 *from, RSceneObj *fromObj, const ArticleEffect *fromLocator, const Coord3 *to,
                        RSceneObj *toObj, const ArticleEffect *toLocator, float bulge, int life) {
    RLightningBolt *bolt = static_cast<RLightningBolt *>(OperatorNew(sizeof(RLightningBolt)));
    if (bolt != NULL)
        bolt->Construct();
    bolt->handle = nextBoltHandle++;
    bolt->startTick = SimStepCount;
    bolt->life = life;
    bolt->alpha = alphaInitial;
    bolt->alphaDecay = alphaDecay;

    if (bolt->points.data != NULL)
        OperatorDelete(bolt->points.data);
    bolt->points.data = NULL;
    bolt->points.count = 2;
    bolt->points.data = static_cast<RLightningControlPoint *>(OperatorNewArray(2 * sizeof(RLightningControlPoint)));
    RLightningControlPoint *points = bolt->points.data;
    points[0].Init(from, fromObj, fromLocator);
    points[1].Init(to, toObj, toLocator);

    bolt->segments.Allocate(1);
    BuildNewSegment(&bolt->segments.data[0], &points[0], &points[1], life, segments, levels - 1, driftRate,
                    squiggle, spread, bulge);
    bolts.PushBack(&bolt);
    return bolt->handle;
}

// FUNC_AT(0x000a0500)
int RLightning::AddRoundedBolt(RSceneObj *sceneObj, const ArticleEffect *fromLocator, const ArticleEffect *toLocator,
                               float driftMin, float driftMax, float driftVariance, float spread, bool aroundObject,
                               int pointCount, int levels, float bulge, int life) {
    Coord4 centre = { ZeroVector->x, ZeroVector->y, ZeroVector->z, 0.0f };
    RLightningBolt *bolt = static_cast<RLightningBolt *>(OperatorNew(sizeof(RLightningBolt)));
    if (bolt != NULL)
        bolt->Construct();
    bolt->handle = nextBoltHandle++;
    bolt->startTick = SimStepCount;
    bolt->life = life;
    bolt->alpha = alphaInitial;
    bolt->alphaDecay = alphaDecay;
    bolt->segments.Allocate(pointCount);

    // pointCount + 1 control points on the object: the two locators', and the ones between
    if (bolt->points.data != NULL)
        OperatorDelete(bolt->points.data);
    bolt->points.data = NULL;
    bolt->points.count = pointCount + 1;
    bolt->points.data =
        static_cast<RLightningControlPoint *>(OperatorNewArray((pointCount + 1) * sizeof(RLightningControlPoint)));
    RLightningControlPoint *points = bolt->points.data;
    points[0].Init(ZeroVector, sceneObj, fromLocator);
    points[pointCount].Init(ZeroVector, sceneObj, toLocator);

    // The locators' positions
    alignas(16) MATRIX4 transform;
    Coord4 fromPosition = {}, toPosition = {};
    sceneObj->LocateFX(fromLocator, &transform, false);
    VU0_MATRIX4_vect3mult(ZeroVector, &transform, &fromPosition);
    sceneObj->LocateFX(toLocator, &transform, false);
    VU0_MATRIX4_vect3mult(ZeroVector, &transform, &toPosition);

    if (pointCount > 1) {
        float total = (float)pointCount;
        float driftRange = driftMax - driftMin;
        for (int i = 1; i < pointCount; i++) {
            RLightningControlPoint *point = &points[i];

            // i / pointCount of a unit step from the first locator towards the second
            float fraction = (float)(i / (double)total);
            Coord4 along = {}, position = {};
            VU0_v4sub(&toPosition, &fromPosition, &along);
            VU0_v4unitxyz(&along, &along);
            VU0_v4scale(&along, fraction, &position);
            VU0_v3add(&position, &fromPosition, &position);

            // ... or where the line from out beyond the object to its centre meets its box
            if (aroundObject) {
                Coord4 outside = {}, halfExtents, hit;
                VU0_v4sub(&position, &centre, &outside);
                VU0_v4unitxyz(&outside, &outside);
                VU0_v4scale(&outside, sceneObj->GetBoundingRadius(), &outside);
                sceneObj->GetBoundingDimensions(&halfExtents);
                if (FindOBBIntersect(&halfExtents, &outside, &centre, &hit)) {
                    position.x = hit.x;
                    position.y = hit.y;
                    position.z = hit.z;
                }
            }
            point->Init(reinterpret_cast<const Coord3 *>(&position), sceneObj, NULL);

            // Drifting out from the centre, turned by up to `spread`, at a random speed from driftMin to driftMax
            Coord4 outward = {};
            VU0_v4sub(&position, &centre, &outward);
            VU0_v4unitxyz(&outward, &outward);
            Coord3 turned;
            const Coord3 *direction = Util_PerturbVector(&turned, &outward, spread);
            outward.x = direction->x;
            outward.y = direction->y;
            outward.z = direction->z;
            double range = driftRange < 0.0f ? 0.0 : driftRange;
            double speed = range * RandomShort() * kRandomScale + driftMin;
            double drift = Util_ApplyVariance((float)speed, driftVariance) * SimTimeStep;
            VU0_v4scale(&outward, (float)drift, &point->drift);
        }
    }

    for (int i = 0; i < pointCount; i++)
        BuildNewSegment(&bolt->segments.data[i], &points[i], &points[i + 1], life, segments, levels, driftRate,
                        squiggle, this->spread, bulge);
    bolts.PushBack(&bolt);
    return bolt->handle;
}
