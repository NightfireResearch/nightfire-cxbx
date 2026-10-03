#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#pragma fp_contract(off)

#include "Carp.h"
#include "SymbolTable.h"
#include "../Scheduler.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../../helpers.h"
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"

#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// CARP's record resolvers, the resolver map, the two resolving passes, and the record types' own code: damage
// zones, packed dimensions, path evaluation and AI splines. See Carp.h.
//
// Floats: the original's x87 code computes in double and rounds at each store to a float. A chain kept on the x87
// stack is written in double here, in the original's order; ScaleToUnitTime answers a double because the original
// leaves its quotient unrounded on the stack. The linear interpolation is the original's SSE, lane by lane.
// ---------------------------------------------------------------------------------------------------------------

// Other packages' functions not ported yet, called at their addresses.
#define ResolverMap_InsertOne ((TreeInsertResult *(__fastcall *)(CARP::ResolverMap *, int, TreeInsertResult *, const TreePair *))0x001198f0)
#define Crt_printf ((int (*)(const char *, ...))0x00132192)

#define Resolvers (*(CARP::ResolverMap *)0x00243580)
#define ResolversReady BOOL8_AT(0x00243560)
#define CarpVerbose BOOL8_AT(0x00243561)        // report references the symbol table cannot resolve
#define CountedStringBuffer ((char *)0x00243590)
#define IdentityMatrix ((float *)0x001d4c10)    // set up by a static initialiser

constexpr uint32_t kSymbolicResolverVtable = 0x001a20cc;
constexpr uint32_t kTagResolverVtable = 0x001a20c0;

// Record and group tags.
constexpr uint32_t kTagSymbolic = 0x73722020;   // 'rs  '
constexpr uint32_t kTagShared = 0x53686172;     // 'Shar'
constexpr uint32_t kTagSect = 0x53656374;       // 'Sect'
constexpr uint32_t kTagMap = 0x4d617020;        // 'Map '
constexpr uint32_t kTagCollision = 0x43446174;  // 'CDat'
constexpr uint32_t kTagRoads = 0x524e6770;      // 'RNgp'
constexpr uint32_t kTagArticle = 0x41727469;    // 'Arti'
constexpr uint32_t kTagName = 0x4e616d65;       // 'Name'

using namespace CARP;

// ---- references

static UData *DataEnd(UGroup *group) {
    return group->GetArray() + (group->GroupCount() + group->count);
}

static uint32_t Address(const void *p) {
    return uint32_t(uintptr_t(p));
}

// FUNC_AT(0x00117e50)
CARP::TagReference* CARP::TagReference::Construct(UGroup *shared, UGroup *local) {
    uint32_t tag = value;
    if (tag == 0)
        return this;
    if (tag == 0xffffffff) {
        value = Address(local);
        return this;
    }
    UData *record = local->DataLocateTag(tag);
    if (record == DataEnd(local)) {
        record = shared->DataLocateTag(tag);
        if (record == DataEnd(shared)) {
            value = 0;
            return this;
        }
    }
    value = Address(record->Data());
    return this;
}

// The resolvers construct each reference in place; the original's placement new skips a null address.
static void Resolve(TagReference *reference, UGroup *shared, UGroup *parent) {
    if (reference != NULL)
        reference->Construct(shared, parent);
}

// ---- the records the resolvers walk (only their references are known)

