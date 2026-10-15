#include "HeapPad.h"
#include "CodeRetarget.h"

#include "../EventManager.hpp"
#include "../engine/UMemory.hpp"
#include "../platform/Host.h"
#include "../platform/RealMemory.h"
#include "../render/Particles.h"
#include "../sound/snd/System.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_HEAP_PAD=<bytes> (docs/driving/64-bit.md 7.2, package 0): every allocator the game uses is asked for
// <bytes> more than the game asks for, as if each structure had grown, so that block sizes, the spacing between
// blocks, which free block is reused, size-class buckets and pool use all differ from a normal run. A lockstep run
// with it that still matches the baselines shows the game's behaviour does not depend on allocation sizes (what a
// 64-bit build with native pointers changes); a difference is something to find.
//
// The allocators, at their lowest level ours (everything else ends in one of these):
//   HostHeapAlloc, HostHeapReAlloc   the process heap: XAPI's RtlAllocateHeap, the C runtime's malloc, calloc,
//                                    realloc (and through malloc EAGL's own allocator)
//   MEM_alloc, MEM_allocz, MEM_allocalign, MEM_resize   EA's block allocator: every memory class, and through it
//                                    UMemory (operator new, new[], Alloc, the fast pools' blocks)
//   UMemory::FastAlloc, FastFree, FastFreeThunk   the 16-byte size-class lists (the freeing size padded alike)
//   Event::operator new, operator delete   the event queue's bump allocator (both ends padded alike); also
//                                    Event::Delete, which has the base class's operator delete inlined
//   SNDMEMI_alloc                    the sound library's heap (CODA_New)
// Contiguous memory (XPhysicalAlloc, MmAllocateContiguousMemory: the memory classes' own memory, textures and
// vertex buffers) is not padded: it holds data, not structures. A large FastAlloc is padded twice (in FastAlloc and
// again in MEM), which is still "larger". MEM_size and HeapSize answer the padded size, as they would for a larger
// structure.
//
// How: CodeRetarget.cpp moves every call to these functions in our module, and every stored address of them, to
// the wrappers below, which call the real ones through the table. A NULL answer from a padded allocation (a pool
// that a larger request exhausted) is printed, and so is each new 4 KB of the event queue's high-water mark.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_pad;

enum Hook {
    kHeapAlloc, kHeapReAlloc, kMemAlloc, kMemAllocz, kMemAllocAlign, kMemResize, kFastAlloc, kFastFree,
    kFastFreeThunk, kEventNew, kEventDelete, kEventDeleting, kSndAlloc, kHookCount
};

RetargetTarget g_targets[kHookCount];

template <class F>
F Real(Hook hook) {
    return reinterpret_cast<F>(g_targets[hook].from);
}

void Exhausted(const char *what, size_t size) {
    printf("[heappad] %s of %u bytes (padded) answered NULL\n", what, (unsigned)size);
}

void *WrapHeapAlloc(HostHandle heap, unsigned long flags, size_t size) {
    void *block = Real<decltype(&HostHeapAlloc)>(kHeapAlloc)(heap, flags, size + g_pad);
    if (block == NULL)
        Exhausted("HostHeapAlloc", size + g_pad);
    return block;
}

void *WrapHeapReAlloc(HostHandle heap, unsigned long flags, void *block, size_t size) {
    void *moved = Real<decltype(&HostHeapReAlloc)>(kHeapReAlloc)(heap, flags, block, size + g_pad);
    if (moved == NULL)
        Exhausted("HostHeapReAlloc", size + g_pad);
    return moved;
}

void *WrapMemAlloc(const char *label, int size, unsigned flags) {
    if (size < 0)
        return Real<decltype(&MEM_alloc)>(kMemAlloc)(label, size, flags);
    void *block = Real<decltype(&MEM_alloc)>(kMemAlloc)(label, size + g_pad, flags);
    if (block == NULL)
        Exhausted("MEM_alloc", size + g_pad);
    return block;
}

