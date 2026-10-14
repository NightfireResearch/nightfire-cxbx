#include "OwnedGlobals.h"

#define OWNED_GLOBALS_INCLUDES
#include "OwnedGlobals.inc"
#undef OWNED_GLOBALS_INCLUDES
#define OWNED_GLOBALS_CHECKS
#include "OwnedGlobals.inc"
#undef OWNED_GLOBALS_CHECKS

#include "../engine/CoreFoundation.h"   // NullFunction
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

const OwnedGlobal kOwnedGlobals[] = {
#include "OwnedGlobals.inc"
};
const uint32_t kOwnedGlobalCount = sizeof(kOwnedGlobals) / sizeof(kOwnedGlobals[0]);

namespace {

const uint32_t kDummyNullFunction = 0x000d3580;
const uint32_t kImageStart = 0x00010000, kImageEnd = 0x00254f40;   // the XBE as mapped

uint8_t *Game(const OwnedGlobal &g) {
    return reinterpret_cast<uint8_t *>(uintptr_t(g.game));
}

uint32_t LentBytes() {
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++)
        if (kOwnedGlobals[i].lend)
            bytes += kOwnedGlobals[i].size;
    return bytes;
}

// Each original function that reaches an owned global's game copy, by entry (tools/owned_globals.py)
struct Toucher {
    uint32_t function;
    uint32_t global;        // its index in kOwnedGlobals
};

const Toucher kTouchers[] = {
#define OWNED_GLOBALS_TOUCHERS
#include "OwnedGlobals.inc"
#undef OWNED_GLOBALS_TOUCHERS
};

// Each original instruction naming a shared global (allocator state over memory both sides use, the globals the
// standalone prep owned): from the first window on it names ours instead, so the original and the port keep one
// state, as they did before it was ours. A word of the game's data holding one's address is pointed at ours too.
struct Redirect {
    uint32_t instruction;   // or the data word
    uint32_t global;        // its index in kOwnedGlobals
    uint32_t kind;          // 0 an instruction, 1 a data word, 2 an instruction naming an address just past it
};

const Redirect kRedirects[] = {
#define OWNED_GLOBALS_REDIRECTS
#include "OwnedGlobals.inc"
#undef OWNED_GLOBALS_REDIRECTS
};
const uint32_t kRedirectCount = sizeof(kRedirects) / sizeof(kRedirects[0]);

struct RedirectPatch {
    uint32_t at;            // the address word inside the instruction (0: not found)
    uint32_t original;
};
RedirectPatch g_redirectPatches[kRedirectCount];
bool g_redirectsFound;

// The word in each redirected instruction that falls in its global's game copy, read from the original's bytes:
// an instruction at an entry we replaced lies under the jump until the window swaps it in
// A compare of a register with an address (CMP EAX, imm32; CMP r32, imm32): a loop's end. In a global's first bytes
// it ends the array before it (the pointer a few bytes into its element), which stays the game's - pointed at ours,
// the loop would run on past the image; further in, or in the 64 bytes past the global, it ends a loop over the
// global's own arrays, so it is pointed at ours
bool ComparesWithImmediate(const uint8_t *code) {
    int at = 0;
    while (at < 4 && (code[at] == 0x66 || code[at] == 0x2e || code[at] == 0x3e || code[at] == 0x26 ||
                      code[at] == 0x36 || code[at] == 0x64 || code[at] == 0x65))
        at++;
    return code[at] == 0x3d || (code[at] == 0x81 && (code[at + 1] & 0xf8) == 0xf8);
}

// A memory operand with a register in it ([reg + address], [reg*scale + address]). Naming a global of a word or two,
// it is a field or an element of the record or array around it, its base folded into the address by the compiler
// (Ghidra puts the reference on the global): pointed at ours, the original would write into our image
bool IndexedOperand(const uint8_t *code) {
    int at = 0;
    while (at < 4 && (code[at] == 0x66 || code[at] == 0x2e || code[at] == 0x3e || code[at] == 0x26 ||
                      code[at] == 0x36 || code[at] == 0x64 || code[at] == 0x65 || code[at] == 0xf2 || code[at] == 0xf3))
        at++;
    uint8_t op = code[at++];
    if (op == 0x0f)
        op = code[at++];
    else if ((op >= 0xa0 && op <= 0xa3) || (op >= 0xb8 && op <= 0xbf) || op == 0x68 || op == 0x6a || op == 0xe8 ||
             op == 0xe9 || op == 0xa9 || op == 0x3d || op == 0x05 || op == 0x0d || op == 0x15 || op == 0x1d ||
             op == 0x25 || op == 0x2d || op == 0x35)
        return false;   // no ModRM byte
    uint8_t modrm = code[at];
    if ((modrm >> 6) == 3)
        return false;   // a register
    if ((modrm & 7) != 4)
        return (modrm >> 6) != 0;   // [reg + disp]; mod 0 is [disp32] alone (or [reg], with no address in it)
    return ((code[at + 1] >> 3) & 7) != 4 || (modrm >> 6) != 0;   // an index register, or a base with a displacement
}

