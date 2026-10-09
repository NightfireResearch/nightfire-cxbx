#include "StateManager.h"

#include "RenderTree.h"
#include "../eagl/EaglGlobals.h"          // EaglMalloc, EaglFree
#include "../engine/UMemory.hpp"
#include "../platform/RealPrint.h"        // MEM_copy, MEM_fill
#include "../world/SoundMap.h"            // BuyMapHead

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// RStateManager, its state-string handlers and the set of states in use (0x000914f0..0x00093350), ported from the
// listing.
//
// Each handler's table of words was a function-local static the original filled on first use; nothing else reads
// them, so they are constant tables of ours here (the address each had is in its XBE_GLOBAL tag). The handlers call
// EAGL's GeoPrimState setters, the Xbox extension's through the same object.
// ---------------------------------------------------------------------------------------------------------------

// ---- originals called by address

#define CRT_isalnum ((int (*)(int))0x001339ef)                                       // the C runtime's (locale)
#define CRT_atof ((double (*)(const char *))0x00133e84)
#define CRT_sscanf ((int (*)(const char *text, const char *format, ...))0x00133234)

// ---- vtables

static const USingletonVtable *const kStateManagerVtable = (const USingletonVtable *)0x00192130;
static const USingletonVtable *const kSingletonVtable = (const USingletonVtable *)0x0018beb0;   // USingleton's
static const void *const kNamespaceBaseVtable = (const void *)0x0018f8f8;    // the namespace base class's
static const void *const kStateNamespaceVtable = (const void *)0x0019212c;

typedef void *(__fastcall *DeletingDestructor)(void *object, int, unsigned flags);

// ---- the default state's settings, and the values the handlers use

enum PrimitiveType : uint32_t {
    kPrimTriList = 5,
};

enum : uint32_t {
    kShadingGouraudSpecular = 2,
    kTransparencyOpaque = 0,
    kTransparencyAlpha = 1,
    kAlphaBlendOff = 0,
    kAlphaBlendBlend = 1,
    kCullDirectionCw = 0x900,             // GL_CW
    kDefaultAlphaCompare = 0xa0,
    kGlareAlphaCompare = 0x80,
};

static EAGL::GeoPrimStateExtension *Extension(EAGL::GeoPrimState *state) {
    return static_cast<EAGL::GeoPrimStateExtension *>(state);
}

// =============================================================================================================
// The words
// =============================================================================================================

const StateKeyword *FindStateKeyword(const char *name, const StateKeyword *table) {
    return FindStateEntry(name, table);
}

// AUTOLTCG
__declspec(naked) void FUN_000914f0() {
    __asm {
        push edx
        push dword ptr [esp + 8]
        push edx
        call FindStateKeyword
        add esp, 8
        pop edx
        ret
    }
}

// The tables (each ends with a null name)

// XBE_GLOBAL(0x001f2660, 0x58) readonly
static const StateKeyword kPrimTypes[] = {
    {"pointlist", 1}, {"linelist", 2}, {"lineloop", 3}, {"trilist", 5}, {"tristrip", 6}, {"trifan", 7},
    {"quadlist", 8}, {"quadstrip", 9}, {"polygon", 10}, {"sprite", 0xffffffff}, {NULL, 0},
};

// "cull", "texture" and "alphatest" have a table each, all three the same
// XBE_GLOBAL(0x001f26bc, 0x18) readonly
static const StateKeyword kCullSwitch[] = { {"off", 0}, {"on", 1}, {NULL, 0} };
// XBE_GLOBAL(0x001f26d8, 0x18) readonly
static const StateKeyword kTextureSwitch[] = { {"off", 0}, {"on", 1}, {NULL, 0} };
// XBE_GLOBAL(0x001f2814, 0x18) readonly
static const StateKeyword kAlphaTestSwitch[] = { {"off", 0}, {"on", 1}, {NULL, 0} };

// XBE_GLOBAL(0x001f26f4, 0x18) readonly
static const StateKeyword kTexCoordTypes[] = { {"stq", 0xffffffff}, {"uv", 0xffffffff}, {NULL, 0} };

