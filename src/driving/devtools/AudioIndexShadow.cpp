#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AudioIndexShadow.h"

#include "../audio/Index.h"
#include "../audio/SoundManager.h"      // fgBanks
#include "../audio/ZoomObj.h"
#include "../engine/UFileLoader.h"
#include "../engine/UMemory.hpp"
#include "../engine/URefCounter.h"
#include "../platform/RealMemory.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_AUDIOINDEXSHADOW=1, from the first simulation tick (the banks are loaded then).
//
// AIndex (0x00126d10..0x00127a70), original against port, the originals of the whole range swapped in for the
// original's runs:
//   - the header file of every bank in the bank reference counter, read into two indexes (the original's
//     constructor and ours, from the same directory and name); the two compared as trees - shape, colours, numbers,
//     names as offsets into each index's block - and by the bytes of the blocks; then every Lookup both ways on
//     the original's index (every number and name, the names in other cases, misses, NULL), original against ours;
//   - synthetic header files (UFileLoader::FileLoadz hooked to hand back generated text): random definitions with
//     and without underscores, dots, short names, repeated numbers, names repeated in another case, junk lines;
//     the same comparisons, and the path and flags the load asked for;
//   - the maps alone: random insert_unique into two maps built alike (results by position), random erase(first,
//     last), the trees compared after every step; both destructors.
//
// ASound, AOneShotSound's and ALimitedSound's Play, and AZoomObj (0x00127a70..0x00128350), on copies of random
// objects, every callee a recording fake (AVoice::Play, AMix::GetVolume/Get/SetTransition/Reset, the voice's
// "done" test, ABaseSound's constructor, destructor and operator delete, AVoice's constructor and destructor,
// AVoice::View's destructor, and slot 0 of a fake vtable): each method's calls with their arguments, in order, and
// the object's bytes after it, original against port. Scripted GetVolume answers, frame counts, limited-sound
// counts and done answers; times to live that are negative, zero, -0, NaN or run out on a frame. GetName against
// ABank::GetPatchName over the loaded banks.
//
// Mutations that it catches: insert_unique's `<` turned to `<=` (a repeated number would go in twice), _stricmp
// turned to strcmp (names repeated in another case), AOneShotSound::Play's `!(timeLeft >= 0)` written as
// `timeLeft < 0` (a NaN time would count down and delete).
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types: another test's of the same name must not merge with them

int g_cases, g_checks, g_differ, g_faults;

void Report(const char *format, ...) {
    if (g_differ > 10)
        return;
    va_list arguments;
    va_start(arguments, format);
    printf("[audioindex]   ");
    vprintf(format, arguments);
    printf("\n");
    va_end(arguments);
}

void Check(bool same, const char *what, const char *detail) {
    g_checks++;
    if (!same) {
        g_differ++;
        Report("%s differs: %s", what, detail);
    }
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    log += line;
}

uint32_t g_random = 0x6d2b79f5;
uint32_t Random(uint32_t below) {
    g_random = g_random * 1103515245u + 12345u;
    return below == 0 ? 0 : (g_random >> 8) % below;
}

