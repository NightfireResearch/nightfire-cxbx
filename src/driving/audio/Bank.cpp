#include "Bank.h"
#include "SoundManager.h"               // ASystem_fgSystem

#include "../../helpers.h"
#include "../data/SymbolTable.h"        // NamespaceMap::LowerBound
#include "../engine/CoreFoundation.h"   // GameEmptyString
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMemory.h"     // MEM_free
#include "../sound/snd/Banks.h"

// ---------------------------------------------------------------------------------------------------------------
// ABank, URefCounter<ABank>'s tree's entries and the shared map helpers after them (0x00125db0..0x00126d10), ported
// from the listings. See Bank.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code called by address
// directory + name + extension into the buffer, which it answers
#define BuildFileName ((char *(__fastcall *)(char *, int, const char *directory, const char *name, const char *extension))0x00051e90)
// ++ on URefCounter<T>'s maps, shared by all of them
#define BankMap_Increment ((void (__fastcall *)(RefCounterNode **it, int))0x00019d80)
// The C runtime's: the trees' order depends on it.
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)

// ---- globals
#define EmptyBank (*(ABank *)0x00243b18)

namespace {

constexpr int kBankHeaderOnly = 7;                   // SNDbankadd: keep only a copy of the header
constexpr int kBankNotLoaded = -19;
constexpr int kBankFileFlags = 0x100;
constexpr int kHighestPatch = 0x7f;

}  // namespace

// =============================================================================================================
// ABank
// =============================================================================================================

// FUNC_AT(0x00125db0)
ABank* ABank::Construct() {
    data = NULL;
    slot = -1;
    handle = -1;
    index.Construct();
    return this;
}

// FUNC_AT(0x00125dd0)
ABank* ABank::Construct(const char *name, bool withIndex) {
    slot = -1;
    handle = -1;
    status = -1;
    data = NULL;
    index.Construct(withIndex ? AudioDirectory : NULL, withIndex ? name : NULL);
    if (ASystem_fgSystem != NULL) {
        char path[64];
        data = static_cast<SND::BankHeader *>(
            UFileLoader::FileLoadz(BuildFileName(path, 0, AudioDirectory, name, ""), kBankFileFlags));
        if (data != NULL) {
            status = SNDbankadd(&handle, data);
            if (status == kBankHeaderOnly) {
                SND::BankHeader *header = static_cast<SND::BankHeader *>(UMemory::Alloc(SNDbankheadersize(handle), 0, name));
                SNDbankheadercopy(header, handle);
                MEM_free(data);
                data = header;
            }
        } else {
            status = kBankNotLoaded;
        }
    }
    return this;
}

// FUNC_AT(0x00125ef0)
void ABank::Destruct() {
    if (data != NULL) {
        SNDbankremove(handle);
        UMemory::Free(data);
    }
    index.Destruct();
}

// FUNC_AT(0x00125ec0)
bool ABank::IsPatchPresent(int patch) {
    if (handle >= 0 && patch >= 0 && patch <= kHighestPatch)
        return SNDbankpatpresent(handle, patch) == 1;
    return false;
}

// FUNC_AT(0x001269f0)
void ABank::Remove() {
    if (!BankRefCounter::Get()->RemoveReference(this))
        return;
    if (slot > -1)
        fgBanks[slot] = &EmptyBank;
    Destruct();
    UMemory::FastFree(this, sizeof(ABank));
}

// FUNC_AT(0x00126940)
ABank* ABank::Load(const char *name, bool withIndex) {
    ABank *bank = BankRefCounter::Get()->GetReference(name);
    if (bank == NULL) {
        ABank *block = static_cast<ABank *>(UMemory::FastAlloc(sizeof(ABank), "ABank"));
        bank = block != NULL ? block->Construct(name, withIndex) : NULL;
    }
    BankRefCounter::Get()->AddReference(name, bank);
    return bank;
}

// FUNC_AT(0x00126ad0)
ABank* ABank::Load(const char *name, int slot) {
    if (name == NULL)
        return fgBanks[slot];
    fgBanks[slot]->Remove();          // inlined in the original
    fgBanks[slot] = Load(name, true);
    fgBanks[slot]->slot = slot;
    return fgBanks[slot];
}

// FUNC_AT(0x001269d0)
ABank* ABank::Get(const char *name) {
    ABank *bank = BankRefCounter::Get()->GetReference(name);
    return bank != NULL ? bank : &EmptyBank;
}

