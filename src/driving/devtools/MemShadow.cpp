#include "MemShadow.h"

#include "../platform/RealMemory.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of EA's block allocator (platform/RealMemory.cpp) against the original, run at injection time
// when NIGHTFIRE_MEMSHADOW is set in the environment, before the game has a thread of its own (the originals are
// swapped back in process-wide while they run, see common/xbeOriginal.h).
//
// Two classes of the same size, each in its own page-aligned arena: one made and driven by the originals, the
// other by the port, through the same random sequence of allocations (bottom and top, first and largest fit,
// aligned with offsets), frees and resizes. After every step the two heaps must be the same shape: every block's
// offset, magic, flags, size and links, the free list, the class record, and what MEM_totalunused,
// MEM_largestunused and MEM_validate say - pointers compared as offsets from each arena's start. Every
// allocation's data is filled with a pattern and checked too, which catches a resize moving the wrong bytes.
// ---------------------------------------------------------------------------------------------------------------

static const unsigned kOriginalClass = 62, kPortClass = 63;
static const int kArenaBytes = 1 << 20;
static const int kOperations = 20000;
static const int kLive = 400;

#define MemClass        ((uint8_t **)0x00242ce0u)
#define MemOutOfMemory  (*(void **)0x00242cd8u)

// The originals, by address. Each runs inside a scope that puts its entry bytes back.
typedef int (*InitClassFn)(unsigned, const char *, void *, int, int, int, int, char, char, char);
typedef void *(*AllocFn)(const char *, int, unsigned);
typedef void *(*AllocAlignFn)(const char *, int, int, int, unsigned);
typedef bool (*FreeFn)(void *);
typedef void *(*ResizeFn)(void *, int);
typedef int (*UnusedFn)(unsigned);
typedef bool (*ValidateFn)(void);
typedef int (*RestoreFn)(unsigned);

static const unsigned kOriginals[] = { 0x001500f0, 0x00114370, 0x00114390, 0x00114340, 0x00113f20, 0x00117110,
                                       0x00113de0, 0x00113a70, 0x00113ac0, 0x00113d50, 0x00113b50, 0x001143b0,
                                       0x001140a0, 0x00113a50, 0x00113a60, 0x001502b0, 0x00150300 };

// An original we could not put back would make the test compare the port with itself.
static int g_notRestored = 0, g_allocations = 0, g_refused = 0;

struct OriginalsScope {
    OriginalsScope() { for (unsigned a : kOriginals) g_notRestored += !XbeOriginal_Restore(a, true); }
    ~OriginalsScope() { for (unsigned a : kOriginals) XbeOriginal_Restore(a, false); }
};

static uint32_t g_random = 12345;
static uint32_t Random(uint32_t below) {
    g_random = g_random * 1103515245u + 12345u;
    return below == 0 ? 0 : (g_random >> 8) % below;
}

// One heap's shape, as offsets from its arena
struct Shape {
    char text[64 * 1024];
    size_t length;
};

static void Append(Shape *s, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(s->text + s->length, sizeof(s->text) - s->length, format, arguments);
    va_end(arguments);
    if (n > 0 && s->length + n < sizeof(s->text))
        s->length += n;
}

static long Offset(const uint8_t *arena, const void *pointer) {
    return pointer == NULL ? -1 : (long)((const uint8_t *)pointer - arena);
}

static void Describe(Shape *s, const uint8_t *arena, unsigned number) {
    s->length = 0;
    const uint8_t *cls = MemClass[number];
    Append(s, "class@%ld align %d/%d tail %d flags %x first %ld high %ld | ", Offset(arena, cls), *(int *)(cls + 0x28),
           *(int *)(cls + 0x2c), *(int *)(cls + 0x30), *(unsigned *)(cls + 0x34) & ~0x3fu,
           Offset(arena, *(void **)(cls + 8)), Offset(arena, *(void **)(cls + 0xc)));
    for (const uint8_t *b = *(uint8_t **)(cls + 8); b != NULL; b = *(uint8_t **)(b + 8)) {
        Append(s, "%ld:%04x/%04x/%d n%ld p%ld", Offset(arena, b), *(uint16_t *)b, *(uint16_t *)(b + 2) & ~0x3f,
               *(int *)(b + 4), Offset(arena, *(void **)(b + 8)), Offset(arena, *(void **)(b + 0xc)));
        if (*(uint16_t *)b == 0x4246)
            Append(s, " f%ld/%ld", Offset(arena, *(void **)(b + 0x10)), Offset(arena, *(void **)(b + 0x14)));
        Append(s, "; ");
    }
}

