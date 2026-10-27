#include "GcDebugOverlays.h"

#include "../actionhelpers.h"
#include "../memory.h"
#include "../engine/psiGraphics.h"   // Tex, NumXboxTexLoaded
#include "../engine/psiSprite.h"
#include "../gfx/Font.h"
#include "../../common/renderWindow.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

// ---------------------------------------------------------------------------------------------------------------
// The GameCube build's debug overlays, which the Xbox build compiled out (docs/gc-debug-overlays.md). On the
// GameCube they are entries in a debug tweak menu that nothing in the retail build opens; psiPreDraw draws them.
// Here settings.ini's [Settings] switches them on, and psiPostDraw draws them over the finished frame:
//
//  - GcMemMap=on: the heap as a grid of cells, coloured by allocation type, free blocks black, with a cursor
//    and the block under it (Debug_DrawMemMap 0x8000b130, Mem_DrawMap 0x800574f0). GcMemMapCursor=x,y and
//    GcMemMapSize=w,h as the GameCube's "Mem Map Cursor" (64,64) and "Mem Map Size" (128,384); Ctrl+arrows move
//    the cursor a cell, with Shift eight. GcMemMapColours=all gives every type a colour of its own, where the
//    GameCube's table leaves all but its own types grey.
//  - GcMemStats=on: the free and used blocks' counts and bytes, the last line of "Display Stats"
//    (Debug_DrawStats 0x8002cc48, Mem_CountBlocks 0x800573a8).
//  - GcShowTextures=N: twelve of the loaded textures, largest first, from the Nth (Debug_ShowTextures 0x8000b414);
//    Ctrl+PageUp and Ctrl+PageDown move twelve at a time.
//  - GcSafeFrame=on: while the developers' cheat page's coordinates cheat is on, the frame the GameCube draws with
//    it (Debug_DrawSafeFrame 0x8002c9ec); the Xbox's cheat shows only the HUD's readout.
//
// The GameCube writes the heap map straight into the frame buffer a pixel at a time and draws the cursor by
// inverting the pixels under it; here the cells are sprites, a row of a block's cells at a time, and the cursor
// is white. Text uses the game's font FontTable[1] scaled to the GameCube's 8-pixel lines. Apart from the font
// code's statics nothing here writes the game's state: the sort and the counts are kept here, where the GameCube
// put its sort list on the game heap.
// ---------------------------------------------------------------------------------------------------------------

// View.cpp's sprite list, which Font_DrawText fills
extern SPRITE_DRAW SprBuffList[64];
extern uint32_t SprCnt;
// HUD.cpp's: C_CHCHCOORDS_Handler writes it
extern int switch_SHOW_COORDS;

#define VK_SHIFT_ 0x10
#define VK_CONTROL_ 0x11
#define VK_PRIOR_ 0x21
#define VK_NEXT_ 0x22
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28
#define GA_ROOT_ 2

