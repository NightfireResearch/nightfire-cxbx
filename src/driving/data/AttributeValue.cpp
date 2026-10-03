#include "AttributeValue.h"
#include "AttributeSystem.h"
#include "AttributeUntested.h"
#include "../platform/RealPrint.h"

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// AttributeValue (0x00055cf0..0x00056190): the size of a value's data, its block, and the constructors, copies and
// destructor std::map uses. A value owns its data only when kAttributeOwnsData is set; otherwise the data belongs
// to someone else (the string store, the parse buffer of a key being read, an extension structure) and copying the
// value copies the pointer. A value with count 0 keeps its one element in the pointer's place, but ComputeBytes
// still says a one-element block: the copy constructors allocate one element for count 0 (only for owning values,
// which never have count 0).
// ---------------------------------------------------------------------------------------------------------------

#define UMemory_FastAlloc ((void *(*)(uint32_t, const char *))0x00114750)
#define UMemory_FastFree ((void (*)(void *, uint32_t))0x001147d0)

// FUNC_AT(0x00055cf0)
uint32_t AttributeValue::ComputeBytes(int type, uint32_t count) {
    switch (type) {
    case kAttributeSymbol:
    case kAttributeString:
    case kAttributeNone:
        return 0;
    case kAttributeMatrix:
        return count * 64;
    case kAttributeVector:
        return count * 16;
    case kAttributeFloat:
    case kAttributeUInt:
    case kAttributeInt:
        return count * 4;
    case kAttributeBool:
        return count;
    default:   // an extension structure (looked up through operator[], which adds an empty entry for an unknown id)
        return AttributeSystemInstance->ExtensionType(type).size;
    }
}

// FUNC_AT(0x00055ec0)
void* AttributeValue::Alloc(int type, uint32_t count, const void *source) {
    uint32_t bytes = ComputeBytes(type, count);
    if (bytes == 0)
        return NULL;
    void *block = UMemory_FastAlloc(bytes, "AttributeValue");
    if (source != NULL) {
        MEM_copy(block, source, bytes);
        return block;
    }
    MEM_fill(block, 0, bytes);
    return block;
}

// FUNC_AT(0x00055fe0)
AttributeValue* AttributeValue::ConstructCopy(const AttributeValue &other) {
    count = other.count;
    flags = other.flags;
    type = other.type;
    if (flags & kAttributeOwnsData)
        data = Alloc(other.type, other.count == 0 ? 1 : other.count, other.data);
    else
        data = other.data;
    return this;
}

// FUNC_AT(0x00056040)
void AttributeValue::Destruct() {
    uint32_t bytes = 0;
    if ((flags & kAttributeOwnsData) && data != NULL)
        bytes = ComputeBytes(type, count);
    if (data != NULL && bytes != 0)
        UMemory_FastFree(data, bytes);
}

// FUNC_AT(0x00056080)
AttributeValue* AttributeValue::Assign(const AttributeValue &other) {
    uint32_t bytes = 0;
    if ((flags & kAttributeOwnsData) && data != NULL)
        bytes = ComputeBytes(type, count);
    if (data != NULL && bytes != 0)
        UMemory_FastFree(data, bytes);
    type = other.type;
    count = other.count;
    flags = other.flags;
    if (flags & kAttributeOwnsData)
        data = Alloc(other.type, other.count == 0 ? 1 : other.count, other.data);
    else
        data = other.data;
    return this;
}

// FUNC_AT(0x00056110)
AttributeValue* AttributeValue::ConstructVectors(const void *source, uint16_t elements, bool copy) {
    count = elements;
    flags = copy ? kAttributeOwnsData : 0;
    type = kAttributeVector;
    data = copy ? Alloc(kAttributeVector, elements, source) : const_cast<void *>(source);
    return this;
}

// FUNC_AT(0x00056150)
AttributeValue* AttributeValue::ConstructMatrices(const void *source, uint16_t elements, bool copy) {
    ATTRIBUTE_UNTESTED("AttributeValue::ConstructMatrices (a \".m\" key)");
    count = elements;
    flags = copy ? kAttributeOwnsData : 0;
    type = kAttributeMatrix;
    data = copy ? Alloc(kAttributeMatrix, elements, source) : const_cast<void *>(source);
    return this;
}

// FUNC_AT(0x00056190)
AttributeValue* AttributeValue::ConstructStruct(void *structure, const char *collectionName, int8_t extensionType) {
    count = 1;
    flags = 0;
    type = extensionType;
    if (structure != NULL) {
        data = structure;
        return this;
    }
    flags = kAttributeOwnsData;
    data = Alloc(extensionType, 1, NULL);
    AttributeSystemInstance->InitializeExtensionType(type, collectionName, data);
    return this;
}