namespace {

// What RegisterEvent::ResolveEvent answers for an event: the function that resolves its data.
typedef void (*EventResolveFn)(void *data, UGroup *shared, UGroup *parent);

struct EventListHeader {
    uint32_t count;
    uint32_t unknown04[3];
};

struct EventEntry {
    uint32_t event;
    uint32_t unknown04;
    int32_t dataOffset;   // from the entry
    uint32_t unknown0c;
};

struct AsRecord {
    TagReference references[4];
    uint8_t unknown10[0xc];
    uint8_t pairCount;
    uint8_t unknown1d[3];
    struct {
        uint32_t unknown00;
        TagReference reference;
    } pairs[1];
};

struct AnRecord {
    TagReference reference;
    uint32_t unknown04[3];
};

struct AqRecord {
    uint8_t unknown00[0x1c];
    TagReference reference;
};

enum EffectType : uint8_t {
    kEffectReferencing = 6,   // the one type with a reference
};

struct EffectRecord {
    uint8_t unknown00[0x14];
    uint8_t type;
    uint8_t unknown15[3];
    TagReference reference;
    uint8_t unknown1c[0x24];
};

struct DissolveInfo {
    uint8_t unknown00[0xc];
    TagReference references[6];
};

enum ProcAnimType : uint8_t {
    kProcAnimReferencing = 2,
};

struct ProcAnimRecord {
    uint8_t type;
    uint8_t unknown01[0xb];
    TagReference references[2];
    uint8_t unknown14[0xc];
};

struct TriggerRecord {
    uint8_t unknown00[0x18];
    TagReference reference;
    uint8_t unknown1c[0x24];
};

struct RuleRecord {
    uint32_t type;
    uint8_t unknown04[8];
    TagReference reference;
    uint32_t unknown10;
    TagReference argument;   // a reference for the types RuleHasArgument names
    uint8_t unknown18[8];
};

struct AIElementRecord {
    uint8_t unknown00[0xf8];
    TagReference reference;
    uint32_t unknownFc;
};

struct AICommandRecord {
    uint8_t unknown00[0x50];
    TagReference first;
    uint8_t unknown54[0x2c];
    TagReference second;
    uint8_t unknown84[0x1c];
};

struct WorldMap {
    TagReference references[4];
};

struct WorldNodeEntry {
    TagReference reference;
    uint32_t unknown04[3];
};

static_assert(sizeof(EffectRecord) == 0x40 && sizeof(ProcAnimRecord) == 0x20 && sizeof(TriggerRecord) == 0x40 &&
              sizeof(RuleRecord) == 0x20 && sizeof(AIElementRecord) == 0x100 && sizeof(AICommandRecord) == 0xa0 &&
              sizeof(AqRecord) == 0x20 && sizeof(AnRecord) == 0x10 && sizeof(WorldNodeEntry) == 0x10,
              "the records' strides");
static_assert(offsetof(AsRecord, pairs) == 0x20, "'as' pairs at 0x20");

}  // namespace

template <class T> static T *Payload(UGroup *record) {
    return reinterpret_cast<T *>(record->Data());
}

// ---- the resolvers

// FUNC_AT(0x00117df0)
void CARP::EventListResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    EventListHeader *header = Payload<EventListHeader>(record);
    EventEntry *entry = reinterpret_cast<EventEntry *>(header + 1);
    for (uint32_t n = header->count; n != 0; n--, entry++) {
        EventResolveFn resolve = reinterpret_cast<EventResolveFn>(RegisterEvent::ResolveEvent(int(entry->event)));
        if (resolve != NULL)
            resolve(reinterpret_cast<uint8_t *>(entry) + entry->dataOffset, shared, parent);
    }
}

// FUNC_AT(0x00118180)
void CARP::BaseDescResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    BaseDesc *desc = Payload<BaseDesc>(record);
    desc->name.value = kTagName;
    desc->model.Construct(shared, parent);
    Resolve(&desc->name, shared, parent);
    Resolve(&desc->physicsAttributes, shared, parent);
}

// FUNC_AT(0x00118220)
void CARP::AsResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AsRecord *as = Payload<AsRecord>(record);
    for (int i = 0; i < 4; i++)
        Resolve(&as->references[i], shared, parent);
    for (uint32_t i = 0; i < as->pairCount; i++)
        Resolve(&as->pairs[i].reference, shared, parent);
}

// FUNC_AT(0x00118320)
void CARP::AnResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AnRecord *an = Payload<AnRecord>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&an[i].reference, shared, parent);
}

// FUNC_AT(0x001183a0)
void CARP::AqResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AqRecord *aq = Payload<AqRecord>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&aq[i].reference, shared, parent);
}

