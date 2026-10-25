// LoadableLoad and LoadableReload (Loader.h) for the GameCube's data (docs/gc-data.md): a cutscene's loadable bundle
// (0x0780xxxx-0x07A0xxxx, its sequences and script) loaded in place of the level's sequences, and the sequences
// loaded back by LoadableReload. The Xbox's, which load nothing (their bundles hold everything), are left out of this
// build. Built only with NIGHTFIRE_ACTION_DATA_GC.

#include "../engine/Anim.h"         // AnimPreLoadInit
#include "../engine/FS.h"
#include "../engine/Loader.h"
#include "../engine/Script.h"
#include "../game.h"                // GameState
#include "../memory.h"
#include "../util/hashtable.h"
#include "GcFs.h"

#include <stdio.h>

// LoaderLoad's variant n makes bits 20-23 n + 7: the loadables 1 to 3 (0x0780xxxx to 0x07A0xxxx) are the cutscenes'
// sequences and scripts, 8 the level's sequences alone (0x07f0xxxx, which GcBundle_Load makes)
#define LOADER_VARIANT_SEQUENCES 8
#define LOADER_VARIANT_BASE 7
#define LOADER_VARIANT_SHIFT 20
#define LOADER_VARIANT_BITS 0x00f00000

// The level's bundle's variant (1-3 the loadables, 8 the sequences)
static uint32_t VariantOf(uint32_t level, uint32_t variant) {
    return (level & ~LOADER_VARIANT_BITS) | (variant + LOADER_VARIANT_BASE) << LOADER_VARIANT_SHIFT;
}

extern uchar bLevelLoaded;

// The memory the last loadable or sequence bundle was read into. GC LoadableLoad (0x8005c1e4) reads a loadable over
// the level's sequences (LoaderLoad's mode 2, into the buffer it passes); here a bundle goes into the last one when it
// fits, else into new memory, and the old is given back. The level's own sequences stay in its bundle's memory.
static void *AnimBuffer;
static uint32_t AnimBufferBytes;
static uint32_t AnimBufferLoad;     // GcFs_LevelLoads when it was taken: the heap it is in

// Loads the level bundle's variant through LoaderLoad, into the animation buffer when it fits
static void LoadVariant(uint32_t level, int variant) {
    char name[16];
    uint32_t bundle = VariantOf(level, (uint32_t)variant);
    snprintf(name, sizeof(name), "%8.8x.bin", bundle);
    if (FS_MatchFilenameToHeader(name) < 0) {
        printf("[gc] no %s on the disc\n", name);
        return;
    }
    if (AnimBufferLoad != GcFs_LevelLoads())   // the heap has been started again since
        AnimBuffer = NULL;
    AnimBufferLoad = GcFs_LevelLoads();
    if (AnimBuffer != NULL)
        GcFs_LoadNextInto(AnimBuffer, AnimBufferBytes);
    while (LoaderLoad(1, level, 0, variant))
        ;
    uint32_t bytes;
    void *loaded = GcFs_LastLoad(&bytes);
    if (loaded != AnimBuffer) {
        if (AnimBuffer != NULL)
            Mem_Free(&AnimBuffer);
        AnimBuffer = loaded;
        AnimBufferBytes = bytes;
    }
}

// The loadable bundle in place of the level's sequences, with NextLevelHashcode naming it meanwhile, as GC
// LoadableLoad has it
bool LoadableLoad(HASHCODE hashcode, uint loadable) {
    tMallocMethod method = Mem_SetMallocMethod(MALLOC_BEST_FIT);
    hashtable_delete_hashtype(HT_AnimSeq_HASHCODE_BASE);
    HASHCODE next = (HASHCODE)GameState.NextLevelHashcode;
    uint32_t level = (uint32_t)GameState.CurrentLevelHashcode;
    GameState.NextLevelHashcode = VariantOf(level, loadable);
    LoadVariant(level, (int)loadable);
    GameState.NextLevelHashcode = next;
    hashtable_create_sections();
    Mem_SetMallocMethod(method);
    return true;
}

// The script freed, and the level's sequences loaded again
bool LoadableReload(HASHCODE hashcode) {
    tMallocMethod method = Mem_SetMallocMethod(MALLOC_BEST_FIT);
    Script_FreeFromHashTable(hashcode);
    hashtable_delete_hashtype(HT_AnimSeq_HASHCODE_BASE);
    LoadVariant((uint32_t)GameState.CurrentLevelHashcode, LOADER_VARIANT_SEQUENCES);
    bLevelLoaded = 0;
    hashtable_create_sections();
    AnimPreLoadInit(1);
    Mem_SetMallocMethod(method);
    return true;
}
