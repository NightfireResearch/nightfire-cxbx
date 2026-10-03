#ifndef DRIVING_DATA_ATTRIBUTEVALUE_H_
#define DRIVING_DATA_ATTRIBUTEVALUE_H_

// AttributeValue, one value in an attribute collection (AttributeSystem.h): a type, a count and the data - held
// in the value itself when there is one plain value, otherwise pointed at (a block of its own, or somewhere else:
// a string in the system's string store, a field in an extension structure). See AttributeValue.cpp.

#include <stdint.h>

// What a value holds. The negative codes are the built-in types; an extension structure's type is its id,
// counted from 1 (AttributeSystem::RegisterExtensionType). A key's suffix in an .atr file picks a built-in type:
// ".b" bool, ".i" int, ".u" unsigned, ".f" float, ".v" vector, ".m" matrix, ".s" string, ".r" symbol; no suffix
// is a string unless the collection's class registered the key as an extension field.
enum AttributeType : int8_t {
    kAttributeNone = -1,      // not yet set (a value map::operator[] made)
    kAttributeBool = -2,      // one byte
    kAttributeInt = -3,       // int32_t
    kAttributeUInt = -4,      // uint32_t
    kAttributeFloat = -5,     // float
    kAttributeVector = -6,    // four floats
    kAttributeMatrix = -7,    // sixteen floats
    kAttributeString = -8,    // a char * into the system's string store
    kAttributeSymbol = -9,    // a symbol table entry (USymbolTable::NameLookup)
};

// The data of a vector and a matrix value.
struct AttributeVector {
    float x, y, z, w;
};
struct AttributeMatrix {
    float m[4][4];
};

enum AttributeValueFlags : uint8_t {
    kAttributeOwnsData = 0x01,   // data is a block of its own (AttributeValue::Alloc), freed with the value
    kAttributeUsed = 0x02,       // looked up at least once
};

struct AttributeValue {   // 8 bytes
    uint16_t count;       // +0x00 elements data points at; 0: one value held in `bits` itself
    uint8_t flags;        // +0x02 AttributeValueFlags
    int8_t type;          // +0x03 AttributeType, or an extension type
    union {               // +0x04
        void *data;
        uint32_t bits;    // the value itself when count is 0 (a bool in the low byte)
    };

    // The size of `count` elements of `type`: 0 for strings, symbols and kAttributeNone, which own nothing.
    static uint32_t ComputeBytes(int type, uint32_t count);                                     // 0x00055cf0
    // A block of that size from the fixed-size pools, a copy of `source` or zero-filled; NULL for size 0.
    static void *Alloc(int type, uint32_t count, const void *source);                           // 0x00055ec0

    AttributeValue *ConstructCopy(const AttributeValue &other);                                 // 0x00055fe0
    void Destruct();                                                                            // 0x00056040
    AttributeValue *Assign(const AttributeValue &other);                                        // 0x00056080
    // `elements` vectors (or matrices) at `source`, copied into a block of the value's own if `copy`.
    AttributeValue *ConstructVectors(const void *source, uint16_t elements, bool copy);         // 0x00056110
    AttributeValue *ConstructMatrices(const void *source, uint16_t elements, bool copy);        // 0x00056150
    // An extension structure of `extensionType`: `structure` if given, else a new zero-filled one that the
    // type's initialiser fills in for the collection named `collectionName`.
    AttributeValue *ConstructStruct(void *structure, const char *collectionName, int8_t extensionType); // 0x00056190
};
static_assert(sizeof(AttributeValue) == 8, "an AttributeValue is 8 bytes");

#endif // DRIVING_DATA_ATTRIBUTEVALUE_H_
