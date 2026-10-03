#include "AttributeSet.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// AttributeSet (0x00052020, 0x00052030, 0x00057a40, 0x00058020..0x00058470, 0x00058f00, 0x00058f40, 0x000752f0):
// a counted reference to a collection, and the lookups. A lookup searches the collection and then each parent in
// turn, loading any not yet read; a key found anywhere is marked used (kAttributeUsed). A string value asked for
// as a bool, int, unsigned or float is parsed on the spot with that type's parser; any other value is read as the
// asked type whatever it holds (one element in the value itself, else the first element its data points at).
// ---------------------------------------------------------------------------------------------------------------

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

#define DefaultVector (*(AttributeVector *)0x001d4c00)   // (0,0,0,1), filled in by a static initialiser

// FUNC_AT(0x00058f00)
AttributeSet* AttributeSet::Construct(const char *className, const char *name) {
    AttributeSystem *system = AttributeSystemInstance;
    AttributeCollection *found = system->GetCollection(className, name);
    if (!found->loaded)
        system->LoadCollection(found);
    found->refCount++;
    collection = found;
    return this;
}

// FUNC_AT(0x00052020)
AttributeSet* AttributeSet::ConstructCopy(const AttributeSet &other) {
    collection = other.collection;
    collection->refCount++;
    return this;
}

// FUNC_AT(0x00057a40)
void AttributeSet::Destruct() {
    collection->Release();
}

// FUNC_AT(0x000752f0)
void AttributeSet::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x00058f40)
void AttributeSet::SetName(const char *name, bool keepAsParent) {
    AttributeCollection *old = collection;
    AttributeSystem *system = AttributeSystemInstance;
    AttributeCollection *found = system->GetCollection(old->className, name);
    if (!found->loaded)
        system->LoadCollection(found);
    found->refCount++;
    collection = found;
    if (keepAsParent)
        found->SetParent(old);
    old->Release();
}

// FUNC_AT(0x00052030)
const char* AttributeSet::Name() {
    return collection->name;
}

// FUNC_AT(0x00058020)
bool AttributeSet::FindAttribute(const char *key, AttributeValue **value) {
    for (AttributeCollection *at = collection; at != NULL; at = at->parent) {
        if (!at->loaded)
            AttributeSystemInstance->LoadCollection(at);
        AttributeMap &attributes = at->attributes;
        AttributeNode *node = attributes.Lbound(key);
        if (node == attributes.head || CRT_stricmp(key, node->value.name) < 0)
            node = attributes.head;
        if (node != attributes.head) {
            *value = &node->value.value;
            return true;
        }
    }
    *value = NULL;
    return false;
}

// FUNC_AT(0x000580e0)
uint8_t AttributeSet::LookupBool(const char *key, bool *found) {
    AttributeValue *value;
    if (!FindAttribute(key, &value)) {
        if (found != NULL)
            *found = false;
        return 0;
    }
    if (found != NULL)
        *found = true;
    if (value->type == kAttributeString) {
        uint8_t parsed[2] = { 0, 0 };   // an empty string writes two bytes
        value->flags |= kAttributeUsed;
        AttributeParseResult result;
        Bool_AttribByteOffsetParserFunc(&result, key, static_cast<const char *>(value->data), 0, 1, 0, 0,
                                        reinterpret_cast<char *>(parsed));
        return parsed[0];
    }
    value->flags |= kAttributeUsed;
    if (value->count != 0)
        return *static_cast<const uint8_t *>(value->data);
    return static_cast<uint8_t>(value->bits);
}

// FUNC_AT(0x00058180)
int32_t AttributeSet::LookupInt(const char *key, bool *found) {
    AttributeValue *value;
    if (!FindAttribute(key, &value)) {
        if (found != NULL)
            *found = false;
        return 0;
    }
    if (found != NULL)
        *found = true;
    if (value->type == kAttributeString) {
        int32_t parsed = 0;
        value->flags |= kAttributeUsed;
        AttributeParseResult result;
        Int_AttribByteOffsetParserFunc(&result, key, static_cast<const char *>(value->data), 0, 1, 0, 0,
                                       reinterpret_cast<char *>(&parsed));
        return parsed;
    }
    value->flags |= kAttributeUsed;
    if (value->count != 0)
        return *static_cast<const int32_t *>(value->data);
    return static_cast<int32_t>(value->bits);
}

// FUNC_AT(0x00058220)
uint32_t AttributeSet::LookupUInt(const char *key, bool *found) {
    AttributeValue *value;
    if (!FindAttribute(key, &value)) {
        if (found != NULL)
            *found = false;
        return 0;
    }
    if (found != NULL)
        *found = true;
    if (value->type == kAttributeString) {
        uint32_t parsed = 0;
        value->flags |= kAttributeUsed;
        AttributeParseResult result;
        UInt_AttribByteOffsetParserFunc(&result, key, static_cast<const char *>(value->data), 0, 1, 0, 0,
                                        reinterpret_cast<char *>(&parsed));
        return parsed;
    }
    value->flags |= kAttributeUsed;
    if (value->count != 0)
        return *static_cast<const uint32_t *>(value->data);
    return value->bits;
}

// FUNC_AT(0x000582c0)
float AttributeSet::LookupFloat(const char *key, bool *found) {
    AttributeValue *value;
    if (!FindAttribute(key, &value)) {
        if (found != NULL)
            *found = false;
        return 0.0f;
    }
    if (found != NULL)
        *found = true;
    if (value->type == kAttributeString) {
        float parsed = 0.0f;
        value->flags |= kAttributeUsed;
        AttributeParseResult result;
        Float_AttribByteOffsetParserFunc(&result, key, static_cast<const char *>(value->data), 0, 1, 0, 0,
                                         reinterpret_cast<char *>(&parsed));
        return parsed;
    }
    value->flags |= kAttributeUsed;
    if (value->count != 0)
        return *static_cast<const float *>(value->data);
    float inlined;
    memcpy(&inlined, &value->bits, sizeof(inlined));
    return inlined;
}

// FUNC_AT(0x00058360)
const AttributeVector* AttributeSet::LookupVector(const char *key, bool *found) {
    AttributeValue *value;
    if (FindAttribute(key, &value)) {
        if (found != NULL)
            *found = true;
        value->flags |= kAttributeUsed;
        return static_cast<const AttributeVector *>(value->data);
    }
    if (found != NULL)
        *found = false;
    return &DefaultVector;
}

// FUNC_AT(0x000583a0)
const char* AttributeSet::LookupString(const char *key, bool *found) {
    AttributeValue *value;
    if (FindAttribute(key, &value)) {
        if (found != NULL)
            *found = true;
        value->flags |= kAttributeUsed;
        return static_cast<const char *>(value->data);
    }
    if (found != NULL)
        *found = false;
    return NULL;
}

// FUNC_AT(0x000583e0)
const char* AttributeSet::LookupValidString(const char *key, bool *found) {
    AttributeValue *value;
    if (FindAttribute(key, &value)) {
        if (found != NULL)
            *found = true;
        value->flags |= kAttributeUsed;
        return static_cast<const char *>(value->data);
    }
    if (found != NULL)
        *found = false;
    return "";
}

// `found` is not written.
// FUNC_AT(0x00058420)
void* AttributeSet::LookupStruct(const char *key, uint32_t type, bool *found) {
    AttributeValue *value;
    if (!FindAttribute(key, &value)) {
        AttributeValue *made = AttributeSystemInstance->CreateExtensionAttribute(type, collection);
        made->flags |= kAttributeUsed;
        return made->data;
    }
    value->flags |= kAttributeUsed;
    return value->data;
}
