#include "RealMemory.h"

#include "RealSystem.h"   // MUTEX_lock, MUTEX_unlock, MUTEX_create, REALMUTEX_destroy
#include "RealPrint.h"    // MEM_fill, MEM_move

#include <windows.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's block allocator (the MEM_ functions of its portable library), as the driving engine has it. Ported from the
// originals instruction by instruction where it matters - the game and its tools read block headers directly
// (MEM_size, MEM_type), and a heap that drifts from the original's layout corrupts everything else - and with the
// six register-argument helpers made ordinary static functions, now that every caller of each is here.
//
// A memory class is a 0x60-byte record (memclass[n], 64 of them) living in the data of its own low sentinel block,
// at the start of the memory it manages. Every block has a 16-byte header before its data:
//     +0  magic: 'BM' a used block, 'BF' free, 'BS' a sentinel, 'BO' a header left behind by a merge
//     +2  flags: the class number in bits 0-5; 0x100 the high sentinel, or "allocate from the top" in a request;
//         0x200 "best fit", 0x400 "the class's other alignment" in a request; 0x1000 and 0x2000 the class's own
//         options (0x2000: a "BEND" marker after the data, checked by MEM_validate); 0x4000 on the free list;
//         0x8000 a sentinel
//     +4  size: the size asked for (a used block), or the span to the next block (a free one)
//     +8  the next block up;  +0xc the next block down
//     +0x10, +0x14  a free block's next and previous on the free list (address order)
// The class record: +0 its name (as far as it fits), +8 the low sentinel, +0xc the high one, +0x10 the free list's
// own sentinel (a block header: +0x20 the first free block, +0x24 the last, size 0x7fffffff), +0x28 the alignment,
// +0x2c the other alignment, +0x30 the tail size (bytes kept after every block's data), +0x34 the class's flags,
// +0x38 whether it has a lock, +0x3c the lock (a MUTEX).
// ---------------------------------------------------------------------------------------------------------------

#define MemClass         ((uint8_t **)0x00242ce0u)   // 64 class records
#define MemError         (*(int *)0x00242cd4u)       // what MEM_validate last found wrong
#define MemOutOfMemory   (*(MemOutOfMemoryFn *)0x00242cd8u)
#define MemDefaultClass  (*(unsigned *)0x00242cdcu)

enum : uint16_t { MAGIC_USED = 0x424d, MAGIC_FREE = 0x4246, MAGIC_SENTINEL = 0x4253, MAGIC_GONE = 0x424f };

// A block header, by its fields
#define B_MAGIC(b)     (*(uint16_t *)((uint8_t *)(b) + 0x0))
#define B_FLAGS(b)     (*(uint16_t *)((uint8_t *)(b) + 0x2))
#define B_FLAGBYTE(b)  (*(uint8_t *)((uint8_t *)(b) + 0x3))
#define B_SIZE(b)      (*(int32_t *)((uint8_t *)(b) + 0x4))
#define B_NEXT(b)      (*(uint8_t **)((uint8_t *)(b) + 0x8))
#define B_PREV(b)      (*(uint8_t **)((uint8_t *)(b) + 0xc))
#define B_NEXTFREE(b)  (*(uint8_t **)((uint8_t *)(b) + 0x10))
#define B_PREVFREE(b)  (*(uint8_t **)((uint8_t *)(b) + 0x14))

// A class record, by its fields
#define C_FIRST(c)     (*(uint8_t **)((c) + 0x08))
#define C_FREELIST(c)  ((c) + 0x10)
#define C_ALIGN(c)     (*(int32_t *)((c) + 0x28))
#define C_ALTALIGN(c)  (*(int32_t *)((c) + 0x2c))
#define C_TAIL(c)      (*(int32_t *)((c) + 0x30))
#define C_LOCKED(c)    (*(uint8_t *)((c) + 0x38))
#define C_MUTEX(c)     ((RealMutex *)((c) + 0x3c))

static void Lock(uint8_t *cls) {
    if (C_LOCKED(cls) != 0)
        MUTEX_lock(C_MUTEX(cls));
}

static void Unlock(uint8_t *cls) {
    if (C_LOCKED(cls) != 0)
        MUTEX_unlock(C_MUTEX(cls));
}

