#ifndef UI_COMPONENT_H
#define UI_COMPONENT_H

// The menus' skins (Component_*, Menu.cpp)

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"
#include "../gfx/Sprite.h"

#pragma pack(push, 1)

// One piece of a skin: a sprite, where it sits in the control and how it stretches
typedef struct MENU_COMPONENTPIECE {
    SpriteInfo info;            // 0x00 its rectangle: the piece's offset (from the edges its flags give) and size
    float stretchWidth;         // 0x2c the share it takes of the room the fixed pieces leave; 0 for a fixed piece
    float stretchHeight;        // 0x30
    uint flags;                 // 0x34 PIECE_*, and the draw states (M_CONTROL::drawState) it shows in
} MENU_COMPONENTPIECE;
LAYOUT_32(sizeof(MENU_COMPONENTPIECE) == 0x38, "MENU_COMPONENTPIECE is 0x38 bytes");

#define PIECE_BOTTOM            1       // placed from the bottom edge, else the top
#define PIECE_RIGHT             2       // placed from the right edge, else the left
#define PIECE_CENTRE_X          4
#define PIECE_CENTRE_Y          8
#define PIECE_WIDTH_FROM_HEIGHT 0x100   // its width is its sprite's height, or the room's height stretched
#define PIECE_HEIGHT_FROM_WIDTH 0x200   // its height is its width, or the room's width stretched
#define PIECE_KEEP_ROOM_X       0x400   // a fixed piece that leaves the room alone
#define PIECE_KEEP_ROOM_Y       0x800
#define PIECE_SPRITE_100        0x1000  // the sprite's flags 0x100
#define PIECE_SPRITE_200        0x2000  // the sprite's flags 0x200

// A control's skin (Component_GetHandle): its pieces, and the insets of what it frames
typedef struct MENU_COMPONENT {
    HASHCODE texture;           // 0x00
    ushort count;               // 0x04
    short left;                 // 0x06
    short top;                  // 0x08
    short right;                // 0x0a
    short bottom;               // 0x0c
    char pad_0e[2];
    MENU_COMPONENTPIECE *pieces; // 0x10
} MENU_COMPONENT;
LAYOUT_32(sizeof(MENU_COMPONENT) == 0x14, "MENU_COMPONENT is 0x14 bytes");

#define COMPONENTS_PER_SET 14

// A manager's skins (the menu data's 0xfffffffd records); M_CONTROL::skin picks one by set id and index
typedef struct MENU_COMPONENTSET {
    MENU_COMPONENTSET *next;    // 0x00
    MENU_COMPONENTSET *prev;    // 0x04
    MENU_COMPONENT components[COMPONENTS_PER_SET]; // 0x08
    uint id;                    // 0x120
    uchar managerNum;           // 0x124
    char pad_125[3];
} MENU_COMPONENTSET;
LAYOUT_32(sizeof(MENU_COMPONENTSET) == 0x128, "MENU_COMPONENTSET is 0x128 bytes");

#pragma pack(pop)

#endif // UI_COMPONENT_H
