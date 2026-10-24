#include "Font.h"

#include "../engine/Text.h"
#include "../engine/psiSprite.h"
#include "../engine/psiGraphics.h"
#include "../game/view.h"
#include "../util/hashtable.h"

#include <string.h>

#include "FontData.inc"

#pragma fp_contract(off)

// Formats several places share: 0xff and a font, then options (0xfe an alignment, 0xfc and 0xfb a scale)
// XBE_GLOBAL(0x0015eb18, 0x5) readonly relocate
char FontFormat2Centred[] = "\xff\x02\xfe\x03";
// XBE_GLOBAL(0x00161954, 0x5) readonly relocate
char FontFormat1Centred[] = "\xff\x01\xfe\x03";
// XBE_GLOBAL(0x0015eb20, 0x3) readonly relocate
char FontFormat2[] = "\xff\x02";
// XBE_GLOBAL(0x0016167c, 0x3) readonly relocate
char FontFormat3[] = "\xff\x03";
// XBE_GLOBAL(0x0016197c, 0x7) readonly relocate
char FontFormat1Scaled[] = "\xff\x01\xfc""d\xfb""d";
// XBE_GLOBAL(0x0015eb5c, 0x7) readonly relocate
char FontFormat2Scaled[] = "\xff\x02\xfc""d\xfb""d";
// XBE_GLOBAL(0x00161984, 0x7) readonly relocate
char FontFormat3Scaled[] = "\xff\x03\xfc""d\xfb""d";
// XBE_GLOBAL(0x00161964, 0x9) readonly relocate
char FontFormat2CentredScaled[] = "\xff\x02\xfe\x03\xfc""d\xfb""d";
// The letters a font's height is measured by
// XBE_GLOBAL(0x0015eb24, 0x35) readonly relocate
char FontAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
// XBE_GLOBAL(0x0015d308, 0x1) readonly relocate
char FontEmptyString[] = "";
// XBE_GLOBAL(0x00160d8c, 0x2) readonly relocate
char FontSpaceString[] = " ";

// The fonts, by Font_ParseFormat's font number; 1 is the default
// XBE_GLOBAL(0x0017bd90, 0x28)
FONTTABLE_tag FontTable[5] = {
    {.font = (FONT_tag *)0xfc},
    {.texture = HT_Texture_NFont2, .font = &Font1},
    {.texture = HT_Texture_SerpLight, .font = &Font2},
    {.texture = HT_Texture_Medium, .font = &Font3},
};
#define NUM_SPECIAL_CHARS 23
// XBE_GLOBAL(0x0017bdc0, 0x228) readonly relocate
SPECIALCHAR_tag SpecialChars[NUM_SPECIAL_CHARS] = {
    {'A', {0, 0, 0}, HT_Texture_buttons, 256, 128, 1, 0, 17, 22, 17, 20},
    {'B', {0, 0, 0}, HT_Texture_buttons, 256, 128, 25, 0, 17, 22, 17, 20},
    {'C', {0, 0, 0}, HT_Texture_buttons, 256, 128, 92, 0, 17, 22, 17, 20},
    {'D', {0, 0, 0}, HT_Texture_buttons, 256, 128, 116, 0, 17, 22, 17, 20},
    {'E', {0, 0, 0}, HT_Texture_buttons, 256, 128, 54, 25, 21, 21, 17, 20},
    {'F', {0, 0, 0}, HT_Texture_buttons, 256, 128, 77, 25, 21, 21, 17, 20},
    {'H', {0, 0, 0}, HT_Texture_buttons, 256, 128, 26, 48, 22, 22, 17, 20},
    {'I', {0, 0, 0}, HT_Texture_buttons, 256, 128, 198, 59, 7, 7, 7, 7},
    {'J', {0, 0, 0}, HT_Texture_buttons, 256, 128, 176, 57, 16, 12, 16, 12},
    {'K', {0, 0, 0}, HT_Texture_buttons, 256, 128, 142, 25, 13, 20, 13, 20},
    {'L', {0, 0, 0}, HT_Texture_buttons, 256, 128, 52, 48, 14, 23, 13, 20},
    {'M', {0, 0, 0}, HT_Texture_buttons, 256, 128, 156, 25, 13, 20, 13, 20},
    {'N', {0, 0, 0}, HT_Texture_buttons, 256, 128, 171, 25, 13, 20, 13, 20},
    {'O', {0, 0, 0}, HT_Texture_buttons, 256, 128, 185, 25, 13, 20, 13, 20},
    {'P', {0, 0, 0}, HT_Texture_buttons, 256, 128, 198, 25, 13, 20, 13, 20},
    {'Q', {0, 0, 0}, HT_Texture_buttons, 256, 128, 212, 25, 13, 20, 13, 20},
    {'R', {0, 0, 0}, HT_Texture_buttons, 256, 128, 70, 48, 14, 23, 13, 20},
    {'S', {0, 0, 0}, HT_Texture_buttons, 256, 128, 1, 24, 25, 22, 25, 22},
    {'T', {0, 0, 0}, HT_Texture_buttons, 256, 128, 29, 24, 25, 22, 25, 22},
    {'V', {0, 0, 0}, HT_Texture_buttons, 256, 128, 1, 48, 22, 22, 17, 20},
    {'W', {0, 0, 0}, HT_Texture_buttons, 256, 128, 210, 1, 22, 22, 17, 20},
    {'X', {0, 0, 0}, HT_Texture_buttons, 256, 128, 49, 0, 17, 22, 17, 20},
    {'Y', {0, 0, 0}, HT_Texture_buttons, 256, 128, 71, 0, 17, 22, 17, 20},
};
// The font in use
// XBE_GLOBAL(0x001f64b0, 0x4)
FONT_tag *Font;
// Every glyph's sprite, filled in and copied to the sprite list
extern SPRITE_DRAW CharInfo;
// XBE_GLOBAL(0x001f64e8, 0x40)
PRINTINFO_tag PInfo;
// The line Font_WordWrapString hands back for a line break straight after the last one
#define EmptyLine FontEmptyString