// The comparisons, in OpenGL's numbering (GL_NEVER 0x200 ... GL_ALWAYS 0x207); the depth and alpha tests' tables
// XBE_GLOBAL(0x001f2710, 0x48) readonly
static const StateKeyword kDepthTests[] = {
    {"never", 0x200}, {"always", 0x207}, {"notequal", 0x205}, {"less", 0x201}, {"lequal", 0x203},
    {"equal", 0x202}, {"gequal", 0x206}, {"greater", 0x204}, {NULL, 0},
};
// XBE_GLOBAL(0x001f2760, 0x48) readonly
static const StateKeyword kAlphaTests[] = {
    {"never", 0x200}, {"always", 0x207}, {"notequal", 0x205}, {"less", 0x201}, {"lequal", 0x203},
    {"equal", 0x202}, {"gequal", 0x206}, {"greater", 0x204}, {NULL, 0},
};

// XBE_GLOBAL(0x001f27ac, 0x20) readonly
static const StateKeyword kTransparencies[] = { {"opaque", 0}, {"alpha", 1}, {"chroma", 2}, {NULL, 0} };

// XBE_GLOBAL(0x001f27d0, 0x40) readonly
static const StateKeyword kAlphaBlendModes[] = {
    {"off", 0}, {"blend", 1}, {"add", 2}, {"atten", 3}, {"mod", 4}, {"sub", 5}, {"custom", 6}, {NULL, 0},
};

// noA: no alpha test; deepA: compare value 1; 1bitA: compare value 0xa0
// XBE_GLOBAL(0x001f2830, 0x20) readonly
static const StateKeyword kTexAlphas[] = { {"noA", 0}, {"deepA", 1}, {"1bitA", 0xa0}, {NULL, 0} };

// GL_POINT, GL_LINE, GL_FILL
// XBE_GLOBAL(0x001f2854, 0x20) readonly
static const StateKeyword kFillModes[] = { {"point", 0x1b00}, {"wireframe", 0x1b01}, {"solid", 0x1b02}, {NULL, 0} };

// The blend operations (GL_FUNC_ADD 0x8006 ...). The original also fills a table of blend factors ("Zero", "One",
// "SrcCol" ... "InvConstA", at 0x001f2878) in the same function, but never looks anything up in it.
// XBE_GLOBAL(0x001f28f8, 0x40) readonly
static const StateKeyword kBlendOperations[] = {
    {"add", 0x8006}, {"sub", 0x800a}, {"revsub", 0x800b}, {"min", 0x8007}, {"max", 0x8008},
    {"adds", 0xf006}, {"revsubs", 0xf005}, {NULL, 0},
};

// The words "modify" takes; the game points the ones that do nothing at its shared Generic_FuncReturnsFalse.
static bool IgnoreStateTag(const char *, const char *, EAGL::GeoPrimState *, int) {
    return false;
}

// XBE_GLOBAL(0x001f2940, 0x50) readonly
static const StateTag kModifyTags[] = {
    {"chromemap", IgnoreStateTag}, {"glossmap", IgnoreStateTag}, {"envmap", IgnoreStateTag},
    {"specmap", IgnoreStateTag}, {"watermap", IgnoreStateTag}, {"glassdmgmap", IgnoreStateTag},
    {"bumpmap", IgnoreStateTag}, {"shadowmap", IgnoreStateTag}, {"glaregen", StateTagGlareGen}, {NULL, NULL},
};

// The tags (ParseFirstStateTag's)
// XBE_GLOBAL(0x001f2998, 0x90) readonly
static const StateTag kStateTags[] = {
    {"modify", StateTagModify}, {"primtype", StateTagPrimType}, {"shading", IgnoreStateTag},
    {"cull", StateTagCull}, {"texture", StateTagTexture}, {"texcoord", StateTagTexCoord},
    {"depthtest", StateTagDepthTest}, {"alphatestmethod", StateTagAlphaTestMethod},
    {"alphabm", StateTagAlphaBlendMode}, {"transparency", StateTagTransparency}, {"chroma", StateTagChroma},
    {"alphatest", StateTagAlphaTest}, {"texalpha", StateTagTexAlpha}, {"fillmode", StateTagFillMode},
    {"alphablendxbox", StateTagAlphaBlendXbox}, {"zslope", StateTagZSlope}, {"zbias", StateTagZBias},
    {NULL, NULL},
};

// =============================================================================================================
// The handlers
// =============================================================================================================

// FUNC_AT(0x00091560)
bool StateTagPrimType(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kPrimTypes))
        state->SetPrimitiveType(word->value);
    return false;
}

// FUNC_AT(0x00091680)
bool StateTagCull(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kCullSwitch)) {
        state->SetCullEnable(word->value != 0);
        Extension(state)->SetCullDirection(kCullDirectionCw);
    }
    return false;
}

// FUNC_AT(0x00091700)
bool StateTagTexture(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kTextureSwitch))
        state->SetTextureEnable(word->value != 0);
    return false;
}

