#ifndef DRIVING_DATA_ATTRIBUTEPARSERS_H_
#define DRIVING_DATA_ATTRIBUTEPARSERS_H_

// The attribute system's parsers (the game's *_AttribByteOffsetParserFunc): each reads a value's text from an
// .atr file into memory - the parse buffer of a plain key, or a field of an extension structure - and says what it
// wrote. See AttributeParsers.cpp.

#include <stdint.h>

// What a parser wrote (returned through a hidden pointer, which the parser also returns).
struct AttributeParseResult {   // 0xc
    int32_t type;               // +0x00 AttributeType of the elements; -1 when there was nothing to parse
    uint32_t count;             // +0x04 elements written
    void *data;                 // +0x08 where (base + offset); for a symbol, NameLookup's answer
};
static_assert(sizeof(AttributeParseResult) == 0xc, "a parse result is 12 bytes");

// A parser: up to `count` elements of `text`, separated by ',' or ';' (a vector's or matrix's components by ',',
// the vectors or matrices by ';'), written at base + offset. `name` is the key (unused by the stock parsers), and
// unused1/unused2 are the extension field's two spare words, passed through.
typedef AttributeParseResult *(*AttributeParserFunc)(AttributeParseResult *result, const char *name,
                                                     const char *text, uint32_t offset, uint32_t count,
                                                     uint32_t unused1, uint32_t unused2, char *base);

// One field of an extension structure (AttributeSystem::RegisterExtensionField): the key that sets it, in the
// extension class's field map, maps to this.
struct AttributeField {         // 0x18
    AttributeParserFunc parser; // +0x00
    uint32_t offset;            // +0x04 the field's offset in the structure
    uint32_t count;             // +0x08 elements
    uint32_t unused1;           // +0x0c
    uint32_t unused2;           // +0x10
    uint32_t type;              // +0x14 the extension type the structure is

    // The field's parser on `text`, into the structure at `base`; type -1, nothing written, without a parser.
    AttributeParseResult *Parse(AttributeParseResult *result, const char *name, const char *text,
                                char *base);                                                    // 0x00052560
};
static_assert(sizeof(AttributeField) == 0x18, "an extension field is 24 bytes");

AttributeParseResult *Bool_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                      const char *text, uint32_t offset, uint32_t count,
                                                      uint32_t unused1, uint32_t unused2, char *base);     // 0x00052040
AttributeParseResult *Int_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                     const char *text, uint32_t offset, uint32_t count,
                                                     uint32_t unused1, uint32_t unused2, char *base);      // 0x00052170
AttributeParseResult *UInt_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                      const char *text, uint32_t offset, uint32_t count,
                                                      uint32_t unused1, uint32_t unused2, char *base);     // 0x00052210
AttributeParseResult *Float_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                       const char *text, uint32_t offset, uint32_t count,
                                                       uint32_t unused1, uint32_t unused2, char *base);    // 0x000522b0
AttributeParseResult *Vector_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base);   // 0x00052350
AttributeParseResult *Matrix_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base);   // 0x00052430
AttributeParseResult *Symbol_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base);   // 0x00052510
AttributeParseResult *String_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base);   // 0x00055f20

#endif // DRIVING_DATA_ATTRIBUTEPARSERS_H_