void FindRedirects() {
    for (uint32_t r = 0; r < kRedirectCount; r++) {
        const OwnedGlobal &g = kOwnedGlobals[kRedirects[r].global];
        uint8_t code[15];
        XbeOriginal_Read(kRedirects[r].instruction, code, sizeof(code));
        const uint32_t kind = kRedirects[r].kind;
        bool compare = kind != 1 && ComparesWithImmediate(code);
        if (kind == 2 && !compare)
            continue;
        if (kind != 1 && g.size <= 8 && IndexedOperand(code))
            continue;
        int last = kind == 1 ? 4 : 15;
        for (int offset = kind == 1 ? 0 : 1; offset + 4 <= last; offset++) {
            uint32_t word;
            memcpy(&word, code + offset, 4);
            bool past = word - (g.game + g.size) < 64, inside = word - g.game < g.size;
            if (kind == 2 ? past : inside && !(compare && word - g.game < 16)) {
                g_redirectPatches[r].at = kRedirects[r].instruction + offset;
                g_redirectPatches[r].original = word;
                break;
            }
        }
    }
    g_redirectsFound = true;
}

// Points every redirect at ours (true) or back at the game's copy, in the original's bytes whether or not they
// are swapped in
void PointRedirects(bool ours) {
    if (!g_redirectsFound)
        FindRedirects();
    for (uint32_t r = 0; r < kRedirectCount; r++) {
        const OwnedGlobal &g = kOwnedGlobals[kRedirects[r].global];
        const RedirectPatch &patch = g_redirectPatches[r];
        if (patch.at == 0)
            continue;
        uint32_t word = ours ? uint32_t(uintptr_t(g.ours)) + (patch.original - g.game) : patch.original;
        XbeOriginal_Write(patch.at, &word, 4);
    }
}

// The windows' state for every lent global, each image at its offset
struct Images {
    uint32_t *offset;           // by table index
    uint32_t *stamp;            // by table index: the window group that last took it
    uint64_t *lastWindow;       // by table index: the range ([lo, hi) as lo << 32 | hi) of the last window to lend it
    bool *closed;               // by table index: a window over it has closed
    uint32_t group;             // the windows open together (a shadow's several ranges or entries) count as one
    int depth;
    bool redirected;            // the shared globals' redirects are in
    uint8_t *gameAtClose;       // the game's copy as the last window over it closed
    uint8_t *oursAtClose;       // ours then
    uint8_t *untouched;         // the game's copy before anything ran (OwnedGlobals_Install)
    std::set<std::pair<uint64_t, uint32_t>> prepared;   // (window, global): the shadow writes this copy itself
};

Images &WindowImages() {
    static Images images;
    if (images.offset == NULL) {
        uint32_t bytes = LentBytes();
        images.offset = new uint32_t[kOwnedGlobalCount];
        images.stamp = new uint32_t[kOwnedGlobalCount]();
        images.lastWindow = new uint64_t[kOwnedGlobalCount]();
        images.closed = new bool[kOwnedGlobalCount]();
        uint32_t at = 0;
        for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
            images.offset[i] = at;
            if (kOwnedGlobals[i].lend)
                at += kOwnedGlobals[i].size;
        }
        images.gameAtClose = new uint8_t[bytes];
        images.oursAtClose = new uint8_t[bytes];
        images.untouched = new uint8_t[bytes];
    }
    return images;
}

// Lends one global to the originals of `window`, unless the shadow behind that window prepares its copy itself
void Open(Images &images, uint32_t i, uint64_t window) {
    const OwnedGlobal &g = kOwnedGlobals[i];
    uint32_t at = images.offset[i];
    const std::pair<uint64_t, uint32_t> key(window, i);
    bool lend = false, prepares = false;
    if (images.prepared.count(key) != 0) {
        lend = false;
    } else if (!images.closed[i]) {
        prepares = memcmp(Game(g), images.untouched + at, g.size) != 0;   // written before any window
        lend = !prepares;
    } else if (images.lastWindow[i] != window) {
        lend = true;            // another shadow's windows had it last: whatever it left is not this one's input
    } else if (memcmp(Game(g), images.gameAtClose + at, g.size) == 0) {
        lend = memcmp(g.ours, images.oursAtClose + at, g.size) != 0;  // the originals' own state, unless ours moved
    } else {
        prepares = true;        // written between this shadow's windows
    }
    if (prepares)
        images.prepared.insert(key);
    if (lend)
        memcpy(Game(g), g.ours, g.size);
    images.lastWindow[i] = window;
}