// __Font_DrawText's statics (PS2 names)
// XBE_GLOBAL(0x001f6528, 0x4)
uint32_t i_133;  // characters read
// XBE_GLOBAL(0x001f652c, 0x4)
float fStartX_132;  // where a new line starts
// XBE_GLOBAL(0x001f6530, 0x4)
float y_131;
// XBE_GLOBAL(0x001f6534, 0x4)
float x_130;
// XBE_GLOBAL(0x001f6538, 0x4)
CHARDATA_tag *fd_129;
// XBE_GLOBAL(0x001f653c, 0x8)
GPOINT Pos_128;  // the pen
// XBE_GLOBAL(0x001f6544, 0x8)
GPOINT Scale_127;
// XBE_GLOBAL(0x001f654c, 0x8)
GPOINT Size_126;  // what it measured, and returns
// Font_DrawText's
// XBE_GLOBAL(0x001f6554, 0x8)
GPOINT TScale_138;
// XBE_GLOBAL(0x001f655c, 0x8)
GPOINT Size_137;

// The control codes of a format
#define FMT_COLOUR      0xfa    // r g b, and a byte not read
#define FMT_ALIGN       0xfe
#define FMT_FONT        0xff

// The characters a text treats specially
#define CHAR_SPACE      0x20
#define CHAR_NBSP       0xa0
#define CHAR_SYMBOL     '~'     // a button symbol, the next character names it
#define CHAR_COLOUR     0xba    // the first switches to the highlight colour, the second back
#define CHAR_QUOTE      0x92    // drawn as '
#define CHAR_ELLIPSIS   0x85    // drawn as .
#define CHAR_TRADEMARK  0x99    // drawn as a half-size T and M

// The glyph of c, by binary search of the font's glyphs; NULL outside the font's range. A character between two
// glyphs gets the one below it. The search does not stop at the table's end (as the original).
CHARDATA_tag* Font_FindGlyph(uchar c) {
    FONT_tag *font = Font;
    if (c < font->first || c > font->last)
        return NULL;
    CHARDATA_tag *chars = font->chars;
    int lo = 0;
    int hi = (int)font->count;
    do {
        int mid = (hi + lo) >> 1;
        if (c < chars[mid].code)
            hi = mid - 1;
        else if (c > chars[mid].code)
            lo = mid + 1;
        else
            return &chars[mid];
    } while (lo <= hi);
    return &chars[hi];
}

