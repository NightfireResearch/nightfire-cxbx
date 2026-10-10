#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif

#include "AttributeSystem.h"
#include "Dafi.h"
#include "../engine/InputConfig.h"   // BuildFileName, BuildPath
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../../common/xbeOverload.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// AttributeSystem (0x000525d0, 0x00053990..0x00053a40, 0x00055a90..0x00055da0, 0x000570c0, 0x00057240,
// 0x00057520..0x000578b0, 0x00057a80..0x00057ee0, 0x00058cc0..0x00058e30, 0x00059090..0x00059550): the
// singleton's life, the database (attrib.dir), loading a collection's .atr file and parsing its keys, the string
// store, and the extension types. AttributeSystem.h says what the system is for; AttributeContainers.cpp has the
// maps it is built from.
//
// The path builders are called at their addresses (not ours yet).
// ---------------------------------------------------------------------------------------------------------------

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

#define AttributeSystemVtable ((void **)0x0018dc28)
#define AttributeSystemBaseVtable ((void **)0x0018beb0)   // its base class's: a destructor and two pure virtuals

namespace {

const char kAttributeDirectory[] = "data\\sim\\attrib\\";
const char kDefaultCollection[] = "default";

// A map of the system's own, as the constructor makes one (`new` from the fixed-size pools, under its name). The
// allocator byte the game copies here is an uninitialised stack byte.
template <class Map>
Map *NewMap(const char *name) {
    Map *map = static_cast<Map *>(UMemory::FastAlloc(sizeof(Map), name));
    if (map == NULL)
        return NULL;
    map->allocator = 0;
    map->head = Map::BuyHead();
    map->head->isNil = 1;
    map->head->parent = map->head;
    map->head->left = map->head;
    map->head->right = map->head;
    map->size = 0;
    return map;
}

// ... and the destructor's way of deleting one: everything erased, the head and the map freed.
template <class Map>
void DeleteMap(Map *map) {
    if (map == NULL)
        return;
    typename Map::Iterator first = { map->head->left };
    typename Map::Iterator last = { map->head };
    typename Map::Iterator ignored;
    map->EraseRange(&ignored, first, last);
    if (map->head != NULL)
        UMemory::FastFree(map->head, sizeof(*map->head));
    map->head = NULL;
    map->size = 0;
    UMemory::FastFree(map, sizeof(Map));
}

// The built-in parsers, indexed by -2 - type (the game's table at 0x001b7030).
const AttributeParserFunc kParsers[] = {
    Bool_AttribByteOffsetParserFunc,     // kAttributeBool
    Int_AttribByteOffsetParserFunc,      // kAttributeInt
    UInt_AttribByteOffsetParserFunc,     // kAttributeUInt
    Float_AttribByteOffsetParserFunc,    // kAttributeFloat
    Vector_AttribByteOffsetParserFunc,   // kAttributeVector
    Matrix_AttribByteOffsetParserFunc,   // kAttributeMatrix
    String_AttribByteOffsetParserFunc,   // kAttributeString
    Symbol_AttribByteOffsetParserFunc,   // kAttributeSymbol
};

// SetAttribute<T>: the four are one template in the game too. One element is held in the value itself (a bool's
// one byte - the game leaves the other three bytes of the word as its stack had them; here they are 0).
template <AttributeType kType>
void SetPlainAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed) {
    AttributeValue value;
    if (copy && parsed->count == 1) {
        value.count = 0;
        value.flags = 0;
        value.type = kType;
        if (kType == kAttributeBool)
            value.bits = *static_cast<const uint8_t *>(parsed->data);
        else
            value.bits = *static_cast<const uint32_t *>(parsed->data);
        attributes->Lookup(name)->Assign(value);
        value.Destruct();
        return;
    }
    uint16_t count = static_cast<uint16_t>(parsed->count);
    value.count = count;
    value.flags = copy ? kAttributeOwnsData : 0;
    value.type = kType;
    value.data = copy ? AttributeValue::Alloc(kType, count, parsed->data) : parsed->data;
    attributes->Lookup(name)->Assign(value);
    value.Destruct();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The singleton.

// FUNC_AT(0x00059090)
AttributeSystem* AttributeSystem::Construct() {
    vtable = AttributeSystemVtable;
    nextExtensionType = 1;
    loadingEnabled = 0;
    loadingDatabase = 1;
    symbolTable = NULL;
    collectionSection[0] = '\0';
    extensionTypes = NewMap<ExtensionTypeMap>("AttributeExtensionTypeMap");
    extensionFields = NewMap<ExtensionClassMap>("AttributeExtensionClassMap");
    editConfig = NewMap<EditConfigMap>("AttributeEditConfigMap");
    collections = NewMap<CollectionMap>("AttributeCollectionMap");
    strings = NewMap<StringSet>("AttributeStringSet");
    StoreBlockList *blocks = static_cast<StoreBlockList *>(UMemory::FastAlloc(sizeof(StoreBlockList),
                                                                             "AttributeStoreBlockList"));
    if (blocks != NULL) {
        blocks->first = NULL;
        blocks->last = NULL;
        blocks->end = NULL;
    }
    storeBlocks = blocks;
    return this;
}

// FUNC_AT(0x000592d0)
void AttributeSystem::Destruct() {
    vtable = AttributeSystemVtable;
    for (CollectionMap::Iterator at = { collections->head->left }; at.node != collections->head;) {
        AttributeCollection &collection = at.node->value.collection;
        collection.refCount = 0;
        collection.attributes.Clear();
        at.Inc();
    }
    DeleteMap(extensionTypes);
    DeleteMap(extensionFields);
    DeleteMap(editConfig);
    DeleteMap(collections);
    DeleteMap(strings);
    for (AttributeStoreBlock *block = storeBlocks->first; block != storeBlocks->last; block++)
        OperatorDelete(block->block);
    StoreBlockList *blocks = storeBlocks;
    if (blocks != NULL) {
        if (blocks->first != NULL) {   // (each element's destructor is an empty function, 0x000d3580)
            uint32_t capacity = static_cast<uint32_t>(blocks->end - blocks->first);
            UMemory::FastFree(blocks->first, capacity * sizeof(AttributeStoreBlock));
        }
        blocks->first = NULL;
        blocks->last = NULL;
        blocks->end = NULL;
        UMemory::FastFree(blocks, sizeof(StoreBlockList));
    }
    loadingEnabled = 0;
    vtable = AttributeSystemBaseVtable;
}

// FUNC_AT(0x00059530)
AttributeSystem* AttributeSystem::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        OperatorDelete(this);
    return this;
}

