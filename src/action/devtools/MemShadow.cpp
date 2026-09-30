// Shadow test for the heap allocator (src/action/memory.cpp): the original Mem_Init, Mem_Malloc, Mem_Free and
// Mem_Shrink run on one scratch heap and ours on another, through the same long pseudo-random sequence of calls,
// for each allocation method. After every call the two heaps must have the same blocks (sizes, flags, slack, free
// flags, links) and the calls the same results, pointers compared as offsets into their own heap.
//
// The originals use the game's copies of the heap globals (0x00223a7c..0x00223a88, QuickBlock at 0x002237d8),
// which nothing else touches now that the allocator is ours, and ours use memory.cpp's; both are pointed at their
// scratch heap for the test and put back afterwards. Run at start with MenuShadowTests=on (MenuProbe.cpp), before
// the game has a heap.

#include "MemShadow.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// memory.cpp's heap globals
extern uint32_t PtrHeap, HeapByteSize, MallocMethod, QuickBlock;

// The game's
#define GamePtrHeap      U32_AT(0x00223a80)
#define GameHeapByteSize U32_AT(0x00223a88)
#define GameMallocMethod U32_AT(0x00223a7c)
#define GameQuickBlock   U32_AT(0x002237d8)

static const unsigned kMemInit = 0x00070a40;
static const unsigned kMemMalloc = 0x00070ae0;
static const unsigned kMemFree = 0x000705f0;
static const unsigned kMemShrink = 0x00070850;
static const unsigned kMemPrintAllInfo = 0x000709c0;

#define SCRATCH_SIZE 0x100000
#define MAX_LIVE 256

typedef void *(__cdecl *MallocFn)(uint, MallocFlags, uint);
typedef void(__cdecl *FreeFn)(void **);
typedef void(__cdecl *ShrinkFn)(void **, uint);
typedef void(__cdecl *InitFn)(void);

static char *ours, *theirs;   // the two scratch heaps
static int mismatches, reported;

static uint32_t rng;
static uint32_t Rand(uint32_t n) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}

// A heap pointer as an offset into its heap, so the two can be compared; anything else as itself
static uintptr_t Offset(const void *p, const char *heap) {
    if ((const char *)p >= heap && (const char *)p < heap + SCRATCH_SIZE)
        return (uintptr_t)((const char *)p - heap) | 0x80000000u;
    return (uintptr_t)p;
}

static void Mismatch(const char *what, int step, uintptr_t a, uintptr_t b) {
    mismatches++;
    if (reported++ < 10)
        printf("[mem] method %u step %d: %s: ours 0x%08x, theirs 0x%08x\n", MallocMethod, step, what, (uint)a, (uint)b);
}

// Walks both heaps' blocks side by side
static void CompareHeaps(int step) {
    char *a = ours, *b = theirs;
    char *endA = ours + SCRATCH_SIZE - 0xc, *endB = theirs + SCRATCH_SIZE - 0xc;
    while (a < endA && b < endB) {
        if (memcmp(a + 4, b + 4, 8) != 0) {
            Mismatch("block header (size/flags/slack/free)", step, Offset(a, ours), Offset(b, theirs));
            return;
        }
        if (Offset(*(char **)a, ours) != Offset(*(char **)b, theirs)) {
            Mismatch("block prev", step, Offset(*(char **)a, ours), Offset(*(char **)b, theirs));
            return;
        }
        uint size = *(uint *)(a + 4);
        if (size == 0)
            return;
        a += size;
        b += size;
    }
    if (Offset(a, ours) != Offset(b, theirs))
        Mismatch("heap end", step, Offset(a, ours), Offset(b, theirs));
    if (Offset((void *)(uintptr_t)QuickBlock, ours) != Offset((void *)(uintptr_t)GameQuickBlock, theirs))
        Mismatch("QuickBlock", step, Offset((void *)(uintptr_t)QuickBlock, ours),
                 Offset((void *)(uintptr_t)GameQuickBlock, theirs));
}

