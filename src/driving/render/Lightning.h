#ifndef DRIVING_RENDER_LIGHTNING_H_
#define DRIVING_RENDER_LIGHTNING_H_

// ---------------------------------------------------------------------------------------------------------------
// RLightning (one instance, 0x700 bytes): the electric bolts. A bolt runs between two control points (each a point
// in a scene object's frame, or the world's) through a tree of segments: each segment is split by midpoints that
// wander round their place on the line, and each part is split again, `levels` deep. Every frame the bolts are
// drawn as one triangle strip, two vertices for every point a random width apart, from tables of random
// directions, colours and numbers the lightning steps through in a fixed order. See Lightning.cpp.
//
// The point, segment and bolt classes are RLightning's nested ones in Ghidra (RLightning::Point, ::ControlPoint,
// ::MidPoint, ::Segment, ::Bolt); they are flat here, the names joined.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../data/CoordConvert.h"         // Coord3, Coord4
#include "../eagl/GeoPrimState.h"
#include "../engine/CoreContainers.h"     // USimpleVec, LightningSegmentVec, GameVector
#include "../engine/USingleton.h"
#include "../world/CollisionTypes.h"      // ColVector
#include "Materials.h"

namespace EAGL {
struct TAR;
}
class RSceneObj;
struct ArticleEffect;                     // world/World.h: what RSceneObj::LocateFX places