void Close(Images &images, uint32_t i) {
    const OwnedGlobal &g = kOwnedGlobals[i];
    uint32_t at = images.offset[i];
    memcpy(images.gameAtClose + at, Game(g), g.size);
    memcpy(images.oursAtClose + at, g.ours, g.size);
    images.closed[i] = true;
}

// Every owned global an original with its entry in [lo, hi) reaches, once each per group of windows
void OwnedGlobals_Window(unsigned lo, unsigned hi, bool original) {
    Images &images = WindowImages();
    if (original) {
        if (images.depth++ == 0)
            images.group++;
        if (!images.redirected) {   // for good: nothing but an original reaches those instructions
            PointRedirects(true);
            images.redirected = true;
        }
    } else if (images.depth > 0) {
        images.depth--;
    }
    const uint64_t window = uint64_t(lo) << 32 | hi;
    const uint32_t count = sizeof(kTouchers) / sizeof(kTouchers[0]);
    uint32_t first = 0, last = count;
    while (first < last) {
        uint32_t middle = (first + last) / 2;
        if (kTouchers[middle].function < lo)
            first = middle + 1;
        else
            last = middle;
    }
    for (uint32_t t = first; t < count && kTouchers[t].function < hi; t++) {
        uint32_t i = kTouchers[t].global;
        if (!kOwnedGlobals[i].lend)
            continue;
        if (original) {
            if (images.stamp[i] == images.group)
                continue;       // lent (or left) by another of the group's windows
            images.stamp[i] = images.group;
            Open(images, i, window);
        } else {
            Close(images, i);
        }
    }
}

}  // namespace

void OwnedGlobals_Install() {
    Images &images = WindowImages();
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        if (g.lend)
            memcpy(images.untouched + images.offset[i], Game(g), g.size);
    }
    XbeOriginal_SetWindowHook(OwnedGlobals_Window);
}

void OwnedGlobals_LendAll() {
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        if (g.lend)
            memcpy(Game(g), g.ours, g.size);
    }
}

struct OwnedGlobalsSnapshot {
    uint8_t *game;
    uint8_t *ours;
};

OwnedGlobalsSnapshot *OwnedGlobals_Snapshot() {
    uint32_t bytes = LentBytes();
    OwnedGlobalsSnapshot *snapshot = new OwnedGlobalsSnapshot;
    snapshot->game = new uint8_t[bytes];
    snapshot->ours = new uint8_t[bytes];
    uint32_t at = 0;
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        if (!g.lend)
            continue;
        memcpy(snapshot->game + at, Game(g), g.size);
        memcpy(snapshot->ours + at, g.ours, g.size);
        at += g.size;
    }
    return snapshot;
}

void OwnedGlobals_Restore(const OwnedGlobalsSnapshot *snapshot) {
    uint32_t at = 0;
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        if (!g.lend)
            continue;
        memcpy(Game(g), snapshot->game + at, g.size);
        memcpy(g.ours, snapshot->ours + at, g.size);
        at += g.size;
    }
}

void OwnedGlobals_Free(OwnedGlobalsSnapshot *snapshot) {
    if (snapshot == NULL)
        return;
    delete[] snapshot->game;
    delete[] snapshot->ours;
    delete snapshot;
}

void *OwnedGlobals_Ours(uint32_t game) {
    // the table by game address (the copies never overlap), made once: the shadows look ours up in their loops
    static std::vector<const OwnedGlobal *> byGame;
    if (byGame.empty()) {
        for (uint32_t i = 0; i < kOwnedGlobalCount; i++)
            byGame.push_back(&kOwnedGlobals[i]);
        std::sort(byGame.begin(), byGame.end(),
                  [](const OwnedGlobal *a, const OwnedGlobal *b) { return a->game < b->game; });
    }
    auto above = std::upper_bound(byGame.begin(), byGame.end(), game,
                                  [](uint32_t address, const OwnedGlobal *g) { return address < g->game; });
    if (above != byGame.begin()) {
        const OwnedGlobal &g = **(above - 1);
        if (game - g.game < g.size)
            return static_cast<uint8_t *>(g.ours) + (game - g.game);
    }
    return NULL;
}

uint32_t OwnedGlobals_OursWord(uint32_t game) {
    return uint32_t(uintptr_t(OwnedGlobals_Ours(game)));
}

uint32_t OwnedGlobals_Game(const void *ours) {
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        uint32_t offset = uint32_t(static_cast<const uint8_t *>(ours) - static_cast<const uint8_t *>(g.ours));
        if (offset < g.size)
            return g.game + offset;
    }
    return 0;
}

uint32_t OwnedGlobals_AsOurs(uint32_t word) {
    void *ours = OwnedGlobals_Ours(word);
    return ours != NULL ? uint32_t(uintptr_t(ours)) : word;
}

