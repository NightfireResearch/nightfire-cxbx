#include "Ps2Loader.h"
#include "../engine/FS.h"
#include "../engine/Loader.h"
#include "../engine/Script.h"
#include "../game.h"            // GameState
#include "../memory.h"
#include "../util/hashtable.h"
#include "FS_ps2.h"

#include <stdio.h>

// LoaderLoad's variant n makes bits 20-23 n + 7: 8 the level's sequences (0x07F0xxxx); the loadables 1 to 3
// (0x0780xxxx to 0x07A0xxxx) are the cutscenes' sequences and scripts
#define LOADER_VARIANT_SEQUENCES 8
#define LOADER_VARIANT_BASE 7
#define LOADER_VARIANT_SHIFT 20
#define LOADER_VARIANT_BITS 0x00f00000

extern uchar bLevelLoaded;

// LoaderAnimBase (PS2 0x0030e750): the memory the level's sequence bundle was read into. The PS2's LoaderLoad reads
// it fresh (mode 0) and remembers it; its modes 2 and 3 (the loadables, and the sequences again) read into it over
// what was there. Here a bundle that does not fit takes new memory, and the old is given back.
static void *AnimBuffer;
static uint32_t AnimBufferBytes;

// Loads the level bundle's variant through LoaderLoad, if the disc has it: into the animation buffer, with intoBuffer
static void LoadVariant(uint32_t level, int variant, bool intoBuffer) {
    char name[16];
    uint32_t bundle = (level & ~LOADER_VARIANT_BITS) | (uint32_t)(variant + LOADER_VARIANT_BASE) << LOADER_VARIANT_SHIFT;
    snprintf(name, sizeof(name), "%8.8x.bin", bundle);
    if (FS_MatchFilenameToHeader(name) < 0) {
        printf("[ps2] no %s on the disc\n", name);
        return;
    }
    if (intoBuffer && AnimBuffer != NULL)
        Ps2Fs_LoadNextInto(AnimBuffer, AnimBufferBytes);
    while (LoaderLoad(1, level, 0, variant))
        ;
    uint32_t bytes;
    void *loaded = Ps2Fs_LastLoad(&bytes);
    if (loaded != AnimBuffer) {
        if (intoBuffer && AnimBuffer != NULL) {
            printf("[ps2] %s does not fit the animation buffer (%u bytes): it takes new memory\n", name,
                   AnimBufferBytes);
            Mem_Free(&AnimBuffer);
        }
        AnimBuffer = loaded;
        AnimBufferBytes = bytes;
    } else {
        printf("[ps2] %s loaded into the animation buffer\n", name);
    }
}

void Ps2_LoadLevelSequences(uint32_t level) {
    AnimBuffer = NULL;      // the heap it was in has been started again
    LoadVariant(level, LOADER_VARIANT_SEQUENCES, false);
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
    LoadVariant(level, (int)loadable, true);
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
    LoadVariant((uint32_t)GameState.CurrentLevelHashcode, LOADER_VARIANT_SEQUENCES, true);
    bLevelLoaded = 0;
    hashtable_create_sections();
    AnimPreLoadInit(1);
    Mem_SetMallocMethod(method);
    return true;
}