static void UnlinkFree(uint8_t *block) {
    uint8_t *previous = B_PREVFREE(block), *next = B_NEXTFREE(block);
    B_NEXTFREE(previous) = next;
    B_PREVFREE(next) = previous;
}

// 0x00113d80 (class in EAX, block in ECX): onto the address-ordered free list, scanning from whichever end of it the
// block is nearer to (the midpoint of the first and last free blocks, halved as a signed difference), its span
// recomputed, and marked free.
static void InsertFree(uint8_t *cls, uint8_t *block) {
    uint8_t *span = B_NEXT(block);
    uint8_t *sentinel = C_FREELIST(cls);
    uint8_t *first = B_NEXTFREE(sentinel), *last = B_PREVFREE(sentinel);
    uint8_t *middle = first + ((int32_t)(last - first)) / 2;
    uint8_t *after, *before;
    if ((uintptr_t)block > (uintptr_t)middle) {
        before = sentinel;
        do {
            before = B_PREVFREE(before);
        } while ((uintptr_t)block < (uintptr_t)before);
        after = B_NEXTFREE(before);
    } else {
        after = sentinel;
        do {
            after = B_NEXTFREE(after);
        } while ((uintptr_t)block > (uintptr_t)after);
        before = B_PREVFREE(after);
    }
    B_NEXTFREE(block) = after;
    B_PREVFREE(block) = before;
    B_SIZE(block) = (int32_t)(span - block);
    B_NEXTFREE(before) = block;
    B_PREVFREE(after) = block;
    B_FLAGBYTE(block) |= 0x40;
    B_MAGIC(block) = MAGIC_FREE;
}

// AUTOINJECT
int MEM_initblock(void *block, const char *name, int size, int tail, unsigned flags, void *previous, void *next) {
    (void)name; (void)tail;
    B_FLAGS(block) = (uint16_t)flags;
    B_NEXT(block) = (uint8_t *)next;
    B_MAGIC(block) = MAGIC_USED;
    B_SIZE(block) = size;
    B_PREV(block) = (uint8_t *)previous;
    return size + 0x10;
}

// AUTOINJECT
int MEM_tailsize(const char *name, int flags) {
    (void)name;
    return C_TAIL(MemClass[flags & 0x3f]);
}

// AUTOINJECT
size_t MEM_size(void *data) {
    return *(size_t *)((uint8_t *)data - 0xc);
}

// AUTOINJECT
unsigned short MEM_type(void *data) {
    return *(uint16_t *)((uint8_t *)data - 0xe);
}

// 0x00114040 (class in ECX, size in EDX, direction on the stack): the "best fit" search, which in fact keeps the
// largest block that fits, scanning from the bottom or (top != 0) the top of the free list.
static uint8_t *LargestFit(uint8_t *cls, int32_t total, int top) {
    uint8_t *candidate = C_FREELIST(cls), *best = NULL;
    int32_t threshold = total - 1;
    if (threshold < 0)
        threshold = 0;
    for (;;) {
        candidate = top == 0 ? B_NEXTFREE(candidate) : B_PREVFREE(candidate);
        int32_t usable = B_SIZE(candidate) - 0x10;
        if (usable <= threshold)
            continue;
        if (B_MAGIC(candidate) == MAGIC_SENTINEL)
            return best;
        best = candidate;
        threshold = usable;
    }
}