// FUNC_AT(0x00059550)
void AttributeSystem::Init() {
    AttributeSystem *system = static_cast<AttributeSystem *>(OperatorNew(sizeof(AttributeSystem)));
    AttributeSystemInstance = system != NULL ? system->Construct() : NULL;
}

// FUNC_AT(0x000592b0)
void AttributeSystem::Kill() {
    AttributeSystem *system = AttributeSystemInstance;
    if (system != NULL) {
        typedef AttributeSystem *(AttributeSystem::*DeletingDestructor)(unsigned flags);
        (system->*XbeVirtual<DeletingDestructor>(system, 0))(1);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The database and the collections.

// FUNC_AT(0x000525d0)
void AttributeSystem::SetCollectionSection(const char *section) {
    strcpy(collectionSection, section);
}

// attrib.dir: one "class\name" per line (any byte below ' ' - or above 0x7f - ends a line). Every listed
// collection is registered, to be read when first used.
// FUNC_AT(0x00058e30)
void AttributeSystem::PrepareDatabase() {
    char path[64];
    char *text = static_cast<char *>(
        UFileLoader::FileLoadz(BuildFileName(path, 0, kAttributeDirectory, "attrib", "dir"), 0x100));
    if (text != NULL) {
        char *end = text + UMemory::Size(text);
        char *line = text;
        for (char *at = text; at < end;) {
            if (static_cast<signed char>(*at) >= ' ') {
                at++;
                continue;
            }
            *at++ = '\0';
            if (strlen(line) != 0) {
                char *slash = line;
                while (*slash != '\0' && *slash != '\\')
                    slash++;
                if (*slash == '\\') {
                    *slash = '\0';
                    const char *name = MakeString(slash + 1);
                    GetCollection(MakeString(line), name);
                }
            }
            line = at;
        }
        UMemory::Free(text);
    }
    loadingEnabled = 1;
    loadingDatabase = 0;
}

// A new collection is made in a temporary, copied into the map's node, and the temporaries' (empty) attribute
// maps torn down. Every collection but a "default" gets its class's "default" as its parent - made too if need be.
// FUNC_AT(0x00058cc0)
AttributeCollection* AttributeSystem::GetCollection(const char *className, const char *name) {
    const char *storedName = MakeString(name);
    const char *storedClass = MakeString(className);
    CollectionKey key = { storedClass, storedName };
    CollectionMap::Iterator found;
    collections->Find(&found, key);
    if (found.node != collections->head)
        return &found.node->value.collection;
    AttributeCollection made;
    made.Construct(storedClass, storedName);
    CollectionEntry entry;
    entry.key = key;
    entry.collection.ConstructCopy(made);
    CollectionMap::InsertResult inserted;
    collections->InsertUnique(&inserted, entry);
    CollectionNode *node = inserted.position.node;
    entry.collection.attributes.Destruct();
    made.attributes.Destruct();
    if (strcmp(name, kDefaultCollection) != 0 && node->value.collection.parent == NULL)
        node->value.collection.SetParent(GetCollection(className, kDefaultCollection));
    return &node->value.collection;
}

// The collection's file, <class>/<name>.atr: the current track's section, the collection's own (unless it is the
// "default" collection, whose own section is the next one) and "[default]", in that order. Reading takes a
// reference of its own on the collection, which is never given back.
// FUNC_AT(0x00057ee0)
void AttributeSystem::LoadCollection(AttributeCollection *collection) {
    if (!loadingEnabled)
        return;
    collection->refCount++;
    collection->loaded = 1;
    char path[64];
    char *text = static_cast<char *>(UFileLoader::FileLoadz(
        BuildPath(path, 0, kAttributeDirectory, collection->className, collection->name, "atr"), 0x100));
    if (text == NULL)
        return;
    DAFI *file = DAFI_open(text, int(UMemory::Size(text)));
    if (DAFI_setsection(file, collectionSection) >= 0)
        ProcessDafiSection(&file, collection);
    if (DAFI_setsection(file, collection->name) >= 0 && strcmp(collection->name, kDefaultCollection) != 0)
        ProcessDafiSection(&file, collection);
    if (DAFI_setsection(file, kDefaultCollection) >= 0)
        ProcessDafiSection(&file, collection);
    if (file != NULL)
        DAFI_close(file);
    UMemory::Free(text);
}

// Each key: its type suffix taken off, and either parsed into the collection's extension structure (a field of
// the class's, whatever the suffix) - the attribute then points into the structure - or parsed by the suffix's
// parser into a buffer and stored as a value that owns a copy (one element held in the value itself). A later
// section's key overwrites an earlier one's value.
// FUNC_AT(0x00057a80)
void AttributeSystem::ProcessDafiSection(DAFI **file, AttributeCollection *collection) {
    const char *className = collection->className;
    AttributeFieldMap *fields = extensionFields->Lookup(className);
    int keys = DAFI_getkeycount(*file);
    for (int i = 0; i < keys; i++) {
        const char *key = DAFI_getkeybyindex(*file, i);
        const char *text = DAFI_getvaluebyindex(*file, i);
        if (key[0] == '\0')
            continue;
        int type = kAttributeNone;
        size_t length = strlen(key);
        const char *suffix = key + length - 2;
        if (suffix > key && suffix[0] == '.') {
            switch (suffix[1]) {
            case 'b': type = kAttributeBool; break;
            case 'f': type = kAttributeFloat; break;
            case 'i': type = kAttributeInt; break;
            case 'm': type = kAttributeMatrix; break;
            case 'r': type = kAttributeSymbol; break;
            case 's': type = kAttributeString; break;
            case 'u': type = kAttributeUInt; break;
            case 'v': type = kAttributeVector; break;
            }
            length -= 2;
        }
        char name[128];
        strncpy(name, key, length);
        name[length] = '\0';
        const char *stored = MakeString(name);

        bool copy = true;
        AttributeFieldNode *field = fields->Lbound(stored);
        if (field == fields->head || CRT_stricmp(stored, field->value.name) < 0)
            field = fields->head;
        AttributeParseResult result;
        uint8_t buffer[1024];   // what a plain key's parser writes into
        if (field != fields->head) {
            AttributeValue *structure = AttributeSystemInstance->CreateExtensionAttribute(field->value.field.type,
                                                                                         collection);
            structure->flags |= kAttributeUsed;
            field->value.field.Parse(&result, stored, text, static_cast<char *>(structure->data));
            copy = false;
        } else {
            if (type == kAttributeNone)
                type = kAttributeString;
            result = *kParsers[-2 - type](&result, stored, text, 0, 0x100, 0, 0, reinterpret_cast<char *>(buffer));
        }
        if (result.count == 0)
            continue;

        AttributeMap *attributes = &collection->attributes;
        AttributeValue value;
        switch (result.type) {
        case kAttributeSymbol:
        case kAttributeString:   // the string (or symbol) itself, never a copy
            value.count = 1;
            value.flags = 0;
            value.type = static_cast<int8_t>(result.type);
            value.data = *static_cast<void **>(result.data);
            attributes->Lookup(stored)->Assign(value);
            value.Destruct();
            break;
        case kAttributeMatrix:
            value.ConstructMatrices(result.data, static_cast<uint16_t>(result.count), copy);
            attributes->Lookup(stored)->Assign(value);
            value.Destruct();
            break;
        case kAttributeVector:
            value.ConstructVectors(result.data, static_cast<uint16_t>(result.count), copy);
            attributes->Lookup(stored)->Assign(value);
            value.Destruct();
            break;
        case kAttributeFloat:
            SetFloatAttribute(attributes, stored, copy, &result);
            break;
        case kAttributeUInt:
            SetUIntAttribute(attributes, stored, copy, &result);
            break;
        case kAttributeInt:
            SetIntAttribute(attributes, stored, copy, &result);
            break;
        case kAttributeBool:
            SetBoolAttribute(attributes, stored, copy, &result);
            break;
        }
    }
}

// FUNC_AT(0x00057520)
void SetBoolAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed) {
    SetPlainAttribute<kAttributeBool>(attributes, name, copy, parsed);
}

// FUNC_AT(0x00057650)
void SetIntAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed) {
    SetPlainAttribute<kAttributeInt>(attributes, name, copy, parsed);
}

