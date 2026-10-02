// RLE-encoded 1-bit image, combined with a scrolling fire texture, to create the pause menu background
#include "Woman.h"
#include "psiGraphics.h"

#include "../memory.h"
#include "FS.h"
#include "../util/hashtable.h"

#define pWoman (*(void**)0x002ae2e0) // Current point in the buffer to which we have decoded
#define WomanFrame U32_AT(0x002ae2e4) // Frame number


// Loads the pause menu background's RLE file whole and rewinds the decoder to its first frame
// AUTOINJECT
void LoadWoman(void) {
    int sizeBytes = FS_GetFileSize((char *)"woman.rle");
    MemoryForWoman = Mem_Malloc(sizeBytes, 0x1204, 4);
    FS_LoadFileIfReady((char *)"woman.rle", MemoryForWoman);
    while (FS_StateMachineIterate())
        ;
    pWoman = MemoryForWoman;
    WomanFrame = 0;
}

// The Xbox layer's thunk to it (0x000dc860), which ResetMap_Load calls
// FUNC_AT(000dc860)
void LoadWoman_Thunk(void) {
    LoadWoman();
}

void* Texture_GetRawDataPtr(int texIdx); // reimplemented in Direct3D/d3dSeam.cpp


// AUTOGEN
void psiDecompressWoman(void);

// NOAUTOGEN
void WIP(void){

    if(MemoryForWoman == NULL) 
        return;

    // On PS2, the skipping is implemented using "skipit.288" and "PS2FramesToSkip"
    // Xbox just runs it at the rate that psiDecompressWoman is called, and does this maths instead
    // Unclear if this is manual or a result of optimisation
    if(WomanFrame > 347) { // No more frame data
        if(WomanFrame < 587) {
            // Hold on the last frame for (587-347) = 240 frames = 4s
            WomanFrame++;
            return;
        } else {
            // Then restart the animation
            WomanFrame = 0;
            pWoman = MemoryForWoman;
        }
    }

    // Locate the used textures
    // In the original Xbox logic, this is called 3 times but this looks like a bug.
    // All it does is access one member of the thing each time - likely some macro which calls hashtable_getitem()?
    // Each access could just be about obtaining each component (dataPtr, numFrames, etc)
    void* pFireTexture = hashtable_getitem(TEX_FIRE_FOR_WOMAN);
    int FireIdx = pFireTexture ? *((int*)pFireTexture) : 0;
    void* pOutTexture = hashtable_getitem(TEX_DISCOWOMAN); // Placeholder for the output texture, we will overwrite the data - hacky way to implement animated texture using RLE
    int OutIdx = pOutTexture ? *((int*)pOutTexture) : 0;

    TextureInfo t;
    
    t = *Tex[FireIdx];
    int fireIdxBase = t.baseIdx;
    int fireFrames = t.numFrames;
    int fireAnimSpeed = t.animSpeed;
    int fireTexIdx = fireIdxBase + (GameState.NumFramesUnpaused / fireAnimSpeed) % fireFrames; // FIXME: NumFramesUnpaused doesn't make sense because it's part of the pause menu and still animates!
    void* FireData = Texture_GetRawDataPtr(fireTexIdx);


    t = *Tex[OutIdx];
    int outIdxBase = t.baseIdx;
    int outFrames = t.numFrames;
    int outAnimSpeed = t.animSpeed;
    int outTexIdx = outIdxBase + (GameState.NumFramesUnpaused / outAnimSpeed) % outFrames; // FIXME: NumFramesUnpaused doesn't make sense because it's part of the pause menu and still animates!
    void* OutData = Texture_GetRawDataPtr(outTexIdx);

    if(FireData == NULL || OutData == NULL)
        return;

    // Simultaneously RLE decode and blend with the fire texture

    // TODO: This

}