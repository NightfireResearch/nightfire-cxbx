#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Index.h"

#include "../engine/CoreFoundation.h"   // GameEmptyString
#include "../engine/InputConfig.h"      // BuildFileName
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMemory.h"     // MEM_size, MEM_free_copy
#include "../platform/RealPrint.h"      // MEM_copy
#include "../world/SoundGroup.h"        // WSoundMap::Find
#include "../world/SoundMap.h"          // MapBuyHead

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// AIndex and its two maps (0x00126d10..0x00127a70), ported from the listings. See Index.h.
// ---------------------------------------------------------------------------------------------------------------

// The maps' helpers the game shares among its maps (Bank.cpp's SharedTreeIterator::Dec and SharedTree::FindName, on
// the data layer's node type), by address on this file's node types.
#define SharedTreeIterator_Dec ((void (__fastcall *)(void *it, int))0x00126bd0)
#define SharedTree_FindName ((AIndexNameNode **(__fastcall *)(AIndexNameMap *, int, AIndexNameNode **, const char *const *))0x00126cb0)

// The C runtime's.
#define Crt_sscanf ((int (*)(const char *text, const char *format, ...))0x00133234)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)

namespace {

const char kDefineFormat[] = "#define%31s%d";

}  // namespace

// =============================================================================================================
// The number map
// =============================================================================================================

// FUNC_AT(0x00126d80)
AIndexIdNode** AIndexIdMap::InsertAt(AIndexIdNode **result, bool addLeft, AIndexIdNode *where,
                                     const AIndexIdPair *value) {
    AsTree()->InsertAt(reinterpret_cast<TreeNode **>(result), addLeft, reinterpret_cast<TreeNode *>(where),
                       reinterpret_cast<const TreePair *>(value));
    return result;
}

// FUNC_AT(0x00126f60)
AIndexIdNode** AIndexIdMap::EraseAt(AIndexIdNode **result, AIndexIdNode *where) {
    AsTree()->EraseAt(reinterpret_cast<TreeNode **>(result), reinterpret_cast<TreeNode *>(where));
    return result;
}

