#ifndef DRIVING_DATA_CARP_H_
#define DRIVING_DATA_CARP_H_

// CARP: the driving engine's level and object data (EA Redwood Shores' format) - a world's map, articles
// (objects), instances, paths, AI splines, rules and triggers, as one data-group file (UData.h) per track or
// object. RCARPFile (RCARPFile.h) loads a file and resolves it; this header is what resolving does to the
// records, and the record types the engine reads straight out of the file.
//
// Resolving is two passes over the file's groups, breadth first (UGroup::ProcessBreadthFirst):
//   1. SymbolicResolver: every 'rs' record (a symbolic reference, its payload a name such as
//      "EAGL::<shape>" or "CARP::<<Shared>>::{Base}") has its payload replaced by what the symbol table
//      (SymbolTable.h) answers for the name - UData::AdoptData.
//   2. TagResolver: every record whose tag has a resolver in the resolver map (InitResolvers) is passed to it.
//      A resolver walks the record's elements and turns each TagReference field - a tag naming another record
//      - into that record's address, looked up first in the enclosing group, then in the file's 'Shar' group;
//      -1 names the enclosing group itself, 0 stays null.
//
// PathInfo is a keyed animation: channels of keys over time, linear or Bezier, which RPathEngine and the AI
// splines evaluate into matrices. Its float code is bit-exact to the original's.

#include "Tree.h"
#include "UData.h"

#include <stddef.h>
#include <stdint.h>

class USymbolTable;

namespace CARP {

// ---- references

// A reference in a record: a tag until resolved, then the address of the record's payload (or group) it names.
class TagReference {
public:
    uint32_t value;

    // Resolves in place (0x00117e50): 0 stays 0, -1 becomes `local` itself, a tag the payload of `local`'s
    // record of that tag, else of `shared`'s, else 0.
    TagReference *Construct(UGroup *shared, UGroup *local);
};

// ---- record types

// An article's description (tag 'Base' or 'pD', 100 bytes).
class BaseDesc {
public:
    TagReference model;             // the model's group
    uint32_t flags;
    uint32_t unknown08;
    TagReference physicsAttributes;
    float bboxMin[3];
    uint32_t bboxMinW;
    float bboxMax[3];
    uint32_t bboxMaxW;
    uint32_t unknown30;
    TagReference name;              // 'Name', set by the resolver before it resolves it
    uint32_t unknown38;
    uint32_t numCollisionPrims;
    float damageBoxMin[3];
    uint32_t damageBoxMinW;
    float damageBoxMax[3];
    uint32_t damageBoxMaxW;
    uint32_t collisionGeometry;

    // Which of the 16 damage zones a point (in the article's space) falls in (0x00118030); 0 if none matches.
    int CalcDamageZone(const float *point);
    // A zone's mask of the zones damage there spreads to (0x00118170).
    int GetZoneBits(int zone);
};
static_assert(sizeof(BaseDesc) == 100, "CARP::BaseDesc is 100 bytes");

// An instance of an article in the world (tags 'ni', 'ci' and 'Cams', 64 bytes).
class Instance {
public:
    float axisX[3];
    uint8_t flags;
    uint8_t procAnimType;
    uint16_t procAnimIndex;
    float axisY[3];
    TagReference articleDesc;       // the article's BaseDesc
    float axisZ[3];
    uint32_t unknown2c;
    float position[3];
    // bits 0-9, 10-19, 20-29: x, y, z in steps of 0.25 (or 16 with bit 30 set, for anything over 250); bit 31:
    // the three were given separately
    uint32_t packedDimensions;

    // Packs the dimensions (0x00118cc0), rounding each up to the step; `separate` false uses x for all three.
    void SetDimensions(bool separate, float x, float y, float z);
};
static_assert(sizeof(Instance) == 64, "CARP::Instance is 64 bytes");

// A path channel: keyCount keys and their times, at offsets from the PathInfo (20 bytes).
struct PathChannel {
    uint32_t unknown00;
    uint32_t timesOffset;   // float[keyCount], ascending
    uint32_t keysOffset;    // float[4] per key, or 48 bytes (in-control, value, out-control) for a spline
    uint32_t keyCount;
    uint32_t flags;         // PathChannelFlag
};
static_assert(sizeof(PathChannel) == 20, "a path channel is 20 bytes");

enum PathChannelFlag : uint32_t {
    kPathSpline = 0x01,     // Bezier between keys, else linear
    kPathRotation = 0x02,   // quaternions (slerped); a spline's keys are Euler angles turned into one
    kPathScalar = 0x04,     // one float per key, spread to all four components
    kPathStepped = 0x08,    // the key is floor(time)
};

class PathInfo {
public:
    uint8_t unknown00[0xc];
    int rotationChannel;    // -1 for none
    int positionChannel;    // -1 for none
    uint8_t unknown14[0x2c];
    PathChannel channels[1];   // as many as the path has

    // A channel's value at `time` (0x001190e0, 0x00119190): the key it falls after, starting the search at
    // `hint`, and the value in out[4].
    uint32_t EvaluateLinear(float time, uint32_t hint, PathChannel *channel, float *out);
    uint32_t EvaluateSpline(float time, uint32_t hint, PathChannel *channel, float *out);
    // The path's matrix at `time` (0x001192d0): the rotation channel's quaternion as the rotation, the position
    // channel's value as the translation (its w, the channel's fourth component, in *weight); a missing channel
    // leaves its part of the matrix as it was. *positionKey and *rotationKey are the search hints, updated.
    void EvaluateMatrix(float time, uint32_t *positionKey, uint32_t *rotationKey, float *matrix, float *weight);