// FUNC_AT(0x00118420)
void CARP::EffectResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    EffectRecord *effect = Payload<EffectRecord>(record);
    for (uint32_t i = 0; i < record->count; i++)
        if (effect[i].type == kEffectReferencing)
            Resolve(&effect[i].reference, shared, parent);
}

// FUNC_AT(0x001184b0)
void CARP::DissolveInfoResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    DissolveInfo *dissolve = Payload<DissolveInfo>(record);
    for (int i = 0; i < 6; i++)
        Resolve(&dissolve->references[i], shared, parent);
}

// FUNC_AT(0x001185a0)
void CARP::ProcAnimResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    ProcAnimRecord *anim = Payload<ProcAnimRecord>(record);
    for (uint32_t i = 0; i < record->count; i++) {
        if (anim[i].type == kProcAnimReferencing) {
            Resolve(&anim[i].references[0], shared, parent);
            Resolve(&anim[i].references[1], shared, parent);
        }
    }
}

// FUNC_AT(0x00118650)
void CARP::InstanceResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    Instance *instance = Payload<Instance>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&instance[i].articleDesc, shared, parent);
}

// FUNC_AT(0x001186d0)
void CARP::TriggerResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    TriggerRecord *trigger = Payload<TriggerRecord>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&trigger[i].reference, shared, parent);
}

// The rule types whose argument is a reference: the original's switch over type - 2, whose byte table at
// 0x00118820 reads 00 00 00 01 01 00 00 01 00 01 00 (0: resolve, 1: skip).
static bool RuleHasArgument(uint32_t type) {
    switch (type) {
    case 2: case 3: case 4: case 7: case 8: case 10: case 12:
        return true;
    default:
        return false;
    }
}

// FUNC_AT(0x00118750)
void CARP::RuleResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    RuleRecord *rule = Payload<RuleRecord>(record);
    for (uint32_t i = 0; i < record->count; i++) {
        Resolve(&rule[i].reference, shared, parent);
        if (RuleHasArgument(rule[i].type))
            Resolve(&rule[i].argument, shared, parent);
    }
}

// FUNC_AT(0x00118830)
void CARP::AIElementResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AIElementRecord *element = Payload<AIElementRecord>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&element[i].reference, shared, parent);
}

// FUNC_AT(0x001188c0)
void CARP::AICommandResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AICommandRecord *command = Payload<AICommandRecord>(record);
    for (uint32_t i = 0; i < record->count; i++) {
        Resolve(&command[i].first, shared, parent);
        Resolve(&command[i].second, shared, parent);
    }
}

// FUNC_AT(0x00118980)
void CARP::AISplineResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    AISpline *spline = Payload<AISpline>(record);
    for (uint32_t i = 0; i < record->count; i++)
        Resolve(&spline[i].path, shared, parent);
}

// FUNC_AT(0x00118a10)
void CARP::WorldMapResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    WorldMap *map = Payload<WorldMap>(record);
    for (int i = 0; i < 4; i++)
        Resolve(&map->references[i], shared, parent);
}

// FUNC_AT(0x00118ac0)
void CARP::WorldNodeResolver(UGroup *record, UGroup *shared, UGroup *parent) {
    WorldNodeEntry *node = Payload<WorldNodeEntry>(record);
    for (int i = 0; i < 4; i++)
        Resolve(&node[i].reference, shared, parent);
}

// ---- the resolver map

// FUNC_AT(0x00119e40)
CARP::ResolverMap* CARP::ResolverMap::Construct() {
    allocator = 0;   // copied from an uninitialised local in the original; nothing reads it
    Init();
    return this;
}

// FUNC_AT(0x00119e80)
void CARP::ResolverMap::Destruct() {
    Destroy();
}

