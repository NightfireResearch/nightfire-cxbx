#include "view.h"
#include "../engine/psiSprite.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../engine/viewer.h"
#include "../engine/Direct3D/GraphicsSystem.h"
#include <math.h>

#include <stdio.h>

// AUTOGEN
void View_SetDrawInViews(obj_tag* o, short views);

// Visible to everyone
// AUTOINJECT
void View_SetDrawInAllViews(obj_tag* o) {
    View_SetDrawInViews(o, 0x1f);
}

// Visible to nobody
// AUTOINJECT
void View_SetDrawInNoViews(obj_tag* o) {
    View_SetDrawInViews(o, 0);
}

// Visible to everyone except the specified player (eg 3rd person view of a gun model)
// AUTOINJECT
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum) {
    char views = 0x1f & (~(1 << (playerNum & 0x1f)));
    View_SetDrawInViews(param_1, views);
}

// Visible to the specified player only (eg 1st person view of a gun model)
// AUTOINJECT
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum) {
    char views = (1 << (playerNum & 0x1f));
    View_SetDrawInViews(param_1, views);
}

// AUTOINJECT
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix) {
  
  float cos_rx = cosf(rotation->x);
  float sin_rx = sinf(rotation->x);
  float cos_ry = cosf(rotation->y);
  float sin_ry = sinf(rotation->y);
  float cos_rz = cosf(rotation->z);
  float sin_rz = sinf(rotation->z);

  matrix->m[0] = (cos_rz * cos_ry);
  matrix->m[1] = sin_rz;
  matrix->m[2] = -(cos_rz * sin_ry);
  matrix->m[4] = (sin_ry * sin_rx - sin_rz * cos_ry * cos_rx);
  matrix->m[5] = (cos_rz * cos_rx);
  matrix->m[6] = (cos_ry * sin_rx + sin_rz * sin_ry * cos_rx);
  matrix->m[8] = (sin_ry * cos_rx + sin_rz * cos_ry * sin_rx);
  matrix->m[9] = -(cos_rz * sin_rx);
  matrix->m[10] = (cos_ry * cos_rx - sin_rz * sin_ry * sin_rx);
  matrix->m[0xc] = position->x;
  matrix->m[0xd] = position->y;
  matrix->m[0xe] = position->z;
  return;
}

// AUTOINJECT
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix) {
    View_RotTransMatrix(rotation, position, matrix);
    matrix->m[0] *= scale->x;
    matrix->m[1] *= scale->x;
    matrix->m[2] *= scale->x;
    matrix->m[4] *= scale->y;
    matrix->m[5] *= scale->y;
    matrix->m[6] *= scale->y;
    matrix->m[8] *= scale->z;
    matrix->m[9] *= scale->z;
    matrix->m[10] *= scale->z;
}

// AUTOINJECT
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale) {
    _MATRIX mtx;
    View_RotTransScaleMatrix(rotation, translation, scale, &mtx);
    psiDrawObjectMatrix(celglist, &mtx);
}

#define object_display_mask U32_AT(0x0029e80c)
#define GfxList U32_AT(0x0029d79c)
// XBE_GLOBAL(0x001dfa18, 0x4)
uint32_t switch_ForceDrawAll;

// AUTOGEN
void Vision_Init_Portal_Recurse(viewer_tag* viewer);
// AUTOGEN
void Vision_AddCelToDraw(cel_tag* cel, ushort param_2);
// AUTOGEN
void vision_generate_display_list(viewer_tag* viewer);
// AUTOGEN
void vision_GetCamPos(_VECTOR* camPos);
// AUTOGEN
void Vision_Portal_Recurse(viewer_tag* viewer);


// XBE_GLOBAL(0x0029dbf0, 0xc)
#define CamPos (*(_VECTOR*)(0x0029dbf0))

// Cannot autoinject - custom calling convention
// Only used from within View_CaptureScene, so not a problem
// UNINJECTABLE
void View_CaptureSceneSub(byte mask, viewer_tag* viewer) {
    if(viewer == NULL)
        return;

    object_display_mask = (1 << (mask & 0x1f));
    GfxList = NULL;

    for(cel_tag* cel = viewer->world->firstCel; cel != NULL; cel = cel->nextCel) {
        cel->addedToDraw = 0;
    }
    
    Vision_Init_Portal_Recurse(viewer);

    if(!switch_ForceDrawAll) {
        Vision_Portal_Recurse(viewer);
        vision_GetCamPos(&CamPos);
        return;
    }

    for(cel_tag* cel = viewer->world->firstCel; cel != NULL; cel = cel->nextCel) {
        Vision_AddCelToDraw(cel, 0);
    }

    vision_generate_display_list(viewer);
    vision_GetCamPos(&CamPos);
}

