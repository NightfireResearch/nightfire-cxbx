#include <string.h>
#include <stdio.h>
#include "actionhelpers.h"
#include "memory.h"

// XBE_GLOBAL(0x00223a60, 0x1c)
uint32_t MemStats[7];

// The game's one heap: a single block of HeapByteSize bytes at PtrHeap, carved into blocks that each start with a
// MemBlock header. Mem_Init makes it one free block; ResetMap_Load calls Mem_Init again at every level load, which
// is how the game frees almost everything - most allocations are never passed to Mem_Free.
// XBE_GLOBAL(0x00223a80, 0x4)
uint32_t PtrHeap;     // not static: devtools/MemShadow.cpp points these at a scratch heap
// XBE_GLOBAL(0x00223a84, 0x4)
static uint32_t PtrHeapEnd;
// XBE_GLOBAL(0x00223a88, 0x4)
uint32_t HeapByteSize;
// How Mem_Malloc picks a free block (Mem_SetMallocMethod; Mem_Init sets MALLOC_BEST_FIT)
// XBE_GLOBAL(0x00223a7c, 0x4)
uint32_t MallocMethod;
// Where MALLOC_NEXT_FIT starts looking: the block it last allocated from, or one Mem_Free made
// XBE_GLOBAL(0x002237d8, 0x4)
uint32_t QuickBlock;
// Reported by Mem_Info as the bytes allocated; nothing ever adds to it
// XBE_GLOBAL(0x00223a8c, 0x4)
static uint32_t MallocSize;

// A heap block's header. The block's data follows it; the next block follows the data, at size bytes from here.
#pragma pack(push, 1)
typedef struct MemBlock {
    struct MemBlock *prev;  // 0x0 - the previous block; the first block's is itself
    uint size;              // 0x4 - including this header
    ushort flags;           // 0x8 - the MallocFlags it was allocated with (low byte the alignment, high byte the
                            //       type); 4 once free
    uchar slack;            // 0xa - bytes at the end of the block past what was asked for (Mem_Shrink uses it)
    uchar isFree;           // 0xb
} MemBlock;
#pragma pack(pop)
static_assert(sizeof(MemBlock) == 0xc, "MemBlock is 0xc bytes");

#define MEM_FREE_FLAGS 4

// tMallocMethod in Ghidra (Malloc_Type_0..3). ResetMap_Load and the loaders switch between them.
enum {
    MALLOC_NEXT_FIT = 0,    // the first big enough block from QuickBlock back to the start, else on to the end
    MALLOC_BEST_FIT = 1,    // the smallest big enough block (also any value above 3)
    MALLOC_LOW_END = 2,     // the first big enough block, allocated from its low end instead of its high end
    MALLOC_LAST_FIT = 3,    // the last big enough block
};

// The end of the walk over the blocks: every walk stops at the last header-sized slot of the heap
static inline MemBlock* Mem_HeapStart(void) { return (MemBlock *)(uintptr_t)PtrHeap; }
static inline MemBlock* Mem_HeapEnd(void) { return (MemBlock *)(uintptr_t)(PtrHeap + HeapByteSize - sizeof(MemBlock)); }
static inline MemBlock* Mem_NextBlock(MemBlock *block) { return (MemBlock *)((char *)block + block->size); }
static inline uint Mem_Distance(const void *from, const void *to) { return (uint)((const char *)to - (const char *)from); }
static inline MemBlock* Mem_AlignDown(uintptr_t address, uint mask) { return (MemBlock *)(address & ~(uintptr_t)mask); }

// AUTOGEN
void* allocateAligned0x1000(int a);

// 49MB of heap allocation from the Xbox kernel, then using an internal allocator
// This is very similar to what Halo does
#define HEAP_SIZE (40 * 1024 * 1024) // Reduced from 49MB to 40MB to free up video memory for resolution increase.

// Only called from Mem_Init, no need to inject
void psiMem_Init(uint *pMem_out, uint *size_out) {

    *size_out = HEAP_SIZE;
    void *mem = allocateAligned0x1000(HEAP_SIZE + 0x1000);

    NF_ASSERT(mem != NULL, "Could not allocate heap memory");

    // Align to 4KB boundary
    *pMem_out = (int)mem + 0xfffU & 0xfffff000;

}

