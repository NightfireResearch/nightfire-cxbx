#include "Host.h"
#include "Ptr32.h"

// The game window on a 64-bit host (docs/driving/64-bit.md 7.1): the 4 GB of address space from kGameWindowBase (0,
// or 4 GB: Ptr32.h), where every address the game may keep in its files' data must lie, so that it fits the 32-bit
// words of Ptr32.h. (Address space - not the display window of HostWindow.h.) The executable is linked into it
// (CMakeLists.txt), so its statics are inside, which the arena's set-up checks; contiguous memory comes from
// HostAllocatePages and the game's thread stacks from HostCreateThread, both kept in the window; and the heaps are
// here: Host.h's heap functions over one arena the host reserves (HostReserveLow) and commits as it fills
// (HostCommit). The standalone executable's operator new uses a heap of its own here too, for the host-side objects
// the game's data can point at.
//
// A 32-bit host keeps the system's heaps (HostWin32.cpp, HostPosix.cpp): every address is in the window there.
//
// The allocator: blocks with a 16-byte header and boundary tags, free blocks in size-class lists (exact below 1 KB,
// four per power of two above), first fit within a list, neighbours merged on free, and the arena's untouched top
// taken from last. Every heap shares the arena and one lock; a heap with a maximum fails an allocation that would
// take its live blocks past it, as a fixed-size Win32 heap fails when full.