// Can't generate automatically - custom calling convention
void __declspec(naked) View_AddCels(viewer_tag* viewer) {
    // Custom wrapper - viewer pointer is expected in ESI by the original function, which is located at 0x000daa00.
    // ESI is callee-saved, so it is restored afterwards rather than handed back holding the viewer - see
    // SP_LoadScript for what a tail-jmp here costs when the caller keeps something else in ESI.
    _asm {
        push esi
        mov esi, [esp + 8]
        push esi
        mov eax, 0x000DAA00
        call eax
        add esp, 4
        pop esi
        ret
    }
}

// AUTOGEN
void View_AddForcedObjects(viewer_tag* viewer);

#define Tots U32_AT(0x0029e800)
#define DAT_0029e804 U32_AT(0x0029e804)
#define DAT_0029e808 U32_AT(0x0029e808)

void _View_CaptureScene(viewer_tag *viewer) {
    viewer->field11_0x14 = 0;
    viewer->field12_0x16 = 0;
    viewer->field13_0x18 = 0;
    if (viewer->field25_0x29 && viewer->field2_0x8) {

        View_CaptureSceneSub(viewer->idx, viewer);
        View_AddCels(viewer);
        View_AddForcedObjects(viewer);

        // Log statistics
        Tots += viewer->field11_0x14;
        DAT_0029e804 += viewer->field12_0x16;
        DAT_0029e808 += viewer->field13_0x18; // This handles first-person weapon models intersecting level geometry. Non-zero value causes camera 5 to be drawn in Game_Draw
    }
}

// AUTOLTCG
void __declspec(naked) View_CaptureScene(viewer_tag* viewer) {
    // The original function is provided with its parameter in EAX, but we need to call our reimplementation
    // which doesn't have custom calling convention
    _asm {
        push eax
        call _View_CaptureScene
        add esp, 4
        ret
    }

}

// AUTOGEN
void View_AddSkyObj(ushort param_1,celglist_tag *param_2,_VECTOR *param_3,_VECTOR *param_4,obj_tag *param_5,char param_6,ushort param_7,ushort param_8,char param_9);


#define SprCnt U32_AT(0x0029e810)

// A pointer to memory address 0x0029dc00 which contains 64 SPRITE_DRAW instances (not pointers)
// We must preserve the original type for correct code generation
#define SprBuffList (*(SPRITE_DRAW(*)[64])0x0029dc00)

static_assert(ARRAY_SIZE(SprBuffList) == 64, "Sprite buffer number of entries incorrect");
static_assert(sizeof(SPRITE_DRAW) == 0x30, "Sprite size incorrect");

#ifdef _MSC_VER
#include <intrin.h>
#define CALLER_ADDRESS() ((uint32_t)(uintptr_t)_ReturnAddress())
#else
#define CALLER_ADDRESS() ((uint32_t)(uintptr_t)__builtin_return_address(0))
#endif

// The glyph call in __Font_DrawText (0x00069c50): the return address of its View_AddSprite, which copies the
// font's sprite template into the slot straight after. (Its other call, at 0x00069e0a, is an image placed in
// the text, which is not a glyph.) So a glyph's texture is known by the next call, which is when the font's
// glyph boxes can go out beside the dumped texture (DumpTextures in settings.ini).
#define FONT_GLYPH_CALL_RETURN 0x0006a14du
static int s_pendingGlyph = -1;
static const uint8_t *s_pendingFont = NULL;   // the font that glyph is from - by the next call it may not be current

// A sprite's textureIndex selects one of the game's texture records; psiDrawSprites binds one of the record's
// animation frames (count at +0x24, slots from +0x54) through d3dSetTextureStage0, which takes a slot in the
// D3D texture table, Gfx.textures - and a slot's Xbox texture header is the slot itself (see D3DTextureSlotRaw in
// engine/Direct3D/GraphicsSystem.h). Gfx is ours, so the table is not at the game's address any more.
#define TextureRecords         ((const uint8_t *const *)0x002abe80)

// The current font (__Font_DrawText's): its first and last character at +8 and +0xa, the number of glyphs at
// +0xc, and at +0x10 a table of 24-byte glyph records sorted by character, whose layout FUN_00069a40 and
// __Font_DrawText read - the box in the sheet at +0 (u, v, width, height), the vertical offset at +8, the
// extra advance at +0xa and the character at +0x14. (The lookup's binary search starts one past the end;
// the character range test before it is what keeps it from ever reading there.)
#define CurrentFont (*(const uint8_t *const *)0x001f64b0)