// The kerning of c after prev, from prev's list. The list is in ascending order and the search stops at the
// first pair past c, so only a pair the list starts with ever counts (as the original).
float Font_GetKernAdjust(char prev, char c) {
    CHARDATA_tag *glyph = Font_FindGlyph(prev);
    if (glyph == NULL)
        return 0.0f;
    int code = c;
    if (code < Font->first || code > Font->last)
        return 0.0f;
    if (glyph->kernCount == 0 || glyph->kern == NULL)
        return 0.0f;
    for (uint i = 0; i < glyph->kernCount; i++) {
        if (code > glyph->kern[i].second)
            return 0.0f;
        if (code == glyph->kern[i].second)
            return glyph->kern[i].adjust;
    }
    return 0.0f;
}

// Applies a format: the default font, then its codes. Only the low byte of the flags is cleared for an alignment.
// AUTOINJECT
void Font_ParseFormat(const char *format, uint *flags) {
    Font = FontTable[1].font;
    if (format == NULL)
        return;
    const char *p = format;
    while (*p != '\0') {
        uchar code = (uchar)*p++;
        switch (code) {
            case FMT_COLOUR:
                CharInfo.clrR = (uchar)*p++;
                CharInfo.clrG = (uchar)*p++;
                CharInfo.clrB = (uchar)*p;
                p += 2;
                break;
            case 0xfb:
            case 0xfc:
                p++;
                break;
            case FMT_ALIGN:
                *(uchar *)flags = 0;
                if (*p == 2)
                    *flags |= FONT_RIGHT;
                else if (*p == 3)
                    *flags |= FONT_CENTRE;
                p++;
                break;
            case FMT_FONT: {
                FONTTABLE_tag *entry = &FontTable[(signed char)*p];
                Font = entry->font;
                if ((*flags & FONT_MEASURE_ONLY) == 0) {
                    texDataEntry *texture = (texDataEntry *)hashtable_getitem(entry->texture);
                    if (texture != NULL) {
                        CharInfo.textureIndex = texture->texIdx;
                        CharInfo.invHeight = 1.0f / (ushort)texture->height;
                        CharInfo.invWidth = 1.0f / (ushort)texture->width;
                    }
                }
                p++;
                break;
            }
            default:
                break;
        }
    }
}

// The pen moved on to pen, and the measured width out to its run when it is further. The run is taken from the
// pen before it is rounded to a float, as the original keeps it on the x87.
static void Font_MovePen(double pen) {
    Pos_128.x = (float)pen;
    double run = pen - x_130;
    if (!(Size_126.x > run))
        Size_126.x = (float)run;
}