static int RunMethod(uint32_t method, int steps) {
    // Both heaps start as Mem_Init leaves them
    Mem_Init();
    {
        XbeOriginalScope init(kMemInit);
        ((InitFn)(uintptr_t)kMemInit)();
    }
    MallocMethod = method;
    GameMallocMethod = method;
    CompareHeaps(-1);

    void *liveA[MAX_LIVE], *liveB[MAX_LIVE];
    uint liveSize[MAX_LIVE];
    int live = 0;
    int step = 0;

    for (; step < steps && reported < 10; step++) {
        uint op = Rand(100);
        if (live > 40)
            op = 55 + Rand(45); // keep the heap from filling up
        if (op < 55 || live == 0) {
            static const uint aligns[] = {0, 0, 0, 8, 0x10, 0x20, 0x80, 0x1000};
            static const uint flagAligns[] = {4, 4, 8, 0x10, 0x20, 0x40};
            uint size = Rand(8) == 0 ? Rand(0x8000) : Rand(0x800) + 1;
            MallocFlags flags = (MallocFlags)((Rand(0x50) << 8) | flagAligns[Rand(6)]);
            uint align = aligns[Rand(8)];
            void *a = Mem_Malloc(size, flags, align);
            void *b;
            {
                XbeOriginalScope m(kMemMalloc), p(kMemPrintAllInfo);
                b = ((MallocFn)(uintptr_t)kMemMalloc)(size, flags, align);
            }
            if (Offset(a, ours) != Offset(b, theirs))
                Mismatch("Mem_Malloc result", step, Offset(a, ours), Offset(b, theirs));
            // Next fit leaves QuickBlock NULL when nothing fits, and the next call (either version) reads through
            // it: the game's heap never runs dry, but this one can, so that is where a next-fit run ends
            if (method == 0 && (a == NULL || b == NULL)) {
                CompareHeaps(step);
                break;
            }
            if (a != NULL && b != NULL && live < MAX_LIVE) {
                liveA[live] = a;
                liveB[live] = b;
                liveSize[live] = size;
                live++;
            }
        } else if (op < 90) {
            int i = Rand(live);
            void *a = liveA[i], *b = liveB[i];
            Mem_Free(&a);
            {
                XbeOriginalScope f(kMemFree);
                ((FreeFn)(uintptr_t)kMemFree)(&b);
            }
            if (a != NULL || b != NULL)
                Mismatch("Mem_Free cleared pointer", step, (uintptr_t)a, (uintptr_t)b);
            live--;
            liveA[i] = liveA[live];
            liveB[i] = liveB[live];
            liveSize[i] = liveSize[live];
        } else if (op < 93) {
            // Not a heap pointer: only cleared
            int local;
            void *a = &local, *b = &local;
            Mem_Free(&a);
            {
                XbeOriginalScope f(kMemFree);
                ((FreeFn)(uintptr_t)kMemFree)(&b);
            }
            if (a != NULL || b != NULL)
                Mismatch("Mem_Free of a foreign pointer", step, (uintptr_t)a, (uintptr_t)b);
        } else {
            // Free a part of an allocation: its front, its back, the middle, or all of it, with a pointer that may be
            // a little way in (but inside it: the original faults on a pointer in no block). The allocation is then forgotten (what is left of it stays allocated).
            int i = Rand(live);
            uint size = liveSize[i];
            uint start = 0, count = size;
            switch (Rand(4)) {
            case 0: count = Rand(size + 1); break;
            case 1: start = Rand(size); count = size - start; break;
            case 2:
                // At least 0x20: a smaller one at a pointer that is not 4-aligned puts the tail header before the
                // middle one, and both versions build a block of negative size (see Mem_FreeMiddle)
                start = Rand(size);
                count = Rand(size - start + 1);
                if (count < 0x20)
                    count = 0;
                break;
            default: break;
            }
            void *a = (char *)liveA[i] + start, *b = (char *)liveB[i] + start;
            Mem_Shrink(&a, count);
            {
                XbeOriginalScope s(kMemShrink), f(kMemFree);
                ((ShrinkFn)(uintptr_t)kMemShrink)(&b, count);
            }
            if (Offset(a, ours) != Offset(b, theirs))
                Mismatch("Mem_Shrink cleared pointer", step, Offset(a, ours), Offset(b, theirs));
            live--;
            liveA[i] = liveA[live];
            liveB[i] = liveB[live];
            liveSize[i] = liveSize[live];
        }
        CompareHeaps(step);
    }
    return step;
}

void MemShadow_Run(void) {
    uint32_t saved[4] = {PtrHeap, HeapByteSize, MallocMethod, QuickBlock};
    uint32_t savedGame[4] = {GamePtrHeap, GameHeapByteSize, GameMallocMethod, GameQuickBlock};

    // Same alignment for both, so that every alignment computation comes out the same
    ours = (char *)_aligned_malloc(SCRATCH_SIZE, 0x10000);
    theirs = (char *)_aligned_malloc(SCRATCH_SIZE, 0x10000);
    PtrHeap = (uint32_t)(uintptr_t)ours;
    HeapByteSize = SCRATCH_SIZE;
    GamePtrHeap = (uint32_t)(uintptr_t)theirs;
    GameHeapByteSize = SCRATCH_SIZE;

    mismatches = 0;
    reported = 0;
    static const uint32_t methods[] = {0, 1, 2, 3, 5};
    int steps = 4000;
    char ran[128] = "";
    for (int m = 0; m < 5; m++) {
        rng = 0x1234567u + m;
        int n = RunMethod(methods[m], steps);
        sprintf(ran + strlen(ran), "%s%u:%d", m ? ", " : "", methods[m], n);
    }
    printf("[mem] Mem_Malloc/Mem_Free/Mem_Shrink, calls per method (%s): %d mismatches\n", ran, mismatches);

    _aligned_free(ours);
    _aligned_free(theirs);
    PtrHeap = saved[0];
    HeapByteSize = saved[1];
    MallocMethod = saved[2];
    QuickBlock = saved[3];
    GamePtrHeap = savedGame[0];
    GameHeapByteSize = savedGame[1];
    GameMallocMethod = savedGame[2];
    GameQuickBlock = savedGame[3];
}
