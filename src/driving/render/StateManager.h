#ifndef DRIVING_RENDER_STATEMANAGER_H_
#define DRIVING_RENDER_STATEMANAGER_H_

// ---------------------------------------------------------------------------------------------------------------
// RStateManager: the renderer's store of primitive render states (EAGL::GeoPrimState), made from state strings such
// as "alphabm=add;cull=off" - each tag=value pair handed to the tag's handler, which sets fields of a state that
// starts from the renderer's default. Equal states are shared: a set orders the ones in use by their 0x4c bytes,
// and a string whose state is already there answers that one. The manager is a USingleton (its pointer at
// 0x001f2608) and also a symbol namespace whose names are state strings.
// See StateManager.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../eagl/GeoPrimState.h"
#include "../engine/CoreContainers.h"     // StateRefSet, StateRefValue: the set of states in use
#include "../engine/USingleton.h"

class RStateManager;

// ---- the state strings' vocabulary

// A word of a tag's value and what it stands for (the tables end with a null name)
struct StateKeyword {
    const char *name;
    uint32_t value;
};

// A tag's handler: sets `state` from `value`. Every handler answers false. `pass` is ParseFirstStateTag's.
typedef bool (*StateTagHandler)(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);

struct StateTag {
    const char *name;
    StateTagHandler handler;
};

// The entry of `table` named `name` (strcmp), or NULL; NULL for a null name or table. The original takes the name
// in EDX (FUN_000914f0, below); its callers are ours and call this.
template <class Entry>
const Entry *FindStateEntry(const char *name, const Entry *table) {
    if (name == NULL || table == NULL)
        return NULL;
    int i = 0;
    while (table[i].name != NULL && strcmp(name, table[i].name) != 0)
        i++;
    return table[i].name != NULL ? &table[i] : NULL;
}

// The adaptor under Ghidra's name: EDX the name, the table on the stack (the caller pops it); EDX kept.
void FUN_000914f0();
const StateKeyword *FindStateKeyword(const char *name, const StateKeyword *table);   // its body

// The handlers, by the tag each serves (the names are ours)
bool StateTagPrimType(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);       // 0x00091560
bool StateTagCull(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);           // 0x00091680
bool StateTagTexture(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);        // 0x00091700
bool StateTagTexCoord(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);       // 0x00091780
bool StateTagDepthTest(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);      // 0x00091800
bool StateTagAlphaTestMethod(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);   // 0x00091900
bool StateTagTransparency(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);   // 0x00091a00
bool StateTagAlphaBlendMode(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass); // 0x00091a90
bool StateTagAlphaTest(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);      // 0x00091b70
bool StateTagTexAlpha(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);       // 0x00091bf0
// "glaregen", one of "modify"'s words: textured, opaque, alpha test off, compare value 0x80
bool StateTagGlareGen(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);       // 0x00091cf0
bool StateTagFillMode(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);       // 0x00091d20
bool StateTagAlphaBlendXbox(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass); // 0x00091db0
bool StateTagZSlope(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);         // 0x00092030
bool StateTagZBias(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);          // 0x00092080
bool StateTagChroma(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);         // 0x00092140
// "modify": the value is itself a tag (chromemap, glossmap, ..., glaregen), whose handler is called
bool StateTagModify(const char *tag, const char *value, EAGL::GeoPrimState *state, int pass);         // 0x00092170

// EAGL's free hook called through a jump of its own (Ghidra: eagl_free, the name View.h's 0x000e4f60 has); the
// manager's and other objects' unwind code calls it.
void CallEaglFree(void *pointer, uint32_t size);                                                    // 0x00092130

// ---- the set of states in use (std::set<StateRef>), and its compiled helpers

typedef GameTreeNode<StateRefValue> StateRefNode;

