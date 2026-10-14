#pragma once

// ---------------------------------------------------------------------------------------------------------------
// The font the disc error screen draws with (DiscError_Init, GameLoop.cpp). The game links a font of its own
// (0x001b7480); ours is our own design in the same FNTX layout and size, so that loading it makes the same
// allocations: 5x11 glyphs drawn at twice the size into the same 256x182 texture. See DebugFont.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include "../eagl/Realgraph.h"          // LinkedFontFile

typedef LinkedFontFile<190, 8, 256, 182> DebugFontFile;
static_assert(sizeof(DebugFontFile) == 0xbfd0, "the debug font is 0xbfd0 bytes");
static_assert(offsetof(DebugFontFile, imageHeader) == 0x970 && offsetof(DebugFontFile, info) == 0xbf80,
              "DebugFontFile layout");

extern DebugFontFile DebugFont;