// The warning beside a provisional port: code no shipped data reaches (the STL's "too long" throw, a failed
// allocation), ported from the listing without a test. Once.
inline void LightningUntested(const char *what) {
    printf("[lightning] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define LIGHTNING_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            LightningUntested(what); \
        } \
    } while (0)

// A point of a bolt (0x50 bytes). It drifts from `base` at `drift` per tick from startTick, shaken by a random
// direction `jitter` long. A midpoint's base and current position are fractions of its segment (its place along
// the line, spread round it); a control point's are a position.
struct RLightningPoint {
    Coord3 base;                        // +0x00
    int32_t startTick;                  // +0x0c
    Coord3 drift;                       // +0x10 per tick
    int32_t life;                       // +0x1c ticks, before the life variance
    Coord3 current;                     // +0x20
    int32_t deathTick;                  // +0x2c
    Coord3 worldPos;                    // +0x30
    float jitter;                       // +0x3c
    float spread;                       // +0x40
    float driftSpeed;                   // +0x44
    uint8_t unknown48[8];

    void Update(int tick);                                                                      // 0x0009eae0
};
static_assert(sizeof(RLightningPoint) == 0x50, "a lightning point is 0x50 bytes");

// A segment's midpoint (Ghidra: RLightning::MidPoint)
struct RLightningMidPoint : RLightningPoint {
    // A new place `fraction` along the segment, raised by `lift`, a new drift and a new life
    void Respawn(float fraction, float lift);                                                   // 0x0009ec70
};
static_assert(sizeof(RLightningMidPoint) == 0x50, "a midpoint is 0x50 bytes");

// A bolt's end or bend (Ghidra: RLightning::ControlPoint, 0x60 bytes): placed by a scene object and its effect
// locator, or the object's transform without one, or in the world without an object
struct RLightningControlPoint : RLightningPoint {
    RSceneObj *sceneObj;                // +0x50
    const ArticleEffect *locator;           // +0x54
    uint8_t unknown58[8];

    void Init(const Coord3 *position, RSceneObj *sceneObj, const ArticleEffect *locator);           // 0x0009eb90
    void CalcWorldPos();                                                                        // 0x0009ebf0
};
static_assert(sizeof(RLightningControlPoint) == 0x60, "a control point is 0x60 bytes");

struct RLightningSegment;

// A segment's parts: USimpleVec<RLightning::Segment>, an array from new[] (its count in the word before it)
struct RLightningSegmentArray : LightningSegmentVec {
    // The old parts destroyed, `count` new ones made (FUN_0009f8b0, the name is ours)
    void Allocate(uint32_t newCount);                                                           // 0x0009f8b0
};

class RLightning;

// A stretch of bolt between two points (Ghidra: RLightning::Segment, 0x18 bytes): its midpoints, and the parts
// between them, one more than the midpoints - each a segment again, down to the last level, which has none
struct RLightningSegment {
    RLightningPoint *start;             // +0x00
    RLightningPoint *end;               // +0x04
    USimpleVec<RLightningMidPoint> midpoints;   // +0x08
    RLightningSegmentArray parts;       // +0x10

    RLightningSegment* Construct();     // 0x0009f7c0 (Ghidra has no function there)
    void Destruct();                    // FUN_0009f7e0                                         // 0x0009f7e0
    void Update(int tick);                                                                      // 0x0009f250
    void Draw(RLightning *lightning);                                                           // 0x0009f2f0
};
static_assert(sizeof(RLightningSegment) == 0x18, "a segment is 0x18 bytes");

// A bolt (Ghidra: RLightning::Bolt, 0x24 bytes): its control points and the segments between them, its handle, when
// it started, how long it lives and how it fades
struct RLightningBolt {
    USimpleVec<RLightningControlPoint> points;  // +0x00
    RLightningSegmentArray segments;    // +0x08 one fewer than the points
    int32_t handle;                     // +0x10
    int32_t startTick;                  // +0x14
    int32_t life;                       // +0x18 ticks
    float alpha;                        // +0x1c at the start
    float alphaDecay;                   // +0x20 per second

    void Construct() {                  // inlined in the game: no points, no segments
        points.data = NULL;
        points.count = 0;
        segments.data = NULL;
        segments.count = 0;
    }
    void Destruct();                                                                            // 0x0009f4c0
    void Draw(RLightning *lightning);                                                           // 0x0009f6b0
};
static_assert(sizeof(RLightningBolt) == 0x24, "a bolt is 0x24 bytes");

// std::vector<RLightning::Bolt *>
struct RLightningBoltVector : ColVector<RLightningBolt *> {
    void PushBack(RLightningBolt *const *bolt);                                                 // 0x000a0390
    void InsertN(RLightningBolt **where, uint32_t count, RLightningBolt *const *value);         // 0x000a0020
    static void Xlen();                 // length_error("vector<T> too long")                   // 0x0009ff80
};
static_assert(sizeof(RLightningBoltVector) == 0x10, "a vector is 16 bytes");

// The strip the bolts are drawn as, rebuilt every frame (0xe000 bytes, the name is ours)
constexpr int kLightningStripVertices = 0x800;
struct RLightningStrip {
    Coord4 positions[kLightningStripVertices];      // +0x0000
    float uvs[kLightningStripVertices][2];          // +0x8000
    uint32_t colours[kLightningStripVertices];      // +0xc000
};
static_assert(sizeof(RLightningStrip) == 0xe000, "the strip buffer is 0xe000 bytes");

constexpr int kLightningRandomCount = 64;

class RLightning {
public:
    USingleton singleton;               // +0x00 the base, its vtable pointer: RLightning's (0x00192d20)
    RLightningBoltVector bolts;         // +0x04
    int32_t nextBoltHandle;             // +0x14
    int32_t lastSimTick;                // +0x18 the tick Update last stepped to
    // The tuning ("Lightning ..." debug variables, LoadAttributes)
    float width;                        // +0x1c "Lightning width"
    float squiggle;                     // +0x20 "Lightning squiggle": the midpoints' jitter
    float spread;                       // +0x24 "Lightning spread"
    float driftRate;                    // +0x28 "Lightning drift rate"
    float driftVariance;                // +0x2c "Lightning drift variance"
    float lifeVariance;                 // +0x30 "Lightning life variance"
    float alphaInitial;                 // +0x34 "Lightning alpha (initial)"
    float alphaDecay;                   // +0x38 "Lightning alpha decay"
    float alphaNoise;                   // +0x3c "Lightning alpha noise"
    int32_t segments;                   // +0x40 "Lightning segments": the parts a segment splits into
    int32_t levels;                     // +0x44 "Lightning levels"
    int32_t passes;                     // +0x48 "Lightning passes": each bolt is drawn this many times
    uint32_t unknown4C;
    // The random tables and where each was last read
    Coord4 randomCoords[kLightningRandomCount];     // +0x050 unit directions, w 1
    uint32_t randomColours[kLightningRandomCount];  // +0x450 white with a random alpha
    float randomNums[kLightningRandomCount];        // +0x550 Gaussian
    int32_t randomCoordIndex;           // +0x650
    int32_t randomColourIndex;          // +0x654
    int32_t randomNumIndex;             // +0x658
    uint32_t unknown65C;
    UVolatileMaterial material;         // +0x660
    RLightningStrip *strip;             // +0x6c0 allocated by the first BeginBoltDraw
    uint32_t stripCount;                // +0x6c4 vertices in the strip
    bool joinNext;                      // +0x6c8 a bolt has been drawn: the next is joined to it by degenerate triangles
    uint8_t unknown6C9[3];
    EAGL::TAR *textures[4];             // +0x6cc "lit0".."lit3", drawn in turn
    int32_t textureCycle;               // +0x6dc
    float alphaScale;                   // +0x6e0 the bolt being drawn's alpha
    bool unknown6E4;                    // +0x6e4 cleared by Reset
    uint8_t unknown6E5[3];
    int32_t metricsInterval;            // +0x6e8 the simulation's steps per second at Reset
    int32_t nextMetricsTick;            // +0x6ec
    uint32_t lastStripCount;            // +0x6f0 the strip's vertices when last drawn
    int32_t unknown6F4;                 // +0x6f4 cleared by Reset
    uint8_t unknown6F8[8];

    RLightning* Construct();                                                                    // 0x000a02e0
    void Destruct();                                                                            // 0x0009fec0
    RLightning* Delete(unsigned flags);     // the scalar deleting destructor, vtable slot 0     // 0x000a0000
    static void Kill();                     // vtable slot 2                                     // 0x0009ff60
    void Reset();                           // vtable slot 1                                     // 0x0009fe30
    void LoadAttributes();                                                                      // 0x0009e8e0

    void CalcRandomTables();                                                                    // 0x0009ed50
    // A bolt between two points, each placed by a scene object and a locator, or in the world; `bulge` lifts its
    // midpoints, most at the middle. Answers its handle.
    int AddBolt(const Coord3 *from, RSceneObj *fromObj, const ArticleEffect *fromLocator, const Coord3 *to,
                RSceneObj *toObj, const ArticleEffect *toLocator, float bulge, int life);           // 0x000a0400
    // A bolt between two of a scene object's locators through `pointCount` - 1 control points on the line between
    // them (on the object's surface, with aroundObject), each drifting at a random speed from driftMin to driftMax
    // in a direction `spread` from straight out. Answers its handle.
    int AddRoundedBolt(RSceneObj *sceneObj, const ArticleEffect *fromLocator, const ArticleEffect *toLocator,
                       float driftMin, float driftMax, float driftVariance, float spread, bool aroundObject,
                       int pointCount, int levels, float bulge, int life);                     // 0x000a0500
    bool IsBoltAlive(int handle);                                                               // 0x0009f840
    void ClearAllBolts();                                                                       // 0x0009fdc0
    // Splits a segment into `segmentCount` parts, `levels` deep
    void BuildNewSegment(RLightningSegment *segment, RLightningPoint *start, RLightningPoint *end, int life,
                         int segmentCount, int levels, float driftSpeed, float jitter, float spread,
                         float bulge);                                                          // 0x0009f970
    // Steps the bolts to the current tick: dead ones removed, the points moved
    void Update();                                                                              // 0x0009fb90
    void Draw();                                                                                // 0x0009fd20

    // Two strip vertices either side of the point and their colours, at the next random direction and colour
    void JitterStripPoints(const Coord3 *point, Coord4 *positions, uint32_t *colours);          // 0x0009ef00
    // A bolt's first point into the strip, joined to the last bolt's end
    void BeginBoltDraw(const Coord3 *start, float alpha);                                       // 0x0009f520
    // The strip drawn in batches of up to 512 vertices
    void RenderBolts();                                                                         // 0x0009f020
};
static_assert(sizeof(RLightning) == 0x700, "RLightning is 0x700 bytes");
static_assert(offsetof(RLightning, randomCoords) == 0x50 && offsetof(RLightning, randomNums) == 0x550 &&
              offsetof(RLightning, material) == 0x660 && offsetof(RLightning, textures) == 0x6cc &&
              offsetof(RLightning, metricsInterval) == 0x6e8, "RLightning layout");

#define TheLightning (*(RLightning **)0x00200f4c)    // RLightning::Init's instance

#endif // DRIVING_RENDER_LIGHTNING_H_