// The allocator itself (0x001140c0, called by MEM_alloc, MEM_allocz and MEM_allocalign). size bytes, aligned to
// `alignment` (or the class's, or its other alignment with 0x400) at `offset` into the data; from the bottom of the
// class, or from the top with 0x100; first fit, or the largest fit with 0x200. A free block big enough to leave a
// useful remainder (more than the class's alignment + 0x2f) is split; otherwise the whole of it is taken. With no
// block big enough, the out-of-memory callback is asked, and returning 1 means "try again".
static void *Allocate(const char *label, int size, int alignment, int offset, unsigned flags) {
    (void)label;
    uint8_t *result = NULL;
    if (flags == 0)
        flags = MemDefaultClass;
    uint8_t *cls = MemClass[flags & 0x3f];
    if (size < 0)
        return NULL;
    int best = flags & 0x200, top = flags & 0x100;

    int retry;
    do {
        retry = 0;
        Lock(cls);
        int32_t classMask = C_ALIGN(cls) - 1;
        int32_t mask;
        if (alignment != 0)
            mask = alignment - 1;
        else
            mask = ((flags & 0x400) != 0 ? C_ALTALIGN(cls) : C_ALIGN(cls)) - 1;
        int32_t need = C_TAIL(MemClass[flags & 0x3f]) + size;
        if (need < 8)
            need = 8;
        int32_t total = need + mask + 0x10;

        uint8_t *block;
        if (best != 0) {
            block = LargestFit(cls, total, top);
        } else {
            block = C_FREELIST(cls);
            do {
                block = top == 0 ? B_NEXTFREE(block) : B_PREVFREE(block);
            } while (total > B_SIZE(block));
            if (B_MAGIC(block) == MAGIC_SENTINEL)
                block = NULL;
        }

        if (block == NULL) {
            if (MemOutOfMemory != NULL)
                retry = MemOutOfMemory(0, size, flags);
        } else {
            int32_t span = B_SIZE(block);
            UnlinkFree(block);
            uint8_t *below = B_PREV(block);
            B_FLAGBYTE(block) &= 0xbf;
            uint8_t *above = B_NEXT(block);
            B_MAGIC(block) = MAGIC_GONE;

            if (top != 0)
                result = (uint8_t *)((((uintptr_t)block + span - need + offset) & ~(uintptr_t)mask) - offset);
            else
                result = (uint8_t *)((((uintptr_t)block + mask + offset + 0x10) & ~(uintptr_t)mask) - offset);
            uint8_t *header = result - 0x10;
            B_PREV(header) = below;
            B_NEXT(header) = above;

            if (span - total > classMask + 0x30) {
                if (top != 0) {
                    // The remainder stays below, as the free block it was
                    B_PREV(B_NEXT(block)) = header;
                    B_NEXT(header) = B_NEXT(block);
                    B_PREV(header) = block;
                    MEM_initblock(block, NULL, 0, 0, 0, B_PREV(block), header);
                    InsertFree(cls, block);
                } else {
                    // The remainder goes above, from the first aligned address after the data
                    uint8_t *rest = (uint8_t *)(((uintptr_t)result + need + classMask) & ~(uintptr_t)classMask);
                    B_PREV(above) = rest;
                    B_NEXT(B_PREV(header)) = header;
                    MEM_initblock(rest, NULL, 0, 0, 0, header, B_NEXT(header));
                    InsertFree(cls, rest);
                    B_NEXT(header) = rest;
                }
            } else {
                B_PREV(above) = header;
                B_NEXT(B_PREV(header)) = header;
            }
            MEM_initblock(header, NULL, size, C_TAIL(cls), flags, B_PREV(header), B_NEXT(header));
        }
        Unlock(cls);
    } while (retry == 1);
    return result;
}

// AUTOINJECT
void* MEM_allocalign(const char *label, int size, int alignment, int offset, unsigned flags) {
    return Allocate(label, size, alignment, offset, flags);
}

// AUTOINJECT
void* MEM_alloc(const char *label, int size, unsigned flags) {
    return Allocate(label, size, 0, 0, flags);
}

// Despite the name, nothing is cleared: the original is MEM_alloc again.
// AUTOINJECT
void* MEM_allocz(const char *label, int size, unsigned flags) {
    return Allocate(label, size, 0, 0, flags);
}

