#include "xbeOriginal.h"

#include <windows.h>
#include <string.h>
#include <stdio.h>

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

bool XbeOriginal_Restore(unsigned at, bool original) {
    PatchRecord *r = Find(at);
    if (r == NULL)
        return false;
    // The jump's bytes are only known once it has been written, so they are captured on the first swap.
    if (original)
        memcpy(r->patched, (void *)(size_t)at, 5);
    memcpy((void *)(size_t)at, original ? r->original : r->patched, 5);
    FlushInstructionCache(GetCurrentProcess(), (void *)(size_t)at, 5);
    return true;
}

int XbeOriginal_RestoreRange(unsigned lo, unsigned hi, bool original) {
    int n = 0;
    for (int i = 0; i < g_numPatches; i++)
        if (g_patches[i].at >= lo && g_patches[i].at < hi && XbeOriginal_Restore(g_patches[i].at, original))
            n++;
    return n;
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