// The set's own copies (insert_unique is CoreContainers.cpp's). RTexList's map, whose nodes are the same size,
// calls the rotations, the minimum and maximum, ++, -- and the node maker here too.
struct StateRefTree : StateRefSet {
    StateRefNode* LowerBound(const StateRefValue *key);                                             // 0x00092540
    void Lrotate(StateRefNode *where);                                                              // 0x00092590
    void Rrotate(StateRefNode *where);                                                              // 0x000925f0
    void EraseSubtree(StateRefNode *node);                                                          // 0x00092650
    StateRefNode** InsertAt(StateRefNode **result, bool addLeft, StateRefNode *where,
                            const StateRefValue *value);                                            // 0x00092690
    StateRefNode** EraseAt(StateRefNode **result, StateRefNode *where);                             // 0x00092870
    StateRefNode** EraseRange(StateRefNode **result, StateRefNode *first, StateRefNode *last);      // 0x00092c60
    static StateRefNode* Max(StateRefNode *node);                                                   // 0x00093180
    static StateRefNode* Min(StateRefNode *node);                                                   // 0x000931a0
    StateRefNode* BuyNode(StateRefNode *left, StateRefNode *parent, StateRefNode *right,
                          const StateRefValue *value, uint8_t color);                               // 0x00093260
    StateRefNode* BuyHead();                                                                        // 0x00093310
};
static_assert(sizeof(StateRefTree) == 0xc, "a set is 12 bytes");

struct StateRefIterator {
    StateRefNode *node;

    void Increment();                                                                               // 0x000920d0
    void Decrement();                                                                               // 0x00093200
};

// ---- the manager

// The manager's second base, a symbol namespace (its vtable 0x0019212c has the one slot): a name is a state string.
struct RStateNamespace {
    const void *vtable;

    RStateManager *Owner() { return reinterpret_cast<RStateManager *>(reinterpret_cast<uint8_t *>(this) - 4); }

    // The state of `name`; *size the state's 0x4c bytes.
    void* NameLookup(const char *name, int *size);                                                  // 0x00092f30
};

class RStateManager {
public:
    static constexpr int kMaxStates = 32;

    const USingletonVtable *vtable;       // +0x00 a USingleton's (0x00192130)
    RStateNamespace symbols;              // +0x04
    int32_t count;                        // +0x08 the states made
    EAGL::GeoPrimState *states;           // +0x0c kMaxStates of them, from EAGL's allocator ("new[]")
    StateRefTree *lookup;                 // +0x10 the states made, ordered by their bytes, with their strings

    RStateManager* Construct();                                                                     // 0x00092f50
    void Destruct();                                                                                // 0x000930a0
    RStateManager* Delete(unsigned flags);                                                          // 0x00093160
    // The vtable's kill slot: deletes the manager the singleton pointer holds (without clearing it).
    void Kill();                                                                                    // 0x00093080

    // The state for a state string: the default state, then each tag applied in turn; an equal state already made
    // is shared, otherwise the next free slot takes it (no check against kMaxStates).
    EAGL::GeoPrimState* GetGeoPrimState(const char *text);                                         // 0x00092d20
    // Applies the first tag=value pair of `text` (up to ';') to `state`: leading non-alphanumerics skipped, the tag
    // cut at '=' to 31 characters, the value at ';' to 63. *handled takes the handler's answer when the tag has one.
    // Answers the text after the pair, or NULL when there is no text or no tag.
    const char* ParseFirstStateTag(const char *text, EAGL::GeoPrimState *state, int pass, bool *handled);  // 0x00092280
};
static_assert(sizeof(RStateManager) == 0x14, "RStateManager is 0x14 bytes");
static_assert(offsetof(RStateManager, symbols) == 4 && offsetof(RStateManager, lookup) == 0x10, "RStateManager layout");

// The singleton (made by its USingleton Init, 0x0008b5b0..0x0008bf30)
#define TheStateManager (*(RStateManager **)0x001f2608)

#endif // DRIVING_RENDER_STATEMANAGER_H_