// Draws (or with FONT_MEASURE_ONLY only measures) up to count characters of text from (x, y) in the font in use
// and returns its size. With fit, how many characters were read while the width was within width. Our name: the
// C++ behind __Font_DrawText, whose colour (ECX) and scale (EAX) are in registers. Its third and fourth arguments
// are not read.
GPOINT* Font_DrawGlyphs(uint colour, const GPOINT *scale, float x, float y, uint unused3, uint unused4,
                        const char *text, const char *format, uint flags, int count, float width, uint *fit) {
    Size_126.x = 0.0f;
    Size_126.y = 0.0f;
    Scale_127.x = 1.0f;
    Scale_127.y = 1.0f;
    Pos_128.x = x;
    Pos_128.y = y;
    fd_129 = NULL;
    x_130 = x;
    y_131 = y;
    fStartX_132 = x;
    i_133 = 0;
    int trademark = 0;  // 1 while its T is drawn, 2 its M
    int colourSwitches = 0;
    if (Font == NULL)
        return &Size_126;
    if (scale != NULL)
        Scale_127 = *scale;

    char prev = '\0';
    while (*text != '\0' && count != 0) {
        count--;
        i_133++;
        char c = *text++;
        // The trademark goes round twice, T then M, with the same character
        do {
            switch ((uchar)c) {
                case '\n':
                    Pos_128.x = fStartX_132;
                    Pos_128.y = (float)((((double)Font->chars[0].h + Font->lineGap) + 1.0f) * Scale_127.y +
                                        Pos_128.y);
                    break;

                case CHAR_SPACE:
                case CHAR_NBSP:
                    Pos_128.x = (float)((double)Font->space * Scale_127.x * 1.25 + Pos_128.x);
                    break;

                case CHAR_COLOUR:
                    if (colourSwitches == 0) {
                        colourSwitches = 1;
                        colour = TEXT_HIGHLIGHT;
                    } else {
                        if (colourSwitches == 1)
                            colour = TEXT_NORMAL;
                        colourSwitches++;
                    }
                    break;

                case CHAR_SYMBOL: {
                    SPECIALCHAR_tag *symbol = NULL;
                    float advance = 0.0f;
                    for (uchar i = 0; i < NUM_SPECIAL_CHARS; i++) {
                        if (*text == SpecialChars[i].code) {
                            advance = SpecialChars[i].width;
                            symbol = &SpecialChars[i];
                            break;
                        }
                    }
                    // The symbol's letter is not counted
                    text++;
                    count++;
                    if (symbol == NULL)
                        break;
                    if ((flags & FONT_MEASURE_ONLY) == 0) {
                        SPRITE_DRAW *spr = View_AddSprite(0);
                        texDataEntry *texture = (texDataEntry *)hashtable_getitem(symbol->texture);
                        spr->textureIndex = texture != NULL ? texture->texIdx : 0;
                        spr->invWidth = texture != NULL ? 1.0f / (ushort)texture->width : 1.0f;
                        spr->invHeight = texture != NULL ? 1.0f / (ushort)texture->height : 1.0f;
                        CharInfo.clrA = (uchar)colour;
                        spr->clrB = 0x7f;
                        spr->clrG = 0x7f;
                        spr->clrR = 0x7f;
                        spr->clrA = CharInfo.clrA;
                        spr->width = symbol->width * Scale_127.x;
                        spr->height = symbol->height * Scale_127.y;
                        spr->x = Pos_128.x;
                        spr->y = (float)(Pos_128.y -
                                         ((double)Font->chars[0].h * Scale_127.y + symbol->height) * 0.5f);
                        spr->u0 = symbol->u0;
                        spr->v0 = symbol->v0;
                        spr->u1 = symbol->u1;
                        spr->v1 = symbol->v1;
                        spr->flags = 0;
                    }
                    Font_MovePen((double)Pos_128.x + advance);
                    break;
                }

                default: {
                    if ((uchar)c == CHAR_QUOTE)
                        c = '\'';
                    else if ((uchar)c == CHAR_ELLIPSIS)
                        c = '.';
                    fd_129 = Font_FindGlyph(c);
                    if (fd_129 == NULL)
                        goto next;  // not even the fit is counted
                    if (fd_129->code == CHAR_TRADEMARK) {
                        if (trademark == 0)
                            fd_129 = Font_FindGlyph('T');
                        else if (trademark == 1)
                            fd_129 = Font_FindGlyph('M');
                        trademark++;
                    }
                    CHARDATA_tag *glyph = fd_129;
                    float w = glyph->w * Scale_127.x;
                    float h = glyph->h * Scale_127.y;
                    if (trademark != 0) {
                        w = w * 0.5f;
                        h = h * 0.5f;
                    }
                    Pos_128.x = (float)((double)Font_GetKernAdjust(prev, c) * Scale_127.x + Pos_128.x);
                    Pos_128.x = (float)((prev != '\0' ? (double)glyph->pre * Scale_127.x : 0.0) + Pos_128.x);
                    if ((flags & FONT_MEASURE_ONLY) == 0) {
                        CHARDATA_tag *first = &Font->chars[0];
                        CharInfo.x = Pos_128.x;
                        CharInfo.width = w;
                        CharInfo.height = h;
                        CharInfo.y = (float)(((double)glyph->yOffset * Scale_127.y + Pos_128.y) -
                                             (double)(first->yOffset + first->h) * Scale_127.y);
                        CharInfo.u0 = glyph->u;
                        CharInfo.u1 = glyph->w;
                        CharInfo.v0 = glyph->v;
                        CharInfo.v1 = glyph->h;
                        CharInfo.clrR = (uchar)(colour >> 24);
                        CharInfo.clrG = (uchar)(colour >> 16);
                        CharInfo.clrB = (uchar)(colour >> 8);
                        CharInfo.clrA = (uchar)colour;
                        *View_AddSprite(0) = CharInfo;
                        glyph = fd_129;
                    }
                    Font_MovePen((double)glyph->post * Scale_127.x + Pos_128.x + w);
                    break;
                }
            }

            if (Size_126.x <= width && fit != NULL)
                *fit = i_133;
            if (trademark == 2)
                trademark = 0;
        } while (trademark != 0);
    next:
        prev = c;
    }

    Size_126.y = (float)(((double)Pos_128.y - y_131) +
                         (((double)Font->chars[0].h * Scale_127.y + Font->lineGap) + 1.0f));
    return &Size_126;
}