// One call that may fault, counted rather than fatal
template <class F>
bool Guarded(F &&run) {
#ifdef _MSC_VER
    __try {
        run();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    run();
    return true;
#endif
}

// The originals of the whole index range, for as long as the scope lives
struct Originals {
    Originals() { XbeOriginal_RestoreRange(0x00126d10, 0x00127a70, true); }
    ~Originals() { XbeOriginal_RestoreRange(0x00126d10, 0x00127a70, false); }
};

typedef AIndex *(__fastcall *ConstructFn)(AIndex *, int, const char *directory, const char *file);
typedef void (__fastcall *DestructFn)(AIndex *, int);
typedef const char *(__fastcall *LookupIdFn)(AIndex *, int, int id);
typedef int (__fastcall *LookupNameFn)(AIndex *, int, const char *name);
typedef AIndexMap *(__fastcall *MapConstructFn)(AIndexMap *, int);
typedef void (__fastcall *MapDestructFn)(AIndexMap *, int);
typedef AIndexIdInsert *(__fastcall *IdInsertFn)(AIndexIdMap *, int, AIndexIdInsert *, const AIndexIdPair *);
typedef AIndexNameInsert *(__fastcall *NameInsertFn)(AIndexNameMap *, int, AIndexNameInsert *,
                                                      const AIndexNamePair *);
typedef AIndexIdNode **(__fastcall *IdEraseRangeFn)(AIndexIdMap *, int, AIndexIdNode **, AIndexIdNode *,
                                                    AIndexIdNode *);
typedef AIndexNameNode **(__fastcall *NameEraseRangeFn)(AIndexNameMap *, int, AIndexNameNode **, AIndexNameNode *,
                                                        AIndexNameNode *);

const ConstructFn OriginalConstruct = (ConstructFn)0x001276c0;
const DestructFn OriginalDestruct = (DestructFn)0x00127a40;
const LookupIdFn OriginalLookupId = (LookupIdFn)0x00126d10;
const LookupNameFn OriginalLookupName = (LookupNameFn)0x00126d40;
const MapConstructFn OriginalMapConstruct = (MapConstructFn)0x00127620;
const MapDestructFn OriginalMapDestruct = (MapDestructFn)0x00127580;
const IdInsertFn OriginalIdInsert = (IdInsertFn)0x00127230;
const NameInsertFn OriginalNameInsert = (NameInsertFn)0x001272f0;
const IdEraseRangeFn OriginalIdEraseRange = (IdEraseRangeFn)0x001273c0;
const NameEraseRangeFn OriginalNameEraseRange = (NameEraseRangeFn)0x00127480;

const char *const kAudioDirectory = (const char *)0x001d8318;   // the directory ABank's constructor passes

// ---- trees as text: pre-order, colour, key and value, names as offsets into the block (or the text itself)

void AppendName(std::string &log, const char *name, const char *names) {
    if (names != NULL && name >= names && name < names + 0x100000)
        Append(log, "@%d", int(name - names));
    else
        Append(log, "\"%s\"", name != NULL ? name : "(null)");
}

template <class Node, class Print>
void DumpTree(std::string &log, const Node *node, int depth, Print print) {
    if (depth > 64) {
        log += "!deep";
        return;
    }
    if (node->isNil) {
        log += ".";
        return;
    }
    log += node->color == kTreeRed ? "(r " : "(b ";
    print(node);
    log += " ";
    DumpTree(log, node->left, depth + 1, print);
    log += " ";
    DumpTree(log, node->right, depth + 1, print);
    log += ")";
}

std::string DumpIdMap(const AIndexIdMap &map, const char *names) {
    std::string log;
    Append(log, "size %u ", map.size);
    DumpTree(log, map.head->parent, 0, [&](const AIndexIdNode *node) {
        Append(log, "%d=", node->value.id);
        AppendName(log, node->value.name, names);
    });
    Append(log, " first %s last %s", map.head->left == map.head ? "end" : "node",
           map.head->right == map.head ? "end" : "node");
    if (map.head->left != map.head) {
        Append(log, " %d", map.head->left->value.id);
        Append(log, " %d", map.head->right->value.id);
    }
    return log;
}

std::string DumpNameMap(const AIndexNameMap &map, const char *names) {
    std::string log;
    Append(log, "size %u ", map.size);
    DumpTree(log, map.head->parent, 0, [&](const AIndexNameNode *node) {
        AppendName(log, node->value.name, names);
        Append(log, "=%d", node->value.id);
    });
    if (map.head->left != map.head) {
        log += " first ";
        AppendName(log, map.head->left->value.name, names);
        log += " last ";
        AppendName(log, map.head->right->value.name, names);
    }
    return log;
}

std::string DumpIndex(const AIndex &index) {
    std::string log;
    if (index.maps == NULL)
        return "no maps";
    log = DumpIdMap(index.maps->byId, index.names);
    log += " | ";
    log += DumpNameMap(index.maps->byName, index.names);
    log += index.names != NULL ? " | names" : " | no names";
    return log;
}

void CompareText(const char *what, const std::string &original, const std::string &port) {
    g_checks++;
    if (original == port)
        return;
    g_differ++;
    size_t at = 0;
    while (at < original.size() && at < port.size() && original[at] == port[at])
        at++;
    size_t from = at > 40 ? at - 40 : 0;
    Report("%s differs at %u: original ...%.80s / port ...%.80s", what, unsigned(at), original.c_str() + from,
           port.c_str() + from);
}

// ---- in-order steps (the head stays where it is)

template <class Node>
Node *Next(Node *node) {
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    Node *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

template <class Map>
int Position(const Map &map, const typename Map::Node *node) {
    int i = 0;
    for (const typename Map::Node *n = map.head->left; n != map.head; n = Next(const_cast<typename Map::Node *>(n)), i++) {
        if (n == node)
            return i;
        if (i > 100000)
            break;
    }
    return node == map.head ? -1 : -2;
}

template <class Map>
typename Map::Node *At(const Map &map, int position) {
    typename Map::Node *n = map.head->left;
    for (int i = 0; i < position && n != map.head; i++)
        n = Next(n);
    return n;
}

// ---- the lookups, on the original's index

void CompareLookups(const char *what, AIndex *index) {
    std::vector<int> ids;
    std::vector<std::string> names;
    if (index->maps == NULL)
        return;
    for (AIndexIdNode *n = index->maps->byId.head->left; n != index->maps->byId.head && ids.size() < 4096; n = Next(n))
        ids.push_back(n->value.id);
    for (AIndexNameNode *n = index->maps->byName.head->left; n != index->maps->byName.head && names.size() < 4096;
         n = Next(n))
        names.push_back(n->value.name);
    for (int k = 0; k < 8; k++)
        ids.push_back(int(Random(0x20000)) - 0x1000);
    ids.push_back(-1);
    ids.push_back(0x7fffffff);
    ids.push_back(int(0x80000000u));
    size_t real = names.size();
    for (size_t k = 0; k < real; k++) {
        std::string variant = names[k];
        for (char &c : variant)
            c = char(Random(2) ? toupper((unsigned char)c) : tolower((unsigned char)c));
        names.push_back(variant);
        if (k % 7 == 0)
            names.push_back(names[k] + "x");
    }
    names.push_back("");
    names.push_back("SFX_");
    names.push_back("nothing here");

    for (int id : ids) {
        const char *o = NULL, *p = NULL;
        Guarded([&] {
            Originals originals;
            o = OriginalLookupId(index, 0, id);
        });
        Guarded([&] { p = index->Lookup(id); });
        // a miss answers each side's own empty string
        bool same = o != NULL && p != NULL && (o == p || (*o == '\0' && *p == '\0'));
        char detail[128];
        snprintf(detail, sizeof(detail), "%s: Lookup(%d)", what, id);
        Check(same, "Lookup(int)", detail);
    }
    names.push_back(std::string());     // stands for NULL below
    for (size_t k = 0; k < names.size(); k++) {
        const char *name = k + 1 == names.size() ? NULL : names[k].c_str();
        int o = -7, p = -7;
        Guarded([&] {
            Originals originals;
            o = OriginalLookupName(index, 0, name);
        });
        Guarded([&] { p = index->Lookup(name); });
        char detail[160];
        snprintf(detail, sizeof(detail), "%s: Lookup(\"%.60s\") original %d, port %d", what,
                 name != NULL ? name : "(null)", o, p);
        Check(o == p, "Lookup(const char *)", detail);
    }
}

// ---- one header file, both ways

std::string g_loadLog;

void CompareIndex(const char *what, const char *directory, const char *file) {
    g_cases++;
    AIndex original = {}, port = {};
    std::string loadO, loadP;
    g_loadLog.clear();
    bool okO = Guarded([&] {
        Originals originals;
        OriginalConstruct(&original, 0, directory, file);
    });
    loadO = g_loadLog;
    g_loadLog.clear();
    bool okP = Guarded([&] { port.Construct(directory, file); });
    loadP = g_loadLog;
    char detail[200];
    snprintf(detail, sizeof(detail), "%s: constructor ran %d / %d", what, okO, okP);
    Check(okO == okP, "the constructor", detail);
    if (!okO || !okP)
        return;
    CompareText(what, loadO, loadP);
    CompareText(what, DumpIndex(original), DumpIndex(port));
    if (original.names != NULL && port.names != NULL) {
        size_t sizeO = UMemory::Size(original.names), sizeP = UMemory::Size(port.names);
        snprintf(detail, sizeof(detail), "%s: block of names %u / %u bytes, or its bytes", what, unsigned(sizeO),
                 unsigned(sizeP));
        Check(sizeO == sizeP && memcmp(original.names, port.names, sizeO) == 0, "the names", detail);
    }
    CompareLookups(what, &original);
    Guarded([&] {
        Originals originals;
        OriginalDestruct(&original, 0);
    });
    Guarded([&] { port.Destruct(); });
}

// ---- synthetic header files: UFileLoader::FileLoadz hooked

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[32];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

// An original entry that is already ported holds a jump to the port, which ports call directly: both are hooked.
void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

std::string g_text;
bool g_textMissing;

void *FakeFileLoadz(const char *path, int flags) {
    Append(g_loadLog, "load \"%s\" %d; ", path, flags);
    if (g_textMissing)
        return NULL;
    int size = int(g_text.size());
    char *block = static_cast<char *>(MEM_alloc("AudioIndexShadow", size > 0 ? size : 1, unsigned(flags)));
    if (block != NULL && size > 0)
        memcpy(block, g_text.data(), size);
    return block;
}

void RandomName(std::string &name) {
    static const char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.";
    int length = 1 + int(Random(Random(4) == 0 ? 40 : 14));
    name.clear();
    for (int i = 0; i < length; i++)
        name += kLetters[Random(sizeof(kLetters) - 1)];
}

void MakeText(int definitions) {
    g_text.clear();
    if (Random(3) == 0)
        g_text += "// header\n";
    std::vector<std::string> made;
    for (int i = 0; i < definitions; i++) {
        std::string name;
        int kind = int(Random(10));
        if (kind == 0 && !made.empty()) {
            name = made[Random(uint32_t(made.size()))];                 // repeated, maybe in another case
            for (char &c : name)
                if (Random(2))
                    c = char(isupper((unsigned char)c) ? tolower((unsigned char)c) : toupper((unsigned char)c));
        } else if (kind == 1) {
            RandomName(name);
            name = "SND_" + name;
        } else {
            RandomName(name);
        }
        made.push_back(name);
        int id = Random(5) == 0 ? int(Random(8)) : int(Random(4000)) - (Random(9) == 0 ? 2000 : 0);
        char line[160];
        switch (Random(12)) {
        case 0:
            snprintf(line, sizeof(line), "#define %s\n", name.c_str());             // no number
            break;
        case 1:
            snprintf(line, sizeof(line), "#include \"%s\"\n", name.c_str());        // not a definition
            break;
        case 2:
            snprintf(line, sizeof(line), "#define\t%s\t\t%d /* # */\n", name.c_str(), id);
            break;
        default:
            snprintf(line, sizeof(line), "#define %s %d\n", name.c_str(), id);
            break;
        }
        g_text += line;
    }
}

void SyntheticCases() {
    HookInstall(0x001176d0, (const void *)&FakeFileLoadz);
    HookInstall(uint32_t(uintptr_t(&UFileLoader::FileLoadz)), (const void *)&FakeFileLoadz);
    char what[64];
    for (int round = 0; round < 160; round++) {
        MakeText(round < 8 ? round : int(Random(round < 120 ? 60 : 400)));
        g_textMissing = round == 3;
        snprintf(what, sizeof(what), "synthetic %d", round);
        static const char *const kFiles[] = { "bank.bnk", "a.b.c", "x.", "Weapons.BNK" };
        CompareIndex(what, Random(4) == 0 ? "data\\audio\\" : kAudioDirectory, kFiles[Random(4)]);
    }
    g_textMissing = false;
    CompareIndex("no directory", NULL, "bank.bnk");
    CompareIndex("no file", kAudioDirectory, NULL);
    HooksRemove();
}

// ---- the banks on the disc

void BankCases() {
    BankRefCounter *banks = BankRefCounter::Get();
    int count = 0;
    for (RefCounterNode *node = banks->head->left; node != banks->head && count < 64; node = Next(node), count++) {
        char what[160];
        snprintf(what, sizeof(what), "bank \"%.100s\"", node->value.name);
        CompareIndex(what, kAudioDirectory, node->value.name);
    }
    printf("[audioindex] %d banks\n", count);
}

// ---- the maps alone

void MapCases() {
    for (int round = 0; round < 40; round++) {
        g_cases++;
        AIndexMap *mapO = static_cast<AIndexMap *>(UMemory::FastAlloc(sizeof(AIndexMap), "AIndexMap"));
        AIndexMap *mapP = static_cast<AIndexMap *>(UMemory::FastAlloc(sizeof(AIndexMap), "AIndexMap"));
        Guarded([&] {
            Originals originals;
            OriginalMapConstruct(mapO, 0);
        });
        mapP->Construct();
        static char pool[64][8];
        for (int i = 0; i < 64; i++)
            snprintf(pool[i], sizeof(pool[i]), "%c%c%d", "aAbB"[i % 4], "xXyY"[i / 16], i % 5);
        int steps = 20 + int(Random(200));
        char what[64];
        for (int step = 0; step < steps; step++) {
            snprintf(what, sizeof(what), "maps %d step %d", round, step);
            if (Random(8) != 0) {
                AIndexIdPair idPair = { int(Random(100)) - 20, pool[Random(64)] };
                AIndexIdInsert o = {}, p = {};
                Guarded([&] {
                    Originals originals;
                    OriginalIdInsert(&mapO->byId, 0, &o, &idPair);
                });
                Guarded([&] { mapP->byId.InsertUnique(&p, &idPair); });
                char detail[128];
                snprintf(detail, sizeof(detail), "%s: insert %d at %d/%d, %d/%d", what, idPair.id,
                         Position(mapO->byId, o.node), Position(mapP->byId, p.node), o.inserted, p.inserted);
                Check(Position(mapO->byId, o.node) == Position(mapP->byId, p.node) && o.inserted == p.inserted,
                      "number map insert", detail);
                AIndexNamePair namePair = { pool[Random(64)], int(Random(1000)) };
                AIndexNameInsert on = {}, pn = {};
                Guarded([&] {
                    Originals originals;
                    OriginalNameInsert(&mapO->byName, 0, &on, &namePair);
                });
                Guarded([&] { mapP->byName.InsertUnique(&pn, &namePair); });
                snprintf(detail, sizeof(detail), "%s: insert %s at %d/%d, %d/%d", what, namePair.name,
                         Position(mapO->byName, on.node), Position(mapP->byName, pn.node), on.inserted,
                         pn.inserted);
                Check(Position(mapO->byName, on.node) == Position(mapP->byName, pn.node) &&
                          on.inserted == pn.inserted,
                      "name map insert", detail);
            } else {
                int size = int(mapO->byId.size);
                int first = int(Random(uint32_t(size + 1)));
                int last = Random(6) == 0 ? size : first + int(Random(uint32_t(size - first + 1)));
                if (Random(5) == 0)
                    first = 0;
                AIndexIdNode *o = NULL, *p = NULL;
                Guarded([&] {
                    Originals originals;
                    OriginalIdEraseRange(&mapO->byId, 0, &o, At(mapO->byId, first), At(mapO->byId, last));
                });
                Guarded([&] { mapP->byId.EraseRange(&p, At(mapP->byId, first), At(mapP->byId, last)); });
                char detail[128];
                snprintf(detail, sizeof(detail), "%s: erase [%d, %d) answered %d/%d", what, first, last,
                         Position(mapO->byId, o), Position(mapP->byId, p));
                Check(Position(mapO->byId, o) == Position(mapP->byId, p), "number map erase", detail);
                size = int(mapO->byName.size);
                first = int(Random(uint32_t(size + 1)));
                last = Random(6) == 0 ? size : first + int(Random(uint32_t(size - first + 1)));
                AIndexNameNode *on = NULL, *pn = NULL;
                Guarded([&] {
                    Originals originals;
                    OriginalNameEraseRange(&mapO->byName, 0, &on, At(mapO->byName, first), At(mapO->byName, last));
                });
                Guarded([&] { mapP->byName.EraseRange(&pn, At(mapP->byName, first), At(mapP->byName, last)); });
                snprintf(detail, sizeof(detail), "%s: erase names [%d, %d)", what, first, last);
                Check(Position(mapO->byName, on) == Position(mapP->byName, pn), "name map erase", detail);
            }
            CompareText(what, DumpIdMap(mapO->byId, NULL), DumpIdMap(mapP->byId, NULL));
            CompareText(what, DumpNameMap(mapO->byName, NULL), DumpNameMap(mapP->byName, NULL));
        }
        // the destructors, each on the other's map now and then
        bool swap = Random(2) == 0;
        AIndexMap *forOriginal = swap ? mapP : mapO, *forPort = swap ? mapO : mapP;
        if (round % 5 == 4) {
            AIndexIdMap *ids = &forOriginal->byId;
            Guarded([&] {
                Originals originals;
                ((void (__fastcall *)(AIndexIdMap *, int))0x00127540)(ids, 0);
            });
            forPort->byId.Destruct();
            Check(ids->head == NULL && ids->size == 0 && forPort->byId.head == NULL && forPort->byId.size == 0,
                  "the number map's destructor", "head or size left");
            forOriginal->byName.AsTree()->Destroy();
            forPort->byName.AsTree()->Destroy();
        } else {
            Guarded([&] {
                Originals originals;
                OriginalMapDestruct(forOriginal, 0);
            });
            forPort->Destruct();
            Check(forOriginal->byId.head == NULL && forOriginal->byName.head == NULL && forPort->byId.head == NULL &&
                      forPort->byName.head == NULL,
                  "the maps' destructor", "a head left");
        }
        UMemory::FastFree(mapO, sizeof(AIndexMap));
        UMemory::FastFree(mapP, sizeof(AIndexMap));
    }
}


// =============================================================================================================
// ASound, the one-shot kinds' Play and AZoomObj, on copies, every callee a recording fake
// =============================================================================================================

struct SoundOriginals {
    SoundOriginals() { XbeOriginal_RestoreRange(0x00127a70, 0x00128350, true); }
    ~SoundOriginals() { XbeOriginal_RestoreRange(0x00127a70, 0x00128350, false); }
};

std::string g_calls;            // what the fakes saw
const uint8_t *g_base;          // the object under test: pointers into it are logged as offsets
double g_volumes[4];            // what the fake GetVolume answers, in turn
int g_volumeTurn;
bool g_done;                    // what the fake IsDone answers

int &AudioFrames() { return *(int *)0x00243a68; }
int &LimitedSounds() { return *(int *)0x00243b30; }

uint32_t Bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return bits;
}

// A pointer into the object as its offset, any other as itself
long Offset(const void *p) {
    const uint8_t *at = (const uint8_t *)p;
    if (at >= g_base && at < g_base + sizeof(ASound))
        return long(at - g_base);
    return long(0x40000000 | (uintptr_t)at);
}

AMix *FakeMix(const char *name) {
    uint32_t h = 2166136261u;
    for (const char *c = name; *c; c++)
        h = (h ^ uint8_t(*c)) * 16777619u;
    return (AMix *)(uintptr_t)(0x70000000u | (h & 0x00fffff0u));
}

void __fastcall FakeVoicePlay(AVoice *voice, int, int view, float volume, float pitch, float azimuth, float delay,
                              float fxLevel) {
    Append(g_calls, "Play %ld view %d %08x %08x %08x %08x %08x; ", Offset(voice), view, Bits(volume), Bits(pitch),
           Bits(azimuth), Bits(delay), Bits(fxLevel));
}

double __fastcall FakeGetVolume(AMix *mix, int) {
    Append(g_calls, "GetVolume %p; ", (void *)mix);
    return g_volumes[g_volumeTurn++ & 3];
}

AMix *FakeMixGet(const char *name) {
    Append(g_calls, "Get %s; ", name);
    return FakeMix(name);
}

void __fastcall FakeSetTransition(AMix *mix, int, float volume, int steps) {
    Append(g_calls, "SetTransition %p %08x %d; ", (void *)mix, Bits(volume), steps);
}

void FakeReset(int steps) {
    Append(g_calls, "Reset %d; ", steps);
}

bool __fastcall FakeIsDone(AVoice *voice, int) {
    Append(g_calls, "IsDone %ld; ", Offset(voice));
    return g_done;
}

void *__fastcall FakeDelete(void *self, int, unsigned flags) {
    Append(g_calls, "Delete %ld %u; ", Offset(self), flags);
    return self;
}

ABaseSound *__fastcall FakeBaseConstruct(ABaseSound *sound, int, const char *mixName, int view) {
    Append(g_calls, "ABaseSound %ld %s %d; ", Offset(sound), mixName, view);
    sound->mix = FakeMix(mixName);
    return sound;
}

void __fastcall FakeBaseDestruct(ABaseSound *sound, int) {
    Append(g_calls, "~ABaseSound %ld vtable %08x; ", Offset(sound), sound->vtable);
}

void FakeOperatorDelete(void *block, unsigned size) {
    Append(g_calls, "operator delete %ld %u; ", Offset(block), size);
}

AVoice *__fastcall FakeVoiceConstruct(AVoice *voice, int, AMix *mix, int bank, int patch) {
    Append(g_calls, "AVoice %s %p %d %d; ", (const uint8_t *)voice == g_base + 0xc0 ? "own" : "new", (void *)mix,
           bank, patch);
    return voice;
}

void __fastcall FakeVoiceDestruct(AVoice *voice, int) {
    Append(g_calls, "~AVoice %s; ", voice != NULL ? "voice" : "NULL");
}

void __fastcall FakeViewDestruct(AVoice::View *view, int) {
    Append(g_calls, "~View %ld; ", Offset(view));
}

uint32_t g_fakeVtable[4];

float RandomFloat() {
    switch (Random(8)) {
    case 0: return 0.0f;
    case 1: return -float(Random(1000)) / 100.0f;
    case 2: return float(Random(5));
    case 3: {
        uint32_t bits = (g_random = g_random * 1103515245u + 12345u);
        float value;
        memcpy(&value, &bits, 4);
        return value;                   // anything, NaNs included
    }
    default: return float(Random(100000)) / 30000.0f;
    }
}

float RandomTime() {
    switch (Random(7)) {
    case 0: return 0.0f;
    case 1: return -1.0f;
    case 2: {
        uint32_t bits = 0x7fc00000u | Random(0x1000);
        float nan;
        memcpy(&nan, &bits, 4);
        return nan;
    }
    case 3: return float(Random(6)) / 60.0f;      // runs out exactly at some frame count
    case 4: return -0.0f;
    default: return float(Random(3000)) / 1000.0f;
    }
}

struct SoundCase {
    uint8_t object[sizeof(ASound)];
    ASoundPlayParams params;
    AListener listener;
    double volumes[4];
    bool done;
    int frames;
    int limited;
};

void MakeSoundCase(SoundCase &c) {
    for (size_t i = 0; i < sizeof(c.object); i++)
        c.object[i] = uint8_t(Random(256));
    ASound *sound = reinterpret_cast<ASound *>(c.object);
    sound->vtable = uint32_t(uintptr_t(g_fakeVtable));
    sound->volume = RandomFloat();
    sound->pitch = RandomFloat();
    sound->mix = FakeMix(Random(2) ? "Mix A" : "Mix B");
    AOneShotSound *oneShot = reinterpret_cast<AOneShotSound *>(c.object);
    oneShot->fxLevel = RandomFloat();
    oneShot->timeLeft = RandomTime();
    sound->voice.views[0].patch = int(Random(40)) - 1;
    ABank *bank = fgBanks[Random(kBankSlotCount)];
    sound->voice.views[0].bank = bank != NULL && Random(4) != 0 ? bank->handle : int(Random(5)) - 1;
    memset(&c.listener, 0, sizeof(c.listener));
    c.listener.unknown5c = int(Random(3));
    c.params.volume = RandomFloat();
    c.params.pitch = RandomFloat();
    c.params.azimuth = RandomFloat();
    c.params.unknown0c = RandomFloat();
    c.params.unknown10 = RandomFloat();
    c.params.unknown14 = Random(0x10000);
    c.params.listener = &c.listener;
    for (double &v : c.volumes)
        v = Random(5) == 0 ? double(RandomFloat()) : double(Random(1u << 30)) / double(1u << 29) / 3.0;
    c.done = Random(3) == 0;
    c.frames = int(Random(6));
    c.limited = 1 + int(Random(9));
}

// One method on a copy of the case, original or port: the log of calls and the object's bytes after.
template <class F>
std::string RunOn(SoundCase &c, uint8_t *object, bool original, F &&method) {
    memcpy(object, c.object, sizeof(c.object));
    g_base = object;
    g_calls.clear();
    memcpy(g_volumes, c.volumes, sizeof(g_volumes));
    g_volumeTurn = 0;
    g_done = c.done;
    AudioFrames() = c.frames;
    LimitedSounds() = c.limited;
    bool ran = Guarded([&] {
        if (original) {
            SoundOriginals originals;
            method(object);
        } else {
            method(object);
        }
    });
    std::string log = ran ? g_calls : "fault " + g_calls;
    Append(log, "| ");
    for (size_t i = 0; i < sizeof(c.object); i++)
        Append(log, "%02x", object[i]);
    return log;
}

template <class F>
void CompareSoundMethod(const char *what, SoundCase &c, uint32_t originalAddress, F &&port) {
    g_cases++;
    alignas(16) uint8_t objectO[sizeof(ASound)], objectP[sizeof(ASound)];
    std::string o = RunOn(c, objectO, true, [&](uint8_t *object) {
        ((void (__fastcall *)(void *, int, ASoundPlayParams *))originalAddress)(object, 0, &c.params);
    });
    std::string p = RunOn(c, objectP, false, [&](uint8_t *object) { port(object, &c.params); });
    CompareText(what, o, p);
}

void SoundPlayCases() {
    for (int round = 0; round < 3000; round++) {
        SoundCase c;
        MakeSoundCase(c);
        CompareSoundMethod("ASound::Play", c, 0x00127b00, [](uint8_t *object, ASoundPlayParams *params) {
            reinterpret_cast<ASound *>(object)->Play(params);
        });
        CompareSoundMethod("AOneShotSound::Play", c, 0x00127cb0, [](uint8_t *object, ASoundPlayParams *params) {
            reinterpret_cast<AOneShotSound *>(object)->Play(params);
        });
        CompareSoundMethod("ALimitedSound::Play", c, 0x00127da0, [](uint8_t *object, ASoundPlayParams *params) {
            reinterpret_cast<ALimitedSound *>(object)->Play(params);
        });
        // GetName answers ABank::GetPatchName's pointer (into a bank's index, or "")
        g_cases++;
        ASound *sound = reinterpret_cast<ASound *>(c.object);
        const char *o = NULL, *p = NULL;
        Guarded([&] {
            SoundOriginals originals;
            o = ((const char *(__fastcall *)(ASound *, int))0x00127bd0)(sound, 0);
        });
        Guarded([&] { p = sound->GetName(); });
        char detail[96];
        snprintf(detail, sizeof(detail), "GetName of patch %d bank %d", sound->voice.views[0].patch,
                 sound->voice.views[0].bank);
        Check(o == p, "ASound::GetName", detail);
    }
}

void SoundLifeCases() {
    static const char *const kMixes[] = { "Weapons Fire", "World Sounds", "" };
    typedef ASound *(__fastcall *ConstructBankFn)(void *, int, int, int, const char *);
    typedef ASound *(__fastcall *ConstructMixFn)(void *, int, const char *);
    typedef void (__fastcall *SoundDestructFn)(void *, int);
    typedef ASound *(__fastcall *SoundDeleteFn)(void *, int, unsigned);
    for (int round = 0; round < 200; round++) {
        SoundCase c;
        MakeSoundCase(c);
        int bank = int(Random(40)) - 1, patch = int(Random(300)) - 1;
        const char *mixName = kMixes[Random(3)];
        unsigned flags = Random(4);
        g_cases += 4;
        alignas(16) uint8_t objectO[sizeof(ASound)], objectP[sizeof(ASound)];
        std::string o = RunOn(c, objectO, true, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(((ConstructBankFn)0x00127a70)(object, 0, bank, patch, mixName)));
        });
        std::string p = RunOn(c, objectP, false, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(reinterpret_cast<ASound *>(object)->Construct(bank, patch, mixName)));
        });
        CompareText("ASound::Construct(bank, patch, mix)", o, p);
        o = RunOn(c, objectO, true, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(((ConstructMixFn)0x00127bf0)(object, 0, mixName)));
        });
        p = RunOn(c, objectP, false, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(reinterpret_cast<ASound *>(object)->Construct(mixName)));
        });
        CompareText("ASound::Construct(mix)", o, p);
        o = RunOn(c, objectO, true, [&](uint8_t *object) { ((SoundDestructFn)0x00127b70)(object, 0); });
        p = RunOn(c, objectP, false, [&](uint8_t *object) { reinterpret_cast<ASound *>(object)->Destruct(); });
        CompareText("ASound::Destruct", o, p);
        o = RunOn(c, objectO, true, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(((SoundDeleteFn)0x00127c80)(object, 0, flags)));
        });
        p = RunOn(c, objectP, false, [&](uint8_t *object) {
            Append(g_calls, "=%ld ", Offset(reinterpret_cast<ASound *>(object)->Delete(flags)));
        });
        CompareText("ASound::Delete", o, p);
    }
}