#if UINTPTR_MAX > 0xffffffffu

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {

constexpr size_t kArenaSize = size_t(1) << 30;
constexpr uintptr_t kArenaLowest = 0x20000000;   // an offset: above where contiguous memory and the stacks go first
constexpr size_t kCommitStep = size_t(1) << 20;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMinBlock = 32;
constexpr uint32_t kInUse = 1;
constexpr uint32_t kPrevInUse = 2;
constexpr uint32_t kFlags = 15;
constexpr int kLists = 160;
constexpr int kMaxHeaps = 64;
constexpr HostDword kZeroMemory = HOST_HEAP_ZERO_MEMORY;
constexpr HostDword kReallocInPlaceOnly = 0x10;

struct Block {
    uint32_t prevSize;    // the size of the block before, while that one is free
    uint32_t size;        // with the header, a multiple of 16; the low bits hold kInUse and kPrevInUse
    uint32_t requested;   // the size asked for, which HostHeapSize answers
    uint32_t heap;        // the heap's index
    Block *next;          // a free block's neighbours in its list
    Block *prev;

    uint32_t Size() const { return size & ~kFlags; }
    Block *After() { return reinterpret_cast<Block *>(reinterpret_cast<char *>(this) + Size()); }
    Block *Before() { return reinterpret_cast<Block *>(reinterpret_cast<char *>(this) - prevSize); }
    void *Data() { return reinterpret_cast<char *>(this) + kHeaderSize; }
};

struct Heap {
    size_t maximum;   // 0: growable
    size_t used;
};

struct Arena {
    HostLock lock;
    char *start;
    char *end;
    char *committed;
    Block *top;            // the untouched rest, from here to `end`; its header's kPrevInUse is kept
    Block *lists[kLists];
    uint64_t nonEmpty[(kLists + 63) / 64];
    Heap heaps[kMaxHeaps];
    int heapCount;
};

bool InWindow(const void *address) {
    return uintptr_t(address) - kGameWindowBase < kGameWindowEnd - kGameWindowBase;
}

Arena *TheArena() {
    static Arena *arena = [] {
        static Arena a;
        // The statics and functions the game's data names (registered variables, vtables, channel functions)
        if (!InWindow(&a) || !InWindow(reinterpret_cast<const void *>(&TheArena))) {
            printf("[window] the executable (%p) is not in the game window at 0x%llx\n", static_cast<void *>(&a),
                   static_cast<unsigned long long>(kGameWindowBase));
            fflush(stdout);
            HostExitProcess(3);
        }
        a.start = static_cast<char *>(HostReserveLow(kArenaSize, kArenaLowest));
        if (a.start == NULL) {
            printf("[window] could not reserve %zu MB in the game window for its heaps\n", kArenaSize >> 20);
            fflush(stdout);
            HostExitProcess(3);
        }
        a.end = a.start + kArenaSize;
        a.committed = a.start;
        HostInitLock(&a.lock);
        a.top = reinterpret_cast<Block *>(a.start);
        if (!HostCommit(a.start, kCommitStep)) {
            printf("[window] could not commit the game's heaps at %p\n", static_cast<void *>(a.start));
            fflush(stdout);
            HostExitProcess(3);
        }
        a.committed = a.start + kCommitStep;
        a.top->size = kPrevInUse;
        return &a;
    }();
    return arena;
}

int HighestBit(uint64_t value) {
#ifdef _MSC_VER
    unsigned long index;
    _BitScanReverse64(&index, value);
    return int(index);
#else
    return 63 - __builtin_clzll(value);
#endif
}

int LowestBit(uint64_t value) {
#ifdef _MSC_VER
    unsigned long index;
    _BitScanForward64(&index, value);
    return int(index);
#else
    return __builtin_ctzll(value);
#endif
}

int ListOf(size_t size) {
    if (size < 1024)
        return int(size / 16);
    int bit = HighestBit(size);
    return 64 + (bit - 10) * 4 + int((size >> (bit - 2)) & 3);
}

void Link(Arena *a, Block *block) {
    int list = ListOf(block->Size());
    block->prev = NULL;
    block->next = a->lists[list];
    if (block->next != NULL)
        block->next->prev = block;
    a->lists[list] = block;
    a->nonEmpty[list / 64] |= uint64_t(1) << (list % 64);
}

void Unlink(Arena *a, Block *block) {
    int list = ListOf(block->Size());
    if (block->prev != NULL)
        block->prev->next = block->next;
    else
        a->lists[list] = block->next;
    if (block->next != NULL)
        block->next->prev = block->prev;
    if (a->lists[list] == NULL)
        a->nonEmpty[list / 64] &= ~(uint64_t(1) << (list % 64));
}

// A free block of `size` bytes at `block`, merged with the free blocks either side or with the top.
void Release(Arena *a, Block *block, uint32_t size) {
    block->size = size | (block->size & kPrevInUse);
    Block *after = block->After();
    if (after != a->top && !(after->size & kInUse)) {
        Unlink(a, after);
        block->size += after->Size();
    }
    if (!(block->size & kPrevInUse)) {
        Block *before = block->Before();
        Unlink(a, before);
        before->size += block->Size();
        block = before;
    }
    after = block->After();
    if (after == a->top) {
        block->size &= kPrevInUse;
        a->top = block;
        return;
    }
    after->prevSize = block->Size();
    after->size &= ~kPrevInUse;
    Link(a, block);
}

// Cuts `block` (in use) down to `size`, freeing the rest if it is large enough to be a block.
void Trim(Arena *a, Block *block, uint32_t size) {
    uint32_t spare = block->Size() - size;
    if (spare < kMinBlock)
        return;
    block->size -= spare;
    Block *rest = block->After();
    rest->size = kPrevInUse;
    Release(a, rest, spare);
}

// Grows the top to at least `size` bytes past its header's start.
bool TopFits(Arena *a, size_t size) {
    char *needed = reinterpret_cast<char *>(a->top) + size + kHeaderSize;
    if (needed <= a->committed)
        return true;
    if (needed > a->end)
        return false;
    size_t more = (size_t(needed - a->committed) + kCommitStep - 1) & ~(kCommitStep - 1);
    if (more > size_t(a->end - a->committed))
        more = size_t(a->end - a->committed);
    if (!HostCommit(a->committed, more))
        return false;
    a->committed += more;
    return true;
}

Block *Take(Arena *a, uint32_t size) {
    int list = ListOf(size);
    for (Block *block = a->lists[list]; block != NULL; block = block->next) {
        if (block->Size() >= size) {
            Unlink(a, block);
            return block;
        }
    }
    for (int word = (list + 1) / 64; word < (kLists + 63) / 64; word++) {
        uint64_t bits = a->nonEmpty[word];
        if (word == (list + 1) / 64)
            bits &= ~uint64_t(0) << ((list + 1) % 64);
        if (bits != 0) {
            Block *block = a->lists[word * 64 + LowestBit(bits)];
            Unlink(a, block);
            return block;
        }
    }
    if (!TopFits(a, size))
        return NULL;
    Block *block = a->top;
    block->size = size | (block->size & kPrevInUse);
    a->top = block->After();
    a->top->size = kPrevInUse;
    return block;
}

// The block size for `bytes` asked for, or 0 if no block can hold it.
uint32_t BlockSize(size_t bytes) {
    if (bytes > kArenaSize)
        return 0;
    size_t size = (bytes + kHeaderSize + 15) & ~size_t(15);
    return uint32_t(size < kMinBlock ? kMinBlock : size);
}

void MarkInUse(Block *block, Heap *heap, int index, size_t requested) {
    block->size |= kInUse;
    block->requested = uint32_t(requested);
    block->heap = uint32_t(index);
    block->After()->size |= kPrevInUse;
    heap->used += block->Size();
}

void *Allocate(Arena *a, Heap *heap, HostDword flags, size_t bytes) {
    uint32_t size = BlockSize(bytes);
    if (size == 0 || (heap->maximum != 0 && heap->used + size > heap->maximum))
        return NULL;
    Block *block = Take(a, size);
    if (block == NULL)
        return NULL;
    Trim(a, block, size);
    MarkInUse(block, heap, int(heap - a->heaps), bytes);
    if (flags & kZeroMemory)
        memset(block->Data(), 0, bytes);
    return block->Data();
}

void Free(Arena *a, Block *block) {
    a->heaps[block->heap].used -= block->Size();
    block->size &= ~kInUse;
    Release(a, block, block->Size());
}

Block *BlockOf(const void *data) {
    return reinterpret_cast<Block *>(static_cast<char *>(const_cast<void *>(data)) - kHeaderSize);
}

} // namespace

