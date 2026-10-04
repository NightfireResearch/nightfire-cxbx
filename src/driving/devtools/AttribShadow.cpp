#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // (the build defines it; for a file compiled alone)
#endif

#include "AttribShadow.h"

#include "../data/AttributeSet.h"
#include "../data/AttributeSystem.h"
#include "../engine/UFileLoader.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_ATTRIBSHADOW=1: data/Attribute*.cpp against the originals (0x00052020..0x00059630 and 0x000752f0,
// swapped back in together for the original side, common/xbeOriginal.h). It needs the game's attribute system
// made, its extension types registered and the mission's archive open - the .atr files are only in the mission
// archives - so call it after Bond_StartUpSystem has prepared the database and the track has loaded (the first
// frame of a mission is fine). It does not change the game's own system: everything runs on systems of its own,
// with AttributeSystem::fgThis pointed at them while they run.
//
//   - Parsers: every parser (bool, int, unsigned, float, vector, matrix, string, symbol) on ~150 texts - every
//     separator, empty tokens, bad and huge numbers, spaces, tokens that fill the buffer, vectors and matrices
//     short and long - at several counts and two offsets, each into a buffer filled with a pattern: the result and
//     every byte of the buffer compared. AttributeField::Parse with and without a parser.
//   - Values: ComputeBytes and Alloc for every type, the vector/matrix/struct constructors, copy, assignment.
//   - Containers: random insert/erase/erase-range/find sequences on a string set, an attribute map and the
//     extension type map, the trees compared node for node (key, colour, depth) after every step; a field map
//     copied; SetBool/Int/UInt/FloatAttribute on every count and copy flag; std::sort over store blocks of random
//     fullness (with the depth allowance at 0 and 1 too, so the partition, median, heap sort and rotate all run)
//     and the store-block vector's insert at random places.
//   - The database: two systems, one built by the originals and one by the port, from the same calls - the
//     extension types and fields the game registered, PrepareDatabase (attrib.dir), an AttributeSet on every
//     collection (each .atr file read), every lookup of every key of every collection and its parents (and a
//     missing one), LookupStruct for every extension type of the class, class name enumeration, SetName, a
//     collection made after the database, MakeString past many blocks. Each side logs what it answered (strings
//     and structures by content), and then the two systems are walked and compared whole: the string set, every
//     store block's bytes, every collection with its attributes (type, count, flags, data by content; pointers to
//     collections and stored strings are compared by what they point at), the extension maps, and the trees'
//     shapes; again after every set is released; then both are destroyed.
//
// Prints one line: [attribshadow] ...: N cases, M checks, D differ. What it would catch, for one: a bool parser
// that did not write the extra byte for an empty token differs in the buffer bytes; a store-block sort that kept
// the list in the wrong order puts strings in other blocks, which the block comparison shows.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

int g_cases, g_checks, g_differ, g_faults;

void Report(const char *format, ...) {
    if (g_differ++ >= 10)
        return;
    char text[600];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    printf("[attribshadow] %s\n", text);
}

void Check(bool same, const char *format, ...) {
    g_checks++;
    if (same)
        return;
    char text[600];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Report("%s", text);
}