void ZoomCases() {
    for (int round = 0; round < 600; round++) {
        SoundCase c;
        MakeSoundCase(c);
        AZoomObj *zoom = reinterpret_cast<AZoomObj *>(c.object);
        zoom->mix = FakeMix("Weapons Init");
        zoom->voice = reinterpret_cast<AVoice *>(c.object + 0x40);     // logged as an offset
        zoom->inZone = Random(2) != 0;
        bool entering = Random(2) != 0;
        CompareSoundMethod("AZoomObj::Play", c, 0x00127e50, [](uint8_t *object, ASoundPlayParams *params) {
            reinterpret_cast<AZoomObj *>(object)->Play(params);
        });
        g_cases++;
        alignas(16) uint8_t objectO[sizeof(ASound)], objectP[sizeof(ASound)];
        std::string o = RunOn(c, objectO, true, [&](uint8_t *object) {
            ((void (__fastcall *)(void *, int, bool))0x00127ed0)(object, 0, entering);
        });
        std::string p = RunOn(c, objectP, false, [&](uint8_t *object) {
            reinterpret_cast<AZoomObj *>(object)->InTheZone(entering);
        });
        CompareText("AZoomObj::InTheZone", o, p);
    }
    // the constructor and destructor: the voice comes from the pools, so it is compared only as made or not
    if (fgBanks[6] == NULL) {
        printf("[audioindex] no bank in slot 6: AZoomObj's constructor not compared\n");
        return;
    }
    for (int round = 0; round < 4; round++) {
        SoundCase c;
        MakeSoundCase(c);
        g_cases += 2;
        alignas(16) uint8_t objectO[sizeof(ASound)], objectP[sizeof(ASound)];
        AVehicle *vehicle = (AVehicle *)(uintptr_t)(0x00400000u + Random(0x1000) * 4);
        AVoice *voiceO = NULL, *voiceP = NULL;
        std::string o = RunOn(c, objectO, true, [&](uint8_t *object) {
            ((AZoomObj *(__fastcall *)(void *, int, AVehicle *))0x00127dc0)(object, 0, vehicle);
            voiceO = reinterpret_cast<AZoomObj *>(object)->voice;
            reinterpret_cast<AZoomObj *>(object)->voice = NULL;
        });
        std::string p = RunOn(c, objectP, false, [&](uint8_t *object) {
            reinterpret_cast<AZoomObj *>(object)->Construct(vehicle);
            voiceP = reinterpret_cast<AZoomObj *>(object)->voice;
            reinterpret_cast<AZoomObj *>(object)->voice = NULL;
        });
        CompareText("AZoomObj::Construct", o, p);
        Check((voiceO != NULL) == (voiceP != NULL), "AZoomObj::Construct", "the voice made on one side only");
        o = RunOn(c, objectO, true, [&](uint8_t *object) {
            reinterpret_cast<AZoomObj *>(object)->voice = voiceO;
            ((void (__fastcall *)(void *, int))0x00128330)(object, 0);
            reinterpret_cast<AZoomObj *>(object)->voice = NULL;
        });
        p = RunOn(c, objectP, false, [&](uint8_t *object) {
            reinterpret_cast<AZoomObj *>(object)->voice = voiceP;
            reinterpret_cast<AZoomObj *>(object)->Destruct();
            reinterpret_cast<AZoomObj *>(object)->voice = NULL;
        });
        CompareText("AZoomObj::Destruct", o, p);
    }
}