// FUNC_AT(0x00057780)
void SetUIntAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed) {
    SetPlainAttribute<kAttributeUInt>(attributes, name, copy, parsed);
}

// FUNC_AT(0x000578b0)
void SetFloatAttribute(AttributeMap *attributes, const char *name, bool copy, const AttributeParseResult *parsed) {
    SetPlainAttribute<kAttributeFloat>(attributes, name, copy, parsed);
}

// The stored copy: found in the string set, or copied into the fullest store block it fits in (a new block when
// none has room), the blocks sorted again, and added to the set.
// FUNC_AT(0x00055da0)
const char* AttributeSystem::MakeString(const char *text) {
    if (text == NULL)
        return NULL;
    StringSet::Iterator found;
    strings->Find(&found, text);
    if (found.node == strings->head) {
        uint32_t bytes = static_cast<uint32_t>(strlen(text)) + 1;
        AttributeStoreBlock *block = storeBlocks->first;
        while (block != storeBlocks->last && block->Free() < bytes)
            block++;
        if (block == storeBlocks->last) {
            AttributeStoreBlock fresh = { NULL, 0 };
            storeBlocks->PushBack(fresh);
            block = storeBlocks->last - 1;
        }
        if (block->block == NULL)
            block->block = static_cast<char *>(OperatorNewArray(AttributeStoreBlock::kBlockSize));
        char *stored = block->block + block->used;
        block->used += bytes;
        StoreBlockSort(storeBlocks->first, storeBlocks->last, static_cast<int>(storeBlocks->last - storeBlocks->first));
        strcpy(stored, text);
        StringSet::InsertResult inserted;
        const char *storedText = stored;
        strings->InsertUnique(&inserted, storedText);
        found = inserted.position;
    }
    return found.node->value;
}