// Frees a block: merged with a free neighbour on either side, and then - when the block below is in use - moved
// down to the first aligned address after that block's data and tail, so the slack between them is free too.
static bool Free(void *data) {
    if (data == NULL)
        return true;
    uint8_t *block = (uint8_t *)data - 0x10;
    uint8_t *cls = MemClass[*((uint8_t *)data - 0xe) & 0x3f];
    Lock(cls);

    uint8_t *below = B_PREV(block);
    uint8_t **belowLink = &B_PREV(block);
    uint8_t *above = B_NEXT(block);
    if ((B_FLAGS(below) & 0x4000) != 0) {
        UnlinkFree(below);
        B_FLAGS(below) &= 0xbfff;
        block = below;
        B_MAGIC(below) = MAGIC_GONE;
        belowLink = &B_PREV(below);
        below = *belowLink;
        B_NEXT(block) = above;
        B_NEXT(below) = block;
        B_PREV(above) = block;
    }
    if ((B_FLAGS(above) & 0x4000) != 0) {
        UnlinkFree(above);
        B_FLAGS(above) &= 0xbfff;
        B_MAGIC(above) = MAGIC_GONE;
        above = B_NEXT(above);
        *belowLink = below;
        B_NEXT(block) = above;
        B_PREV(above) = block;
    }
    if ((B_FLAGS(below) & 0xc000) == 0) {
        int32_t used = MEM_tailsize(NULL, B_FLAGS(below)) + B_SIZE(below);
        if (used < 8)
            used = 8;
        uint8_t *lowest = below + ((C_ALIGN(cls) + used + 0xf) & ~(C_ALIGN(cls) - 1));
        if (block != lowest) {
            B_FLAGS(lowest) = B_FLAGS(block);
            B_NEXT(lowest) = above;
            B_PREV(lowest) = below;
            B_NEXT(below) = lowest;
            block = lowest;
            B_PREV(above) = lowest;
        }
    }
    InsertFree(cls, block);
    Unlock(cls);
    return true;
}

// The two copies of MEM_free in the XBE (0x00113f20 and 0x00117110) are the same code.
// FUNC_AT(0x00113f20)
bool MEM_free(void *data) {
    return Free(data);
}

// FUNC_AT(0x00117110)
bool MEM_free_copy(void *data) {
    return Free(data);
}

// Grows or shrinks a block in place, as far as the space up to the next used block allows: a free block above is
// absorbed first, the tail is moved to the new end, and anything over 0x300 bytes left above becomes a free block
// again. -1 asks for everything there is. Returns the block, which never moves.
// AUTOINJECT
void* MEM_resize(void *data, int size) {
    uint8_t *block = (uint8_t *)data - 0x10;
    uint16_t flags = B_FLAGS(block);
    uint8_t *cls = MemClass[flags & 0x3f];
    Lock(cls);

    uint8_t *above = B_NEXT(block);
    if ((B_FLAGBYTE(above) & 0x40) != 0) {
        UnlinkFree(above);
        B_FLAGBYTE(above) &= 0xbf;
        B_MAGIC(above) = MAGIC_GONE;
        above = B_NEXT(above);
        B_NEXT(block) = above;
        B_PREV(above) = block;
    }

    int32_t wanted = size;
    if (size < 8) {
        if (size == -1)
            wanted = 0x40000000;
        else if (size >= 0)
            wanted = 8;
    }
    int32_t tail = MEM_tailsize(NULL, flags);
    int32_t mask = C_ALIGN(cls) - 1;
    int32_t room = (int32_t)(above - block) - 0x10;
    if (tail + wanted > room) {
        wanted = room - tail;
        size = room - tail;
    }
    MEM_move((uint8_t *)data + wanted, (uint8_t *)data + B_SIZE(block), tail);
    B_SIZE(block) = size;

    uint8_t *rest = (uint8_t *)(((uintptr_t)data + tail + mask + wanted) & ~(uintptr_t)mask);
    if ((int32_t)((above - rest) & ~0xf) > 0x300) {
        MEM_initblock(rest, NULL, 0, 0, 0, block, above);
        InsertFree(cls, rest);
        B_PREV(above) = rest;
        B_NEXT(block) = rest;
    }
    Unlock(cls);
    return data;
}

// The full size field of the largest free block in the class (0 for none, or the default class).
// AUTOINJECT
int MEM_largestunused(unsigned flags) {
    if (flags == 0)
        flags = MemDefaultClass;
    uint8_t *candidate = C_FREELIST(MemClass[flags & 0x3f]), *best = NULL;
    int32_t threshold = 0;
    for (;;) {
        candidate = B_NEXTFREE(candidate);
        int32_t usable = B_SIZE(candidate) - 0x10;
        if (usable <= threshold)
            continue;
        if (B_MAGIC(candidate) == MAGIC_SENTINEL)
            break;
        best = candidate;
        threshold = usable;
    }
    return best == NULL ? 0 : B_SIZE(best);
}