// FUNC_AT(0x00118e90)
void CARP::ResolverMap::EraseSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x00118f20)
TreeNode** CARP::ResolverMap::Find(TreeNode **result, const uint32_t *tag) {
    TreeNode *bound = head;
    for (TreeNode *node = head->parent; !node->isNil;) {
        if (node->tag < *tag) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    *result = (bound == head || *tag < bound->tag) ? head : bound;
    return result;
}

// FUNC_AT(0x00119440)
TreeNode** CARP::ResolverMap::InsertAt(TreeNode **result, bool addLeft, TreeNode *where, const TreePair *value) {
    return Tree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00119620)
TreeNode** CARP::ResolverMap::EraseAt(TreeNode **result, TreeNode *where) {
    return Tree::EraseAt(result, where);
}

// FUNC_AT(0x001199b0)
TreeNode** CARP::ResolverMap::EraseRange(TreeNode **result, TreeNode *first, TreeNode *last) {
    return Tree::EraseRange(result, first, last);
}

// The table InitResolvers inserts, in its order. The functions are entered at their original addresses (each
// jumps to the port above), so the map holds what the original's holds; 'MSet' is another package's, and 'AISp'
// is inserted twice.
struct ResolverEntry {
    uint32_t tag;
    uint32_t function;
};

static const ResolverEntry kResolvers[] = {
    { 0x576d6170, 0x00118a10 },   // 'Wmap' WorldMapResolver
    { 0x776e2020, 0x00118ac0 },   // 'wn'   WorldNodeResolver
    { 0x696e2020, 0x00118650 },   // 'ni'   InstanceResolver
    { 0x43616d73, 0x00118650 },   // 'Cams' InstanceResolver
    { 0x64692020, 0x001184b0 },   // 'di'   DissolveInfoResolver
    { 0x70732020, 0x001185a0 },   // 'ps'   ProcAnimResolver
    { 0x63692020, 0x00118650 },   // 'ci'   InstanceResolver
    { 0x54726772, 0x001186d0 },   // 'Trgr' TriggerResolver
    { 0x52756c65, 0x00118750 },   // 'Rule' RuleResolver
    { 0x4149456c, 0x00118830 },   // 'AIEl' AIElementResolver
    { 0x41495370, 0x00118980 },   // 'AISp' AISplineResolver
    { 0x4d536574, 0x000d3580 },   // 'MSet'
    { 0x4149436f, 0x001188c0 },   // 'AICo' AICommandResolver
    { 0x41495370, 0x00118980 },   // 'AISp' again
    { 0x42617365, 0x00118180 },   // 'Base' BaseDescResolver
    { 0x70442020, 0x00118180 },   // 'pD'   BaseDescResolver
    { 0x61732020, 0x00118220 },   // 'as'   AsResolver
    { 0x616e2020, 0x00118320 },   // 'an'   AnResolver
    { 0x61712020, 0x001183a0 },   // 'aq'   AqResolver
    { 0x656c2020, 0x00117df0 },   // 'el'   EventListResolver
    { 0x65662020, 0x00118420 },   // 'ef'   EffectResolver
};

// FUNC_AT(0x00119a70)
void CARP::InitResolvers() {
    if (ResolversReady)
        return;
    for (const ResolverEntry &entry : kResolvers) {
        TreePair pair;
        pair.tag = entry.tag;
        pair.resolver = reinterpret_cast<CarpResolverFn>(uintptr_t(entry.function));
        TreeInsertResult result;
        ResolverMap_InsertOne(&Resolvers, 0, &result, &pair);
    }
    ResolversReady = 1;
}

// FUNC_AT(0x00118f90)
void CARP::ResolveTag(UGroup *shared, UGroup *parent, UGroup *record) {
    uint32_t tag = record->MatchTag();
    TreeNode *node;
    Resolvers.Find(&node, &tag);
    if (node != Resolvers.head)
        node->resolver(record, shared, parent);
}

// FUNC_AT(0x00118ff0)
bool CARP::TagResolver::ProcessData(UGroup *parent, UGroup *record) {
    ResolveTag(shared, parent, record);
    return true;
}

// FUNC_AT(0x00118b40)
bool CARP::SymbolicResolver::ProcessData(UGroup *parent, UData *record) {
    if (record->MatchTag() != kTagSymbolic)
        return true;
    int size = 0;
    void *found = symbols->NameLookup(reinterpret_cast<char *>(record->Data()), &size);
    if (CarpVerbose && found == NULL) {
        const char *where;
        uint32_t tag = parent->MatchTag();
        if (tag == kTagMap) {
            where = "<<Map>>";
        } else if (tag == kTagShared) {
            where = "<<Shared>>";
        } else if (tag == kTagCollision) {
            where = "<<Collision>>";
        } else if (tag == kTagRoads) {
            where = "<<RoadNetwork>>";
        } else if (tag == kTagArticle) {
            UData *name = parent->DataLocateTag(kTagName);
            if (name != parent->DataEnd())
                where = reinterpret_cast<char *>(name->Data());
            else
                where = "<<Unnamed Article>>";
        } else {
            where = "<<Unknown Group>>";
        }
        Crt_printf("CARP FAILURE resolving [%s] for '%s'\n", reinterpret_cast<char *>(record->Data()), where);
    }
    record->AdoptData(found, size, 0, false);
    return true;
}

// FUNC_AT(0x00119d80)
uint32_t CARP::ResolveSymbolicReferences(UGroup *root, USymbolTable *symbols) {
    InitResolvers();
    SymbolicResolver symbolic;
    TagResolver tags;
    symbolic.vtable = reinterpret_cast<void *>(uintptr_t(kSymbolicResolverVtable));
    symbolic.symbols = symbols;
    tags.vtable = reinterpret_cast<void *>(uintptr_t(kTagResolverVtable));
    tags.root = root;
    tags.shared = root->GroupLocateTag(kTagShared);
    if (tags.shared == tags.root->GetArray() + tags.root->GroupCount())
        tags.shared = tags.root;   // no 'Shar' group: the file is its own
    root->ProcessBreadthFirst(reinterpret_cast<UGroup::Processor *>(&symbolic));
    root->ProcessBreadthFirst(reinterpret_cast<UGroup::Processor *>(&tags));
    UData *sect = root->DataLocateTag(kTagSect);
    if (sect == DataEnd(root))
        return 0;
    return reinterpret_cast<uint32_t *>(sect->Data())[0x7c / 4];
}

// FUNC_AT(0x00119ec0)
char* CARP::CountedString::CString() {
    strncpy(CountedStringBuffer, text, length);
    CountedStringBuffer[length] = 0;
    return CountedStringBuffer;
}

// ---- damage zones

// Where a point is against the damage box, axis by axis (x and z mirrored first): each zone in the table below
// is one combination.
enum DamageZoneBits : uint32_t {
    kZoneAbove = 0x001,       // y above the box
    kZoneBelow = 0x002,       // y below it
    kZoneXLow = 0x004,        // x in the box's low half (or below it)
    kZoneXHigh = 0x008,       // x in the high half (or above it)
    kZoneXOutLow = 0x010,     // x below the box
    kZoneXOutHigh = 0x020,    // x above it
    kZoneZLow = 0x040,
    kZoneZHigh = 0x080,
    kZoneZOutLow = 0x100,
    kZoneZOutHigh = 0x200,
    kZoneXInside = 0x400,     // x inside, neither above nor below: replaces the x halves
};

// The 16 zones: the combination of bits each stands for, and the zones damage there spreads to (the game's table
// at 0x001a2080).
struct DamageZone {
    uint16_t position;
    uint16_t spread;
};

static const DamageZone kDamageZones[16] = {
    { 0x0168, 0x4602 }, { 0x0440, 0x4c05 }, { 0x0154, 0x480a }, { 0x0054, 0xd816 },
    { 0x0094, 0xd868 }, { 0x0294, 0x9050 }, { 0x0480, 0xb0a0 }, { 0x02a8, 0xa140 },
    { 0x00a8, 0xe6c0 }, { 0x0068, 0xe503 }, { 0x0049, 0x2a03 }, { 0x0045, 0x140e },
    { 0x0085, 0x2870 }, { 0x0089, 0x15c0 }, { 0x0042, 0x820f }, { 0x0082, 0x41f0 },
};

// FUNC_AT(0x00118030)
int CARP::BaseDesc::CalcDamageZone(const float *point) {
    float mirror[4] = { -1.0f, 1.0f, -1.0f, 0.0f };   // the original leaves w unset; v4multxyz ignores it
    float p[4];
    VU0_v4multxyz(point, mirror, p);

    uint32_t bits;
    if (p[0] < damageBoxMin[0])
        bits = kZoneXOutLow | kZoneXLow;
    else if ((double(damageBoxMax[0]) + damageBoxMin[0]) * 0.5 > p[0])
        bits = kZoneXLow;
    else if (p[0] > damageBoxMax[0])
        bits = kZoneXOutHigh | kZoneXHigh;
    else
        bits = kZoneXHigh;

    if (p[2] < damageBoxMin[2])
        bits |= kZoneZOutLow | kZoneZLow;
    else if ((double(damageBoxMax[2]) + damageBoxMin[2]) * 0.5 > p[2])
        bits |= kZoneZLow;
    else if (!(p[2] > damageBoxMax[2]))
        bits |= kZoneZHigh;
    else
        bits |= kZoneZOutHigh | kZoneZHigh;

    if (p[1] < damageBoxMin[1])
        bits = (bits & 0xfffff8c3) | kZoneBelow;   // below: only the z halves stay
    else if (p[1] > damageBoxMax[1])
        bits = (bits & 0xfffffccf) | kZoneAbove;   // above: x and z outside dropped, the halves stay

    if (!(bits & (kZoneAbove | kZoneBelow)) && (bits & (kZoneXLow | kZoneXHigh)) &&
        !(bits & (kZoneXOutLow | kZoneXOutHigh)))
        bits = (bits & 0xfffffcf3) | kZoneXInside;   // the x halves and z outside dropped

    for (int zone = 0; zone < 16; zone++)
        if (kDamageZones[zone].position == bits)
            return zone;
    return 0;
}

// FUNC_AT(0x00118170)
int CARP::BaseDesc::GetZoneBits(int zone) {
    return kDamageZones[zone].spread;
}

// ---- packed dimensions

// FUNC_AT(0x00118cc0)
void CARP::Instance::SetDimensions(bool separate, float x, float y, float z) {
    static const float kStep[2] = { 0.25f, 16.0f };   // fine, and coarse for anything over 250
    uint32_t packed = (packedDimensions & 0x7fffffff) | uint32_t(separate) << 31;
    packedDimensions = packed;
    if (!separate) {
        y = x;
        z = x;
    }
    uint32_t coarse = (x > 250.0f || z > 250.0f || y > 250.0f) ? 1 : 0;
    packed = (packed & ~0x40000000u) | coarse << 30;
    packedDimensions = packed;

    double scale = 1.0 / kStep[packed >> 30 & 1];
    float scaleStored = float(scale);   // exact: 4 or 1/16
    uint32_t steps = uint32_t(Ftol(ceil(scale * x)));
    packed = (packed & ~0x000003ffu) | (steps & 0x000003ff);
    packedDimensions = packed;
    steps = uint32_t(Ftol(ceil(double(scaleStored) * z)));
    packed = (packed & ~0x3ff00000u) | (steps << 20 & 0x3ff00000);
    packedDimensions = packed;
    steps = uint32_t(Ftol(ceil(double(scaleStored) * y)));
    packed = (packed & ~0x000ffc00u) | (steps << 10 & 0x000ffc00);
    packedDimensions = packed;
}

// ---- paths

static float *Times(PathInfo *path, PathChannel *channel) {
    return reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(path) + channel->timesOffset);
}

