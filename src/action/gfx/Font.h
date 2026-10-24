#ifndef ACTION_GFX_FONT_H
#define ACTION_GFX_FONT_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"
#include "Sprite.h"

// The game's fonts (PS2: font.cpp): measuring, word-wrapping and drawing text in the screen-space sprite list.
//
// A text has a format, a string of control codes Font_ParseFormat reads: 0xfa r g b x the colour, 0xfb n and
// 0xfc n nothing here, 0xfe n the alignment (2 right, 3 centred), 0xff n the font (FontTable[n]). In the text
// itself, 0xba switches the colour, "~X" draws button symbol X (SpecialChars) and 0x99 draws T and M at half size.

// The text's colours: its own, and the one 0xba switches to
#define TEXT_NORMAL     0x7d6d59ff
#define TEXT_HIGHLIGHT  0x785a14ff

#pragma pack(push, 1)
// A kerning pair of a glyph's list
typedef struct KERN_tag {
    ushort first;
    ushort second;      // 0x02 the character after
    signed char adjust; // 0x04 pixels
    uchar pad;
} KERN_tag;
static_assert(sizeof(KERN_tag) == 6, "KERN_tag is 6 bytes");

// A glyph, in the font's texture
typedef struct CHARDATA_tag {
    ushort u;           // 0x00 its box in the texture
    ushort v;
    ushort w;
    ushort h;
    short yOffset;      // 0x08
    short pre;          // 0x0a space before it, when there is a character before
    short post;         // 0x0c space after it
    ushort kernCount;   // 0x0e
    KERN_tag *kern;     // 0x10 in ascending order of the character after
    ushort code;        // 0x14
    ushort pad;
} CHARDATA_tag;
LAYOUT_32(sizeof(CHARDATA_tag) == 0x18, "CHARDATA_tag is 0x18 bytes");

typedef struct FONT_tag {
    uint kernCount;
    KERN_tag *kern;         // 0x04
    ushort first;           // 0x08 the lowest and highest character it has
    ushort last;            // 0x0a
    uint count;             // 0x0c glyphs
    CHARDATA_tag *chars;    // 0x10 in ascending order of code; the first sets the line's height
    float lineGap;          // 0x14
    float space;            // 0x18 the width of a space, before x 1.25
} FONT_tag;
LAYOUT_32(sizeof(FONT_tag) == 0x1c, "FONT_tag is 0x1c bytes");
extern FONT_tag *Font;   // the font in use (Font.cpp)

typedef struct FONTTABLE_tag {
    HASHCODE texture;
    FONT_tag *font;
} FONTTABLE_tag;
LAYOUT_32(sizeof(FONTTABLE_tag) == 8, "FONTTABLE_tag is 8 bytes");

// A button symbol, "~X" in a text
typedef struct SPECIALCHAR_tag {
    char code;              // the character after the '~'
    char pad[3];
    HASHCODE texture;       // 0x04
    ushort textureWidth;    // 0x08
    ushort textureHeight;
    short u0;               // 0x0c its box in the texture
    short v0;
    short u1;
    short v1;
    short width;            // 0x14 on screen, and the advance
    short height;           // 0x16
} SPECIALCHAR_tag;
static_assert(sizeof(SPECIALCHAR_tag) == 0x18, "SPECIALCHAR_tag is 0x18 bytes");

// What Font_WordWrapString resets; nothing else in the game reads it
typedef struct PRINTINFO_tag {
    ushort unknown00;
    ushort unknown02;
    ushort unknown04;
    ushort unknown06;
    float unknown08[2];
    float unknown10[6];
    float unknown28;
    uchar unknown2c[4];
    uint unknown30;
    uint unknown34;
    const char *format;     // 0x38
    ushort unknown3c;
    ushort pad;
} PRINTINFO_tag;
LAYOUT_32(sizeof(PRINTINFO_tag) == 0x40, "PRINTINFO_tag is 0x40 bytes");
#pragma pack(pop)

// The flags of a draw: Font_ParseFormat's alignment in the low byte
#define FONT_MEASURE_ONLY   0x0001  // no sprites
#define FONT_CENTRE         0x0002
#define FONT_RIGHT          0x0004
#define FONT_COPY           0x0008  // one of Font_DrawText's shadow or outline copies
#define FONT_SHADOW         0x1000  // a copy at (+1, +1) in the shadow colour
#define FONT_OUTLINE        0x2000  // a copy at (-1, -1) in the outline colour
#define FONT_SHADOW_RING    0x4000  // without FONT_SHADOW: four copies at (+-1, +-1) in the shadow colour

CHARDATA_tag* Font_FindGlyph(uchar c);
float Font_GetKernAdjust(char prev, char c);
void Font_ParseFormat(const char *format, uint *flags);
GPOINT* Font_DrawGlyphs(uint colour, const GPOINT *scale, float x, float y, uint unused3, uint unused4,
                        const char *text, const char *format, uint flags, int count, float width, uint *fit);
GPOINT* Font_DrawText(float x, float y, uint colour, uint shadow, uint outline, const char *text, const char *format,
                      const GPOINT *scale, uint flags, int count, float width);
char* Font_WordWrapString(char **text, int length, const char *format, uint width);
bool Font_GetTextExtent(const char *text, const char *format, GPOINT *extent, const GPOINT *scale, int count);
uint Font_GetTextCountExtent(const char *text, const char *format, const GPOINT *scale, float width);
ushort Font_GetAlignment(const char *format);

extern SPECIALCHAR_tag SpecialChars[23];

// Formats and strings several places share (Font.cpp)
extern char FontFormat2Centred[5], FontFormat1Centred[5], FontFormat2[3], FontFormat3[3];
extern char FontFormat1Scaled[7], FontFormat2Scaled[7], FontFormat3Scaled[7], FontFormat2CentredScaled[9];
extern char FontAlphabet[53], FontEmptyString[1], FontSpaceString[2];

#endif // ACTION_GFX_FONT_H