// AUTOINJECT
int MEM_totalunused(unsigned flags) {
    if (flags == 0)
        flags = MemDefaultClass;
    uint8_t *cls = MemClass[flags & 0x3f];
    int total = 0;
    for (uint8_t *block = B_NEXTFREE(C_FREELIST(cls)); B_MAGIC(block) != MAGIC_SENTINEL; block = B_NEXTFREE(block))
        total += B_SIZE(block) - 0x10;
    return total;
}

// ---- validation (MEM_validate runs every game loop, from RunTheGame)

// 0x00113b10 (the pointer in ESI): readable, and a free block or a sentinel.
static bool FreeLinkOk(uint8_t *link) {
    if (!IsBadReadPtr(link, 0x18) && (B_MAGIC(link) == MAGIC_FREE || B_MAGIC(link) == MAGIC_SENTINEL))
        return true;
    MemError = 1;
    return false;
}

// AUTOINJECT
char checksentinel(void *pointer) {
    uint8_t *block = (uint8_t *)pointer;
    uint16_t magic = B_MAGIC(block);
    if (magic == MAGIC_SENTINEL) {
        if ((B_FLAGBYTE(block) & 0x80) != 0)
            return 1;
        MemError = 2;
        return 0;
    }
    if (magic == MAGIC_FREE) {
        if ((B_FLAGBYTE(block) & 0x40) == 0)
            return 0;
        if (!FreeLinkOk(B_NEXTFREE(block)))
            return 0;
        return FreeLinkOk(B_PREVFREE(block)) ? 1 : 0;
    }
    if (magic == MAGIC_USED) {
        if ((B_FLAGBYTE(block) & 0x20) != 0) {
            const uint8_t *end = block + B_SIZE(block) + 0x10;
            uint32_t marker = ((uint32_t)end[0] << 24) | ((uint32_t)end[1] << 16) | ((uint32_t)end[2] << 8) | end[3];
            if (marker != 0x42454e44u) {   // "BEND"
                MemError = 3;
                return 0;
            }
        }
        return 1;
    }
    MemError = 4;
    return 0;
}

// 0x00113c00 (block in ECX): its neighbours point back at it - on the free list for a free block, in address order
// for a used one.
static bool LinksOk(uint8_t *block) {
    uint16_t magic = B_MAGIC(block);
    if (magic == MAGIC_SENTINEL) {
        if ((B_FLAGBYTE(block) & 0x80) != 0)
            return true;
        MemError = 2;
        return false;
    }
    if (magic == MAGIC_FREE) {
        if ((B_FLAGBYTE(block) & 0x40) == 0)
            return false;
        if (B_PREVFREE(B_NEXTFREE(block)) == block && B_NEXTFREE(B_PREVFREE(block)) == block)
            return true;
    } else if (magic == MAGIC_USED) {
        if (B_PREV(B_NEXT(block)) == block && B_NEXT(B_PREV(block)) == block)
            return true;
    } else {
        MemError = 4;
        return false;
    }
    MemError = 5;
    return false;
}

// 0x00113c70 (the free list's sentinel in ECX): round the free list one way to the sentinel and back the other.
static bool FreeListOk(uint8_t *sentinel) {
    uint8_t *block = sentinel;
    do {
        block = B_NEXTFREE(block);
    } while (B_MAGIC(block) == MAGIC_FREE && (B_FLAGS(block) & 0x4000) != 0);
    if (B_MAGIC(block) == MAGIC_SENTINEL) {
        do {
            block = B_PREVFREE(block);
        } while (B_MAGIC(block) == MAGIC_FREE && (B_FLAGS(block) & 0x4000) != 0);
        if (block == sentinel)
            return true;
    }
    MemError = 6;
    return false;
}

// 0x00113cd0 (the class number in EAX): every block in address order, then the free list.
static bool ClassOk(unsigned number) {
    uint8_t *cls = MemClass[number & 0x3f];
    Lock(cls);
    uint8_t *block = C_FIRST(cls);
    bool ok;
    for (;;) {
        ok = checksentinel(block) != 0 && LinksOk(block);
        if (ok)
            block = B_NEXT(block);
        if (block == NULL || !ok)
            break;
    }
    if (ok && block == NULL)
        ok = FreeListOk(C_FREELIST(cls));
    Unlock(cls);
    return ok;
}