static uint8_t *Keys(PathInfo *path, PathChannel *channel) {
    return reinterpret_cast<uint8_t *>(path) + channel->keysOffset;
}

// FUNC_AT(0x00118dd0)
void CARP::PathInfo::ComputeLinear(const float *a, const float *b, float *out, float t, bool slerp) {
    if (slerp) {
        VU0_fastqslerp(a, b, out, t);
        return;
    }
    for (int i = 0; i < 4; i++)   // the original's subps, mulps, addps
        out[i] = t * (b[i] - a[i]) + a[i];
}

// FUNC_AT(0x00118e30)
double CARP::PathInfo::ScaleToUnitTime(float time, uint32_t from, uint32_t to, PathChannel *channel) {
    if (channel->flags & kPathStepped)
        return floor(double(time));
    float *times = Times(this, channel);
    float start = times[from];
    float end = times[to];
    if (start < end)
        return (double(time) - start) / (double(end) - start);
    return 0.0;
}

// FUNC_AT(0x00118ed0)
float* CARP::LowerBound(float *first, float *last, const float *value, int *) {
    int n = int(last - first);
    while (n > 0) {
        int half = n / 2;
        if (first[half] < *value) {
            first = first + half + 1;
            n = n - half - 1;
        } else {
            n = half;
        }
    }
    return first;
}