// FUNC_AT(0x00127230)
AIndexIdInsert* AIndexIdMap::InsertUnique(AIndexIdInsert *result, const AIndexIdPair *value) {
    AIndexIdNode *where = head;
    bool addLeft = true;
    for (AIndexIdNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->id < node->value.id;
    }
    AIndexIdNode *before = where;
    if (addLeft) {
        if (where == head->left) {
            AIndexIdNode *node;
            result->node = *InsertAt(&node, true, where, value);
            result->inserted = true;
            return result;
        }
        SharedTreeIterator_Dec(&before, 0);
    }
    if (before->value.id < value->id) {
        AIndexIdNode *node;
        result->node = *InsertAt(&node, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = before;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x001273c0)
AIndexIdNode** AIndexIdMap::EraseRange(AIndexIdNode **result, AIndexIdNode *first, AIndexIdNode *last) {
    AsTree()->EraseRange(reinterpret_cast<TreeNode **>(result), reinterpret_cast<TreeNode *>(first),
                         reinterpret_cast<TreeNode *>(last));
    return result;
}

// FUNC_AT(0x00127540)
void AIndexIdMap::Destruct() {
    AUDIO_INDEX_UNTESTED("AIndexIdMap::Destruct (an exception unwind's)");
    AsTree()->Destroy();
}

// =============================================================================================================
// The name map
// =============================================================================================================

// The map's _Insert (in the game, the number map's copy).
AIndexNameNode* AIndexNameMap::InsertAt(bool addLeft, AIndexNameNode *where, const AIndexNamePair *value) {
    TreeNode *node;
    AsTree()->InsertAt(&node, addLeft, reinterpret_cast<TreeNode *>(where),
                       reinterpret_cast<const TreePair *>(value));
    return reinterpret_cast<AIndexNameNode *>(node);
}

// FUNC_AT(0x001272f0)
AIndexNameInsert* AIndexNameMap::InsertUnique(AIndexNameInsert *result, const AIndexNamePair *value) {
    AIndexNameNode *where = head;
    bool addLeft = true;
    for (AIndexNameNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = Crt_stricmp(value->name, node->value.name) < 0;
    }
    AIndexNameNode *before = where;
    if (addLeft) {
        if (where == head->left) {
            result->node = InsertAt(true, where, value);
            result->inserted = true;
            return result;
        }
        SharedTreeIterator_Dec(&before, 0);
    }
    if (Crt_stricmp(before->value.name, value->name) < 0) {
        result->node = InsertAt(addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->node = before;
    result->inserted = false;
    return result;
}

// FUNC_AT(0x00127480)
AIndexNameNode** AIndexNameMap::EraseRange(AIndexNameNode **result, AIndexNameNode *first, AIndexNameNode *last) {
    AsTree()->EraseRange(reinterpret_cast<TreeNode **>(result), reinterpret_cast<TreeNode *>(first),
                         reinterpret_cast<TreeNode *>(last));
    return result;
}

// =============================================================================================================
// The maps together
// =============================================================================================================

// FUNC_AT(0x00127620)
AIndexMap* AIndexMap::Construct() {
    byId.allocator = 0;         // (the original copies an uninitialised stack byte)
    byId.AsTree()->Init();
    byName.allocator = 0;
    byName.AsTree()->Init();
    return this;
}

// FUNC_AT(0x00127580)
void AIndexMap::Destruct() {
    byName.AsTree()->Destroy();
    byId.AsTree()->Destroy();
}

// =============================================================================================================
// The index
// =============================================================================================================

static AIndexMap *NewIndexMap() {
    AIndexMap *map = static_cast<AIndexMap *>(UMemory::FastAlloc(sizeof(AIndexMap), "AIndexMap"));
    return map != NULL ? map->Construct() : NULL;
}

// FUNC_AT(0x001276c0)
AIndex* AIndex::Construct(const char *directory, const char *file) {
    maps = NewIndexMap();
    names = NULL;
    if (directory == NULL || file == NULL)
        return this;

    char name[64] = {};         // (the original's starts as uninitialised stack)
    int id;
    char base[64];
    char path[64];
    int length = int(strrchr(file, '.') - file);
    strncpy(base, file, length);
    base[length] = '\0';
    char *text = static_cast<char *>(UFileLoader::FileLoadz(BuildFileName(path, 0, directory, base, "h"), 0x100));
    if (text == NULL)
        return this;
    int size = int(MEM_size(text));
    char *copy = size > 0 ? static_cast<char *>(UMemory::Alloc(size + 1, 0x100, path)) : NULL;
    if (copy == NULL) {
        MEM_free_copy(text);
        return this;
    }
    MEM_copy(copy, text, size);
    MEM_free_copy(text);
    copy[size] = '\0';

    // Every definition's name, with its terminator: the size of the block of names.
    unsigned total = 0;
    for (const char *line = copy; line != NULL; line = strchr(line + 1, '#')) {
        if (Crt_sscanf(line, kDefineFormat, name, &id) == 2)
            total += unsigned(strlen(name)) + 1;
    }
    if (total == 0) {
        UMemory::Free(copy);
        return this;
    }

    char *out = static_cast<char *>(UMemory::Alloc(total, 0, file));
    names = out;
    for (const char *line = copy; line != NULL; line = strchr(line + 1, '#')) {
        if (Crt_sscanf(line, kDefineFormat, name, &id) != 2)
            continue;
        // The search starts at the fifth character, past a name's four-character prefix; in a shorter name it
        // reads on into what earlier names left in the buffer.
        const char *underscore = strchr(name + 4, '_');
        if (underscore != NULL) {
            strcpy(out, "SFX_");
            strcat(out, underscore + 1);
        } else {
            strcpy(out, name);
        }
        for (char *c = out + 4; *c != '\0'; c++) {
            if (*c == '.')
                *c = '\0';
        }
        AIndexIdPair byNumber = { id, out };
        AIndexIdInsert ignoredId;
        maps->byId.InsertUnique(&ignoredId, &byNumber);
        AIndexNamePair byName = { out, id };
        AIndexNameInsert ignoredName;
        maps->byName.InsertUnique(&ignoredName, &byName);
        out += strlen(out) + 1;
    }
    UMemory::Free(copy);
    return this;
}

// FUNC_AT(0x001279e0)
AIndex* AIndex::Construct() {
    maps = NewIndexMap();
    names = NULL;
    return this;
}

// FUNC_AT(0x00127a40)
void AIndex::Destruct() {
    if (maps != NULL) {
        maps->Destruct();
        UMemory::FastFree(maps, sizeof(AIndexMap));
    }
    if (names != NULL)
        UMemory::Free(names);
}

// FUNC_AT(0x00126d10)
const char* AIndex::Lookup(int id) {
    AIndexIdNode *node;
    reinterpret_cast<WSoundMap *>(&maps->byId)->Find(reinterpret_cast<SoundMapNode **>(&node), &id);
    return node == maps->byId.head ? GameEmptyString : node->value.name;
}

// FUNC_AT(0x00126d40)
int AIndex::Lookup(const char *name) {
    if (name == NULL)
        return -1;
    AIndexNameNode *node;
    SharedTree_FindName(&maps->byName, 0, &node, &name);
    return node == maps->byName.head ? -1 : node->value.id;
}