// FUNC_AT(0x00091780)
bool StateTagTexCoord(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kTexCoordTypes))
        state->SetTextureCoordType(word->value);
    return false;
}

// FUNC_AT(0x00091800)
bool StateTagDepthTest(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kDepthTests))
        state->SetDepthTestMethod(word->value);
    return false;
}

// FUNC_AT(0x00091900)
bool StateTagAlphaTestMethod(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kAlphaTests))
        state->SetAlphaTestMethod(word->value);
    return false;
}

// FUNC_AT(0x00091a00)
bool StateTagTransparency(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kTransparencies))
        state->SetTransparencyMethod(word->value);
    return false;
}

// FUNC_AT(0x00091a90)
bool StateTagAlphaBlendMode(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kAlphaBlendModes))
        state->SetAlphaBlendMode(word->value);
    return false;
}

// FUNC_AT(0x00091b70)
bool StateTagAlphaTest(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kAlphaTestSwitch))
        state->SetAlphaTestEnable(word->value != 0);
    return false;
}

// Only while the alpha test is on: noA turns it off with blending; the others set the compare value and blending,
// and a blend mode of off becomes blend.
// FUNC_AT(0x00091bf0)
bool StateTagTexAlpha(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    const StateKeyword *word = FindStateEntry(value, kTexAlphas);
    bool alphaTest = true;
    state->GetAlphaTestEnable(&alphaTest);
    if (word == NULL || !alphaTest)
        return false;
    if (word->value == 0) {
        state->SetAlphaTestEnable(false);
        state->SetTransparencyMethod(kTransparencyOpaque);
        return false;
    }
    state->SetAlphaCompareValue(word->value);
    state->SetTransparencyMethod(kTransparencyAlpha);
    uint32_t mode;
    state->GetAlphaBlendMode(&mode);
    if (mode == kAlphaBlendOff)
        state->SetAlphaBlendMode(kAlphaBlendBlend);
    return false;
}

// FUNC_AT(0x00091cf0)
bool StateTagGlareGen(const char *, const char *, EAGL::GeoPrimState *state, int) {
    state->SetTextureEnable(true);
    state->SetTransparencyMethod(kTransparencyOpaque);
    state->SetAlphaTestEnable(false);
    state->SetAlphaCompareValue(kGlareAlphaCompare);
    return false;
}

// FUNC_AT(0x00091d20)
bool StateTagFillMode(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (const StateKeyword *word = FindStateEntry(value, kFillModes))
        Extension(state)->SetFillMode(word->value);
    return false;
}

// "source,destination,operation". Each word is looked up from where it starts to the end of the value, the commas
// left in, and all three among the operations, so the first two are never found and nothing is set; kept as the
// original has it.
// FUNC_AT(0x00091db0)
bool StateTagAlphaBlendXbox(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    const char *second = strchr(value, ',');
    if (second == NULL)
        return false;
    second++;
    const char *third = strchr(second, ',');
    if (third == NULL)
        return false;
    const StateKeyword *source = FindStateEntry(value, kBlendOperations);
    const StateKeyword *destination = FindStateEntry(second, kBlendOperations);
    const StateKeyword *operation = FindStateEntry(third + 1, kBlendOperations);
    if (source != NULL && destination != NULL && operation != NULL)
        Extension(state)->SetAlphaBlend(source->value, destination->value, operation->value);
    return false;
}

// FUNC_AT(0x00092030)
bool StateTagZSlope(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (strlen(value) == 0)
        Extension(state)->SetZSlopeScale(0.0f);
    else
        Extension(state)->SetZSlopeScale((float)CRT_atof(value));
    return false;
}

// FUNC_AT(0x00092080)
bool StateTagZBias(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    if (strlen(value) == 0)
        Extension(state)->SetZOffset(0.0f);
    else
        Extension(state)->SetZOffset((float)CRT_atof(value));
    return false;
}

// FUNC_AT(0x00092140)
bool StateTagChroma(const char *, const char *value, EAGL::GeoPrimState *state, int) {
    uint32_t colour;
    if (CRT_sscanf(value, "0x%08x", &colour) > 0)
        state->SetChromaColour(colour);
    return false;
}

// FUNC_AT(0x00092170)
bool StateTagModify(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass) {
    const StateTag *word = FindStateEntry(value, kModifyTags);
    if (word != NULL && word->handler != NULL)
        return word->handler(tag, value, state, pass);
    return false;
}