// FUNC_AT(0x00119010)
uint32_t CARP::PathInfo::FindKey(float time, uint32_t hint, PathChannel *channel) {
    if (channel->flags & kPathStepped)
        return uint32_t(Ftol(floor(double(time))));
    float *times = Times(this, channel);
    uint32_t count = channel->keyCount;
    uint32_t last = count - 1;
    uint32_t key = hint;
    if (key <= last) {
        // the hint, the key after it, the key before it
        if (time >= times[key] && time < times[key + 1])
            return key;
        key++;
        if (key < last && time >= times[key] && time < times[key + 1])
            return key;
        key -= 2;
        if (key < last && time >= times[key] && time < times[key + 1])
            return key;
    }
    uint32_t index = uint32_t(LowerBound(times, times + count, &time, NULL) - times);
    if (index > 0) {
        index--;
        if (index >= count)
            index = last;
    }
    return index;
}

// FUNC_AT(0x001190e0)
uint32_t CARP::PathInfo::EvaluateLinear(float time, uint32_t hint, PathChannel *channel, float *out) {
    uint32_t key = FindKey(time, hint, channel);
    uint32_t next = key + 1;
    if (!(key < channel->keyCount - 1))
        next = key;
    float t = float(ScaleToUnitTime(time, key, next, channel));
    if (channel->flags & kPathScalar) {
        float *keys = reinterpret_cast<float *>(Keys(this, channel));
        float value = float((double(keys[next]) - keys[key]) * t + keys[key]);
        out[3] = value;
        out[2] = value;
        out[1] = value;
        out[0] = value;
        return key;
    }
    float(*keys)[4] = reinterpret_cast<float(*)[4]>(Keys(this, channel));
    ComputeLinear(keys[key], keys[next], out, t, (channel->flags >> 1) & 1);
    return key;
}