// The text in its format, aligned in width, with the copies the flags ask for drawn first, each with the low byte
// of the flags and FONT_COPY. Returns the size of the last draw.
// AUTOINJECT
GPOINT* Font_DrawText(float x, float y, uint colour, uint shadow, uint outline, const char *text, const char *format,
                      const GPOINT *scale, uint flags, int count, float width) {
    Size_137.y = 0.0f;
    Size_137.x = 0.0f;
    if (count == 0 || text == NULL || format == NULL)
        return &Size_137;
    if (scale != NULL) {
        TScale_138 = *scale;
    } else {
        TScale_138.x = 1.0f;
        TScale_138.y = 1.0f;
    }
    Font_ParseFormat(format, &flags);
    if (Font == NULL)
        return &Size_137;

    if (flags & FONT_CENTRE)
        x = x - width * 0.5f;
    if (flags & FONT_RIGHT)
        x = x - width;

    uint copy = (flags & 0xf7) | FONT_COPY;
    if (flags & FONT_SHADOW) {
        Font_DrawGlyphs(shadow, &TScale_138, x + 1.0f, y + 1.0f, 0, 0, text, format, copy, count, -1.0f, NULL);
    } else if (flags & FONT_SHADOW_RING) {
        float up = y - 1.0f;
        float left = x - 1.0f;
        Font_DrawGlyphs(shadow, &TScale_138, left, up, 0, 0, text, format, copy, count, -1.0f, NULL);
        float down = y + 1.0f;
        Font_DrawGlyphs(shadow, &TScale_138, left, down, 0, 0, text, format, copy, count, -1.0f, NULL);
        float right = x + 1.0f;
        Font_DrawGlyphs(shadow, &TScale_138, right, up, 0, 0, text, format, copy, count, -1.0f, NULL);
        Font_DrawGlyphs(shadow, &TScale_138, right, down, 0, 0, text, format, copy, count, -1.0f, NULL);
    }
    if (flags & FONT_OUTLINE)
        Font_DrawGlyphs(outline, &TScale_138, x - 1.0f, y - 1.0f, 0, 0, text, format, copy, count, -1.0f, NULL);
    return Font_DrawGlyphs(colour, &TScale_138, x, y, shadow, 0, text, format, flags, count, -1.0f, NULL);
}