// Our DLL's image: where every variable and function of ours lies
static void OurImage(uint32_t *start, uint32_t *end) {
    static uint32_t imageStart, imageEnd;
    if (imageEnd == 0) {
        HMODULE ours = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&OurImage), &ours);
        const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(ours);
        const IMAGE_NT_HEADERS *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
            reinterpret_cast<const uint8_t *>(ours) + dos->e_lfanew);
        imageStart = uint32_t(uintptr_t(ours));
        imageEnd = imageStart + nt->OptionalHeader.SizeOfImage;
    }
    *start = imageStart;
    *end = imageEnd;
}

uint32_t OwnedGlobals_AsGame(uint32_t word) {
    uint32_t start, end;
    OurImage(&start, &end);
    if (word < start || word >= end)
        return word;
    for (uint32_t i = 0; i < kOwnedGlobalCount; i++) {
        const OwnedGlobal &g = kOwnedGlobals[i];
        uint32_t offset = word - uint32_t(uintptr_t(g.ours));
        if (offset < g.size)
            return g.shared ? word : g.game + offset;
    }
    uint32_t entry = XbeOriginal_EntryOf(word);
    return entry != 0 ? entry : word;
}

bool OwnedGlobals_SameWord(uint32_t original, uint32_t port) {
    if (original == port || OwnedGlobals_AsOurs(original) == port || OwnedGlobals_AsGame(port) == original)
        return true;
    // the original's bare RET (dummyNullFunction, which SndSystemShadow hooks rather than our jump) and ours
    if (original == kDummyNullFunction && port == uint32_t(uintptr_t(&NullFunction)))
        return true;
    // a replaced original's entry where the port has the replacement (one of several entries jumping there)
    uint32_t start, end;
    OurImage(&start, &end);
    return port >= start && port < end && original < start && XbeOriginal_Target(original) == port;
}

void OwnedGlobals_MatchWords(void *original, const void *port, size_t size) {
    uint8_t *o = static_cast<uint8_t *>(original);
    const uint8_t *p = static_cast<const uint8_t *>(port);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t a, b;
        memcpy(&a, o + i, 4);
        memcpy(&b, p + i, 4);
        if (a != b && OwnedGlobals_SameWord(a, b))
            memcpy(o + i, &b, 4);
    }
}

size_t OwnedGlobals_FirstDifference(const void *original, const void *port, size_t size) {
    const uint8_t *o = static_cast<const uint8_t *>(original), *p = static_cast<const uint8_t *>(port);
    for (size_t i = 0; i < size; i++) {
        if (o[i] == p[i])
            continue;
        size_t word = i & ~size_t(3);
        uint32_t a, b;
        if (word + 4 <= size) {
            memcpy(&a, o + word, 4);
            memcpy(&b, p + word, 4);
            if (OwnedGlobals_SameWord(a, b)) {
                i = word + 3;
                continue;
            }
        }
        return i;
    }
    return size;
}

uint32_t OwnedGlobals_Canonical(uint32_t word) {
    if (word < kImageStart || word >= kImageEnd)   // most words: no address in the image
        return word;
    if (word == kDummyNullFunction)   // the original's bare RET, too short for a jump: ours is NullFunction
        return uint32_t(uintptr_t(&NullFunction));
    uint32_t ours = OwnedGlobals_AsOurs(word);
    if (ours != word)
        return ours;
    uint32_t target = XbeOriginal_Target(word);
    return target != 0 ? target : word;
}

void OwnedGlobals_CanonicalWords(void *bytes, size_t size) {
    uint8_t *at = static_cast<uint8_t *>(bytes);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t word;
        memcpy(&word, at + i, 4);
        uint32_t translated = OwnedGlobals_Canonical(word);
        if (translated != word)
            memcpy(at + i, &translated, 4);
    }
}

void OwnedGlobals_TranslateWords(void *bytes, size_t size) {
    uint8_t *at = static_cast<uint8_t *>(bytes);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t word;
        memcpy(&word, at + i, 4);
        uint32_t translated = OwnedGlobals_AsOurs(word);
        if (translated != word)
            memcpy(at + i, &translated, 4);
    }
}

uint32_t OwnedGlobals_VectorWord(const void *vector) {
    static const float kZeroVector[3] = {};
    HMODULE ours = NULL, holder = NULL;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (vector != NULL && memcmp(vector, kZeroVector, sizeof(kZeroVector)) == 0 &&
        GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(&OwnedGlobals_VectorWord), &ours) &&
        GetModuleHandleExA(flags, static_cast<LPCSTR>(vector), &holder) && holder == ours)
        return 0x00243030;
    return uint32_t(uintptr_t(vector));
}
