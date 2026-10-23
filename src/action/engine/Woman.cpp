// RLE-encoded 1-bit image, combined with a scrolling fire texture, to create the pause menu background
#include "Woman.h"
#include "psiGraphics.h"

#include "../memory.h"
#include "FS.h"
#include "../util/hashtable.h"
#include "../game.h"
#include "Direct3D/d3dSeam.h"

#include <string.h>

// XBE_GLOBAL(0x002ae2dc, 0x4)
void *MemoryForWoman;

// XBE_GLOBAL(0x002ae2e0, 0x4)
void *pWoman;  // Current point in the buffer to which we have decoded
// XBE_GLOBAL(0x002ae2e4, 0x4)
uint32_t WomanFrame;  // Frame number

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

// XBE_GLOBAL(0x002ae4f0, 0x4)
uint32_t WomanFirePixel;  // where the next frame takes the fire texture from, in pixels

#define WOMAN_FRAMES 348        // the frames in the file
#define WOMAN_HELD_UNTIL 588    // the last is held until then, and the film starts again
#define WOMAN_PIXELS 0x4000     // a frame's

static int WomanTextureIndex(HASHCODE texture) {
    int *item = (int *)hashtable_getitem(texture);
    return item != NULL ? *item : 0;
}

// A texture's current frame's data, its frames turning over at its own rate (looked up three times, as the original
// does). The frames' slots run on past the record from its first.
static void *WomanTextureFrame(HASHCODE texture) {
    TextureInfo *frames = Tex[WomanTextureIndex(texture)];
    TextureInfo *rate = Tex[WomanTextureIndex(texture)];
    TextureInfo *count = Tex[WomanTextureIndex(texture)];
    uint frame = (uint)GameState.NumFramesUnpaused / (uint)rate->animSpeed % (uint)count->numFrames;
    return Texture_GetRawDataPtr((&frames->baseIdx)[frame]);
}

// The next frame of the pause menu's film into its texture. A frame alternates between a run of clear pixels (a
// 16-bit count) and a 16-bit count of runs (8-bit lengths) that alternate between the fire's pixels and clear
// ones, until its 0x4000 pixels are done.
// AUTOINJECT
void psiDecompressWoman(void) {
    if (MemoryForWoman == NULL)
        return;
    if (WomanFrame >= WOMAN_FRAMES) {
        WomanFrame++;
        if (WomanFrame < WOMAN_HELD_UNTIL)
            return;
        WomanFrame = 0;
        pWoman = MemoryForWoman;
    }
    uint32_t *fire = (uint32_t *)WomanTextureFrame(HT_Texture_FireForWoman);
    if (fire == NULL)
        return;
    uint32_t *out = (uint32_t *)WomanTextureFrame(HT_Texture_Discowoman);
    if (out == NULL)
        return;

    uchar *in = (uchar *)pWoman;
    uint left = WOMAN_PIXELS;
    bool clearRun = true;
    do {
        uint count = in[0] << 8 | in[1];
        in += 2;
        if (clearRun) {
            memset(out, 0, count * 4);
            out += count;
            WomanFirePixel += count;
            left -= count;
        } else {
            bool fireRun = true;
            for (int runs = (int)count; runs > 0; runs--) {
                uint length = *in++;
                if (fireRun)
                    memcpy(out, &fire[WomanFirePixel & (WOMAN_PIXELS - 1)], length * 4);
                else
                    memset(out, 0, length * 4);
                out += length;
                WomanFirePixel += length;
                left -= length;
                fireRun = !fireRun;
            }
        }
        clearRun = !clearRun;
    } while (left != 0);
    pWoman = in;
    // The original's WBINVD here (for the GPU to see the frame) has no work to do off the Xbox
    WomanFirePixel += 0x7f;
    WomanFrame++;
}