// FUNC_AT(0x00053990)
int AttributeSystem::CountClassNames(const char *className) {
    int count = 0;
    CollectionKey key = { className, "" };
    CollectionNode *node = collections->Lbound(key);
    while (node != collections->head) {
        if (CRT_stricmp(className, node->value.key.className) != 0)
            return count;
        CollectionMap::Iterator next = { node };
        next.Inc();
        node = next.node;
        count++;
    }
    return count;
}

// The first collection of the class at or after `after` - and then the next one, when `after` was given. Past
// the last collection of the map it compares the head's (unset) key, as the game does.
// FUNC_AT(0x00053a40)
const char* AttributeSystem::GetClassNextName(const char *className, const char *after) {
    CollectionKey key = { className, after != NULL ? after : "" };
    CollectionMap::Iterator at = { collections->Lbound(key) };
    if (at.node == collections->head || CRT_stricmp(className, at.node->value.key.className) != 0)
        return NULL;
    if (after != NULL) {
        at.Inc();
        if (CRT_stricmp(className, at.node->value.key.className) != 0)
            return NULL;
    }
    return at.node->value.key.name;
}

// ---------------------------------------------------------------------------------------------------------------
// Extension types.

AttributeExtension &AttributeSystem::ExtensionType(uint32_t type) {
    ExtensionTypeEntry entry;
    entry.type = type;
    entry.extension.init = NULL;
    entry.extension.size = 0;
    entry.extension.type = 0xffffffff;
    entry.extension.attributeName = NULL;
    entry.extension.className = NULL;
    ExtensionTypeMap::InsertResult inserted;
    extensionTypes->InsertUnique(&inserted, entry);
    return inserted.position.node->value.extension;
}

