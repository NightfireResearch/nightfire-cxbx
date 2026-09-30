#include <string.h>
#include <stdio.h>
#include "actionhelpers.h"
#include "memory.h"

// XBE_GLOBAL(0x00223a60, 0x1c)
uint32_t MemStats[7];
#include <stdlib.h>

#define Addr_PtrHeap 0x00223a80
#define Addr_HeapByteSize 0x00223a88

#define PtrHeap U32_AT(Addr_PtrHeap)
// XBE_GLOBAL(0x00223a84, 0x4)
static uint32_t PtrHeapEnd;
#define HeapByteSize U32_AT(Addr_HeapByteSize)
#define MallocMethod U32_AT(0x00223a7c)
#define QuickBlock U32_AT(0x002237d8)
// XBE_GLOBAL(0x00223a8c, 0x4)
static uint32_t MallocSize;

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

    void *puVar1;

    memset(&MemStats, 0, sizeof(MemStats));

    MallocMethod = 1;

    if (PtrHeap == NULL) {
        printf("No heap yet, getting config...\n");
        psiMem_Init((uint *)Addr_PtrHeap, (uint*)Addr_HeapByteSize);
    }

    printf("Mem_Init: Heap: 0x%08x, size: 0x%08x\n", PtrHeap, HeapByteSize);

    PtrHeapEnd = (HeapByteSize + PtrHeap);

    memset((void*)PtrHeap, 0x98, HeapByteSize);

    puVar1 = (void*)PtrHeap;
    *(uint32_t *)((int)PtrHeap + 4) = HeapByteSize - 0xc;
    *(uint8_t *)((int)puVar1 + 11) = 1;
    *(void**)puVar1 = (void*)puVar1;
    *(uint16_t *)((int)puVar1 + 8) = 4;
    QuickBlock = PtrHeap;
    MallocSize = 0;

}

// Not auto generated or injected - we call the original allocator in some cases, but if we inject, we end up calling ourself
void* Mem_Malloc(size_t size, MallocFlags flags, uint32_t unknownMaybeAlignment) {

    //printf("Allocating %i bytes of type %02x\n", size, flags);

    // If it's type Xbox, must be allocated in the first 64MB - video memory must be in this region

    // The type is the high byte of the flags (0x12 = malloc_Xbox in MemInfo's names)
    if((flags & 0xFF00) == 0x1200) { // Xbox memory type
        return reinterpret_cast<void * (*)(uint, MallocFlags, uint)>(0x00070ae0)(size, flags, unknownMaybeAlignment);
    }

    // TODO: The game does NOT free this memory, it just assumes the entire heap is wiped. So this results in a memory leak
    return malloc(size);
    
}

// AUTOGEN
void Mem_Free(void **ptr);

// AUTOGEN
void Mem_Shrink(void **param_1,uint param_2);

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

// A heap block's header, as far as Mem_Info reads it.
typedef struct {
    uint unknown0;
    uint size;          // 0x4 - including this header; the next block follows it
    uchar unknown8;
    uchar type;         // 0x9 - the MallocFlags type byte
    uchar unknownA;
    uchar isFree;       // 0xb - Mem_Info counts only blocks with this clear
} MemBlockHeader;

// Counts the bytes in use per allocation type by walking the heap, and fills in MemInfo. The original then
// passes it to an empty debug hook (0x000e0ec0).
// AUTOINJECT
void* Mem_Info(void) {
    char *heap = (char *)(uintptr_t)PtrHeap;
    char *end = heap + HeapByteSize - 0xc;
    for (ushort type = 0; type <= 0x4f; type++) {
        MemInfo.typeBytes[type] = 0;
        for (char *p = heap; p < end; p += ((MemBlockHeader *)p)->size) {
            MemBlockHeader *block = (MemBlockHeader *)p;
            if (block->isFree == 0 && block->type == type)
                MemInfo.typeBytes[type] += block->size;
        }
    }
    MemInfo.bytesFree = HeapByteSize - MallocSize;
    MemInfo.bytesMalloced = MallocSize;
    for (int i = 0; i < 7; i++)
        MemInfo.unused[i] = 0;
    return &MemInfo;
}