// AUTOINJECT
void Mem_Init(void) {

    printf("Mem_Init\n");

    memset(&MemStats, 0, sizeof(MemStats));

    MallocMethod = MALLOC_BEST_FIT;

    if (PtrHeap == 0) {
        printf("No heap yet, getting config...\n");
        psiMem_Init(&PtrHeap, &HeapByteSize);
    }

    printf("Mem_Init: Heap: 0x%08x, size: 0x%08x\n", PtrHeap, HeapByteSize);

    PtrHeapEnd = (HeapByteSize + PtrHeap);

    memset((void*)(uintptr_t)PtrHeap, 0x98, HeapByteSize);

    MemBlock *heap = Mem_HeapStart();
    heap->size = HeapByteSize - sizeof(MemBlock);
    heap->isFree = 1;
    heap->prev = heap;
    heap->flags = MEM_FREE_FLAGS;
    QuickBlock = PtrHeap;
    MallocSize = 0;

}

// Reports the heap when an allocation fails. The original's printing is compiled out: it fills in MemInfo (Mem_Info)
// and then only works out the free blocks' sizes, largest first, for output that is no longer there.
// AUTOINJECT
void Mem_PrintAllInfo(void) {
    Mem_Info();
}

// Chooses the free block Mem_Malloc will carve from: one of at least `need` bytes, by the current MallocMethod.
static MemBlock* Mem_FindFreeBlock(uint need) {

    MemBlock *end = Mem_HeapEnd();
    MemBlock *found = NULL;

    switch (MallocMethod) {
    case MALLOC_NEXT_FIT: {
        MemBlock *start = (MemBlock *)(uintptr_t)QuickBlock;
        if (start->isFree && start->size >= need)
            return start; // QuickBlock stays as it is

        // Back from QuickBlock to the first block (the one that is its own prev), then forward from QuickBlock
        for (MemBlock *block = start; ; block = block->prev) {
            if (block->isFree && block->size >= need) {
                found = block;
                break;
            }
            if (block->prev == block)
                break;
        }
        if (found == NULL) {
            for (MemBlock *block = start; block < end; block = Mem_NextBlock(block)) {
                if (block->isFree && block->size >= need) {
                    found = block;
                    break;
                }
            }
        }
        QuickBlock = (uint32_t)(uintptr_t)found; // NULL too, when nothing fitted
        return found;
    }

    case MALLOC_LOW_END:
        for (MemBlock *block = Mem_HeapStart(); block < end; block = Mem_NextBlock(block))
            if (block->isFree && block->size >= need)
                return block;
        return NULL;

    case MALLOC_LAST_FIT:
        for (MemBlock *block = Mem_HeapStart(); block < end; block = Mem_NextBlock(block))
            if (block->isFree && block->size >= need)
                found = block;
        return found;

    default: {
        uint bestSize = HeapByteSize;
        for (MemBlock *block = Mem_HeapStart(); block < end; block = Mem_NextBlock(block)) {
            if (block->isFree && block->size >= need && block->size < bestSize) {
                found = block;
                bestSize = block->size;
            }
        }
        return found;
    }
    }
}