// The next line of *text that fits in width pixels (at scale 1), in a string from the heap; *text moves on to the
// rest, or NULL at the end. A line breaks after the last space or tab that fitted, after a line break (which it
// keeps), or, with no space, before the character that did not fit. NULL when nothing is left. Spaces add no width.
// The length is not read.
// AUTOINJECT
char* Font_WordWrapString(char **text, int length, const char *format, uint width) {
    char *result = NULL;
    char *begin = *text;
    if (begin == NULL)
        return NULL;

    PInfo.unknown00 = 0;
    PInfo.unknown02 = 0;
    PInfo.unknown06 = 0xffff;
    PInfo.unknown08[0] = 1.0f;
    PInfo.unknown08[1] = 1.0f;
    for (int i = 0; i < 6; i++)
        PInfo.unknown10[i] = 0.0f;
    for (int i = 0; i < 4; i++)
        PInfo.unknown2c[i] = 0;
    PInfo.unknown30 = 0;
    PInfo.unknown34 = 0;
    PInfo.format = format;
    PInfo.unknown3c = 0;
    PInfo.unknown28 = 0.0f;
    PInfo.unknown04 = 0;
    uint flags = 0;
    Font_ParseFormat(format, &flags);

    char *line = *text;
    char *cut = begin;
    uint n = 0;
    char prev = '\0';
    float extent = 0.0f;
    if (*line != '\0') {
        float limit = (float)width;
        for (;;) {
            if (!(extent < limit))
                goto done;
            char c = line[n++];
            switch ((uchar)c) {
                case '\t':
                case ' ':
                    cut = line + n;
                    break;
                case '\n':
                    cut = line + n;
                    result = EmptyLine;
                    // Tests the line break itself, so never steps back
                    if (line[n - 1] == ' ')
                        cut--;
                    goto done;
                default: {
                    if ((uchar)c == CHAR_QUOTE)
                        c = '\'';
                    CHARDATA_tag *glyph = Font_FindGlyph(c);
                    if (glyph != NULL)
                        extent = (float)(((((double)Font_GetKernAdjust(prev, c) + extent) + glyph->pre) +
                                          glyph->post + glyph->w) + 1.0f);
                    break;
                }
            }
            prev = c;
            if (line[n] == '\0')
                break;
        }
    }
    if (line[n] == '\0') {
        result = NULL;
        cut = line + n;
    }

done:
    if (begin == cut && n != 0 && (double)width <= extent)
        cut = begin + n - 1;
    *text = (*cut != '\0') ? cut : NULL;
    if (begin != cut) {
        result = Txt_GetStringFromHeap(0);
        memcpy(result, begin, (ushort)(cut - begin));
        result[cut - begin] = '\0';
    }
    return result;
}

// The size of up to count characters of text (-1 for all) in its format. The flags handed to Font_ParseFormat are
// the text's address, as the original's are, so an odd one keeps the font's texture out of CharInfo.
// AUTOINJECT
bool Font_GetTextExtent(const char *text, const char *format, GPOINT *extent, const GPOINT *scale, int count) {
    if (text == NULL || extent == NULL)
        return false;
    GPOINT size;
    if (scale != NULL) {
        size = *scale;
    } else {
        size.x = 1.0f;
        size.y = 1.0f;
    }
    uint flags = (uint)(uintptr_t)text;
    Font_ParseFormat(format, &flags);
    GPOINT *measured = Font_DrawGlyphs(0, &size, 0.0f, 0.0f, 0, 0, text, format, FONT_MEASURE_ONLY, count, -1.0f,
                                       NULL);
    *extent = *measured;
    return true;
}

// How many characters of text fit in width pixels. The original hands Font_ParseFormat flags of uninitialised
// stack; ours are 0.
// AUTOINJECT
uint Font_GetTextCountExtent(const char *text, const char *format, const GPOINT *scale, float width) {
    if (text == NULL)
        return 0;
    uint fit = 0;
    GPOINT size;
    if (scale != NULL) {
        size = *scale;
    } else {
        size.x = 1.0f;
        size.y = 1.0f;
    }
    uint flags = 0;
    Font_ParseFormat(format, &flags);
    Font_DrawGlyphs(0, &size, 0.0f, 0.0f, 0, 0, text, format, FONT_MEASURE_ONLY, -1, width, &fit);
    return fit;
}

// The first alignment a format names (2 right, 3 centred), 1 if none
// AUTOINJECT
ushort Font_GetAlignment(const char *format) {
    const char *p = format;
    while (*p != '\0') {
        uchar code = (uchar)*p++;
        switch (code) {
            case FMT_COLOUR:
                p += 4;
                break;
            case 0xfb:
            case 0xfc:
            case FMT_FONT:
                p++;
                break;
            case FMT_ALIGN:
                return (ushort)(short)*p;
            default:
                break;
        }
    }
    return 1;
}
