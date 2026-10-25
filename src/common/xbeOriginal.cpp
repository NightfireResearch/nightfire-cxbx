#include "xbeOriginal.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// One entry per jump the injector wrote; a few thousand at most. Kept unsorted - lookups only happen in
// shadow tests, and a linear scan of this is nothing beside the function being tested.
struct PatchRecord {
    unsigned at;
    unsigned char original[5];
    unsigned char patched[5];
};

#define MAX_PATCHES 8192
static PatchRecord g_patches[MAX_PATCHES];
static int g_numPatches = 0;

static PatchRecord *Find(unsigned at) {
    for (int i = 0; i < g_numPatches; i++)
        if (g_patches[i].at == at)
            return &g_patches[i];
    return NULL;
}

void XbeOriginal_Record(unsigned at) {
    // Recorded once: a second patch of the same address must not record our own jump as the "original".
    if (Find(at) != NULL)
        return;
    if (g_numPatches >= MAX_PATCHES) {
        printf("[original] too many patches to record 0x%08x\n", at);
        return;
    }
    PatchRecord *r = &g_patches[g_numPatches++];
    r->at = at;
    memcpy(r->original, (void *)(size_t)at, 5);
    memset(r->patched, 0, 5);
}

int XbeOriginal_Count(void) {
    return g_numPatches;
}

unsigned XbeOriginal_At(int i) {
    return i >= 0 && i < g_numPatches ? g_patches[i].at : 0;
}

static XbeOriginalWindowHook g_windowHook = NULL;

void XbeOriginal_SetWindowHook(XbeOriginalWindowHook hook) {
    g_windowHook = hook;
}

static void Swap(PatchRecord *r, bool original) {
    unsigned at = r->at;
    // The jump's bytes are only known once it has been written, so they are captured on the first swap.
    if (original)
        memcpy(r->patched, (void *)(size_t)at, 5);
    memcpy((void *)(size_t)at, original ? r->original : r->patched, 5);
    FlushInstructionCache(GetCurrentProcess(), (void *)(size_t)at, 5);
}

bool XbeOriginal_Restore(unsigned at, bool original) {
    PatchRecord *r = Find(at);
    if (r == NULL)
        return false;
    if (original && g_windowHook != NULL)
        g_windowHook(at, at + 1, true);
    Swap(r, original);
    if (!original && g_windowHook != NULL)
        g_windowHook(at, at + 1, false);
    return true;
}

int XbeOriginal_RestoreRange(unsigned lo, unsigned hi, bool original) {
    int n = 0;
    for (int i = 0; i < g_numPatches; i++)
        if (g_patches[i].at >= lo && g_patches[i].at < hi)
            n++;
    if (n == 0)
        return 0;
    if (original && g_windowHook != NULL)
        g_windowHook(lo, hi, true);
    for (int i = 0; i < g_numPatches; i++)
        if (g_patches[i].at >= lo && g_patches[i].at < hi)
            Swap(&g_patches[i], original);
    if (!original && g_windowHook != NULL)
        g_windowHook(lo, hi, false);
    return n;
}

// The records by address, sorted again whenever more have been recorded
static PatchRecord *g_byAddress[MAX_PATCHES];
static int g_numSorted = 0;

static int ByAddress(const void *a, const void *b) {
    unsigned x = (*(PatchRecord *const *)a)->at, y = (*(PatchRecord *const *)b)->at;
    return x < y ? -1 : x > y;
}

// The jump written over the byte at 'at', if any (jumps never overlap)
static PatchRecord *Covering(unsigned at) {
    if (g_numSorted != g_numPatches) {
        for (int i = 0; i < g_numPatches; i++)
            g_byAddress[i] = &g_patches[i];
        qsort(g_byAddress, g_numPatches, sizeof(g_byAddress[0]), ByAddress);
        g_numSorted = g_numPatches;
    }
    int first = 0, last = g_numSorted;   // the first record above 'at'
    while (first < last) {
        int middle = (first + last) / 2;
        if (g_byAddress[middle]->at <= at)
            first = middle + 1;
        else
            last = middle;
    }
    if (first > 0 && at - g_byAddress[first - 1]->at < 5)
        return g_byAddress[first - 1];
    return NULL;
}

unsigned XbeOriginal_Target(unsigned at) {
    PatchRecord *r = Covering(at);
    if (r == NULL || r->at != at)
        return 0;
    // The jump is in the code unless a window has the original's bytes in; then it is the one the swap captured
    const unsigned char *jump = (const unsigned char *)(size_t)at;
    if (memcmp(jump, r->original, 5) == 0)
        jump = r->patched;
    if (jump[0] != 0xE9)
        return 0;
    unsigned relative;
    memcpy(&relative, jump + 1, 4);
    return at + 5 + relative;
}

// (target, entry) for every jump, by target; made again whenever more have been recorded
struct JumpTarget {
    unsigned target, at;
};
static JumpTarget g_byTarget[MAX_PATCHES];
static int g_numTargets = -1, g_targetsFrom = 0;

static int ByTarget(const void *a, const void *b) {
    const JumpTarget *x = (const JumpTarget *)a, *y = (const JumpTarget *)b;
    if (x->target != y->target)
        return x->target < y->target ? -1 : 1;
    return x->at < y->at ? -1 : x->at > y->at;
}

unsigned XbeOriginal_EntryOf(unsigned target) {
    if (g_numTargets < 0 || g_targetsFrom != g_numPatches) {
        g_numTargets = 0;
        for (int i = 0; i < g_numPatches; i++) {
            unsigned t = XbeOriginal_Target(g_patches[i].at);
            if (t != 0) {
                g_byTarget[g_numTargets].target = t;
                g_byTarget[g_numTargets].at = g_patches[i].at;
                g_numTargets++;
            }
        }
        qsort(g_byTarget, g_numTargets, sizeof(g_byTarget[0]), ByTarget);
        g_targetsFrom = g_numPatches;
    }
    int first = 0, last = g_numTargets;   // the first record at or above 'target'
    while (first < last) {
        int middle = (first + last) / 2;
        if (g_byTarget[middle].target < target)
            first = middle + 1;
        else
            last = middle;
    }
    return first < g_numTargets && g_byTarget[first].target == target ? g_byTarget[first].at : 0;
}

void XbeOriginal_Read(unsigned at, void *out, unsigned n) {
    unsigned char *bytes = (unsigned char *)out;
    for (unsigned i = 0; i < n; i++) {
        PatchRecord *r = Covering(at + i);
        bytes[i] = r != NULL ? r->original[at + i - r->at] : *(unsigned char *)(size_t)(at + i);
    }
}

void XbeOriginal_Write(unsigned at, const void *in, unsigned n) {
    const unsigned char *bytes = (const unsigned char *)in;
    for (unsigned i = 0; i < n; i++) {
        unsigned char *code = (unsigned char *)(size_t)(at + i);
        PatchRecord *r = Covering(at + i);
        if (r == NULL) {
            *code = bytes[i];
            continue;
        }
        bool restored = memcmp((void *)(size_t)r->at, r->original, 5) == 0;
        r->original[at + i - r->at] = bytes[i];
        if (restored)
            *code = bytes[i];
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)(size_t)at, n);
}

bool XbeOriginal_Redirect(unsigned at, const void *to) {
    if (Find(at) == NULL)
        return false;
    unsigned char jump[5] = { 0xE9 };
    unsigned relative = (unsigned)(size_t)to - (at + 5);
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)(size_t)at, jump, 5);
    FlushInstructionCache(GetCurrentProcess(), (void *)(size_t)at, 5);
    return true;
}
