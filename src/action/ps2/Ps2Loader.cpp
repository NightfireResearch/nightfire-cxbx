#include "Ps2Loader.h"
#include "../engine/FS.h"
#include "../engine/Loader.h"
#include "../engine/Script.h"
#include "../game.h"            // GameState
#include "../memory.h"
#include "../util/hashtable.h"

#include <stdio.h>

// LoaderLoad's variant n makes bits 20-23 n + 7: 8 the level's sequences (0x07F0xxxx); the loadables 1 to 3
// (0x0780xxxx to 0x07A0xxxx) are the cutscenes' sequences and scripts
#define LOADER_VARIANT_SEQUENCES 8
#define LOADER_VARIANT_BASE 7
#define LOADER_VARIANT_SHIFT 20
#define LOADER_VARIANT_BITS 0x00f00000

extern uchar bLevelLoaded;

// Loads the level bundle's variant through LoaderLoad, if the disc has it
static void LoadVariant(uint32_t level, int variant) {
    char name[16];
    uint32_t bundle = (level & ~LOADER_VARIANT_BITS) | (uint32_t)(variant + LOADER_VARIANT_BASE) << LOADER_VARIANT_SHIFT;
    snprintf(name, sizeof(name), "%8.8x.bin", bundle);
    if (FS_MatchFilenameToHeader(name) < 0) {
        printf("[ps2] no %s on the disc\n", name);
        return;
    }
    while (LoaderLoad(1, level, 0, variant))
        ;
}

void Ps2_LoadLevelSequences(uint32_t level) {
    LoadVariant(level, LOADER_VARIANT_SEQUENCES);
}

// PS2 LoadableLoad (0x1d7188): the loadable bundle in place of the level's sequences, with NextLevelHashcode naming
// it meanwhile (LoaderLoad's mode 2 loads that one as it is). The PS2's AnimCacheDisable first has no Xbox
// counterpart.
bool LoadableLoad(HASHCODE hashcode, uint loadable) {
    tMallocMethod method = Mem_SetMallocMethod(MALLOC_BEST_FIT);
    hashtable_delete_hashtype(HT_AnimSeq_HASHCODE_BASE);
    HASHCODE next = (HASHCODE)GameState.NextLevelHashcode;
    uint32_t level = (uint32_t)GameState.CurrentLevelHashcode;
    GameState.NextLevelHashcode = (level & ~LOADER_VARIANT_BITS) | (loadable + LOADER_VARIANT_BASE) << LOADER_VARIANT_SHIFT;
    LoadVariant(level, (int)loadable);
    GameState.NextLevelHashcode = next;
    hashtable_create_sections();
    Mem_SetMallocMethod(method);
    return true;
}

// PS2 LoadableReload (0x1d7250): the script freed, and the level's sequences loaded again (LoaderLoad's mode 3). The
// PS2's AnimCacheEnable has no Xbox counterpart.
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