void *WrapMemAllocz(const char *label, int size, unsigned flags) {
    if (size < 0)
        return Real<decltype(&MEM_allocz)>(kMemAllocz)(label, size, flags);
    void *block = Real<decltype(&MEM_allocz)>(kMemAllocz)(label, size + g_pad, flags);
    if (block == NULL)
        Exhausted("MEM_allocz", size + g_pad);
    return block;
}

void *WrapMemAllocAlign(const char *label, int size, int alignment, int offset, unsigned flags) {
    if (size < 0)
        return Real<decltype(&MEM_allocalign)>(kMemAllocAlign)(label, size, alignment, offset, flags);
    void *block = Real<decltype(&MEM_allocalign)>(kMemAllocAlign)(label, size + g_pad, alignment, offset, flags);
    if (block == NULL)
        Exhausted("MEM_allocalign", size + g_pad);
    return block;
}

// -1 asks for everything there is, and stays -1
void *WrapMemResize(void *data, int size) {
    return Real<decltype(&MEM_resize)>(kMemResize)(data, size >= 0 ? size + g_pad : size);
}

void *WrapFastAlloc(unsigned int size, const char *name) {
    void *block = Real<decltype(&UMemory::FastAlloc)>(kFastAlloc)(size + g_pad, name);
    if (block == NULL)
        Exhausted("UMemory::FastAlloc", size + g_pad);
    return block;
}

void WrapFastFree(void *block, unsigned int size) {
    Real<decltype(&UMemory::FastFree)>(kFastFree)(block, size + g_pad);
}

// The thunk's own body may be a jump to FastFree, now FastFree's wrapper: the real FastFree is called instead
void WrapFastFreeThunk(void *block, unsigned int size) {
    Real<decltype(&UMemory::FastFree)>(kFastFree)(block, size + g_pad);
}

char *g_eventBase;              // the lowest event address seen: the queue's buffer
unsigned g_eventHighWater;      // the furthest the creation point has been from it, in bytes

void *WrapEventNew(size_t size) {
    void *event = Real<void *(*)(size_t)>(kEventNew)(size + g_pad);
    if (g_eventBase == NULL || (char *)event < g_eventBase)
        g_eventBase = (char *)event;
    unsigned used = (unsigned)(gCreationPoint - g_eventBase);
    if (used / 0x1000 > g_eventHighWater / 0x1000)
        printf("[heappad] event queue high-water 0x%x of 0x8000 bytes\n", used);
    if (used > g_eventHighWater)
        g_eventHighWater = used;
    return event;
}

void WrapEventDelete(void *event, size_t size) {
    Real<void (*)(void *, size_t)>(kEventDelete)(event, size + g_pad);
}

// Event::Delete (a __thiscall member) as __fastcall with EDX unused
Event *__fastcall WrapEventDeleting(Event *event, int, unsigned flags) {
    Event *result = Real<Event *(__fastcall *)(Event *, int, unsigned)>(kEventDeleting)(event, 0, flags);
    if (flags & 1)
        gDeletionPoint += ((sizeof(Event) + g_pad + 0xf) & ~0xf) - ((sizeof(Event) + 0xf) & ~0xf);
    return result;
}

void *WrapSndAlloc(int size) {
    void *block = Real<decltype(&SNDMEMI_alloc)>(kSndAlloc)(size + g_pad);
    if (block == NULL)
        Exhausted("SNDMEMI_alloc", size + g_pad);
    return block;
}

void Set(Hook hook, const void *from, const void *to) {
    g_targets[hook].from = (uintptr_t)from;
    g_targets[hook].to = (uintptr_t)to;
}

}  // namespace