// A spline key: the value and the Bezier control points either side of it.
struct SplineKey {
    float inControl[4];
    float value[4];
    float outControl[4];
};

// The cubic Bezier basis (the game's at 0x001a2160).
alignas(16) static const float kBezierBasis[16] = {
    -1.0f, 3.0f, -3.0f, 1.0f,
    3.0f, -6.0f, 3.0f, 0.0f,
    -3.0f, 3.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
};

// FUNC_AT(0x00119190)
uint32_t CARP::PathInfo::EvaluateSpline(float time, uint32_t hint, PathChannel *channel, float *out) {
    uint32_t key = FindKey(time, hint, channel);
    uint32_t next = key < channel->keyCount - 1 ? key + 1 : channel->keyCount - 1;
    float t = float(ScaleToUnitTime(time, key, next, channel));

    SplineKey *keys = reinterpret_cast<SplineKey *>(Keys(this, channel));
    alignas(16) float geometry[16];
    VU0_v4copy(keys[key].value, &geometry[0]);
    VU0_v4copy(keys[key].outControl, &geometry[4]);
    VU0_v4copy(keys[next].inControl, &geometry[8]);
    VU0_v4copy(keys[next].value, &geometry[12]);
    VU0_MATRIX4_mult(geometry, kBezierBasis, geometry);

    alignas(16) float powers[4];
    powers[0] = float(double(t) * (double(t) * t));   // t * t is exact in double, t cubed rounded once there
    powers[1] = t * t;
    powers[2] = t;
    powers[3] = 1.0f;
    VU0_MATRIX4_vect4mult(powers, geometry, out);
    if (channel->flags & kPathRotation)
        VU0_EulerToQuat(out, out);
    return key;
}