    // How far `time` is from key `from` to key `to`, 0 to 1 (0x00118e30); 0 if the times do not increase. A
    // stepped channel answers floor(time). Unrounded, as the original leaves it on the x87 stack.
    double ScaleToUnitTime(float time, uint32_t from, uint32_t to, PathChannel *channel);
    // The key `time` falls after (0x00119010, Ghidra: FUN_00119010): the hint and its neighbours first, then a
    // binary search.
    uint32_t FindKey(float time, uint32_t hint, PathChannel *channel);
    // out = a + (b - a) * t, or the slerp of two quaternions (0x00118dd0).
    static void ComputeLinear(const float *a, const float *b, float *out, float t, bool slerp);
};

// std::lower_bound over key times (0x00118ed0, __lower_bound<float *, float, int>); the last argument is the
// unused distance-type tag.
float *LowerBound(float *first, float *last, const float *value, int *);

// An AI spline (tag 'AISp', 0x70 bytes): a placement matrix and the path it follows.
class AISpline {
public:
    float matrix[16];
    TagReference path;              // the PathInfo
    uint8_t unknown44[0x28];
    uint8_t disabled;               // non-zero: the spline is not applied
    uint8_t unknown6d[3];

    // The spline's transform (0x001193c0): its path's matrix at time 0, moved to the spline's placement by
    // RotateSplineAboutBase - or the identity matrix when it has no path or is disabled.
    void GetApplyTransform(float *out);
};
static_assert(sizeof(AISpline) == 0x70, "CARP::AISpline is 0x70 bytes");

// out = base * spline's rotation, translated so that the evaluated path's start keeps its place relative to the
// spline (0x00117ef0).
void RotateSplineAboutBase(const float *base, const float *evaluated, const float *spline, float *out);

// ---- the resolvers (one per record tag; record is the record, shared the file's 'Shar' group, parent the
// group the record is in)

void EventListResolver(UGroup *record, UGroup *shared, UGroup *parent);    // 'el', 0x00117df0 (Ghidra: ResolveSomething)
void BaseDescResolver(UGroup *record, UGroup *shared, UGroup *parent);     // 'Base', 'pD', 0x00118180
void AsResolver(UGroup *record, UGroup *shared, UGroup *parent);           // 'as', 0x00118220
void AnResolver(UGroup *record, UGroup *shared, UGroup *parent);           // 'an', 0x00118320
void AqResolver(UGroup *record, UGroup *shared, UGroup *parent);           // 'aq', 0x001183a0
void EffectResolver(UGroup *record, UGroup *shared, UGroup *parent);       // 'ef', 0x00118420
void DissolveInfoResolver(UGroup *record, UGroup *shared, UGroup *parent); // 'di', 0x001184b0
void ProcAnimResolver(UGroup *record, UGroup *shared, UGroup *parent);     // 'ps', 0x001185a0
void InstanceResolver(UGroup *record, UGroup *shared, UGroup *parent);     // 'ni', 'ci', 'Cams', 0x00118650
void TriggerResolver(UGroup *record, UGroup *shared, UGroup *parent);      // 'Trgr', 0x001186d0
void RuleResolver(UGroup *record, UGroup *shared, UGroup *parent);         // 'Rule', 0x00118750
void AIElementResolver(UGroup *record, UGroup *shared, UGroup *parent);    // 'AIEl', 0x00118830 (Ghidra merged it)
void AICommandResolver(UGroup *record, UGroup *shared, UGroup *parent);    // 'AICo', 0x001188c0
void AISplineResolver(UGroup *record, UGroup *shared, UGroup *parent);     // 'AISp', 0x00118980 (Ghidra merged it)
void WorldMapResolver(UGroup *record, UGroup *shared, UGroup *parent);     // 'Wmap', 0x00118a10
void WorldNodeResolver(UGroup *record, UGroup *shared, UGroup *parent);    // 'wn', 0x00118ac0

// The map of resolvers by tag (std::map<uint32_t, CarpResolverFn>, one, at 0x00243580).
class ResolverMap : public Tree {
public:
    ResolverMap *Construct();                                                 // 0x00119e40
    void Destruct();                                                          // 0x00119e80
    void EraseSubtree(TreeNode *node);                                        // 0x00118e90
    TreeNode** Find(TreeNode **result, const uint32_t *tag);                  // 0x00118f20
    TreeNode** InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value);  // 0x00119440
    TreeNode** EraseAt(TreeNode **result, TreeNode *where);                   // 0x00119620
    TreeNode** EraseRange(TreeNode **result, TreeNode *first, TreeNode *last);   // 0x001199b0
};

// Fills the resolver map, once (0x00119a70).
void InitResolvers();

// The two passes over a loaded file (0x00119d80); answers the 'Sect' record's dword at 0x7c, 0 without one.
uint32_t ResolveSymbolicReferences(UGroup *root, USymbolTable *symbols);

// Pass 2's dispatch (0x00118f90, Ghidra: FUN_00118f90): the record's resolver, if its tag has one.
void ResolveTag(UGroup *shared, UGroup *parent, UGroup *record);

// The processors ProcessBreadthFirst calls back (the game's vtables; slot 1 is ProcessData).
class SymbolicResolver {
public:
    void *vtable;
    USymbolTable *symbols;

    bool ProcessData(UGroup *parent, UData *record);   // 0x00118b40
};

class TagResolver {
public:
    void *vtable;
    UGroup *root;
    UGroup *shared;

    bool ProcessData(UGroup *parent, UGroup *record);  // 0x00118ff0
};

// A counted string as a C string, in one static buffer (0x00119ec0, Ghidra: FUN_00119ec0).
struct CountedString {
    const char *text;
    int length;

    char *CString();
};

}  // namespace CARP

#endif // DRIVING_DATA_CARP_H_