void HeapPad_Install(void) {
    char text[32];
    DWORD n = GetEnvironmentVariableA("NIGHTFIRE_HEAP_PAD", text, sizeof(text));
    g_pad = n > 0 && n < sizeof(text) ? atoi(text) : 0;
    if (g_pad <= 0)
        return;

    Set(kHeapAlloc, (const void *)&HostHeapAlloc, (const void *)&WrapHeapAlloc);
    Set(kHeapReAlloc, (const void *)&HostHeapReAlloc, (const void *)&WrapHeapReAlloc);
    Set(kMemAlloc, (const void *)&MEM_alloc, (const void *)&WrapMemAlloc);
    Set(kMemAllocz, (const void *)&MEM_allocz, (const void *)&WrapMemAllocz);
    Set(kMemAllocAlign, (const void *)&MEM_allocalign, (const void *)&WrapMemAllocAlign);
    Set(kMemResize, (const void *)&MEM_resize, (const void *)&WrapMemResize);
    Set(kFastAlloc, (const void *)&UMemory::FastAlloc, (const void *)&WrapFastAlloc);
    Set(kFastFree, (const void *)&UMemory::FastFree, (const void *)&WrapFastFree);
    Set(kFastFreeThunk, (const void *)&UMemory::FastFreeThunk, (const void *)&WrapFastFreeThunk);
    Set(kEventNew, (const void *)static_cast<void *(*)(size_t)>(&Event::operator new), (const void *)&WrapEventNew);
    Set(kEventDelete, (const void *)static_cast<void (*)(void *, size_t)>(&Event::operator delete),
        (const void *)&WrapEventDelete);
    Event *(Event::*deleting)(unsigned) = &Event::Delete;
    const void *deletingAddress;
    memcpy(&deletingAddress, &deleting, sizeof(deletingAddress));
    Set(kEventDeleting, deletingAddress, (const void *)&WrapEventDeleting);
    Set(kSndAlloc, (const void *)&SNDMEMI_alloc, (const void *)&WrapSndAlloc);

    static const char *const kNames[kHookCount] = {
        "HostHeapAlloc", "HostHeapReAlloc", "MEM_alloc", "MEM_allocz", "MEM_allocalign", "MEM_resize",
        "UMemory::FastAlloc", "UMemory::FastFree", "UMemory::FastFreeThunk", "Event::operator new",
        "Event::operator delete", "Event::Delete", "SNDMEMI_alloc",
    };
    int moved = Retarget(g_targets, kHookCount, g_targets, sizeof(g_targets));
    printf("[heappad] every allocation padded by %d bytes: %d references moved (", g_pad, moved);
    for (int i = 0; i < kHookCount; i++)
        printf("%s%s %d", i != 0 ? ", " : "", kNames[i], g_targets[i].sites);
    printf(")\n");
}

// ---- NIGHTFIRE_PARTICLE_ORDER=1: the particle systems' map, keyed by emitter address, in the order the manager
// updates it (RParticleSystemManager::UpdateSpawnAllSystems), every tick: "[porder] <tick> <systems> <ordered hash>
// <set hash>". Two runs whose set hashes match and ordered hashes differ hold the same systems in another order.

namespace {

uint32_t SystemHash(const RParticleSystem *system) {
    uint32_t words[6];
    memcpy(words, &system->lastPosition, 12);
    words[3] = (uint32_t)system->index;
    words[4] = system->time;
    words[5] = (uint32_t)system->keepAlive;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 6; i++)
        hash = (hash ^ words[i]) * 16777619u;
    return hash;
}

}  // namespace

void HeapPad_Tick(void) {
    static int enabled = -1, tick;
    if (enabled < 0) {
        char text[8];
        enabled = GetEnvironmentVariableA("NIGHTFIRE_PARTICLE_ORDER", text, sizeof(text)) != 0 && text[0] != '0';
    }
    tick++;
    if (!enabled || fgParticleSystems == NULL || fgParticleSystems->systems == NULL)
        return;
    ParticleMap *systems = fgParticleSystems->systems;
    uint32_t ordered = 2166136261u, set = 0;
    int count = 0;
    for (TreeNode *node = systems->Begin(); node != systems->End(); node = TreeNext(node)) {
        uint32_t hash = SystemHash(node->value.system);
        ordered = (ordered ^ hash) * 16777619u;
        set += hash;
        count++;
    }
    if (count != 0)
        printf("[porder] %d %d %08x %08x\n", tick, count, ordered, set);
}