namespace {

const int kMapX = 32;                   // where Debug_DrawMemMap puts the map
const int kMapY = 32;
const int kLine = 8;                    // the GameCube debug font's line
const int kTexturesShown = 12;

// Mem_DrawMap's colours by allocation type (0x801a7328), 0xAARRGGBB; the frame buffer keeps no alpha
const uint32_t kTypeColours[MALLOC_TYPE_COUNT] = {
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080,  // 0x00
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0xffffffff,  // 0x08
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0xffff0000, 0x80808080,  // 0x10
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080,  // 0x18
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080,  // 0x20
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080,  // 0x28
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080,  // 0x30
    0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0x80808080, 0xff00ff00, 0x80800080,  // 0x38
    0x80808080, 0xff0000ff, 0x80808080, 0x80ffffff, 0xffff00ff, 0x8000ffff, 0x80ffff00, 0x800000ff,  // 0x40
    0x80ffff80, 0x80800000, 0x8080ff00, 0x80ff0080, 0x0000ff80, 0x80808080, 0x80808080, 0x80808080,  // 0x48
};
const uint32_t kFreeColour = 0xff000000;

// The type names, as the GameCube's tweak table has them for the map (0x801bfb7c, 90 bytes apiece): memnames
const char *const kTypeNames[MALLOC_TYPE_COUNT] = {
    "malloc_zero", "malloc_celstructs", "malloc_compplaneeq", "malloc_entity_table", "malloc_objs",
    "malloc_viewerstructs", "malloc_worldstructs", "malloc_lightstructs", "malloc_portalstructs",
    "malloc_celgliststructs", "malloc_map_header", "malloc_textures", "malloc_anim_distance_table",
    "malloc_anim_frame_table", "malloc_anim_frame_buffer", "malloc_xyzmiscqzyx", "malloc_temp_buffer",
    "malloc_PS2_DMA_List", "malloc_Xbox", "malloc_SFXdata", "malloc_scriptdata", "malloc_bot_path1",
    "malloc_Gamecube", "malloc_parsefilebuffer", "malloc_pathpointers", "malloc_rigidbody", "malloc_colldata",
    "malloc_llist", "malloc_anim_skin_data", "malloc_anim_skel_data", "malloc_anim_seq_data",
    "malloc_anim_script_data", "malloc_anim_skin_structs", "malloc_anim_skinmat_structs",
    "malloc_anim_altskin_structs", "malloc_anim_seq_structs", "malloc_anim_seq_data_buffers",
    "malloc_anim_script_structs", "malloc_anim_object_structs", "malloc_plrhud", "malloc_PS2_Object",
    "malloc_PS2_File", "malloc_PS2_Sprite", "malloc_PS2_Texture", "malloc_PS2_Palette", "malloc_PS2_Particle",
    "malloc_PS2_Clone", "malloc_PC_Texture", "malloc_PC_Mesh", "malloc_Menu", "malloc_shard",
    "malloc_copter_path", "malloc_drone_spawn_objs", "malloc_drone_spawn_vars", "malloc_lang_data",
    "malloc_light_data", "malloc_unpak_buffer", "malloc_aram_buffer", "malloc_anim_cache",
    "malloc_anim_morph_data_buffers", "malloc_anim_set_structs", "malloc_anim_unpak_opt", "malloc_anim_seq_hdr",
    "malloc_physics", "malloc_load_dir", "malloc_load_data", "malloc_game_particle", "malloc_GC_Anim_Cache",
    "malloc_GC_DL", "malloc_GC_Skins", "malloc_GC_PCList128", "malloc_GC_BigBuffer", "malloc_GC_AnimMatrix",
    "malloc_GC_Card", "malloc_GC_USB2EXI", "malloc_GC_Texture", "malloc_GC_Sound", "malloc_bot_path2",
    "malloc_bot_path3", "malloc_null",
};

struct Settings {
    bool read;
    bool memMap;
    bool allColours;
    bool memStats;
    bool safeFrame;
    int showTextures;   // -1: off
    int cursorX, cursorY;
    int mapWidth, mapHeight;
};
Settings g_settings = {false, false, false, false, false, -1, 64, 64, 128, 384};

uint32_t g_frames;      // psiPreDraw's frame count on the GameCube, which flashes the block under the cursor
float g_sway;           // Debug_ShowTextures' angle, in degrees

void ReadSetting(const char *key, char *value, int size) {
    GetPrivateProfileStringA("Settings", key, "", value, size, ".\\settings.ini");
}

bool SettingIsOn(const char *value) {
    return _stricmp(value, "on") == 0 || _stricmp(value, "1") == 0 || _stricmp(value, "true") == 0;
}

void ReadSettings(void) {
    char v[64];
    ReadSetting("GcMemMap", v, sizeof(v));
    g_settings.memMap = SettingIsOn(v);
    ReadSetting("GcMemMapColours", v, sizeof(v));
    g_settings.allColours = _stricmp(v, "all") == 0;
    ReadSetting("GcMemStats", v, sizeof(v));
    g_settings.memStats = SettingIsOn(v);
    ReadSetting("GcSafeFrame", v, sizeof(v));
    g_settings.safeFrame = SettingIsOn(v);
    ReadSetting("GcShowTextures", v, sizeof(v));
    if (v[0] != '\0')
        g_settings.showTextures = atoi(v);
    ReadSetting("GcMemMapCursor", v, sizeof(v));
    sscanf(v, " %d , %d", &g_settings.cursorX, &g_settings.cursorY);
    ReadSetting("GcMemMapSize", v, sizeof(v));
    sscanf(v, " %d , %d", &g_settings.mapWidth, &g_settings.mapHeight);
    // Debug_DrawMemMap rounds the size down to fours
    g_settings.mapWidth &= ~3;
    g_settings.mapHeight &= ~3;
    if (g_settings.mapWidth < 4)
        g_settings.mapWidth = 4;
    if (g_settings.mapHeight < 4)
        g_settings.mapHeight = 4;
    g_settings.read = true;
    if (g_settings.memMap || g_settings.memStats || g_settings.safeFrame || g_settings.showTextures >= 0)
        printf("[gcdebug] overlays: map %s (%dx%d, cursor %d,%d), stats %s, safe frame %s, textures from %d\n",
               g_settings.memMap ? "on" : "off", g_settings.mapWidth, g_settings.mapHeight, g_settings.cursorX,
               g_settings.cursorY, g_settings.memStats ? "on" : "off", g_settings.safeFrame ? "on" : "off",
               g_settings.showTextures);
}

// ---------------------------------------------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------------------------------------------

bool GameWindowHasFocus(void) {
    HWND render = FindOwnRenderWindow();
    if (render == NULL)
        return true;
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == GetAncestor(render, GA_ROOT_));
}