// With DumpTextures on, a font's glyph boxes go out beside its texture once, for making a replacement.
static void DescribeFontOnce(const uint8_t *font, const void *xboxTexture) {
    static const void *described[32];
    static int describedCount = 0;
    for (int i = 0; i < describedCount; i++)
        if (described[i] == xboxTexture) return;
    if (font == NULL || describedCount >= 32)
        return;
    uint16_t first = *(const uint16_t *)(font + 0x8), last = *(const uint16_t *)(font + 0xa);
    uint32_t count = *(const uint32_t *)(font + 0xc);
    const uint8_t *records = *(const uint8_t *const *)(font + 0x10);
    if (records == NULL || count == 0 || count > 1024)
        return;
    static D3D9FontGlyph glyphs[1024];
    int kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *g = records + i * 24;
        uint16_t code = *(const uint16_t *)(g + 0x14);
        if (code < first || code > last)
            continue;
        D3D9FontGlyph *out = &glyphs[kept++];
        out->u = *(const uint16_t *)(g + 0x0);
        out->v = *(const uint16_t *)(g + 0x2);
        out->w = *(const uint16_t *)(g + 0x4);
        out->h = *(const uint16_t *)(g + 0x6);
        out->yOffset = *(const int16_t *)(g + 0x8);
        out->advance = *(const int16_t *)(g + 0xa);
        out->code = code;
    }
    if (D3D9_DescribeFont(xboxTexture, glyphs, kept))
        described[describedCount++] = xboxTexture;
}

static void NoteGlyphTexture(const SPRITE_DRAW *glyph, const uint8_t *font) {
    if (!D3D9_DumpingTextures() || glyph->textureIndex >= 0x1000)
        return;
    const uint8_t *record = TextureRecords[glyph->textureIndex];
    if (record == NULL)
        return;
    uint32_t frames = *(const uint32_t *)(record + 0x24);
    if (frames == 0 || frames > 8)
        frames = 1;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t slot = *(const uint32_t *)(record + 0x54 + 4 * i);
        if (slot == 0 || slot >= GFX_TEXTURE_SLOTS)
            continue;
        const void *header = &Gfx.textures[slot];
        DescribeFontOnce(font, header);
    }
}

// AUTOINJECT
SPRITE_DRAW * View_AddSprite(ushort someNum) {

    if (s_pendingGlyph >= 0) {
        NoteGlyphTexture(&SprBuffList[s_pendingGlyph], s_pendingFont);
        s_pendingGlyph = -1;
    }

    // Only ever appears to be called with someNum == 0.
    // Potentially intended to allocate multiple at the same time, but not correctly implemented (would need to increment SprCnt by a variable amount rather than just 1)?
    NF_ASSERT(someNum == 0, "Assumed View_AddSprite parameter always 0");

    // If the buffer is too full to allow us to release this many sprites, flush it first
    if((SprCnt + someNum) >= (ARRAY_SIZE(SprBuffList)-1)) { // TODO: Check for off by one errors here?
        psiDrawSprites(SprBuffList, SprCnt);
        SprCnt = 0;
    }

    // Return the buffer, starting at the number of sprites already buffered
    uint32_t caller = CALLER_ADDRESS();
    if (caller == FONT_GLYPH_CALL_RETURN) {
        s_pendingGlyph = (int)SprCnt;
        s_pendingFont = CurrentFont;
    }
    return &SprBuffList[SprCnt++];
}

// AUTOINJECT
short View_3DPoint2Screen(_VECTOR *vecIn, _VECTOR *vecOut, ushort viewerNum) {

    viewer_tag *viewer = glb_viewer[viewerNum];

    if(vecIn == NULL)
        return 0;

    if(vecOut == NULL)
        return 0;

    if(viewer == NULL)
        return 0;

    _VECTOR direction;
    _VECTOR tmp;

    Vec_Subtract(vecIn, &viewer->pos, &direction);

    ApplyMatrixLV(&viewer->worldMatrix, &direction, &tmp);

    // Our return value will be negative if the point is behind the viewer, positive if it is in front (ie z component is negative)
    short returnValue = 1;
    if(tmp.z < 0.0f) {
        returnValue = -1;
        tmp.z = -tmp.z;
    }

    vecOut->x = viewer->width -  (viewer->width * 0.5f + (1.0f / tmp.z) * tmp.x * viewer->projectionScaleX);
    vecOut->y =                   viewer->height * 0.5f + (1.0f / tmp.z) * tmp.y * viewer->projectionScaleY;
    vecOut->z = tmp.z;

    return returnValue;
}