// Allocates `size` bytes from the heap, aligned to `alignment` (the low byte of the flags when 0; at least 4). The
// flags' high byte is the allocation type, which only Mem_Info reads. Returns NULL when nothing fits.
// AUTOINJECT
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t alignment) {

    if (alignment == 0)
        alignment = (uint32_t)flags & 0xff;
    uint mask = (alignment - 1) | 3;
    // Room for the header, the alignment either side and a free block's header after it
    uint need = size + mask * 2 + 0x18;

    MemBlock *block = Mem_FindFreeBlock(need);
    if (block == NULL) {
        Mem_PrintAllInfo();
        return NULL;
    }

    MemBlock *end = Mem_HeapEnd();
    MemBlock *next = Mem_NextBlock(block);

    if (MallocMethod == MALLOC_LOW_END) {
        // From the bottom of the free block: first an aligned header (leaving what is in front of it as a smaller free
        // block), then a new free block with the rest after the data
        MemBlock *used;
        if (((uintptr_t)(block + 1) & mask) == 0) {
            used = block;
        } else {
            used = (MemBlock *)((char *)Mem_AlignDown((uintptr_t)block + mask + 0x18, mask) - sizeof(MemBlock));
            used->prev = block;
            block->size = Mem_Distance(block, used);
            block->flags = MEM_FREE_FLAGS;
            block->isFree = 1;
            block->slack = 0;
        }
        MemBlock *rest = Mem_AlignDown((uintptr_t)used + size + 0xf, 3);
        rest->flags = MEM_FREE_FLAGS;
        rest->slack = 0;
        rest->isFree = 1;
        rest->prev = used;
        rest->size = Mem_Distance(rest, next);
        used->size = Mem_Distance(used, rest);
        used->flags = (ushort)flags;
        used->isFree = 0;
        used->slack = 0;
        if (next < end)
            next->prev = rest;
        return used + 1;
    }

    // From the top of the free block, which stays free below it
    MemBlock *used = (MemBlock *)((char *)Mem_AlignDown((uintptr_t)next - size, mask) - sizeof(MemBlock));
    if ((char *)used <= (char *)(block + 1))
        return NULL; // no room after all (without Mem_PrintAllInfo, as in the original)
    uint usedSize = Mem_Distance(used, next);
    used->slack = (uchar)(usedSize - size - sizeof(MemBlock));
    used->size = usedSize;
    used->flags = (ushort)flags;
    used->isFree = 0;
    used->prev = block;
    block->size = Mem_Distance(block, used);
    if (next < end)
        next->prev = used;
    return used + 1;
}

// Returns an allocation to the heap, merging it with a free block either side, and clears the caller's pointer.
// Anything outside the heap is only cleared - which is also what happens to memory from anywhere else.
// AUTOINJECT
void Mem_Free(void **ptr) {

    char *data = (char *)*ptr;
    MemBlock *end = Mem_HeapEnd();
    if (data < (char *)Mem_HeapStart() || data > (char *)end) {
        *ptr = NULL;
        return;
    }

    MemBlock *block = (MemBlock *)data - 1;
    MemBlock *prev = block->prev;
    block->isFree = 1;
    block->flags = MEM_FREE_FLAGS;

    // Into the free block before it - but only when it is not the last block
    if (prev != block && prev->isFree) {
        MemBlock *next = Mem_NextBlock(block);
        if (next < end) {
            next->prev = prev;
            prev->size += block->size;
            block = prev;
        }
    }

    // And the free block after it into this one
    MemBlock *next = Mem_NextBlock(block);
    if (next < end && next->isFree) {
        block->size += next->size;
        MemBlock *after = Mem_NextBlock(block);
        if (after < end)
            after->prev = block;
    }

    // Next fit starts from here if QuickBlock was before it, or has just been merged into it
    MemBlock *quick = (MemBlock *)(uintptr_t)QuickBlock;
    if (quick < block || (quick > block && quick < Mem_NextBlock(block)))
        QuickBlock = (uint32_t)(uintptr_t)block;

    *ptr = NULL;
}

// Mem_Shrink's three cases, each of which splits the block and frees the part [data, data + numBytes) with Mem_Free.
// All three work from the caller's pointer as it was; Mem_Shrink's clamp to the block's data affects only which one
// runs. (INVENTED NAMES: FUN_000706d0, FUN_00070750, FUN_000707b0.)

// The part is at the start of the block: the rest becomes a block of its own (with the same flags) and the front is
// freed. Nothing happens if there would be no rest.
static void Mem_FreeFront(char *data, uint numBytes, MemBlock *block, MemBlock *next) {
    if (block->slack + numBytes + sizeof(MemBlock) >= block->size)
        return;
    MemBlock *tail = Mem_AlignDown((uintptr_t)data + numBytes - sizeof(MemBlock), 3);
    tail->isFree = 0;
    tail->prev = block;
    tail->size = Mem_Distance(tail, next);
    tail->slack = (uchar)(block->slack - (uchar)(uintptr_t)tail + (uchar)(uintptr_t)data + sizeof(MemBlock));
    tail->flags = block->flags;
    block->slack = 0;
    block->size = Mem_Distance(block, tail);
    if (next < Mem_HeapEnd())
        next->prev = tail;
    void *front = block + 1;
    Mem_Free(&front);
}

