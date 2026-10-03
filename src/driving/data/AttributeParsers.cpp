#include "AttributeParsers.h"
#include "AttributeSystem.h"
#include "AttributeUntested.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// The attribute parsers (0x00052040..0x00052560, 0x00055f20). Each walks `text` a token at a time - a token ends
// at ';', at ',' (except in the vector and matrix parsers, whose tokens are whole vectors), or at the end - and
// writes one element per token at base + offset, up to `count` elements. A token is copied into a buffer of the
// parser's own first (16 bytes for bools and integers, 32 for floats, 128 for a vector, 512 for a matrix); a token
// that fills the buffer stops the parse there, as does the end of the text. Numbers go through the C runtime's
// atol and atof (the game's own, so a bad number reads as the runtime reads it: 0, or its leading digits).
//
// The bool parser has an oddity worth knowing: an empty token (",," or a trailing ",") writes a 0 and moves on a
// byte before its usual write, so it writes two bytes for one element. The vector and matrix parsers start each
// element from the default (0,0,0,1) or identity and parse the components over it - so a short vector keeps the
// default's tail - but they write every element at the same place: of several vectors, the last one wins.
// ---------------------------------------------------------------------------------------------------------------

class USymbolTable;

#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_atol ((int32_t (*)(const char *))0x00133d51)
#define CRT_atof ((double (*)(const char *))0x00133e84)
#define USymbolTable_NameLookup ((void *(__fastcall *)(USymbolTable *, int, const char *, int *))0x0011a950)

// What a vector or matrix element starts from: the game's (0,0,0,1) and identity, globals its static
// initialisers fill in.
#define DefaultVector (*(AttributeVector *)0x001d4c00)
#define IdentityMatrix (*(AttributeMatrix *)0x001d4c10)

namespace {

enum TokenEnd : bool {
    kAtSemicolon = false,   // a token runs to ';' (vectors and matrices: their components are separated by ',')
    kAtComma = true,        // a token runs to ',' or ';'
};

// The next token of `text` into `token`, and `text` past it and past the ',' or ';' that ended it. False when the
// token filled the buffer, which ends the parse.
template <size_t N>
bool NextToken(const char *&text, char (&token)[N], TokenEnd end) {
    char *out = token;
    char c = *text;
    while (c != '\0' && !(end == kAtComma && c == ',') && c != ';' && out < token + (N - 1)) {
        *out++ = c;
        c = *++text;
    }
    *out = '\0';
    if (out + 1 == token + N)
        return false;
    if (*text == ',' || *text == ';')
        text++;
    return true;
}

AttributeParseResult *Finish(AttributeParseResult *result, AttributeType type, uint32_t count, void *data) {
    result->type = type;
    result->count = count;
    result->data = data;
    return result;
}

// The integer parsers: the same code for both, which only the type they report tells apart.
AttributeParseResult *ParseIntegers(AttributeParseResult *result, AttributeType type, const char *text,
                                    uint32_t count, char *start) {
    int32_t *out = reinterpret_cast<int32_t *>(start);
    uint32_t parsed = 0;
    while (*text != '\0' && parsed < count) {
        char token[16];
        if (!NextToken(text, token, kAtComma) || text == NULL)
            break;
        *out++ = CRT_atol(token);
        parsed++;
    }
    return Finish(result, type, parsed, start);
}

}  // namespace

// FUNC_AT(0x00052040)
AttributeParseResult* Bool_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                      const char *text, uint32_t offset, uint32_t count,
                                                      uint32_t unused1, uint32_t unused2, char *base) {
    char *start = base + offset;
    uint8_t *out = reinterpret_cast<uint8_t *>(start);
    uint32_t parsed = 0;
    while (*text != '\0' && parsed < count) {
        char token[16];
        if (!NextToken(text, token, kAtComma) || text == NULL)
            break;
        if (token[0] == '\0')
            *out++ = 0;
        if (CRT_stricmp(token, "true") == 0)
            *out = 1;
        else if (CRT_stricmp(token, "false") == 0)
            *out = 0;
        else if (CRT_stricmp(token, "on") == 0)
            *out = 1;
        else if (CRT_stricmp(token, "off") == 0)
            *out = 0;
        else
            *out = CRT_atol(token) != 0;
        out++;
        parsed++;
    }
    return Finish(result, kAttributeBool, parsed, start);
}