// FUNC_AT(0x001192d0)
void CARP::PathInfo::EvaluateMatrix(float time, uint32_t *positionKey, uint32_t *rotationKey, float *matrix,
                                    float *weight) {
    float *translation = matrix + 12;
    alignas(16) float position[4];
    if (positionChannel != -1) {
        PathChannel *channel = &channels[positionChannel];
        if (channel->flags & kPathSpline)
            *positionKey = EvaluateSpline(time, *positionKey, channel, position);
        else
            *positionKey = EvaluateLinear(time, *positionKey, channel, position);
        *weight = QuietNaN(position[3]);   // moved through the x87
        position[3] = 1.0f;
    } else {
        *positionKey = 0;
        VU0_v4copy(translation, position);
        *weight = 0.0f;
    }
    if (rotationChannel != -1) {
        PathChannel *channel = &channels[rotationChannel];
        alignas(16) float rotation[4];
        if (channel->flags & kPathSpline)
            *rotationKey = EvaluateSpline(time, *rotationKey, channel, rotation);
        else
            *rotationKey = EvaluateLinear(time, *rotationKey, channel, rotation);
        VU0_quattom4(matrix, rotation);
    } else {
        *rotationKey = 0;
    }
    VU0_v4copy(position, translation);
}

// ---- AI splines

// FUNC_AT(0x00117ef0)
void CARP::RotateSplineAboutBase(const float *base, const float *evaluated, const float *spline, float *out) {
    // The positions are copied xyz only; the original's w lanes are whatever its stack held, and nothing reads
    // them back.
    alignas(16) float basePosition[4] = { base[12], base[13], base[14], 0.0f };
    alignas(16) float evaluatedPosition[4] = { evaluated[12], evaluated[13], evaluated[14], 0.0f };
    alignas(16) float splinePosition[4] = { spline[12], spline[13], spline[14], 0.0f };
    alignas(16) float rotation[16];
    memcpy(rotation, spline, sizeof(rotation));
    rotation[12] = 0.0f;
    rotation[13] = 0.0f;
    rotation[14] = 0.0f;

    alignas(16) float offset[4] = {};
    alignas(16) float toSpline[4] = {};
    VU0_v4sub(basePosition, evaluatedPosition, offset);
    MATRIX4_TransformPoint(rotation, offset, offset);
    VU0_v3add(offset, evaluatedPosition, offset);
    VU0_v4sub(splinePosition, evaluatedPosition, toSpline);
    VU0_v3add(offset, toSpline, offset);

    alignas(16) float baseCopy[16];
    memcpy(baseCopy, base, sizeof(baseCopy));
    VU0_MATRIX4_mult(out, baseCopy, rotation);
    out[12] = offset[0];
    out[13] = offset[1];
    out[14] = offset[2];
}

// FUNC_AT(0x001193c0)
void CARP::AISpline::GetApplyTransform(float *out) {
    PathInfo *pathInfo = reinterpret_cast<PathInfo *>(uintptr_t(path.value));
    if (disabled == 0 && pathInfo != NULL) {
        uint32_t positionKey = 0;
        uint32_t rotationKey = 0;
        alignas(16) float evaluated[16];
        float weight;
        pathInfo->EvaluateMatrix(0.0f, &positionKey, &rotationKey, evaluated, &weight);
        RotateSplineAboutBase(IdentityMatrix, evaluated, matrix, out);
        return;
    }
    memcpy(out, IdentityMatrix, 16 * sizeof(float));
}