bool KeyDown(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

// A key that went down since the last frame
bool KeyPressed(int virtualKey, bool *wasDown) {
    bool down = KeyDown(virtualKey);
    bool pressed = down && !*wasDown;
    *wasDown = down;
    return pressed;
}

void ReadKeys(void) {
    static bool left, right, up, down, pageUp, pageDown;
    bool focus = GameWindowHasFocus() && KeyDown(VK_CONTROL_);
    int step = KeyDown(VK_SHIFT_) ? 8 : 1;
    if (KeyPressed(VK_LEFT_, &left) && focus)
        g_settings.cursorX -= step;
    if (KeyPressed(VK_RIGHT_, &right) && focus)
        g_settings.cursorX += step;
    if (KeyPressed(VK_UP_, &up) && focus)
        g_settings.cursorY -= step;
    if (KeyPressed(VK_DOWN_, &down) && focus)
        g_settings.cursorY += step;
    if (KeyPressed(VK_PRIOR_, &pageUp) && focus && g_settings.showTextures >= 0)
        g_settings.showTextures = (std::max)(0, g_settings.showTextures - kTexturesShown);
    if (KeyPressed(VK_NEXT_, &pageDown) && focus && g_settings.showTextures >= 0)
        g_settings.showTextures += kTexturesShown;
    g_settings.cursorX = (std::min)((std::max)(g_settings.cursorX, 0), g_settings.mapWidth - 1);
    g_settings.cursorY = (std::min)((std::max)(g_settings.cursorY, 0), g_settings.mapHeight - 1);
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing: untextured rectangles through texture 0 (as psiFadeView draws), text through the game's font
// ---------------------------------------------------------------------------------------------------------------

SPRITE_DRAW g_rects[256];
int g_rectCount;

void FlushRects(void) {
    if (g_rectCount != 0)
        psiDrawSprites(g_rects, g_rectCount);
    g_rectCount = 0;
}

void Rect(float x, float y, float width, float height, uint32_t argb) {
    if (g_rectCount == (int)ARRAY_SIZE(g_rects))
        FlushRects();
    SPRITE_DRAW *rect = &g_rects[g_rectCount++];
    memset(rect, 0, sizeof(*rect));
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    rect->clrR = (uint8_t)(argb >> 16);
    rect->clrG = (uint8_t)(argb >> 8);
    rect->clrB = (uint8_t)argb;
    rect->clrA = 0xff;
}

char g_textFormat[] = "\xff\x01";   // FontTable[1]
GPOINT g_textScale = {0.0f, 0.0f};

// Text at the GameCube's 8-pixel line, its top left at (x, y); false until the game's fonts are loaded
bool Text(int x, int y, const char *text) {
    if (g_textScale.y == 0.0f) {
        GPOINT one = {1.0f, 1.0f};
        GPOINT extent = {0.0f, 0.0f};
        if (!Font_GetTextExtent("A", g_textFormat, &extent, &one, 1) || extent.y <= 0.0f)
            return false;
        g_textScale.x = g_textScale.y = (float)kLine / extent.y;
    }
    Font_DrawText((float)x, (float)y, 0xffffffff, 0x000000ff, 0xffffffff, text, g_textFormat, &g_textScale,
                  FONT_SHADOW, (int)strlen(text), 0.0f);
    return true;
}

void Textf(int x, int y, const char *format, ...) {
    char text[128];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Text(x, y, text);
}

void FlushText(void) {
    if (SprCnt != 0)
        psiDrawSprites(SprBuffList, SprCnt);
    SprCnt = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The heap
// ---------------------------------------------------------------------------------------------------------------

MemBlock* HeapStart(void) {
    return PtrFrom32<MemBlock>(PtrHeap);
}

// Every walk of the heap stops at its last header-sized slot, as memory.cpp's do
MemBlock* HeapEnd(void) {
    return PtrFrom32<MemBlock>(PtrHeap + HeapByteSize - (uint32_t)sizeof(MemBlock));
}

MemBlock* NextBlock(MemBlock *block) {
    return (MemBlock *)((char *)block + block->size);
}

// Mem_DrawMap's index into its colours: a type past the last counts as type 0
int BlockType(const MemBlock *block) {
    int type = MALLOC_TYPE(block->flags);
    return type > MALLOC_TYPE_NULL ? 0 : type;
}

uint32_t TypeColour(int type) {
    if (!g_settings.allColours)
        return kTypeColours[type];
    // A hue per type, the GameCube's own colours aside
    static const uint32_t steps[6] = {0xff4040, 0xffc040, 0x40ff40, 0x40ffff, 0x4060ff, 0xff40ff};
    uint32_t base = steps[type % 6];
    uint32_t shade = 0xff - (uint32_t)(type / 6) * 0x0c;
    uint32_t r = ((base >> 16) & 0xff) * shade / 0xff, g = ((base >> 8) & 0xff) * shade / 0xff;
    uint32_t b = (base & 0xff) * shade / 0xff;
    return 0xff000000 | r << 16 | g << 8 | b;
}

// The cells first..last-1 of a w-wide grid at (x, y), as at most three rectangles
void Cells(uint32_t first, uint32_t last, int width, uint32_t argb) {
    while (first < last) {
        uint32_t row = first / width, column = first % width;
        uint32_t rowEnd = (row + 1) * width;
        if (column == 0 && last - first >= (uint32_t)width) {
            uint32_t rows = (last - first) / width;
            Rect((float)kMapX, (float)(kMapY + row), (float)width, (float)rows, argb);
            first += rows * width;
            continue;
        }
        uint32_t end = (std::min)(last, rowEnd);
        Rect((float)(kMapX + column), (float)(kMapY + row), (float)(end - first), 1.0f, argb);
        first = end;
    }
}

// Debug_DrawMemMap: the map, the cursor, and the type, address and size of the block under it
void DrawMemMap(void) {
    if (PtrHeap == 0)
        return;
    int width = g_settings.mapWidth, height = g_settings.mapHeight;
    uint32_t cells = (uint32_t)(width * height);
    uint32_t cellBytes = HeapByteSize / cells;
    uint32_t cursorByte = (uint32_t)(g_settings.cursorX + g_settings.cursorY * width) * cellBytes;

    int cursorType = -1;
    uint32_t cursorAddress = 0, cursorSize = 0;
    MemBlock *start = HeapStart();
    MemBlock *end = HeapEnd();
    for (MemBlock *block = start; block < end; block = NextBlock(block)) {
        uint32_t offset = (uint32_t)((char *)block - (char *)start);
        int type = BlockType(block);
        uint32_t colour = block->isFree ? kFreeColour : TypeColour(type);
        if (offset <= cursorByte && cursorByte < offset + block->size) {
            // Mem_DrawMap reports the cursor's byte as the address, and flashes the block four frames in eight
            cursorAddress = PtrHeap + cursorByte;
            cursorType = type;
            cursorSize = block->size;
            if ((g_frames & 4) == 0)
                colour = (~colour & 0xf0f0f0f0) >> 3;
        }
        uint32_t first = (std::min)(offset / cellBytes, cells);
        uint32_t last = (std::min)((offset + block->size + cellBytes - 1) / cellBytes, cells);
        Cells(first, last, width, colour);
        if (block->size == 0)
            break;
    }

    // The cursor: the cell and its four neighbours
    float cx = (float)(kMapX + g_settings.cursorX), cy = (float)(kMapY + g_settings.cursorY);
    Rect(cx - 2.0f, cy, 5.0f, 1.0f, 0xffffffff);
    Rect(cx, cy - 2.0f, 1.0f, 5.0f, 0xffffffff);
    FlushRects();

    int textX = kMapX + g_settings.cursorX + 4, textY = kMapY + g_settings.cursorY + 4;
    if (cursorType >= 0)
        Text(textX, textY, kTypeNames[cursorType]);
    Text(textX, textY + kLine, "0x");
    Textf(textX + 16, textY + kLine, "%x", cursorAddress);
    Textf(textX + 16, textY + 2 * kLine, "%u", cursorSize);
}

// Mem_CountBlocks's totals, under Debug_DrawStats' heading
void DrawMemStats(void) {
    if (PtrHeap == 0)
        return;
    uint32_t freeCount = 0, freeBytes = 0, usedCount = 0, usedBytes = 0;
    MemBlock *end = HeapEnd();
    for (MemBlock *block = HeapStart(); block < end; block = NextBlock(block)) {
        if (block->isFree) {
            freeCount++;
            freeBytes += block->size;
        } else {
            usedCount++;
            usedBytes += block->size;
        }
        if (block->size == 0)
            break;
    }
    // Debug_DrawStats' places on a 640-pixel line
    Text(442, 0xda, "FC    FB    AC    AB");
    Textf(426, 0xe4, "%u", freeCount);
    Textf(458, 0xe4, "%u", freeBytes);
    Textf(530, 0xe4, "%u", usedCount);
    Textf(570, 0xe4, "%u", usedBytes);
}

// ---------------------------------------------------------------------------------------------------------------
// The textures
// ---------------------------------------------------------------------------------------------------------------

bool IsTexture(const TextureInfo *tex) {
    return tex != NULL && tex != (TextureInfo *)-1 && tex->magic == TEXINFO_MAGIC && tex->baseIdx != 0;
}

uint32_t TextureBytes(const TextureInfo *tex) {
    return tex->frameBytes * (uint32_t)tex->numFrames;
}

// psiCreateMapTextures' choice of format
const char* FormatName(const TextureInfo *tex) {
    switch (tex->formatType) {
    case 0:
        if (tex->bitsPerPixel == 4)
            return "A4R4G4B4";
        return tex->bitsPerPixel == 8 ? "A8R8G8B8" : "A1R5G5B5";
    case 2: return "DXT2";
    case 3: return "DXT3";
    case 4: return "DXT4";
    case 5: return "DXT5";
    default: return "DXT1";
    }
}

// Debug_ShowTextures: the textures of their own ("DT" duplicates left out) largest first, keeping the order they
// were loaded in among equals; twelve rows of 64 pixels from the chosen one, each texture swaying sideways
void DrawTextures(void) {
    static int order[ARRAY_SIZE(Tex)];
    int count = 0;
    for (int i = 1; i < NumXboxTexLoaded && i < (int)ARRAY_SIZE(Tex); i++)
        if (IsTexture(Tex[i]))
            order[count++] = i;
    std::stable_sort(order, order + count,
                     [](int a, int b) { return TextureBytes(Tex[a]) > TextureBytes(Tex[b]); });

    int first = g_settings.showTextures;
    for (int row = 0; row < kTexturesShown && first + row < count; row++) {
        int index = order[first + row];
        const TextureInfo *tex = Tex[index];
        int y = row * 64;
        float x = (float)(sin((g_sway + 96.0f + (float)y) * 3.1415925f / 180.0f) * 64.0 + (double)(row * 48 + 128));
        SPRITE_DRAW sprite;
        memset(&sprite, 0, sizeof(sprite));
        sprite.textureIndex = (uint32_t)index;
        sprite.x = x;
        sprite.y = (float)(y + 32);
        sprite.width = (float)tex->width;
        sprite.height = (float)tex->height;
        sprite.u1 = (short)tex->width;
        sprite.v1 = (short)tex->height;
        sprite.clrR = sprite.clrG = sprite.clrB = sprite.clrA = 0xff;
        psiDrawSprites(&sprite, 1);
        g_sway += 0.07f;
        if (g_sway > 360.0f)
            g_sway -= 360.0f;

        Textf(32, y + 32, "Tex %d", index);
        Text(32, y + 40, "Mips:");
        Textf(80, y + 40, "%u", tex->levels);
        Textf(32, y + 48, "%u", tex->width);
        Textf(64, y + 48, "%u", tex->height);
        Textf(32, y + 56, "%u", TextureBytes(tex));
        Text(32, y + 64, FormatName(tex));
        FlushText();
    }
}

// Debug_DrawSafeFrame: a one-pixel rectangle, x 8 to 632 and y 40 to 440
void DrawSafeFrame(void) {
    if (switch_SHOW_COORDS == 0)
        return;
    const uint32_t white = 0x80ffffff;
    Rect(8.0f, 40.0f, 624.0f, 1.0f, white);
    Rect(8.0f, 440.0f, 624.0f, 1.0f, white);
    Rect(8.0f, 40.0f, 1.0f, 400.0f, white);
    Rect(632.0f, 40.0f, 1.0f, 400.0f, white);
}

} // namespace

void GcDebugOverlays_Draw(void) {
    if (!g_settings.read)
        ReadSettings();
    if (!g_settings.memMap && !g_settings.memStats && !g_settings.safeFrame && g_settings.showTextures < 0)
        return;
    ReadKeys();
    if (g_settings.showTextures >= 0)
        DrawTextures();
    if (g_settings.memMap)
        DrawMemMap();
    if (g_settings.memStats)
        DrawMemStats();
    if (g_settings.safeFrame)
        DrawSafeFrame();
    FlushRects();
    FlushText();
    g_frames++;
}