// FUNC_AT(0x00052170)
AttributeParseResult* Int_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                     const char *text, uint32_t offset, uint32_t count,
                                                     uint32_t unused1, uint32_t unused2, char *base) {
    return ParseIntegers(result, kAttributeInt, text, count, base + offset);
}

// FUNC_AT(0x00052210)
AttributeParseResult* UInt_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                      const char *text, uint32_t offset, uint32_t count,
                                                      uint32_t unused1, uint32_t unused2, char *base) {
    return ParseIntegers(result, kAttributeUInt, text, count, base + offset);
}

// FUNC_AT(0x000522b0)
AttributeParseResult* Float_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                       const char *text, uint32_t offset, uint32_t count,
                                                       uint32_t unused1, uint32_t unused2, char *base) {
    char *start = base + offset;
    float *out = reinterpret_cast<float *>(start);
    uint32_t parsed = 0;
    while (*text != '\0' && parsed < count) {
        char token[32];
        if (!NextToken(text, token, kAtComma) || text == NULL)
            break;
        *out++ = static_cast<float>(CRT_atof(token));
        parsed++;
    }
    return Finish(result, kAttributeFloat, parsed, start);
}

// FUNC_AT(0x00052350)
AttributeParseResult* Vector_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base) {
    char *start = base + offset;
    uint32_t parsed = 0;
    while (*text != '\0' && parsed < count) {
        char token[128];
        if (!NextToken(text, token, kAtSemicolon) || text == NULL)
            break;
        *reinterpret_cast<AttributeVector *>(start) = DefaultVector;
        AttributeParseResult components;
        Float_AttribByteOffsetParserFunc(&components, name, token, 0, 4, 0, 0, start);
        parsed++;
    }
    return Finish(result, kAttributeVector, parsed, start);
}

// FUNC_AT(0x00052430)
AttributeParseResult* Matrix_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base) {
    ATTRIBUTE_UNTESTED("Matrix_AttribByteOffsetParserFunc");
    char *start = base + offset;
    uint32_t parsed = 0;
    while (*text != '\0' && parsed < count) {
        char token[512];
        if (!NextToken(text, token, kAtSemicolon) || text == NULL)
            break;
        *reinterpret_cast<AttributeMatrix *>(start) = IdentityMatrix;
        AttributeParseResult components;
        Float_AttribByteOffsetParserFunc(&components, name, token, 0, 16, 0, 0, start);
        parsed++;
    }
    return Finish(result, kAttributeMatrix, parsed, start);
}

// The symbol parser, which Ghidra folds into the matrix parser: only the parser table reaches it, for a ".r" key
// (the disc has none, nor ".m" keys for the matrix parser). It stores what the system's symbol table answers for
// the whole text, and reports that answer itself as the data.
// FUNC_AT(0x00052510)
AttributeParseResult* Symbol_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base) {
    ATTRIBUTE_UNTESTED("Symbol_AttribByteOffsetParserFunc");
    int size = 0;
    void *symbol = USymbolTable_NameLookup(AttributeSystemInstance->symbolTable, 0, text, &size);
    *reinterpret_cast<void **>(base + offset) = symbol;
    return Finish(result, kAttributeSymbol, 1, symbol);
}

// FUNC_AT(0x00055f20)
AttributeParseResult* String_AttribByteOffsetParserFunc(AttributeParseResult *result, const char *name,
                                                        const char *text, uint32_t offset, uint32_t count,
                                                        uint32_t unused1, uint32_t unused2, char *base) {
    *reinterpret_cast<const char **>(base + offset) = AttributeSystemInstance->MakeString(text);
    return Finish(result, kAttributeString, 1, base + offset);
}

// FUNC_AT(0x00052560)
AttributeParseResult* AttributeField::Parse(AttributeParseResult *result, const char *name, const char *text,
                                            char *base) {
    if (parser != NULL) {
        AttributeParseResult parsed;
        *result = *parser(&parsed, name, text, offset, count, unused1, unused2, base);
        return result;
    }
    return Finish(result, kAttributeNone, 0, NULL);
}