// FUNC_AT(0x00055a90)
uint32_t AttributeSystem::RegisterExtensionType(const char *className, const char *attributeName, uint32_t size,
                                                AttributeExtensionInit init) {
    uint32_t type = nextExtensionType;
    ExtensionTypeEntry entry;
    entry.type = type;
    entry.extension.init = init;
    entry.extension.size = size;
    entry.extension.type = type;
    entry.extension.attributeName = attributeName;
    entry.extension.className = className;
    ExtensionTypeMap::InsertResult inserted;
    extensionTypes->InsertUnique(&inserted, entry);
    nextExtensionType++;
    return type;
}

// FUNC_AT(0x00057240)
void AttributeSystem::RegisterExtensionField(uint32_t type, const char *name, AttributeParserFunc parser,
                                             uint32_t offset, uint32_t count, uint32_t unused1, uint32_t unused2) {
    const char *className = ExtensionType(type).className;
    AttributeFieldMap *fields = extensionFields->Lookup(className);
    AttributeFieldEntry entry;
    entry.name = name;
    entry.field.parser = parser;
    entry.field.offset = offset;
    entry.field.count = count;
    entry.field.unused1 = unused1;
    entry.field.unused2 = unused2;
    entry.field.type = type;
    AttributeFieldMap::InsertResult inserted;
    fields->InsertUnique(&inserted, entry);
}

// FUNC_AT(0x00055b30)
const char* AttributeSystem::GetExtensionTypeClass(uint32_t type) {
    return ExtensionType(type).className;
}

// FUNC_AT(0x00055b80)
void AttributeSystem::InitializeExtensionType(int type, const char *collectionName, void *data) {
    AttributeExtension &extension = ExtensionType(type);
    if (extension.init != NULL)
        extension.init(extension.className, collectionName, extension.attributeName, extension.type, data);
}

// FUNC_AT(0x000570c0)
AttributeValue* AttributeSystem::CreateExtensionAttribute(uint32_t type, AttributeCollection *collection) {
    AttributeExtension &extension = ExtensionType(type);
    const char *name = extension.attributeName;
    AttributeMap &attributes = collection->attributes;
    AttributeNode *node = attributes.Lbound(name);
    if (node == attributes.head || CRT_stricmp(name, node->value.name) < 0)
        node = attributes.head;
    if (node == attributes.head) {
        AttributeValue structure;
        structure.ConstructStruct(NULL, collection->name, static_cast<int8_t>(extension.type));
        AttributeMapValue entry;
        entry.name = extension.attributeName;
        entry.value.ConstructCopy(structure);
        AttributeMap::InsertResult inserted;
        attributes.InsertUnique(&inserted, entry);
        node = inserted.position.node;
        entry.value.Destruct();
        structure.Destruct();
    }
    return &node->value.value;
}

// FUNC_AT(0x000525f0)
void AttributeSystem::ConfigEditParameters(const char *className, const char *name, uint32_t low, float high,
                                           float scope, uint32_t unknown6) {
}