// The part runs to the end of the block: it becomes a block of its own, which is freed
static void Mem_FreeBack(char *data, MemBlock *block, MemBlock *next) {
    MemBlock *back = Mem_AlignDown((uintptr_t)data + 3, 3);
    back->isFree = 0;
    back->slack = 0;
    back->size = Mem_Distance(back, next);
    back->prev = block;
    block->size = Mem_Distance(block, back);
    block->slack = (uchar)((uchar)(uintptr_t)back - (uchar)(uintptr_t)data);
    if (next < Mem_HeapEnd())
        next->prev = back;
    void *backData = back + 1;
    Mem_Free(&backData);
}

// The part is in the middle: the block becomes three, and the middle one is freed. Nothing happens if the middle
// would be smaller than a header - but the test is unsigned, as in the original, so a part of under about 16 bytes
// at a pointer that is not 4-aligned, which puts the tail header before the middle one, gets through and makes a
// block of negative size. parsemap_parsemap, the only caller, frees far larger parts.
static void Mem_FreeMiddle(char *data, uint numBytes, MemBlock *block, MemBlock *next) {
    MemBlock *tail = Mem_AlignDown((uintptr_t)data + numBytes - sizeof(MemBlock), 3);
    MemBlock *mid = Mem_AlignDown((uintptr_t)data + 3, 3);
    if (Mem_Distance(mid, tail) < sizeof(MemBlock))
        return;
    mid->size = Mem_Distance(mid, tail);
    mid->prev = block;
    mid->isFree = 0;
    mid->slack = 0;
    tail->prev = mid;
    tail->isFree = 0;
    tail->size = Mem_Distance(tail, next);
    tail->slack = (uchar)(block->slack - (uchar)(uintptr_t)tail + (uchar)numBytes + (uchar)(uintptr_t)data - sizeof(MemBlock));
    tail->flags = block->flags;
    block->size = Mem_Distance(block, mid);
    block->slack = (uchar)((uchar)(uintptr_t)mid - (uchar)(uintptr_t)data);
    if (next < Mem_HeapEnd())
        next->prev = tail;
    void *midData = mid + 1;
    Mem_Free(&midData);
}

// Frees numBytes of an allocation starting at *ptr, which may point anywhere inside it, keeping the rest allocated,
// and clears the caller's pointer (unless numBytes is too small to bother with, 12 or less). parsemap_parsemap is
// the only caller. There is also a thunk to this at 0x000bec80 with the same name, hence FUNC_AT.
// FUNC_AT(00070850)
void Mem_Shrink(void **ptr, uint numBytes) {

    if (numBytes <= sizeof(MemBlock))
        return;

    char *data = (char *)*ptr;
    MemBlock *end = Mem_HeapEnd();
    MemBlock *block = NULL;
    for (MemBlock *b = Mem_HeapStart(); b < end; b = Mem_NextBlock(b)) {
        if ((char *)b < data && (char *)Mem_NextBlock(b) > data) {
            block = b;
            break;
        }
    }
    // The original reads through a NULL block here, and faults
    NF_ASSERT(block != NULL, "Mem_Shrink: the pointer is not in the heap");

    if (!block->isFree) {
        char *blockData = (char *)(block + 1);
        if (data < blockData)
            data = blockData;
        MemBlock *next = Mem_NextBlock(block);
        if (data == blockData) {
            Mem_FreeFront((char *)*ptr, numBytes, block, next);
        } else {
            char *partEnd = data + block->slack + numBytes;
            if (partEnd < (char *)next)
                Mem_FreeMiddle((char *)*ptr, numBytes, block, next);
            else if (partEnd == (char *)next)
                Mem_FreeBack((char *)*ptr, block, next);
        }
    }
    *ptr = NULL;
}

// Returns the previous method
// AUTOINJECT
uint32_t Mem_SetMallocMethod(uint32_t method) {
    uint32_t previous = MallocMethod;
    MallocMethod = method;
    return previous;
}

