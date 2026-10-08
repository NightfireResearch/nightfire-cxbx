#ifndef DRIVING_AUDIO_BANK_H_
#define DRIVING_AUDIO_BANK_H_

// ---------------------------------------------------------------------------------------------------------------
// ABank: a sound bank - a BNKl file loaded from the audio directory and handed to the SND library (SNDbankadd), and
// the index of its patch names (AIndex, Index.h). Banks are shared by name through URefCounter<ABank>; ten slots
// (0x001d8398) hold the banks ASoundManager loads by number, an empty slot the empty bank at 0x00243b18. See
// Bank.cpp.
//
// Also here: URefCounter<ABank>'s tree's compiled code, and the code the linker placed after ABank's - helpers
// every map with 0x18-byte nodes shares, and the two maps of AIndex's _Erase.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "Index.h"                      // AIndex
#include "Mix.h"                        // RefCounterTree
#include "../data/Tree.h"
#include "../engine/URefCounter.h"
#include "../../helpers.h"

namespace SND {
struct BankHeader;
}

// (field names ours)
class ABank {
public:
    SND::BankHeader *data;      // +0x00 the bank's file, or only a copy of its header when SNDbankadd answers 7
    int32_t slot;               // +0x04 the bank slot ABank::Load(name, slot) put it in; -1: none
    int32_t handle;             // +0x08 the SND bank handle; -1: none
    int32_t status;             // +0x0c SNDbankadd's answer; -19 when the file did not load
    AIndex index;               // +0x10

    // The empty bank (no file, no handle, an empty index).
    ABank* Construct();                                                             // 0x00125db0
    // The bank `name` from the audio directory (when the sound system exists), with its index if `withIndex`.
    ABank* Construct(const char *name, bool withIndex);                             // 0x00125dd0
    void Destruct();                                                                // 0x00125ef0

    bool IsPatchPresent(int patch);                                                 // 0x00125ec0
    // One reference fewer; the last frees the bank (and empties its slot).
    void Remove();                                                                  // 0x001269f0

    // The bank `name`, loaded the first time; one more reference to it.
    static ABank* Load(const char *name, bool withIndex);                           // 0x00126940
    // The bank `name` (with its index) into the slot, releasing the slot's bank; with no name, the slot's bank.
    static ABank* Load(const char *name, int slot);                                 // 0x00126ad0
    // The loaded bank `name`, or the empty bank.
    static ABank* Get(const char *name);                                            // 0x001269d0
    // begin() and end() of the loaded banks (URefCounter<ABank>'s map)
    static RefCounterNode** Begin(RefCounterNode **result);                         // 0x00126910
    static RefCounterNode** End(RefCounterNode **result);                           // 0x00126930
    // The name of the patch in the loaded bank with the SND handle: the index's name without its first four
    // characters, or the bank's file name when the index has none; "" when no bank has the handle.
    static const char* GetPatchName(int handle, int patch);                         // 0x00126a30
};
static_assert(sizeof(ABank) == 0x18, "ABank is 24 bytes");
static_assert(offsetof(ABank, index) == 0x10, "ABank::index");

enum { kBankSlotCount = 10 };

#define fgBanks ((ABank **)0x001d8398)              // the banks by slot (ABank::Load's second argument)
#define AudioDirectory ((char *)0x001d8318)         // [0x80] where the banks and banks.ini are; "./" until
                                                    // ASoundManager::Init (name ours)

// ---- URefCounter<ABank>'s tree (BankRefCounter's map, at 0x00243b08): its compiled copies of RefCounterTree's
// code (Mix.h). Its _Erase (0x00125f50) is BankRefCounter::EraseSubtree, and its destructor BankRefCounter::Destruct.

class BankRefTree : public RefCounterTree {
public:
    RefCounterNode** EraseAt(RefCounterNode **result, RefCounterNode *where);       // 0x00125fa0
    RefCounterNode** InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                              const RefCounterValue *value);                        // 0x00126310
    RefCounterNode** EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last);  // 0x00126500
    RefCounterInsertResult* InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value); // 0x001265f0
    // The destructor's erase and free, without its subtree erase (only an exception unwind calls it).
    void DestroyRange();                                                            // 0x001267b0
};
static_assert(sizeof(BankRefTree) == 0xc, "a map is 12 bytes");

// ---- helpers every map with 0x18-byte nodes shares (data/Tree.h's TreeNode; the name is ours)

class SharedTree : public Tree {
public:
    // A node's right rotation.
    void Rrotate(TreeNode *node);                                                   // 0x00126b70
    // find, for the maps keyed by a name compared with _stricmp (UCarpNamespace's groups, AIndex's names).
    TreeNode** FindName(TreeNode **result, const char *const *key);                 // 0x00126cb0
    // _Erase of AIndex's number map and of its name map (a compiled copy each).
    void EraseIdSubtree(TreeNode *node);                                            // 0x00126c30
    void EraseNameSubtree(TreeNode *node);                                          // 0x00126c70
};
static_assert(sizeof(SharedTree) == 12, "a map is 12 bytes");

// The rightmost node under `node` (_Max).
TreeNode* SharedTreeMax(TreeNode *node);                                            // 0x00126b50

// An iterator's --: from end() to the rightmost node.
struct SharedTreeIterator {
    TreeNode *node;

    void Dec();                                                                     // 0x00126bd0
};

#endif // DRIVING_AUDIO_BANK_H_