// AUTOINJECT
bool MEM_validate() {
    bool ok = true;
    for (int i = 0; i < 64 && ok; i++) {
        if (MemClass[i] != NULL)
            ok = ClassOk((unsigned)i);
    }
    return ok;
}

// ---- classes (names invented: Ghidra has none for these three, in either build)

// 0x001500f0: makes class `number` over `size` bytes at `base`. Three blocks: a low sentinel whose data is the
// class record, one free block over everything between, and a high sentinel `tail` + 0x30 bytes from the end.
// The two booleans become the class's 0x2000 ("BEND" markers) and 0x1000 flags; `locked` gives it a mutex.
// Returns the free block's size.
// FUNC_AT(0x001500f0)
int MEMCLASS_init(unsigned number, const char *name, void *base, int size, int alignment, int otherAlignment,
                  int tail, char endMarkers, char option1000, char locked) {
    unsigned flags = number;
    if (endMarkers != 0)
        flags |= 0x2000;
    if (option1000 != 0)
        flags |= 0x1000;
    uint8_t *low = (uint8_t *)base;
    // The first free block is placed at the other alignment (parameter 6), as the original does
    uint8_t *firstFree =
        (uint8_t *)(((uintptr_t)low + otherAlignment + tail + 0x9f) & ~(uintptr_t)(otherAlignment - 1)) - 0x10;
    uint8_t *high = low + size - 0x30 - tail;
    uint8_t *cls = low + 0x10;

    MEM_initblock(low, name, 0x60, tail, flags | 0x8000, NULL, firstFree);
    MEM_initblock(firstFree, NULL, (int32_t)(high - firstFree) - 0x10, tail, flags, low, high);
    MEM_initblock(high, name, 0, tail, flags | 0x8100, firstFree, NULL);
    MemClass[number & 0x3f] = cls;
    MEM_fill(cls, 0, 0x60);
    for (int i = 0;; i++) {   // the name, over the start of the record (anything past 7 characters is overwritten)
        cls[i] = (uint8_t)name[i];
        if (name[i] == '\0')
            break;
    }
    C_FIRST(cls) = low;
    *(uint8_t **)(cls + 0xc) = high;
    B_MAGIC(low) = MAGIC_SENTINEL;
    B_MAGIC(high) = MAGIC_SENTINEL;
    uint8_t *sentinel = C_FREELIST(cls);
    B_MAGIC(sentinel) = MAGIC_SENTINEL;
    B_NEXTFREE(sentinel) = sentinel;
    B_PREVFREE(sentinel) = sentinel;
    C_ALIGN(cls) = alignment;
    C_ALTALIGN(cls) = otherAlignment;
    B_SIZE(sentinel) = 0x7fffffff;
    C_TAIL(cls) = tail;
    *(uint32_t *)(cls + 0x34) = flags;
    C_LOCKED(cls) = 0;
    InsertFree(cls, firstFree);
    if (locked != 0) {
        MUTEX_create(C_MUTEX(cls));
        C_LOCKED(cls) = 1;
    }
    return B_SIZE(firstFree);
}

// 0x001502b0: forgets class `number` (its memory is the caller's).
// FUNC_AT(0x001502b0)
int MEMCLASS_restore(unsigned number) {
    uint8_t *cls = MemClass[number & 0x3f];
    if (cls == NULL)
        return 0;
    if (C_LOCKED(cls) != 0)
        REALMUTEX_destroy(C_MUTEX(cls));
    MEM_fill(cls, 0, 0x60);
    MemClass[number & 0x3f] = NULL;
    return 1;
}

// 0x00150300: the first unused class number from 2 up, looking at pairs (an even one free, or its odd partner).
// FUNC_AT(0x00150300)
int MEMCLASS_findfree() {
    int number = 2;
    for (;;) {
        if (MemClass[number] == NULL)
            return number;
        if (MemClass[number + 1] == NULL)
            return number + 1;
        number += 2;
        if (number > 0x3f)
            return number;
    }
}