// FUNC_AT(0x00092130)
void CallEaglFree(void *pointer, uint32_t size) {
    EaglFree(pointer, size);
}

// =============================================================================================================
// The manager
// =============================================================================================================

// FUNC_AT(0x00092280)
const char* RStateManager::ParseFirstStateTag(const char *text, EAGL::GeoPrimState *state, int pass, bool *handled) {
    const size_t kMaxTag = 0x1f;
    const size_t kMaxValue = 0x3f;
    if (text == NULL)
        return NULL;
    while (*text != '\0' && !CRT_isalnum(*text))
        text++;
    const char *tagStart = text;
    while (*text != '\0' && *text != '=')
        text++;
    size_t tagLength = text - tagStart;
    if (tagLength > kMaxTag)
        tagLength = kMaxTag;
    if (*text != '\0')
        text++;
    if (tagLength == 0)
        return NULL;
    const char *valueStart = text;
    while (*text != '\0' && *text != ';')
        text++;
    size_t valueLength = text - valueStart;
    if (valueLength > kMaxValue)
        valueLength = kMaxValue;
    if (*text != '\0')
        text++;

    char tag[kMaxTag + 1];
    char value[kMaxValue + 1];
    strncpy(tag, tagStart, tagLength);
    tag[tagLength] = '\0';
    strncpy(value, valueStart, valueLength);
    value[valueLength] = '\0';
    const StateTag *entry = FindStateEntry(tag, kStateTags);
    if (entry != NULL && entry->handler != NULL)
        *handled = entry->handler(tag, value, state, pass);
    return text;
}

// The tags whose handlers answer true are applied again, in order, after all the others (none does).
// FUNC_AT(0x00092d20)
EAGL::GeoPrimState* RStateManager::GetGeoPrimState(const char *text) {
    const int kMaxDeferred = 16;          // the room the original's frame has; it does not check
    EAGL::GeoPrimState state;
    state.Construct();
    MEM_fill(&state, 0, sizeof(state));
    state.Construct();
    state.SetPrimitiveType(kPrimTriList);
    state.SetCullEnable(false);
    state.SetTextureEnable(true);
    state.SetAlphaTestEnable(true);
    state.SetAlphaCompareValue(kDefaultAlphaCompare);
    state.SetAlphaBlendMode(kAlphaBlendBlend);
    state.SetShading(kShadingGouraudSpecular);

    const char *deferred[kMaxDeferred];
    int deferredCount = 0;
    bool handled = false;                 // the original leaves it uninitialised
    for (const char *at = text; at != NULL;) {
        deferred[deferredCount] = at;
        at = ParseFirstStateTag(at, &state, 1, &handled);
        if (handled)
            deferredCount++;
    }
    for (int i = 0; i < deferredCount; i++)
        ParseFirstStateTag(deferred[i], &state, 0, &handled);

    StateRefValue key;
    key.state = &state;
    key.name[0] = 0;
    StateRefNode *node = lookup->LowerBound(&key);
    if (node == lookup->head || memcmp(&state, node->value.state, sizeof(state)) < 0) {
        EAGL::GeoPrimState *slot = &states[count++];
        MEM_copy(slot, &state, sizeof(state));
        // The new entry: the state's slot and the first 15 characters of its string
        StateRefValue value;
        value.state = slot;
        memset(value.name, 0, sizeof(value.name));
        if (text != NULL)
            strncpy(value.name, text, sizeof(value.name) - 1);
        StateRefInsert inserted;
        node = lookup->InsertUnique(&inserted, &value)->node;
    }
    EAGL::GeoPrimState *found = node->value.state;
    state.Destruct();
    return found;
}

// FUNC_AT(0x00092f30)
void* RStateNamespace::NameLookup(const char *name, int *size) {
    EAGL::GeoPrimState *state = Owner()->GetGeoPrimState(name);
    *size = sizeof(EAGL::GeoPrimState);
    return state;
}