std::string Format(const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

std::string Hex(const void *data, size_t size) {
    std::string out;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        out += Format("%02x", bytes[i]);
    return out;
}

uint32_t g_seed = 0x2545f491;
uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

// The original side: the whole package swapped back in.
struct Originals {
    Originals() {
        XbeOriginal_RestoreRange(0x00052020, 0x00059630, true);
        XbeOriginal_Restore(0x000752f0, true);
    }
    ~Originals() {
        XbeOriginal_RestoreRange(0x00052020, 0x00059630, false);
        XbeOriginal_Restore(0x000752f0, false);
    }
};

typedef void (*Phase)(int side);   // side 0 runs the originals, side 1 the port

uint32_t g_faultCode, g_faultAddress;

#ifdef _MSC_VER
int FaultFilter(EXCEPTION_POINTERS *info) {
    g_faultCode = info->ExceptionRecord->ExceptionCode;
    g_faultAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool Guarded(Phase phase, int side) {
#ifdef _MSC_VER
    __try {
        phase(side);
        return true;
    } __except (FaultFilter(GetExceptionInformation())) {
        return false;
    }
#else
    phase(side);
    return true;
#endif
}

bool g_sideFaulted[2];

// Runs a phase on one side, with fgThis pointed at `system` while it runs.
bool RunSide(Phase phase, int side, AttributeSystem *system) {
    AttributeSystem *game = AttributeSystemInstance;
    AttributeSystemInstance = system;
    bool ok;
    if (side == 0) {
        Originals scope;
        ok = Guarded(phase, side);
    } else {
        ok = Guarded(phase, side);
    }
    AttributeSystemInstance = game;
    if (!ok) {
        g_faults++;
        g_sideFaulted[side] = true;
        printf("[attribshadow] the %s side faulted (exception %08x at %08x)\n", side == 0 ? "original" : "port",
               g_faultCode, g_faultAddress);
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------
// Tree walking (the game's layout; no game code).

template <class Node>
Node *Successor(Node *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    Node *parent;
    while (!(parent = node->parent)->isNil && node == parent->right)
        node = parent;
    return parent;
}

template <class Tree>
std::vector<typename Tree::Node *> InOrder(const Tree *tree) {
    std::vector<typename Tree::Node *> nodes;
    for (typename Tree::Node *node = tree->head->left; node != tree->head && nodes.size() < 100000;
         node = Successor(node))
        nodes.push_back(node);
    return nodes;
}

// The tree's shape: in order, each node's description, colour and depth (which determine the tree), and the head's
// links and the size.
template <class Node, class Describe>
void ShapeOf(Node *node, int depth, std::string &out, Describe describe) {
    if (node->isNil || depth > 64)
        return;
    ShapeOf(node->left, depth + 1, out, describe);
    out += Format("[%d%c ", depth, node->color ? 'b' : 'r') + describe(node) + "]";
    ShapeOf(node->right, depth + 1, out, describe);
}

template <class Tree, class Describe>
std::string Shape(const Tree *tree, Describe describe) {
    std::string out = Format("size %u:", tree->size);
    if (tree->head == NULL)
        return out + "nohead";
    ShapeOf(tree->head->parent, 0, out, describe);
    typename Tree::Node *leftmost = tree->head->left, *rightmost = tree->head->right;
    out += " L:" + (leftmost == tree->head ? std::string("head") : describe(leftmost));
    out += " R:" + (rightmost == tree->head ? std::string("head") : describe(rightmost));
    out += Format(" root%s", tree->head->parent->isNil ? "nil" : "");
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Scratch systems: made by the port's constructor, with the game's extension types copied in.

alignas(8) uint8_t g_scratchStorage[sizeof(AttributeSystem)];
AttributeSystem *g_scratch;

void RegisterGameExtensions(int side, AttributeSystem *system);

void MakeScratch() {
    AttributeSystem *game = AttributeSystemInstance;
    g_scratch = reinterpret_cast<AttributeSystem *>(g_scratchStorage);
    AttributeSystemInstance = g_scratch;
    g_scratch->Construct();
    g_scratch->symbolTable = game->symbolTable;
    AttributeSystemInstance = game;
}

// ---------------------------------------------------------------------------------------------------------------
// Parsers.

typedef AttributeParseResult *(*ParserFn)(AttributeParseResult *, const char *, const char *, uint32_t, uint32_t,
                                          uint32_t, uint32_t, char *);
struct ParserUnderTest {
    const char *name;
    uint32_t original;
    ParserFn port;
    bool relativeData;   // the result's data is base + offset (not the symbol parser's answer)
};
const ParserUnderTest kParsers[] = {
    { "Bool", 0x00052040, Bool_AttribByteOffsetParserFunc, true },
    { "Int", 0x00052170, Int_AttribByteOffsetParserFunc, true },
    { "UInt", 0x00052210, UInt_AttribByteOffsetParserFunc, true },
    { "Float", 0x000522b0, Float_AttribByteOffsetParserFunc, true },
    { "Vector", 0x00052350, Vector_AttribByteOffsetParserFunc, true },
    { "Matrix", 0x00052430, Matrix_AttribByteOffsetParserFunc, true },
    { "String", 0x00055f20, String_AttribByteOffsetParserFunc, true },
    { "Symbol", 0x00052510, Symbol_AttribByteOffsetParserFunc, false },
};

std::vector<std::string> g_texts;

void BuildTexts() {
    static const char *const fixed[] = {
        "", "1", "0", "true", "TRUE", "True", "false", "FALSE", "on", "ON", "off", "Off", "yes", "no", "2", "-1", " 1",
        "1 ", "\t1", "abc", "12abc", "1,0,1", "1;0", ",", ",,", ",,,", "1,", ",1", ";", ";;", "1;;1", "1,,1", "0x10",
        "010", "2147483647", "2147483648", "-2147483648", "-2147483649", "99999999999", "4294967295", "4294967296",
        "+5", "--5", "1.5", "-0.0", "0.0", "1e10", "1e38", "3.4028236e38", "1e40", "-1e40", "1e-38", "1e-45",
        "1e-50", ".5", "5.", "  3.25  ", "nan", "inf", "-inf", "0.1", "0.3333333333333333", "1.0000000596046448",
        "0.1,0.2,0.3,0.4", "0.1,0.2", "0.1,0.2,0.3", "1,2,3,4,5,6", "1,2,3,4;5,6,7,8", "1,2,3,4;5,6,7,8;9",
        "1;2;3;4", "-0.3957,-0.2454,-0.2415,1.0", "0.000,-0.143,0.254,1.0", "+0.3957 , -0.2454 ,x, 1",
        "123456789012345", "1234567890123456", "12345678901234567890",
        "1.2345678901234567890123456789", "1.234567890123456789012345678901", "1.2345678901234567890123456789012",
        "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15",
        "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17;2", "true,false,on,off,1,0,7,,x", "HORN1", "vanquish.crp",
        "SFX_GlassShatter1", "paris0.txt", "a b c", "a;b,c", "on;off", "1;0;1;0;1;0;1;0;1;0;1;0;1;0;1;0;1;0",
    };
    for (const char *text : fixed)
        g_texts.push_back(text);
    // Tokens of exactly the buffers' sizes, and one longer.
    for (int length : { 14, 15, 16, 30, 31, 32, 126, 127, 128, 510, 511, 512, 600 }) {
        std::string token;
        for (int i = 0; i < length; i++)
            token += i == 1 ? '.' : static_cast<char>('0' + i % 10);
        g_texts.push_back(token);
        g_texts.push_back(token + ",1;2");
    }
    static const char alphabet[] = "0123456789.,;-+e tTrRuUfFaAlLsSoOnNxX";
    for (int i = 0; i < 60; i++) {
        std::string text;
        int length = static_cast<int>(Next() % 40);
        for (int j = 0; j < length; j++)
            text += alphabet[Next() % (sizeof(alphabet) - 1)];
        g_texts.push_back(text);
    }
}

const uint32_t kCounts[] = { 1, 2, 4, 16, 0x40, 0 };
const uint32_t kOffsets[] = { 0, 3 };
const size_t kParseBuffer = 0x1100;

struct ParseRecord {
    AttributeParseResult result;
    AttributeParseResult *returned;
    std::vector<uint8_t> buffer;
};
std::vector<ParseRecord> g_parsed[2];
const ParserUnderTest *g_parser;

void ParsePhase(int side) {
    for (const std::string &text : g_texts) {
        for (uint32_t count : kCounts) {
            for (uint32_t offset : kOffsets) {
                ParseRecord record;
                record.buffer.assign(kParseBuffer, 0xa5);
                memset(&record.result, 0xcc, sizeof(record.result));
                ParserFn parser = side == 0 ? reinterpret_cast<ParserFn>(static_cast<uintptr_t>(g_parser->original))
                                            : g_parser->port;
                char *base = reinterpret_cast<char *>(record.buffer.data());
                record.returned = parser(&record.result, "KEY", text.c_str(), offset, count, 7, 9, base);
                // data relative to the buffer, so the two sides compare
                if (g_parser->relativeData && record.result.data != NULL)
                    record.result.data = reinterpret_cast<void *>(static_cast<char *>(record.result.data) - base);
                record.returned = record.returned == &record.result ? NULL : record.returned;
                g_parsed[side].push_back(record);
            }
        }
    }
}

void TestParsers() {
    for (const ParserUnderTest &parser : kParsers) {
        if (!parser.relativeData && g_scratch->symbolTable == NULL)
            continue;   // no symbol table to look in
        g_parser = &parser;
        g_parsed[0].clear();
        g_parsed[1].clear();
        if (!RunSide(ParsePhase, 0, g_scratch) || !RunSide(ParsePhase, 1, g_scratch))
            continue;
        size_t n = g_parsed[0].size() < g_parsed[1].size() ? g_parsed[0].size() : g_parsed[1].size();
        for (size_t i = 0; i < n; i++) {
            const ParseRecord &a = g_parsed[0][i], &b = g_parsed[1][i];
            const char *text = g_texts[i / (6 * 2)].c_str();
            g_cases++;
            Check(a.returned == b.returned, "%s(\"%s\"): returned another pointer", parser.name, text);
            Check(memcmp(&a.result, &b.result, sizeof(a.result)) == 0,
                  "%s(\"%s\") count %u: result %d,%u,%p vs %d,%u,%p", parser.name, text, kCounts[(i / 2) % 6],
                  a.result.type, a.result.count, a.result.data, b.result.type, b.result.count, b.result.data);
            size_t at = 0;
            while (at < kParseBuffer && a.buffer[at] == b.buffer[at])
                at++;
            Check(at == kParseBuffer, "%s(\"%s\") count %u offset %u: buffer differs at %u (%02x vs %02x)",
                  parser.name, text, kCounts[(i / 2) % 6], kOffsets[i % 2], (unsigned)at,
                  at < kParseBuffer ? a.buffer[at] : 0, at < kParseBuffer ? b.buffer[at] : 0);
        }
    }
}

// AttributeField::Parse, with a parser and without.
std::string g_fieldLog[2];

void FieldPhase(int side) {
    typedef AttributeParseResult *(__fastcall *ParseFn)(AttributeField *, int, AttributeParseResult *, const char *,
                                                       const char *, char *);
    AttributeField fields[2] = {
        { Float_AttribByteOffsetParserFunc, 8, 3, 1, 2, 5 },
        { NULL, 0, 1, 0, 0, 1 },
    };
    for (AttributeField &field : fields) {
        uint8_t structure[64];
        memset(structure, 0x5a, sizeof(structure));
        AttributeParseResult result;
        memset(&result, 0xcc, sizeof(result));
        char *base = reinterpret_cast<char *>(structure);
        if (side == 0)
            reinterpret_cast<ParseFn>(0x00052560)(&field, 0, &result, "MASS", "1.5,2,x", base);
        else
            field.Parse(&result, "MASS", "1.5,2,x", base);
        if (result.data != NULL)
            result.data = reinterpret_cast<void *>(static_cast<char *>(result.data) - base);
        g_fieldLog[side] += Hex(&result, sizeof(result)) + "/" + Hex(structure, sizeof(structure)) + ";";
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Values.

std::string g_valueLog[2];

void ValuePhase(int side) {
    typedef uint32_t (*ComputeBytesFn)(int, uint32_t);
    typedef void *(*AllocFn)(int, uint32_t, const void *);
    typedef AttributeValue *(__fastcall *VectorsFn)(AttributeValue *, int, const void *, uint16_t, bool);
    typedef AttributeValue *(__fastcall *StructFn)(AttributeValue *, int, void *, const char *, int8_t);
    typedef AttributeValue *(__fastcall *CopyFn)(AttributeValue *, int, const AttributeValue *);
    typedef void (__fastcall *DestructFn)(AttributeValue *, int);
    std::string &log = g_valueLog[side];
    bool original = side == 0;
    uint32_t types = AttributeSystemInstance->nextExtensionType;
    for (int type = -12; type <= static_cast<int>(types) + 1; type++) {
        for (uint32_t count : { 0u, 1u, 3u, 255u }) {
            uint32_t bytes = original ? reinterpret_cast<ComputeBytesFn>(0x00055cf0)(type, count)
                                      : AttributeValue::ComputeBytes(type, count);
            log += Format("cb %d %u = %u;", type, count, bytes);
        }
    }
    float source[64];
    for (int i = 0; i < 64; i++)
        source[i] = i * 0.25f - 3.0f;
    for (int type = -9; type <= -1; type++) {
        for (const void *from : { static_cast<const void *>(source), static_cast<const void *>(NULL) }) {
            void *block = original ? reinterpret_cast<AllocFn>(0x00055ec0)(type, 2, from)
                                   : AttributeValue::Alloc(type, 2, from);
            uint32_t bytes = AttributeValue::ComputeBytes(type, 2);
            log += Format("alloc %d %s:", type, block == NULL ? "null" : "") + (block ? Hex(block, bytes) : "") + ";";
            if (block != NULL)
                ((void (*)(void *, uint32_t))0x001147d0)(block, bytes);
        }
    }
    // the constructors, copy, assignment, destructor
    for (int kind = 0; kind < 6; kind++) {
        AttributeValue value, copy, assigned;
        memset(&value, 0, sizeof(value));
        bool own = (kind & 1) != 0;
        if (kind < 2) {
            if (original)
                reinterpret_cast<VectorsFn>(0x00056110)(&value, 0, source, 3, own);
            else
                value.ConstructVectors(source, 3, own);
        } else if (kind < 4) {
            if (original)
                reinterpret_cast<VectorsFn>(0x00056150)(&value, 0, source, 2, own);
            else
                value.ConstructMatrices(source, 2, own);
        } else {
            void *given = own ? NULL : source;
            if (original)
                reinterpret_cast<StructFn>(0x00056190)(&value, 0, given, "default", static_cast<int8_t>(types > 1 ? 1 : 0));
            else
                value.ConstructStruct(given, "default", static_cast<int8_t>(types > 1 ? 1 : 0));
        }
        uint32_t bytes = AttributeValue::ComputeBytes(value.type, value.count == 0 ? 1 : value.count);
        log += Format("v%d %u/%02x/%d own %d:", kind, value.count, value.flags, value.type, value.data != source)
               + (value.data && bytes ? Hex(value.data, bytes) : "") + ";";
        memset(&copy, 0, sizeof(copy));
        if (original)
            reinterpret_cast<CopyFn>(0x00055fe0)(&copy, 0, &value);
        else
            copy.ConstructCopy(value);
        log += Format("c%d %u/%02x/%d same %d:", kind, copy.count, copy.flags, copy.type, copy.data == value.data)
               + (copy.data && bytes ? Hex(copy.data, bytes) : "") + ";";
        assigned.count = 0;
        assigned.flags = 0;
        assigned.type = kAttributeInt;
        assigned.bits = 0x12345678;
        if (original)
            reinterpret_cast<CopyFn>(0x00056080)(&assigned, 0, &copy);
        else
            assigned.Assign(copy);
        log += Format("a%d %u/%02x/%d same %d:", kind, assigned.count, assigned.flags, assigned.type,
                      assigned.data == copy.data) + (assigned.data && bytes ? Hex(assigned.data, bytes) : "") + ";";
        DestructFn destruct = reinterpret_cast<DestructFn>(0x00056040);
        if (original) {
            destruct(&assigned, 0);
            destruct(&copy, 0);
            destruct(&value, 0);
        } else {
            assigned.Destruct();
            copy.Destruct();
            value.Destruct();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Trees: random operations, the same on both sides, the shapes compared.

const char *const kKeys[] = {
    "MASS", "mass", "HITPOINTS", "Glass", "glass", "default", "Default", "vanquish", "MAXACC", "MAXBRAKE",
    "GEAR_LIMIT1", "GEAR_LIMIT2", "GEAR_LIMIT3", "GEAR_RATIO1", "a", "b", "c", "aa", "ab", "ba", "z", "Z", "_", "0",
    "9", "AIC_BOND_POS", "AIC_PASS_POS", "RENDER_FILENAME", "SFX_ENGINE", "SFX_HORN", "TYRE_RADIUS", "WHEEL_RADIUS",
    "SPRING_STIFFNESS_FRONT", "SPRING_STIFFNESS_REAR", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
    "y1", "y2", "y3", "y4", "y5", "y6", "y7", "y8", "y9", "y10", "q", "w", "e", "r", "t", "u", "i", "o", "p", "s",
};
const int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

struct TreeOp {
    int kind;     // 0 insert, 1 erase the n-th, 2 erase a range, 3 find, 4 lower bound
    int key;
    int index;
    int span;
};
std::vector<TreeOp> g_treeOps;
std::vector<std::string> g_treeLog[2];

void BuildTreeOps() {
    for (int i = 0; i < 400; i++) {
        uint32_t r = Next() % 100;
        TreeOp op;
        op.kind = r < 55 ? 0 : r < 80 ? 1 : r < 84 ? 2 : r < 92 ? 3 : 4;
        op.key = static_cast<int>(Next() % kKeyCount);
        op.index = static_cast<int>(Next() % 64);
        op.span = static_cast<int>(Next() % 5);
        g_treeOps.push_back(op);
    }
}

template <class Tree>
void NewTree(Tree *tree, typename Tree::Node *head) {
    tree->allocator = 0;
    tree->head = head;
    head->isNil = 1;
    head->parent = head;
    head->left = head;
    head->right = head;
    tree->size = 0;
}

std::string DescribeString(StringNode *node) {
    return node->value;
}

// The string set.
void StringSetPhase(int side) {
    typedef StringSet::InsertResult *(__fastcall *InsertFn)(StringSet *, int, StringSet::InsertResult *, const char *const *);
    typedef StringSet::Iterator *(__fastcall *EraseFn)(StringSet *, int, StringSet::Iterator *, StringSet::Iterator);
    typedef StringSet::Iterator *(__fastcall *RangeFn)(StringSet *, int, StringSet::Iterator *, StringSet::Iterator, StringSet::Iterator);
    typedef StringSet::Iterator *(__fastcall *FindFn)(StringSet *, int, StringSet::Iterator *, const char *const *);
    typedef StringNode *(__fastcall *LboundFn)(StringSet *, int, const char *const *);
    typedef StringNode *(*BuyHeadFn)();
    bool original = side == 0;
    StringSet set;
    NewTree(&set, original ? reinterpret_cast<BuyHeadFn>(0x00053640)() : StringSet::BuyHead());
    std::vector<std::string> &log = g_treeLog[side];
    for (const TreeOp &op : g_treeOps) {
        const char *key = kKeys[op.key];
        std::vector<StringNode *> nodes = InOrder(&set);
        std::string answer;
        if (op.kind == 0) {
            StringSet::InsertResult result;
            if (original)
                reinterpret_cast<InsertFn>(0x00055390)(&set, 0, &result, &key);
            else
                set.InsertUnique(&result, key);
            answer = Format("ins %s %d ", key, result.inserted) + result.position.node->value;
        } else if ((op.kind == 1 || op.kind == 2) && !nodes.empty()) {
            StringSet::Iterator where = { nodes[op.index % nodes.size()] }, result;
            if (op.kind == 1) {
                if (original)
                    reinterpret_cast<EraseFn>(0x00054c60)(&set, 0, &result, where);
                else
                    set.Erase(&result, where);
            } else {
                size_t last = (op.index % nodes.size()) + op.span;
                StringSet::Iterator end = { last < nodes.size() ? nodes[last] : set.head };
                if (op.span == 4)   // the whole set: clear()
                    where.node = set.head->left, end.node = set.head;
                if (original)
                    reinterpret_cast<RangeFn>(0x00055620)(&set, 0, &result, where, end);
                else
                    set.EraseRange(&result, where, end);
            }
            answer = result.node == set.head ? "erase end" : std::string("erase ") + result.node->value;
        } else if (op.kind == 3) {
            StringSet::Iterator result;
            if (original)
                reinterpret_cast<FindFn>(0x00053b40)(&set, 0, &result, &key);
            else
                set.Find(&result, key);
            answer = result.node == set.head ? "find end" : std::string("find ") + result.node->value;
        } else if (op.kind == 4) {
            StringNode *node = original ? reinterpret_cast<LboundFn>(0x00053190)(&set, 0, &key) : set.Lbound(key);
            answer = node == set.head ? "lb end" : std::string("lb ") + node->value;
        }
        log.push_back(answer + " | " + Shape(&set, DescribeString));
    }
    StringSet::Iterator result, first = { set.head->left }, last = { set.head };
    if (original)
        reinterpret_cast<RangeFn>(0x00055620)(&set, 0, &result, first, last);
    else
        set.EraseRange(&result, first, last);
    log.push_back(Shape(&set, DescribeString));
    ((void (*)(void *, uint32_t))0x001147d0)(set.head, sizeof(StringNode));
}

// An attribute map: values that own blocks, so the node copies and frees show.
std::string DescribeAttribute(AttributeNode *node) {
    const AttributeValue &value = node->value.value;
    std::string out = Format("%s=%u/%02x/%d:", node->value.name, value.count, value.flags, value.type);
    uint32_t bytes = value.count * 16;
    return out + (value.count != 0 && value.data != NULL ? Hex(value.data, bytes) : Format("%08x", value.bits));
}

void AttributeMapPhase(int side) {
    typedef AttributeMap::Iterator *(__fastcall *EraseFn)(AttributeMap *, int, AttributeMap::Iterator *, AttributeMap::Iterator);
    typedef AttributeMap::Iterator *(__fastcall *RangeFn)(AttributeMap *, int, AttributeMap::Iterator *, AttributeMap::Iterator, AttributeMap::Iterator);
    typedef AttributeValue *(__fastcall *LookupFn)(AttributeMap *, int, const char *const *);
    typedef AttributeNode *(__fastcall *LboundFn)(AttributeMap *, int, const char *const *);
    typedef void (__fastcall *DestructFn)(AttributeMap *, int);
    typedef AttributeValue *(__fastcall *AssignFn)(AttributeValue *, int, const AttributeValue *);
    typedef void (*SetFn)(AttributeMap *, const char *, bool, const AttributeParseResult *);
    bool original = side == 0;
    AttributeMap map;
    AttributeNode *head = static_cast<AttributeNode *>(((void *(*)(uint32_t, const char *))0x00114750)(sizeof(AttributeNode), "STL"));
    head->left = head->parent = head->right = NULL;
    head->color = kTreeBlack;
    NewTree(&map, head);
    std::vector<std::string> &log = g_treeLog[side];
    float data[32];   // room for the five vectors a value can have here
    for (int i = 0; i < 32; i++)
        data[i] = static_cast<float>(i) * 1.5f;
    int step = 0;
    for (const TreeOp &op : g_treeOps) {
        const char *key = kKeys[op.key];
        std::vector<AttributeNode *> nodes = InOrder(&map);
        std::string answer;
        if (op.kind == 0 || op.kind == 3) {
            AttributeValue value;
            value.ConstructVectors(data, static_cast<uint16_t>(1 + op.span), (step & 1) != 0);
            AttributeValue *slot = original ? reinterpret_cast<LookupFn>(0x000572e0)(&map, 0, &key) : map.Lookup(key);
            if (original)
                reinterpret_cast<AssignFn>(0x00056080)(slot, 0, &value);
            else
                slot->Assign(value);
            value.Destruct();
            answer = Format("set %s", key);
        } else if ((op.kind == 1 || op.kind == 2) && !nodes.empty()) {
            AttributeMap::Iterator where = { nodes[op.index % nodes.size()] }, result;
            if (op.kind == 1) {
                if (original)
                    reinterpret_cast<EraseFn>(0x00056c60)(&map, 0, &result, where);
                else
                    map.Erase(&result, where);
            } else {
                size_t last = (op.index % nodes.size()) + op.span;
                AttributeMap::Iterator end = { last < nodes.size() ? nodes[last] : map.head };
                if (original)
                    reinterpret_cast<RangeFn>(0x000573a0)(&map, 0, &result, where, end);
                else
                    map.EraseRange(&result, where, end);
            }
            answer = result.node == map.head ? "erase end" : std::string("erase ") + result.node->value.name;
        } else if (op.kind == 4) {
            AttributeNode *node = original ? reinterpret_cast<LboundFn>(0x00052d90)(&map, 0, &key) : map.Lbound(key);
            answer = node == map.head ? "lb end" : std::string("lb ") + node->value.name;
        }
        step++;
        log.push_back(answer + " | " + Shape(&map, DescribeAttribute));
    }
    // SetAttribute<T> on every count and copy flag
    static const uint32_t kSetAddresses[] = { 0x00057520, 0x00057650, 0x00057780, 0x000578b0 };
    static const SetFn kSetPorts[] = { SetBoolAttribute, SetIntAttribute, SetUIntAttribute, SetFloatAttribute };
    static const char *const kSetKeys[] = { "SB", "SI", "SU", "SF" };
    for (int kind = 0; kind < 4; kind++) {
        for (uint32_t count : { 1u, 3u, 0u }) {
            for (int copy = 0; copy < 2; copy++) {
                AttributeParseResult parsed = { -2 - kind, count, data };
                if (original)
                    reinterpret_cast<SetFn>(static_cast<uintptr_t>(kSetAddresses[kind]))(&map, kSetKeys[kind], copy != 0, &parsed);
                else
                    kSetPorts[kind](&map, kSetKeys[kind], copy != 0, &parsed);
                AttributeValue *value = NULL;
                for (AttributeNode *node : InOrder(&map))
                    if (strcmp(node->value.name, kSetKeys[kind]) == 0)
                        value = &node->value.value;
                std::string described = "missing";
                if (value != NULL) {
                    uint32_t bits = value->bits;
                    if (value->count == 0 && kind == 0)
                        bits &= 0xff;   // the game leaves a bool's other three bytes as its stack had them
                    described = Format("%u/%02x/%d owns %d:", value->count, value->flags, value->type,
                                       value->data != data);
                    uint32_t elements = value->count != 0 ? value->count : 1;
                    if (value->count != 0 || (value->flags & kAttributeOwnsData))
                        described += value->data ? Hex(value->data, elements * (kind == 0 ? 1 : 4)) : "null";
                    else if (value->data == data)   // not copied: it points at the parsed data
                        described += "the parsed data";
                    else
                        described += Format("%08x", bits);
                }
                log.push_back(Format("set%d %u %d: ", kind, count, copy) + described);
            }
        }
    }
    if (original)
        reinterpret_cast<DestructFn>(0x00057ea0)(&map, 0);
    else
        map.Destruct();
    log.push_back(Format("destructed %p %u", (void *)map.head, map.size));
}

// The extension type map: integer keys, inserted through 0x000552d0 (another package's), erased by ours.
std::string DescribeType(ExtensionTypeNode *node) {
    return Format("%u:%u", node->value.type, node->value.extension.size);
}

void TypeMapPhase(int side) {
    typedef ExtensionTypeMap::InsertResult *(__fastcall *InsertFn)(ExtensionTypeMap *, int, ExtensionTypeMap::InsertResult *, const ExtensionTypeEntry *);
    typedef ExtensionTypeMap::Iterator *(__fastcall *EraseFn)(ExtensionTypeMap *, int, ExtensionTypeMap::Iterator *, ExtensionTypeMap::Iterator);
    typedef ExtensionTypeMap::Iterator *(__fastcall *RangeFn)(ExtensionTypeMap *, int, ExtensionTypeMap::Iterator *, ExtensionTypeMap::Iterator, ExtensionTypeMap::Iterator);
    typedef ExtensionTypeNode *(*BuyHeadFn)();
    bool original = side == 0;
    ExtensionTypeMap map;
    NewTree(&map, original ? reinterpret_cast<BuyHeadFn>(0x000535c0)() : ExtensionTypeMap::BuyHead());
    std::vector<std::string> &log = g_treeLog[side];
    for (const TreeOp &op : g_treeOps) {
        std::vector<ExtensionTypeNode *> nodes = InOrder(&map);
        std::string answer;
        if (op.kind == 0 || op.kind >= 3) {
            ExtensionTypeEntry entry;
            memset(&entry, 0, sizeof(entry));
            entry.type = static_cast<uint32_t>(op.key * 7 % 61);
            entry.extension.size = static_cast<uint32_t>(op.index);
            ExtensionTypeMap::InsertResult result;
            reinterpret_cast<InsertFn>(0x000552d0)(&map, 0, &result, &entry);
            answer = Format("ins %u %d", entry.type, result.inserted);
        } else if (!nodes.empty()) {
            ExtensionTypeMap::Iterator where = { nodes[op.index % nodes.size()] }, result;
            if (op.kind == 1) {
                if (original)
                    reinterpret_cast<EraseFn>(0x00054990)(&map, 0, &result, where);
                else
                    map.Erase(&result, where);
            } else {
                size_t last = (op.index % nodes.size()) + op.span;
                ExtensionTypeMap::Iterator end = { last < nodes.size() ? nodes[last] : map.head };
                if (original)
                    reinterpret_cast<RangeFn>(0x00055560)(&map, 0, &result, where, end);
                else
                    map.EraseRange(&result, where, end);
            }
            answer = result.node == map.head ? "erase end" : Format("erase %u", result.node->value.type);
        }
        log.push_back(answer + " | " + Shape(&map, DescribeType));
    }
    ExtensionTypeMap::Iterator result, first = { map.head->left }, last = { map.head };
    if (original)
        reinterpret_cast<RangeFn>(0x00055560)(&map, 0, &result, first, last);
    else
        map.EraseRange(&result, first, last);
    ((void (*)(void *, uint32_t))0x001147d0)(map.head, sizeof(ExtensionTypeNode));
}

// A field map: filled, copied (ConstructCopy: _Copy), erased at random.
std::string DescribeField(AttributeFieldNode *node) {
    return Format("%s:%u", node->value.name, node->value.field.offset);
}

void FieldMapPhase(int side) {
    typedef AttributeFieldMap::InsertResult *(__fastcall *InsertFn)(AttributeFieldMap *, int, AttributeFieldMap::InsertResult *, const AttributeFieldEntry *);
    typedef AttributeFieldMap *(__fastcall *CopyFn)(AttributeFieldMap *, int, const AttributeFieldMap *);
    typedef AttributeFieldMap::Iterator *(__fastcall *EraseFn)(AttributeFieldMap *, int, AttributeFieldMap::Iterator *, AttributeFieldMap::Iterator);
    typedef void (__fastcall *DestructFn)(AttributeFieldMap *, int);
    typedef AttributeFieldNode *(*BuyHeadFn)();
    bool original = side == 0;
    AttributeFieldMap map, copy;
    NewTree(&map, original ? reinterpret_cast<BuyHeadFn>(0x000536c0)() : AttributeFieldMap::BuyHead());
    std::vector<std::string> &log = g_treeLog[side];
    for (int i = 0; i < 40; i++) {
        AttributeFieldEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.name = kKeys[(i * 13) % kKeyCount];
        entry.field.offset = static_cast<uint32_t>(i);
        AttributeFieldMap::InsertResult result;
        if (original)
            reinterpret_cast<InsertFn>(0x00055200)(&map, 0, &result, &entry);
        else
            map.InsertUnique(&result, entry);
    }
    if (original)
        reinterpret_cast<CopyFn>(0x00055c60)(&copy, 0, &map);
    else
        copy.ConstructCopy(map);
    log.push_back(Shape(&map, DescribeField) + " / " + Shape(&copy, DescribeField));
    for (int i = 0; i < 30 && copy.size != 0; i++) {
        std::vector<AttributeFieldNode *> nodes = InOrder(&copy);
        AttributeFieldMap::Iterator where = { nodes[(i * 7) % nodes.size()] }, result;
        if (original)
            reinterpret_cast<EraseFn>(0x00054f30)(&copy, 0, &result, where);
        else
            copy.Erase(&result, where);
        log.push_back(Shape(&copy, DescribeField));
    }
    if (original) {
        reinterpret_cast<DestructFn>(0x00055af0)(&copy, 0);
        reinterpret_cast<DestructFn>(0x00055af0)(&map, 0);
    } else {
        copy.Destruct();
        map.Destruct();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The store blocks: std::sort and its helpers on random lists, and the vector's insert.

std::vector<std::vector<AttributeStoreBlock> > g_sortInputs;
std::vector<int> g_sortIdeals;
std::vector<std::string> g_sortLog[2];

void BuildSortInputs() {
    for (int i = 0; i < 120; i++) {
        size_t length = i < 20 ? static_cast<size_t>(i) : 2 + Next() % 140;
        std::vector<AttributeStoreBlock> blocks(length);
        for (size_t j = 0; j < length; j++) {
            blocks[j].block = reinterpret_cast<char *>(static_cast<uintptr_t>(j + 1));   // a tag, to see the order
            blocks[j].used = (i % 3 == 0) ? (Next() % 4) * 0x400 : Next() % 0x1001;
        }
        g_sortInputs.push_back(blocks);
        g_sortIdeals.push_back(i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : static_cast<int>(length));
    }
}

std::string DescribeBlocks(const std::vector<AttributeStoreBlock> &blocks) {
    std::string out;
    for (const AttributeStoreBlock &block : blocks)
        out += Format("%x:%x,", static_cast<unsigned>(reinterpret_cast<uintptr_t>(block.block)), block.used);
    return out;
}

void SortPhase(int side) {
    typedef void (*SortFn)(AttributeStoreBlock *, AttributeStoreBlock *, int);
    typedef void (*RangeFn)(AttributeStoreBlock *, AttributeStoreBlock *);
    typedef void (*ThreeFn)(AttributeStoreBlock *, AttributeStoreBlock *, AttributeStoreBlock *);
    typedef StoreBlockRange *(*PartitionFn)(StoreBlockRange *, AttributeStoreBlock *, AttributeStoreBlock *);
    bool original = side == 0;
    std::vector<std::string> &log = g_sortLog[side];
    g_seed = 0x0badf00d;   // the same random places on both sides
    for (size_t i = 0; i < g_sortInputs.size(); i++) {
        std::vector<AttributeStoreBlock> blocks = g_sortInputs[i];
        AttributeStoreBlock *first = blocks.data(), *last = first + blocks.size();
        if (original)
            reinterpret_cast<SortFn>(0x00053f90)(first, last, g_sortIdeals[i]);
        else
            StoreBlockSort(first, last, g_sortIdeals[i]);
        log.push_back("sort " + DescribeBlocks(blocks));
        if (g_sortInputs[i].size() < 3)
            continue;
        blocks = g_sortInputs[i];
        first = blocks.data();
        last = first + blocks.size();
        AttributeStoreBlock *mid = first + (Next() % blocks.size());
        if (original)
            reinterpret_cast<ThreeFn>(0x00053350)(first, mid, last);
        else
            StoreBlockRotate(first, mid, last);
        log.push_back("rotate " + DescribeBlocks(blocks));
        if (original)
            reinterpret_cast<ThreeFn>(0x00053820)(first, first + blocks.size() / 2, last - 1);
        else
            StoreBlockMedian(first, first + blocks.size() / 2, last - 1);
        log.push_back("median " + DescribeBlocks(blocks));
        StoreBlockRange range;
        if (original)
            reinterpret_cast<PartitionFn>(0x00053c50)(&range, first, last);
        else
            StoreBlockUnguardedPartition(&range, first, last);
        log.push_back(Format("partition %d %d ", static_cast<int>(range.first - first), static_cast<int>(range.last - first))
                      + DescribeBlocks(blocks));
        if (original) {
            reinterpret_cast<RangeFn>(0x00053900)(first, last);
            log.push_back("heap " + DescribeBlocks(blocks));
            reinterpret_cast<RangeFn>(0x00053e80)(first, last);
        } else {
            StoreBlockMakeHeap(first, last);
            log.push_back("heap " + DescribeBlocks(blocks));
            StoreBlockSortHeap(first, last);
        }
        log.push_back("heapsorted " + DescribeBlocks(blocks));
        blocks = g_sortInputs[i];
        first = blocks.data();
        last = first + blocks.size();
        if (original)
            reinterpret_cast<RangeFn>(0x00053ed0)(first, last);
        else
            StoreBlockInsertionSort(first, last);
        log.push_back("insertion " + DescribeBlocks(blocks));
    }
    // the vector: push_back and insert at random places, from empty
    typedef void (__fastcall *PushFn)(StoreBlockList *, int, const AttributeStoreBlock *);
    typedef void (__fastcall *InsertFn)(StoreBlockList *, int, AttributeStoreBlock *, uint32_t, const AttributeStoreBlock *);
    StoreBlockList list;
    memset(&list, 0, sizeof(list));
    for (int i = 0; i < 60; i++) {
        AttributeStoreBlock value = { reinterpret_cast<char *>(static_cast<uintptr_t>(0x100 + i)), static_cast<uint32_t>(i) };
        uint32_t r = Next();
        if (r % 3 == 0) {
            if (original)
                reinterpret_cast<PushFn>(0x00055bf0)(&list, 0, &value);
            else
                list.PushBack(value);
        } else {
            uint32_t size = list.first == NULL ? 0 : static_cast<uint32_t>(list.last - list.first);
            AttributeStoreBlock *where = list.first + (size == 0 ? 0 : (r >> 8) % (size + 1));
            uint32_t count = (r >> 16) % 6;
            if (original)
                reinterpret_cast<InsertFn>(0x000556e0)(&list, 0, where, count, &value);
            else
                list.InsertN(where, count, value);
        }
        std::vector<AttributeStoreBlock> contents(list.first, list.last);
        log.push_back(Format("vector cap %u: ", list.first == NULL ? 0u : static_cast<unsigned>(list.end - list.first))
                      + DescribeBlocks(contents));
    }
    if (list.first != NULL)
        ((void (*)(void *, uint32_t))0x001147d0)(list.first, static_cast<uint32_t>(list.end - list.first) * 8);
}

// ---------------------------------------------------------------------------------------------------------------
// The database, side by side.

#define Orig_SystemConstruct ((AttributeSystem *(__fastcall *)(AttributeSystem *, int))0x00059090)
#define Orig_SystemDestruct ((void (__fastcall *)(AttributeSystem *, int))0x000592d0)
#define Orig_SetCollectionSection ((void (__fastcall *)(AttributeSystem *, int, const char *))0x000525d0)
#define Orig_RegisterExtensionType ((uint32_t (__fastcall *)(AttributeSystem *, int, const char *, const char *, uint32_t, AttributeExtensionInit))0x00055a90)
#define Orig_RegisterExtensionField ((void (__fastcall *)(AttributeSystem *, int, uint32_t, const char *, AttributeParserFunc, uint32_t, uint32_t, uint32_t, uint32_t))0x00057240)
#define Orig_PrepareDatabase ((void (__fastcall *)(AttributeSystem *, int))0x00058e30)
#define Orig_GetCollection ((AttributeCollection *(__fastcall *)(AttributeSystem *, int, const char *, const char *))0x00058cc0)
#define Orig_MakeString ((const char *(__fastcall *)(AttributeSystem *, int, const char *))0x00055da0)
#define Orig_CountClassNames ((int (__fastcall *)(AttributeSystem *, int, const char *))0x00053990)
#define Orig_GetClassNextName ((const char *(__fastcall *)(AttributeSystem *, int, const char *, const char *))0x00053a40)
#define Orig_GetExtensionTypeClass ((const char *(__fastcall *)(AttributeSystem *, int, uint32_t))0x00055b30)
#define Orig_SetConstruct ((AttributeSet *(__fastcall *)(AttributeSet *, int, const char *, const char *))0x00058f00)
#define Orig_SetConstructCopy ((AttributeSet *(__fastcall *)(AttributeSet *, int, const AttributeSet *))0x00052020)
#define Orig_SetDestruct ((void (__fastcall *)(AttributeSet *, int))0x00057a40)
#define Orig_SetDestructThunk ((void (__fastcall *)(AttributeSet *, int))0x000752f0)
#define Orig_SetName ((void (__fastcall *)(AttributeSet *, int, const char *, bool))0x00058f40)
#define Orig_SetNameOf ((const char *(__fastcall *)(AttributeSet *, int))0x00052030)
#define Orig_LookupBool ((uint8_t (__fastcall *)(AttributeSet *, int, const char *, bool *))0x000580e0)
#define Orig_LookupInt ((int32_t (__fastcall *)(AttributeSet *, int, const char *, bool *))0x00058180)
#define Orig_LookupUInt ((uint32_t (__fastcall *)(AttributeSet *, int, const char *, bool *))0x00058220)
#define Orig_LookupFloat ((float (__fastcall *)(AttributeSet *, int, const char *, bool *))0x000582c0)
#define Orig_LookupVector ((const AttributeVector *(__fastcall *)(AttributeSet *, int, const char *, bool *))0x00058360)
#define Orig_LookupString ((const char *(__fastcall *)(AttributeSet *, int, const char *, bool *))0x000583a0)
#define Orig_LookupValidString ((const char *(__fastcall *)(AttributeSet *, int, const char *, bool *))0x000583e0)
#define Orig_LookupStruct ((void *(__fastcall *)(AttributeSet *, int, const char *, uint32_t, bool *))0x00058420)

#define DefaultVector (*(AttributeVector *)0x001d4c00)
#define ArchiveName ((char *)0x002431d8)               // UFileLoader's mission archive name (kept after closing)
#define ArchiveOpen (*(uint8_t *)0x002434da)
#define ArchiveLogRequests (*(uint8_t *)0x002434e0)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)

alignas(8) uint8_t g_systemStorage[2][sizeof(AttributeSystem)];
AttributeSystem *g_game;
std::vector<AttributeSet> g_sets[2];
std::vector<std::string> g_dbLog[2];
std::vector<std::pair<std::string, std::string> > g_collectionKeys;   // from the original side
std::vector<std::vector<std::string> > g_lookupKeys;                 // per collection, from the original side
std::vector<std::string> g_classes;

AttributeSystem *SideSystem(int side) {
    return reinterpret_cast<AttributeSystem *>(g_systemStorage[side]);
}

void Log(int side, const std::string &line) {
    static const bool trace = getenv("NIGHTFIRE_ATTRIBSHADOW_TRACE") != NULL;
    if (trace) {   // every line as it is logged, to find where a side faults
        printf("[attribshadow] %d: %s\n", side, line.c_str());
        fflush(stdout);
    }
    g_dbLog[side].push_back(line);
}

// What a word means in a side's system: a collection, a stored string, or itself.
struct Meanings {
    std::map<uintptr_t, std::string> words;
    explicit Meanings(AttributeSystem *system) {
        for (CollectionNode *node : InOrder(system->collections))
            words[reinterpret_cast<uintptr_t>(&node->value.collection)] =
                std::string("C:") + node->value.key.className + "/" + node->value.key.name;
        for (StringNode *node : InOrder(system->strings))
            words[reinterpret_cast<uintptr_t>(node->value)] = std::string("S:") + node->value;
    }
    std::string Bytes(const void *data, uint32_t size) const {
        std::string out;
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        uint32_t i = 0;
        for (; i + 4 <= size; i += 4) {
            uint32_t word;
            memcpy(&word, bytes + i, 4);
            std::map<uintptr_t, std::string>::const_iterator found = words.find(word);
            out += found != words.end() ? found->second + "," : Format("%08x,", word);
        }
        return out + Hex(bytes + i, size - i);
    }
};

uint32_t ExtensionSize(AttributeSystem *system, uint32_t type) {
    for (ExtensionTypeNode *node : InOrder(system->extensionTypes))
        if (node->value.type == type)
            return node->value.extension.size;
    return 0;
}

uint32_t ElementSize(int type) {
    switch (type) {
    case kAttributeBool: return 1;
    case kAttributeInt: case kAttributeUInt: case kAttributeFloat: return 4;
    case kAttributeVector: return 16;
    case kAttributeMatrix: return 64;
    default: return 0;
    }
}

std::string DescribeValue(AttributeSystem *system, const Meanings &meanings, const AttributeValue &value) {
    std::string out = Format("%u/%02x/%d:", value.count, value.flags, value.type);
    if (value.type == kAttributeString)
        return out + (value.data != NULL ? static_cast<const char *>(value.data) : "(null)");
    if (value.type == kAttributeSymbol)
        return out + Format("%p", value.data);
    if (value.type >= 0)
        return out + (value.data != NULL ? meanings.Bytes(value.data, ExtensionSize(system, value.type)) : "null");
    if (value.count == 0 && !(value.flags & kAttributeOwnsData))
        return out + Format("%08x", value.type == kAttributeBool ? value.bits & 0xff : value.bits);
    if (value.data == NULL)
        return out + "null";
    return out + Hex(value.data, ElementSize(value.type) * (value.count != 0 ? value.count : 1));
}

// The value a lookup of `key` finds, without marking it used.
const AttributeValue *Peek(const AttributeSet *set, const char *key) {
    for (AttributeCollection *c = set->collection; c != NULL; c = c->parent)
        for (AttributeNode *node : InOrder(&c->attributes))
            if (CRT_stricmp(node->value.name, key) == 0)
                return &node->value.value;
    return NULL;
}

// What a pointer-answering lookup gave, safely: a string by content, data a value holds by its bytes (if there are
// at least `size` of them), the held word itself.
std::string Pointed(const AttributeValue *value, const void *answer, uint32_t size) {
    if (value == NULL || answer == NULL)
        return answer == NULL ? "(null)" : "?";
    if (value->type == kAttributeString)
        return static_cast<const char *>(answer);
    if (value->count == 0 && !(value->flags & kAttributeOwnsData))
        return Format("held %08x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(answer)));
    uint32_t bytes = ElementSize(value->type) * (value->count != 0 ? value->count : 1);
    if (value->type >= 0)
        return Format("struct %d", value->type);
    return bytes >= size ? Hex(answer, size) : Format("short %u", bytes);
}

std::string CollectionKeyOf(const AttributeCollection *collection) {
    if (collection == NULL)
        return "none";
    return std::string(collection->className) + "/" + collection->name;
}

// The whole system, as text: what the comparison walks.
void DescribeSystem(AttributeSystem *system, std::vector<std::string> &out) {
    Meanings meanings(system);
    out.push_back(Format("next type %u enabled %u database %u section %s", system->nextExtensionType,
                         system->loadingEnabled, system->loadingDatabase, system->collectionSection));
    std::string strings = "strings:";
    for (StringNode *node : InOrder(system->strings))
        strings += std::string(node->value) + "|";
    out.push_back(strings);
    StoreBlockList *blocks = system->storeBlocks;
    out.push_back(Format("blocks %d", static_cast<int>(blocks->last - blocks->first)));
    for (AttributeStoreBlock *block = blocks->first; block != blocks->last; block++)
        out.push_back(Format("block used %u %s: ", block->used, block->block ? "" : "null")
                      + (block->block ? std::string(block->block, block->used) : ""));
    for (ExtensionTypeNode *node : InOrder(system->extensionTypes)) {
        const AttributeExtension &e = node->value.extension;
        out.push_back(Format("type %u: %p %u %u %s %s", node->value.type, (void *)e.init, e.size, e.type,
                             e.attributeName ? e.attributeName : "-", e.className ? e.className : "-"));
    }
    for (ExtensionClassNode *node : InOrder(system->extensionFields)) {
        std::string line = std::string("fields ") + node->value.className + ":";
        for (AttributeFieldNode *field : InOrder(&node->value.fields))
            line += Format(" %s=%p,%u,%u,%u,%u,%u", field->value.name, (void *)field->value.field.parser,
                           field->value.field.offset, field->value.field.count, field->value.field.unused1,
                           field->value.field.unused2, field->value.field.type);
        out.push_back(line);
    }
    out.push_back("edit config " + Format("%u", system->editConfig->size));
    for (CollectionNode *node : InOrder(system->collections)) {
        const AttributeCollection &c = node->value.collection;
        out.push_back(Format("collection %s/%s ref %u loaded %u name %s class %s parent ", node->value.key.className,
                             node->value.key.name, c.refCount, c.loaded, c.name, c.className)
                      + CollectionKeyOf(c.parent));
        for (AttributeNode *attribute : InOrder(&c.attributes))
            out.push_back(std::string("  ") + attribute->value.name + " = "
                          + DescribeValue(system, meanings, attribute->value.value));
        out.push_back("  shape " + Shape(&c.attributes, [](AttributeNode *n) { return std::string(n->value.name); }));
    }
    out.push_back("collections shape " + Shape(system->collections, [](CollectionNode *n) {
        return std::string(n->value.key.className) + "/" + n->value.key.name;
    }));
    out.push_back("strings shape " + Shape(system->strings, DescribeString));
}

void RegisterGameExtensions(int side, AttributeSystem *system) {
    bool original = side == 0;
    for (ExtensionTypeNode *node : InOrder(g_game->extensionTypes)) {
        const AttributeExtension &e = node->value.extension;
        if (e.type != node->value.type || e.className == NULL)
            continue;   // an entry operator[] made for an unknown id
        uint32_t type = original
            ? Orig_RegisterExtensionType(system, 0, e.className, e.attributeName, e.size, e.init)
            : system->RegisterExtensionType(e.className, e.attributeName, e.size, e.init);
        Log(side, Format("registered %s %s as %u (game %u)", e.className, e.attributeName, type, e.type));
    }
    for (ExtensionClassNode *node : InOrder(g_game->extensionFields)) {
        for (AttributeFieldNode *field : InOrder(&node->value.fields)) {
            const AttributeField &f = field->value.field;
            if (original)
                Orig_RegisterExtensionField(system, 0, f.type, field->value.name, f.parser, f.offset, f.count, f.unused1,
                                            f.unused2);
            else
                system->RegisterExtensionField(f.type, field->value.name, f.parser, f.offset, f.count, f.unused1,
                                               f.unused2);
        }
    }
}

void DatabasePhase(int side) {
    bool original = side == 0;
    AttributeSystem *system = SideSystem(side);
    if (original)
        Orig_SystemConstruct(system, 0);
    else
        system->Construct();
    system->symbolTable = g_game->symbolTable;
    if (original)
        Orig_SetCollectionSection(system, 0, g_game->collectionSection);
    else
        system->SetCollectionSection(g_game->collectionSection);
    RegisterGameExtensions(side, system);
    if (original)
        Orig_PrepareDatabase(system, 0);
    else
        system->PrepareDatabase();
    Log(side, Format("prepared: %u collections", system->collections->size));

    if (original) {
        g_collectionKeys.clear();
        g_classes.clear();
        for (CollectionNode *node : InOrder(system->collections)) {
            g_collectionKeys.push_back(std::make_pair(std::string(node->value.key.className), std::string(node->value.key.name)));
            if (g_classes.empty() || g_classes.back() != node->value.key.className)
                g_classes.push_back(node->value.key.className);
        }
    }
    // a set on every collection: every file read
    std::vector<AttributeSet> &sets = g_sets[side];
    sets.assign(g_collectionKeys.size(), AttributeSet());
    for (size_t i = 0; i < g_collectionKeys.size(); i++) {
        if (original)
            Orig_SetConstruct(&sets[i], 0, g_collectionKeys[i].first.c_str(), g_collectionKeys[i].second.c_str());
        else
            sets[i].Construct(g_collectionKeys[i].first.c_str(), g_collectionKeys[i].second.c_str());
        Log(side, "set " + CollectionKeyOf(sets[i].collection) + " named "
                  + (original ? Orig_SetNameOf(&sets[i], 0) : sets[i].Name()));
    }
    if (original) {
        g_lookupKeys.assign(sets.size(), std::vector<std::string>());
        for (size_t i = 0; i < sets.size(); i++) {
            std::map<std::string, int> keys;
            for (AttributeCollection *c = sets[i].collection; c != NULL; c = c->parent)
                for (AttributeNode *node : InOrder(&c->attributes))
                    keys[node->value.name] = 1;
            for (std::map<std::string, int>::iterator k = keys.begin(); k != keys.end(); ++k)
                g_lookupKeys[i].push_back(k->first);
            g_lookupKeys[i].push_back("NO_SUCH_KEY");
            g_lookupKeys[i].push_back("mass");   // a case change
        }
    }
    // every lookup of every key
    for (size_t i = 0; i < sets.size(); i++) {
        AttributeSet *set = &sets[i];
        for (const std::string &key : g_lookupKeys[i]) {
            const char *k = key.c_str();
            bool found[7];
            memset(found, 0xee, sizeof(found));
            uint8_t b = original ? Orig_LookupBool(set, 0, k, &found[0]) : set->LookupBool(k, &found[0]);
            int32_t n = original ? Orig_LookupInt(set, 0, k, &found[1]) : set->LookupInt(k, &found[1]);
            uint32_t u = original ? Orig_LookupUInt(set, 0, k, &found[2]) : set->LookupUInt(k, &found[2]);
            float f = original ? Orig_LookupFloat(set, 0, k, &found[3]) : set->LookupFloat(k, &found[3]);
            const AttributeVector *v = original ? Orig_LookupVector(set, 0, k, &found[4]) : set->LookupVector(k, &found[4]);
            const char *s = original ? Orig_LookupString(set, 0, k, &found[5]) : set->LookupString(k, &found[5]);
            const char *vs = original ? Orig_LookupValidString(set, 0, k, &found[6]) : set->LookupValidString(k, &found[6]);
            uint32_t fbits;
            memcpy(&fbits, &f, 4);
            const AttributeValue *value = Peek(set, k);
            if (value != NULL && value->type == kAttributeBool && value->count == 0 &&
                !(value->flags & kAttributeOwnsData)) {
                // a bool held in the value: the game leaves the word's other three bytes as SetAttribute<bool>'s
                // stack had them (the port has 0 there), which a lookup of it as another type shows
                n &= 0xff;
                u &= 0xff;
                fbits &= 0xff;
                v = reinterpret_cast<const AttributeVector *>(reinterpret_cast<uintptr_t>(v) & 0xff);
                s = reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(s) & 0xff);
                vs = reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(vs) & 0xff);
            }
            if (value != NULL && value->type >= 0) {
                // an extension structure: its first word may point into the side's own system (smackable's
                // starts with an AttributeSet); the structures are compared by content with the systems
                b = 0;
                n = 0;
                u = 0;
                fbits = 0;
            }
            std::string vector = v == &DefaultVector ? "default" : Pointed(value, v, sizeof(AttributeVector));
            std::string valid = value == NULL ? std::string(vs) : Pointed(value, vs, 1);
            Log(side, Format("%s %s: b %02x i %d u %u f %08x v ", CollectionKeyOf(set->collection).c_str(), k, b, n, u, fbits)
                      + vector + " s " + Pointed(value, s, 1) + " vs " + valid + " found " + Hex(found, sizeof(found)));
            // a NULL found pointer too
            uint8_t b2 = original ? Orig_LookupBool(set, 0, k, NULL) : set->LookupBool(k, NULL);
            if (value != NULL && value->type >= 0)
                b2 = 0;
            Log(side, Format("  nofound %02x", b2));
        }
        // every extension structure of the class
        for (ExtensionTypeNode *node : InOrder(system->extensionTypes)) {
            const AttributeExtension &e = node->value.extension;
            if (e.className == NULL || CRT_stricmp(e.className, set->collection->className) != 0)
                continue;
            void *structure = original ? Orig_LookupStruct(set, 0, e.attributeName, e.type, NULL)
                                       : set->LookupStruct(e.attributeName, e.type, NULL);
            Meanings meanings(system);
            Log(side, Format("struct %s %s: ", CollectionKeyOf(set->collection).c_str(), e.attributeName)
                      + (structure ? meanings.Bytes(structure, e.size) : "null"));
        }
    }
    // class names
    for (const std::string &className : g_classes) {
        const char *c = className.c_str();
        int count = original ? Orig_CountClassNames(system, 0, c) : system->CountClassNames(c);
        std::string names = Format("class %s: %d:", c, count);
        const char *name = NULL;
        for (int j = 0; j < count + 2; j++) {
            name = original ? Orig_GetClassNextName(system, 0, c, name) : system->GetClassNextName(c, name);
            names += std::string(" ") + (name ? name : "(null)");
            if (name == NULL || j >= count - 1)
                break;   // past the last, the game compares the head's unset key: not asked for
        }
        Log(side, names);
    }
    int missing = original ? Orig_CountClassNames(system, 0, "no_such_class") : system->CountClassNames("no_such_class");
    Log(side, Format("no class: %d", missing));
    // type classes
    for (uint32_t type = 1; type < system->nextExtensionType; type++) {
        const char *c = original ? Orig_GetExtensionTypeClass(system, 0, type) : system->GetExtensionTypeClass(type);
        Log(side, Format("type %u class %s", type, c ? c : "(null)"));
    }
    // a collection made after the database, a copy, SetName both ways
    AttributeSet made;
    if (original)
        Orig_SetConstruct(&made, 0, "pvehicle", "no_such_vehicle");
    else
        made.Construct("pvehicle", "no_such_vehicle");
    Log(side, "made " + CollectionKeyOf(made.collection) + " parent " + CollectionKeyOf(made.collection->parent));
    AttributeSet copy;
    if (original)
        Orig_SetConstructCopy(&copy, 0, &made);
    else
        copy.ConstructCopy(made);
    if (!g_collectionKeys.empty()) {
        const char *other = g_collectionKeys[g_collectionKeys.size() / 2].second.c_str();
        if (original)
            Orig_SetName(&copy, 0, other, true);
        else
            copy.SetName(other, true);
        Log(side, "renamed " + CollectionKeyOf(copy.collection) + " parent " + CollectionKeyOf(copy.collection->parent));
        if (original)
            Orig_SetName(&made, 0, "another_vehicle", false);
        else
            made.SetName("another_vehicle", false);
        Log(side, "renamed " + CollectionKeyOf(made.collection));
    }
    if (original) {
        Orig_SetDestruct(&copy, 0);
        Orig_SetDestructThunk(&made, 0);
    } else {
        copy.Destruct();
        made.DestructThunk();
    }
    // MakeString past many blocks (and NULL)
    const char *none = original ? Orig_MakeString(system, 0, NULL) : system->MakeString(NULL);
    Log(side, Format("null string %p", none));
    std::string text;
    for (int j = 0; j < 70; j++) {
        text.assign(static_cast<size_t>(j * 97 % 3900 + 1), static_cast<char>('a' + j % 26));
        text += Format("%d", j);
        const char *stored = original ? Orig_MakeString(system, 0, text.c_str()) : system->MakeString(text.c_str());
        const char *again = original ? Orig_MakeString(system, 0, text.c_str()) : system->MakeString(text.c_str());
        Log(side, Format("string %d same %d content %d", j, stored == again, stored && strcmp(stored, text.c_str()) == 0));
    }
}

void ReleasePhase(int side) {
    bool original = side == 0;
    for (AttributeSet &set : g_sets[side]) {
        if (original)
            Orig_SetDestruct(&set, 0);
        else
            set.Destruct();
    }
}

void DestroyPhase(int side) {
    if (side == 0)
        Orig_SystemDestruct(SideSystem(side), 0);
    else
        SideSystem(side)->Destruct();
}

void CompareLines(const char *what, const std::vector<std::string> &a, const std::vector<std::string> &b) {
    Check(a.size() == b.size(), "%s: %u lines vs %u", what, (unsigned)a.size(), (unsigned)b.size());
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++) {
        g_cases++;
        if (a[i] != b[i]) {
            size_t at = 0;
            while (at < a[i].size() && at < b[i].size() && a[i][at] == b[i][at])
                at++;
            size_t from = at > 60 ? at - 60 : 0;
            Check(false, "%s line %u: ...%.160s | ...%.160s", what, (unsigned)i, a[i].c_str() + from, b[i].c_str() + from);
        } else {
            g_checks++;
        }
    }
}

void TestDatabase() {
    g_dbLog[0].clear();
    g_dbLog[1].clear();
    if (!RunSide(DatabasePhase, 0, SideSystem(0)) || !RunSide(DatabasePhase, 1, SideSystem(1)))
        return;
    Check(!g_collectionKeys.empty(), "the database has no collections: attrib.dir did not load");
    CompareLines("database", g_dbLog[0], g_dbLog[1]);
    std::vector<std::string> systems[2];
    DescribeSystem(SideSystem(0), systems[0]);
    DescribeSystem(SideSystem(1), systems[1]);
    if (getenv("NIGHTFIRE_ATTRIBSHADOW_TRACE") != NULL)
        for (const std::string &line : systems[1])
            printf("[attribshadow] system: %.300s\n", line.c_str());
    CompareLines("system", systems[0], systems[1]);
    if (!RunSide(ReleasePhase, 0, SideSystem(0)) || !RunSide(ReleasePhase, 1, SideSystem(1)))
        return;
    systems[0].clear();
    systems[1].clear();
    DescribeSystem(SideSystem(0), systems[0]);
    DescribeSystem(SideSystem(1), systems[1]);
    CompareLines("released", systems[0], systems[1]);
    RunSide(DestroyPhase, 0, SideSystem(0));
    RunSide(DestroyPhase, 1, SideSystem(1));
    g_cases++;
    Check(SideSystem(0)->vtable == SideSystem(1)->vtable && SideSystem(0)->loadingEnabled == SideSystem(1)->loadingEnabled,
          "destroyed systems differ");
}

void TestPair(Phase phase, std::vector<std::string> *logs, const char *what, AttributeSystem *system) {
    logs[0].clear();
    logs[1].clear();
    if (!RunSide(phase, 0, system) || !RunSide(phase, 1, system))
        return;
    CompareLines(what, logs[0], logs[1]);
}

}  // namespace

void AttribShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_ATTRIBSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    g_game = AttributeSystemInstance;
    if (g_game == NULL || !g_game->loadingEnabled) {
        printf("[attribshadow] the game's attribute database is not ready yet - call AttribShadow_Run later\n");
        fflush(stdout);
        return;
    }
    MakeScratch();
    {
        AttributeSystem *game = AttributeSystemInstance;
        AttributeSystemInstance = g_scratch;
        RegisterGameExtensions(1, g_scratch);
        AttributeSystemInstance = game;
        g_dbLog[1].clear();
    }
    BuildTexts();
    BuildTreeOps();
    BuildSortInputs();

    TestParsers();
    g_fieldLog[0].clear();
    g_fieldLog[1].clear();
    if (RunSide(FieldPhase, 0, g_scratch) && RunSide(FieldPhase, 1, g_scratch)) {
        g_cases++;
        Check(g_fieldLog[0] == g_fieldLog[1], "AttributeField::Parse: %s vs %s", g_fieldLog[0].c_str(), g_fieldLog[1].c_str());
    }
    g_valueLog[0].clear();
    g_valueLog[1].clear();
    if (RunSide(ValuePhase, 0, g_scratch) && RunSide(ValuePhase, 1, g_scratch)) {
        g_cases++;
        Check(g_valueLog[0] == g_valueLog[1], "values: %.200s vs %.200s", g_valueLog[0].c_str(), g_valueLog[1].c_str());
    }
    TestPair(StringSetPhase, g_treeLog, "string set", g_scratch);
    TestPair(AttributeMapPhase, g_treeLog, "attribute map", g_scratch);
    TestPair(TypeMapPhase, g_treeLog, "extension type map", g_scratch);
    TestPair(FieldMapPhase, g_treeLog, "field map", g_scratch);
    TestPair(SortPhase, g_sortLog, "store blocks", g_scratch);
    // The .atr files are only in the mission's archive, which the game closes just before its main loop
    // (UFileLoader::StopUsingBigFile): opened again for the database part, under the name it was opened with,
    // and closed again afterwards.
    bool reopened = false;
    uint8_t logRequests = ArchiveLogRequests;
    if (ArchiveOpen == 0 && ArchiveName[0] != 0) {
        char name[256];
        strcpy(name, ArchiveName);
        reopened = UFileLoader::StartUsingBigFile("driving", name, false);
        if (!reopened)
            printf("[attribshadow] could not open driving\\%s.viv again: the .atr files will not load\n", name);
    }
    TestDatabase();
    if (reopened)
        UFileLoader::StopUsingBigFile();
    ArchiveLogRequests = logRequests;

    printf("[attribshadow] parsers, values, trees, store blocks and the disc's attribute database (%u collections), "
           "original vs port: %d cases, %d checks, %d differ%s\n",
           (unsigned)g_collectionKeys.size(), g_cases, g_checks, g_differ,
           g_faults ? Format(" (%d faulted)", g_faults).c_str() : "");
    fflush(stdout);
}