// What Mem_Info reports: a name and a byte count per allocation type, then totals (Ghidra's MemInfo). The names
// are the game's own, as its initialised data has them.
typedef struct {
    const char *typeNames[0x50];
    uint typeBytes[0x50];
    uint bytesFree;
    uint bytesMalloced;
    uint unused[7];
} MemInfo_t;
static_assert(sizeof(MemInfo_t) == 0x2a4, "Bad size for MemInfo_t");
// XBE_GLOBAL(0x0017c298, 0x2a4)
static MemInfo_t MemInfo = {
    {
        "malloc_zero",
        "malloc_celstructs",
        "malloc_compplaneeq",
        "malloc_entity_table",
        "malloc_objs",
        "malloc_viewerstructs",
        "malloc_worldstructs",
        "malloc_lightstructs",
        "malloc_portalstructs",
        "malloc_celgliststructs",
        "malloc_map_header",
        "malloc_textures",
        "malloc_anim_distance_table",
        "malloc_anim_frame_table",
        "malloc_anim_frame_buffer",
        "malloc_xyzmiscqzyx",
        "malloc_temp_buffer",
        "malloc_PS2_DMA_List",
        "malloc_Xbox",
        "malloc_SFXdata",
        "malloc_scriptdata",
        "malloc_bot_path1",
        "malloc_Gamecube",
        "malloc_parsefilebuffer",
        "malloc_pathpointers",
        "malloc_rigidbody",
        "malloc_colldata",
        "malloc_llist",
        "malloc_anim_skin_data",
        "malloc_anim_skel_data",
        "malloc_anim_seq_data",
        "malloc_anim_script_data",
        "malloc_anim_skin_structs",
        "malloc_anim_skinmat_structs",
        "malloc_anim_altskin_structs",
        "malloc_anim_seq_structs",
        "malloc_anim_seq_data_buffers",
        "malloc_anim_script_structs",
        "malloc_anim_object_structs",
        "malloc_plrhud",
        "malloc_PS2_Object",
        "malloc_PS2_File",
        "malloc_PS2_Sprite",
        "malloc_PS2_Texture",
        "malloc_PS2_Palette",
        "malloc_PS2_Particle",
        "malloc_PS2_Clone",
        "malloc_PC_Texture",
        "malloc_PC_Mesh",
        "malloc_Menu",
        "malloc_shard",
        "malloc_copter_path",
        "malloc_drone_spawn_objs",
        "malloc_drone_spawn_vars",
        "malloc_lang_data",
        "malloc_light_data",
        "malloc_unpak_buffer",
        "malloc_aram_buffer",
        "malloc_anim_cache",
        "malloc_anim_morph_data_buffers",
        "malloc_anim_set_structs",
        "malloc_anim_unpak_opt",
        "malloc_anim_seq_hdr",
        "malloc_physics",
        "malloc_load_dir",
        "malloc_load_data",
        "malloc_game_particle",
        "malloc_GC_Anim_Cache",
        "malloc_GC_DL",
        "malloc_GC_Skins",
        "malloc_GC_PCList128",
        "malloc_GC_BigBuffer",
        "malloc_GC_AnimMatrix",
        "malloc_GC_Card",
        "malloc_GC_USB2EXI",
        "malloc_GC_Texture",
        "malloc_GC_Sound",
        "malloc_bot_path2",
        "malloc_bot_path3",
        "malloc_null"
    },
};

// Counts the bytes in use per allocation type by walking the heap, and fills in MemInfo. The original then
// passes it to an empty debug hook (0x000e0ec0).
// AUTOINJECT
void* Mem_Info(void) {
    char *heap = (char *)(uintptr_t)PtrHeap;
    char *end = heap + HeapByteSize - 0xc;
    for (ushort type = 0; type <= 0x4f; type++) {
        MemInfo.typeBytes[type] = 0;
        for (char *p = heap; p < end; p += ((MemBlock *)p)->size) {
            MemBlock *block = (MemBlock *)p;
            if (block->isFree == 0 && (block->flags >> 8) == type)
                MemInfo.typeBytes[type] += block->size;
        }
    }
    MemInfo.bytesFree = HeapByteSize - MallocSize;
    MemInfo.bytesMalloced = MallocSize;
    for (int i = 0; i < 7; i++)
        MemInfo.unused[i] = 0;
    return &MemInfo;
}