void SoundCases() {
    g_fakeVtable[0] = uint32_t(uintptr_t(&FakeDelete));
    int frames = AudioFrames(), limited = LimitedSounds();
    HookInstall(0x00124690, (const void *)&FakeVoicePlay);
    HookInstall(0x0011ca10, (const void *)&FakeGetVolume);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x0011c980, (const void *)&FakeSetTransition);
    HookInstall(0x0011cb80, (const void *)&FakeReset);
    HookInstall(0x000d36c0, (const void *)&FakeIsDone);
    HookInstall(0x0001bfa0, (const void *)&FakeBaseConstruct);
    HookInstall(0x0011c6f0, (const void *)&FakeBaseDestruct);
    HookInstall(0x0011c830, (const void *)&FakeOperatorDelete);
    HookInstall(0x00123ca0, (const void *)&FakeVoiceConstruct);
    HookInstall(0x0003f400, (const void *)&FakeVoiceDestruct);
    HookInstall(0x00123c50, (const void *)&FakeViewDestruct);
    SoundPlayCases();
    SoundLifeCases();
    ZoomCases();
    HooksRemove();
    AudioFrames() = frames;
    LimitedSounds() = limited;
}

}  // namespace

void AudioIndexShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_AUDIOINDEXSHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;
    BankCases();
    SyntheticCases();
    MapCases();
    SoundCases();
    printf("[audioindex] AIndex and the sounds: %d cases, %d checks, %d differ (%d faults)\n", g_cases, g_checks, g_differ,
           g_faults);
    fflush(stdout);
}