// FUNC_AT(0x00092f50)
RStateManager* RStateManager::Construct() {
    symbols.vtable = kNamespaceBaseVtable;
    vtable = kStateManagerVtable;
    symbols.vtable = kStateNamespaceVtable;
    count = 0;
    // new GeoPrimState[kMaxStates] from EAGL's allocator: the count, then the states
    uint32_t *block = static_cast<uint32_t *>(
        EaglMalloc(sizeof(uint32_t) + kMaxStates * sizeof(EAGL::GeoPrimState), "EAGL::GeoPrimState new[]"));
    if (block != NULL) {
        *block = kMaxStates;
        states = reinterpret_cast<EAGL::GeoPrimState *>(block + 1);
        for (int i = 0; i < kMaxStates; i++)
            states[i].Construct();
    } else {
        states = NULL;
    }
    StateRefTree *tree = static_cast<StateRefTree *>(OperatorNew(sizeof(StateRefTree)));
    if (tree != NULL)
        RenderTree::Construct(tree, tree->BuyHead());
    lookup = tree;
    MEM_fill(states, 0, kMaxStates * sizeof(EAGL::GeoPrimState));
    for (int i = 0; i < kMaxStates; i++)
        states[i].Construct();
    return this;
}

// FUNC_AT(0x000930a0)
void RStateManager::Destruct() {
    vtable = kStateManagerVtable;
    symbols.vtable = kStateNamespaceVtable;
    if (StateRefTree *tree = lookup) {
        StateRefNode *after;
        tree->EraseRange(&after, tree->head->left, tree->head);
        if (tree->head != NULL)
            UMemory::FastFree(tree->head, sizeof(StateRefNode));
        tree->head = NULL;
        tree->size = 0;
        OperatorDelete(tree);
    }
    if (states != NULL) {
        // delete[]: the states destroyed last first, the block handed back with a state's size
        uint32_t *block = reinterpret_cast<uint32_t *>(states) - 1;
        for (uint32_t i = *block; i > 0; i--)
            states[i - 1].Destruct();
        EaglFree(block, sizeof(EAGL::GeoPrimState));
    }
    vtable = kSingletonVtable;
}

// FUNC_AT(0x00093160)
RStateManager* RStateManager::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00093080)
void RStateManager::Kill() {
    RStateManager *manager = TheStateManager;
    if (manager != NULL)
        reinterpret_cast<DeletingDestructor>(manager->vtable->slot0)(manager, 0, 1);
}

// =============================================================================================================
// The set's helpers
// =============================================================================================================

// FUNC_AT(0x000920d0)
void StateRefIterator::Increment() {
    node = RenderTree::Next(node);
}

// FUNC_AT(0x00093200)
void StateRefIterator::Decrement() {
    node = RenderTree::Prev(node);
}

// lower_bound: ordered by the bytes of the states
// FUNC_AT(0x00092540)
StateRefNode* StateRefTree::LowerBound(const StateRefValue *key) {
    StateRefNode *bound = head;
    for (StateRefNode *node = head->parent; !node->isNil;) {
        if (memcmp(node->value.state, key->state, sizeof(EAGL::GeoPrimState)) < 0) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    return bound;
}

// FUNC_AT(0x00092590)
void StateRefTree::Lrotate(StateRefNode *where) {
    RenderTree::Lrotate(this, where);
}

// FUNC_AT(0x000925f0)
void StateRefTree::Rrotate(StateRefNode *where) {
    RenderTree::Rrotate(this, where);
}

// FUNC_AT(0x00092650)
void StateRefTree::EraseSubtree(StateRefNode *node) {
    RenderTree::EraseSubtree(this, node);
}

// FUNC_AT(0x00092690)
StateRefNode** StateRefTree::InsertAt(StateRefNode **result, bool addLeft, StateRefNode *where, const StateRefValue *value) {
    *result = RenderTree::InsertAt(this, addLeft, where, *value);
    return result;
}

// FUNC_AT(0x00092870)
StateRefNode** StateRefTree::EraseAt(StateRefNode **result, StateRefNode *where) {
    *result = RenderTree::EraseAt(this, where);
    return result;
}

// FUNC_AT(0x00092c60)
StateRefNode** StateRefTree::EraseRange(StateRefNode **result, StateRefNode *first, StateRefNode *last) {
    *result = RenderTree::EraseRange(this, first, last);
    return result;
}

// FUNC_AT(0x00093180)
StateRefNode* StateRefTree::Max(StateRefNode *node) {
    return RenderTree::Max(node);
}

// FUNC_AT(0x000931a0)
StateRefNode* StateRefTree::Min(StateRefNode *node) {
    return RenderTree::Min(node);
}

// FUNC_AT(0x00093260)
StateRefNode* StateRefTree::BuyNode(StateRefNode *left, StateRefNode *parent, StateRefNode *right, const StateRefValue *value, uint8_t color) {
    return RenderTree::BuyNode(left, parent, right, *value, color);
}

// FUNC_AT(0x00093310)
StateRefNode* StateRefTree::BuyHead() {
    return BuyMapHead<StateRefNode>();
}