static void Fill(void *data, int bytes, uint32_t seed) {
    for (int i = 0; i < bytes; i++)
        ((uint8_t *)data)[i] = (uint8_t)(seed * 31 + i);
}

static bool Check(const void *data, int bytes, uint32_t seed) {
    for (int i = 0; i < bytes; i++)
        if (((const uint8_t *)data)[i] != (uint8_t)(seed * 31 + i))
            return false;
    return true;
}

void MemShadow_Run(void) {
    if (getenv("NIGHTFIRE_MEMSHADOW") == NULL)
        return;
    printf("[memshadow] %d operations on the original allocator and the port, side by side\n", kOperations);

    uint8_t *arenaO = (uint8_t *)VirtualAlloc(NULL, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    uint8_t *arenaP = (uint8_t *)VirtualAlloc(NULL, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void *savedOom = MemOutOfMemory;
    MemOutOfMemory = NULL;

    int sizeO, sizeP;
    {
        OriginalsScope scope;
        sizeO = ((InitClassFn)0x001500f0)(kOriginalClass, "SHADOW", arenaO, kArenaBytes, 16, 128, 0, 0, 0, 0);
    }
    sizeP = MEMCLASS_init(kPortClass, "SHADOW", arenaP, kArenaBytes, 16, 128, 0, 0, 0, 0);

    static Shape shapeO, shapeP;
    void *liveO[kLive] = {}, *liveP[kLive] = {};
    int liveBytes[kLive] = {};
    uint32_t liveSeed[kLive] = {};
    int failures = 0;

    if (sizeO != sizeP) {
        printf("[memshadow] class sizes differ: original %d, port %d\n", sizeO, sizeP);
        failures++;
    }

    for (int op = 0; op < kOperations && failures == 0; op++) {
        int slot = (int)Random(kLive);
        char what[96];
        void *resultO = NULL, *resultP = NULL;
        if (liveO[slot] == NULL) {
            int size = (int)Random(Random(8) == 0 ? 20000 : 1500);
            unsigned flags = (Random(3) == 0 ? 0x100 : 0) | (Random(4) == 0 ? 0x200 : 0) | (Random(6) == 0 ? 0x400 : 0);
            int alignment = Random(4) == 0 ? (1 << (2 + Random(8))) : 0;
            int offset = alignment != 0 && Random(2) == 0 ? (int)Random(64) * 4 : 0;
            snprintf(what, sizeof(what), "alloc %d flags %x align %d offset %d", size, flags, alignment, offset);
            {
                OriginalsScope scope;
                resultO = alignment != 0 ? ((AllocAlignFn)0x00114340)("t", size, alignment, offset, kOriginalClass | flags)
                                         : ((AllocFn)0x00114370)("t", size, kOriginalClass | flags);
            }
            resultP = alignment != 0 ? MEM_allocalign("t", size, alignment, offset, kPortClass | flags)
                                     : MEM_alloc("t", size, kPortClass | flags);
            g_allocations++;
            g_refused += resultP == NULL;
            if (resultO != NULL && resultP != NULL) {
                liveO[slot] = resultO;
                liveP[slot] = resultP;
                liveBytes[slot] = size;
                liveSeed[slot] = (uint32_t)op;
                Fill(resultO, size, (uint32_t)op);
                Fill(resultP, size, (uint32_t)op);
            }
        } else if (Random(3) != 0) {
            snprintf(what, sizeof(what), "free slot %d (%d bytes)", slot, liveBytes[slot]);
            {
                OriginalsScope scope;
                ((FreeFn)(Random(2) ? 0x00113f20 : 0x00117110))(liveO[slot]);
            }
            MEM_free(liveP[slot]);
            liveO[slot] = liveP[slot] = NULL;
        } else {
            int size = Random(20) == 0 ? -1 : (int)Random(2000);
            snprintf(what, sizeof(what), "resize slot %d from %d to %d", slot, liveBytes[slot], size);
            {
                OriginalsScope scope;
                resultO = ((ResizeFn)0x00113de0)(liveO[slot], size);
            }
            resultP = MEM_resize(liveP[slot], size);
            int kept = (int)MEM_size(liveP[slot]);
            int checked = kept < liveBytes[slot] ? kept : liveBytes[slot];
            if (!Check(liveO[slot], checked, liveSeed[slot]) || !Check(liveP[slot], checked, liveSeed[slot])) {
                printf("[memshadow] op %d (%s): data changed by the resize\n", op, what);
                failures++;
            }
            liveBytes[slot] = kept;
            Fill(liveO[slot], kept, liveSeed[slot]);
            Fill(liveP[slot], kept, liveSeed[slot]);
        }

        if (Offset(arenaO, resultO) != Offset(arenaP, resultP)) {
            printf("[memshadow] op %d (%s): original returned +%ld, port +%ld\n", op, what, Offset(arenaO, resultO),
                   Offset(arenaP, resultP));
            failures++;
        }
        int unusedO, unusedP, largestO, largestP;
        bool validO, validP;
        {
            OriginalsScope scope;
            unusedO = ((UnusedFn)0x00113ac0)(kOriginalClass);
            largestO = ((UnusedFn)0x00113a70)(kOriginalClass);
            validO = ((ValidateFn)0x00113d50)();
        }
        unusedP = MEM_totalunused(kPortClass);
        largestP = MEM_largestunused(kPortClass);
        validP = MEM_validate();
        if (unusedO != unusedP || largestO != largestP || validO != validP || !validP) {
            printf("[memshadow] op %d (%s): unused %d/%d, largest %d/%d, valid %d/%d\n", op, what, unusedO, unusedP,
                   largestO, largestP, validO, validP);
            failures++;
        }
        Describe(&shapeO, arenaO, kOriginalClass);
        Describe(&shapeP, arenaP, kPortClass);
        if (shapeO.length != shapeP.length || memcmp(shapeO.text, shapeP.text, shapeO.length) != 0) {
            size_t at = 0;
            while (at < shapeO.length && at < shapeP.length && shapeO.text[at] == shapeP.text[at])
                at++;
            size_t from = at > 120 ? at - 120 : 0;
            printf("[memshadow] op %d (%s): heaps differ\n  original ...%.240s\n  port     ...%.240s\n", op, what,
                   shapeO.text + from, shapeP.text + from);
            failures++;
        }
        for (int i = 0; i < kLive && failures == 0; i++) {
            if (liveO[i] != NULL && (!Check(liveO[i], liveBytes[i], liveSeed[i]) ||
                                     !Check(liveP[i], liveBytes[i], liveSeed[i]))) {
                printf("[memshadow] op %d (%s): the data of slot %d changed\n", op, what, i);
                failures++;
            }
        }
    }

    {
        OriginalsScope scope;
        ((RestoreFn)0x001502b0)(kOriginalClass);
    }
    MEMCLASS_restore(kPortClass);
    MemOutOfMemory = savedOom;
    VirtualFree(arenaO, 0, MEM_RELEASE);
    VirtualFree(arenaP, 0, MEM_RELEASE);
    if (g_notRestored != 0) {
        printf("[memshadow] %d originals could not be restored\n", g_notRestored);
        failures++;
    }
    printf("[memshadow] %s (%d allocations, %d refused)\n", failures == 0 ? "the same after every operation" : "FAILED",
           g_allocations, g_refused);
    fflush(stdout);
}