HostHandle HostHeapCreate(HostDword flags, size_t initialSize, size_t maximumSize) {
    (void)flags;
    (void)initialSize;
    Arena *a = TheArena();
    HostEnterLock(&a->lock);
    Heap *heap = NULL;
    if (a->heapCount < kMaxHeaps) {
        heap = &a->heaps[a->heapCount++];
        heap->maximum = maximumSize;
        heap->used = 0;
    }
    HostLeaveLock(&a->lock);
    return heap;
}

void *HostHeapAlloc(HostHandle heap, HostDword flags, size_t size) {
    Arena *a = TheArena();
    HostEnterLock(&a->lock);
    void *data = Allocate(a, static_cast<Heap *>(heap), flags, size);
    HostLeaveLock(&a->lock);
    return data;
}

int HostHeapFree(HostHandle heap, HostDword flags, void *data) {
    (void)heap;
    (void)flags;
    if (data == NULL)
        return 1;
    Arena *a = TheArena();
    HostEnterLock(&a->lock);
    Free(a, BlockOf(data));
    HostLeaveLock(&a->lock);
    return 1;
}

// In place where the block shrinks or the space after it is free; otherwise moved, unless
// HEAP_REALLOC_IN_PLACE_ONLY. HEAP_ZERO_MEMORY zeroes what the block gains.
void *HostHeapReAlloc(HostHandle heap, HostDword flags, void *data, size_t size) {
    if (data == NULL)
        return HostHeapAlloc(heap, flags, size);
    Arena *a = TheArena();
    HostEnterLock(&a->lock);
    Block *block = BlockOf(data);
    Heap *owner = &a->heaps[block->heap];
    size_t old = block->requested;
    uint32_t want = BlockSize(size);
    void *result = NULL;
    if (want != 0 && (owner->maximum == 0 || owner->used - block->Size() + want <= owner->maximum)) {
        owner->used -= block->Size();
        Block *after = block->After();
        if (want > block->Size() && after == a->top) {
            if (TopFits(a, want - block->Size())) {
                block->size += want - block->Size();
                a->top = block->After();
                a->top->size = kPrevInUse;
            }
        } else if (want > block->Size() && !(after->size & kInUse) && block->Size() + after->Size() >= want) {
            Unlink(a, after);
            block->size += after->Size();
            block->After()->size |= kPrevInUse;
        }
        if (block->Size() >= want) {
            Trim(a, block, want);
            block->requested = uint32_t(size);
            owner->used += block->Size();
            result = data;
        } else {
            owner->used += block->Size();
            if (!(flags & kReallocInPlaceOnly)) {
                result = Allocate(a, owner, flags & ~kZeroMemory, size);
                if (result != NULL) {
                    memcpy(result, data, old < size ? old : size);
                    Free(a, block);
                }
            }
        }
    }
    HostLeaveLock(&a->lock);
    if (result != NULL && (flags & kZeroMemory) && size > old)
        memset(static_cast<char *>(result) + old, 0, size - old);
    return result;
}

size_t HostHeapSize(HostHandle heap, HostDword flags, const void *data) {
    (void)heap;
    (void)flags;
    return BlockOf(data)->requested;
}

// The executable's own C++ allocations (the seams' objects, the host side's containers) in the window as well. (An
// ELF executable's serve the shared libraries it loads too: what the C++ library allocates, the executable's inline
// code may free.)
static HostHandle OperatorNewHeap() {
    static HostHandle heap = HostHeapCreate(0, 0, 0);
    return heap;
}

void *operator new(size_t size) {
    void *data = HostHeapAlloc(OperatorNewHeap(), 0, size != 0 ? size : 1);
    if (data == NULL) {
        printf("[window] operator new: %zu bytes refused\n", size);
        fflush(stdout);
        HostExitProcess(3);
    }
    return data;
}

void *operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void *data) noexcept {
    HostHeapFree(OperatorNewHeap(), 0, data);
}

void operator delete[](void *data) noexcept {
    HostHeapFree(OperatorNewHeap(), 0, data);
}

void operator delete(void *data, size_t) noexcept {
    HostHeapFree(OperatorNewHeap(), 0, data);
}

void operator delete[](void *data, size_t) noexcept {
    HostHeapFree(OperatorNewHeap(), 0, data);
}

void GameWindowOutOfRange(const void *pointer) {
    printf("[window] %p is outside the game window: it cannot be stored in the game's data\n", pointer);
    fflush(stdout);
    HostDebugBreak();
    abort();
}

#endif