// FUNC_AT(0x00126910)
RefCounterNode** ABank::Begin(RefCounterNode **result) {
    *result = BankRefCounter::Get()->Begin();
    return result;
}

// FUNC_AT(0x00126930)
RefCounterNode** ABank::End(RefCounterNode **result) {
    *result = BankRefCounter::Get()->End();
    return result;
}

// FUNC_AT(0x00126a30)
const char* ABank::GetPatchName(int handle, int patch) {
    const char *name = GameEmptyString;
    RefCounterNode *node = BankRefCounter::Get()->Begin();
    RefCounterNode *end = BankRefCounter::Get()->End();
    for (; node != end; BankMap_Increment(&node, 0)) {
        ABank *bank = static_cast<ABank *>(node->value.entry.object);
        if (bank->handle != handle)
            continue;
        name = bank->index.Lookup(patch);
        if (*name != '\0') {
            name += 4;
        } else {
            // the bank's file name, after the last '\' or '/'
            name = node->value.name;
            for (const char *c = node->value.name; *c != '\0'; c++) {
                if (*c == '\\' || *c == '/')
                    name = c + 1;
            }
        }
    }
    return name;
}

// =============================================================================================================
// URefCounter<ABank>'s tree
// =============================================================================================================

// FUNC_AT(0x00125fa0)
RefCounterNode** BankRefTree::EraseAt(RefCounterNode **result, RefCounterNode *where) {
    return RefCounterTree::EraseAt(result, where);
}

// FUNC_AT(0x00126310)
RefCounterNode** BankRefTree::InsertAt(RefCounterNode **result, bool addLeft, RefCounterNode *where,
                                       const RefCounterValue *value) {
    return RefCounterTree::InsertAt(result, addLeft, where, value);
}

// FUNC_AT(0x00126500)
RefCounterNode** BankRefTree::EraseRange(RefCounterNode **result, RefCounterNode *first, RefCounterNode *last) {
    return RefCounterTree::EraseRange(result, first, last);
}

// FUNC_AT(0x001265f0)
RefCounterInsertResult* BankRefTree::InsertUnique(RefCounterInsertResult *result, const RefCounterValue *value) {
    return RefCounterTree::InsertUnique(result, value);
}

// FUNC_AT(0x001267b0)
void BankRefTree::DestroyRange() {
    RefCounterTree::DestroyRange();
}

// =============================================================================================================
// The helpers every map with 0x18-byte nodes shares
// =============================================================================================================

// FUNC_AT(0x00126b50)
TreeNode* SharedTreeMax(TreeNode *node) {
    while (!node->right->isNil)
        node = node->right;
    return node;
}

// FUNC_AT(0x00126b70)
void SharedTree::Rrotate(TreeNode *node) {
    TreeNode *pivot = node->left;
    node->left = pivot->right;
    if (!pivot->right->isNil)
        pivot->right->parent = node;
    pivot->parent = node->parent;
    if (node == head->parent)
        head->parent = pivot;
    else if (node == node->parent->right)
        node->parent->right = pivot;
    else
        node->parent->left = pivot;
    pivot->right = node;
    node->parent = pivot;
}

// FUNC_AT(0x00126bd0)
void SharedTreeIterator::Dec() {
    if (node->isNil) {
        node = node->right;
    } else if (!node->left->isNil) {
        TreeNode *max = node->left;
        while (!max->right->isNil)
            max = max->right;
        node = max;
    } else {
        TreeNode *parent;
        while (!(parent = node->parent)->isNil && node == parent->left)
            node = parent;
        if (!parent->isNil)
            node = parent;
    }
}

// FUNC_AT(0x00126c30)
void SharedTree::EraseIdSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x00126c70)
void SharedTree::EraseNameSubtree(TreeNode *node) {
    Tree::EraseSubtree(node);
}

// FUNC_AT(0x00126cb0)
TreeNode** SharedTree::FindName(TreeNode **result, const char *const *key) {
    // lower_bound: the namespace map's compiled copy serves every map keyed by a name
    TreeNode *bound = static_cast<NamespaceMap *>(static_cast<Tree *>(this))->LowerBound(key);
    *result = bound == head || Crt_stricmp(*key, bound->value.name) < 0 ? head : bound;
    return result;
}
