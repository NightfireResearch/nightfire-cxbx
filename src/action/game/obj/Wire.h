#ifndef WIRE_H_
#define WIRE_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A wire's obj_tag::extraObjectData (8 bytes): what kind of wire, the third-person camera that watches it (the
// placement's ThirdCam index, 0 for none), and the switch channel set while the player is on it.
typedef struct WIRE {
    short type;         // 0x0 - WireType; the placement's second parameter
    ushort thirdCam;    // 0x2 - the first parameter
    short field_4;      // 0x4 - the third
    short channel;      // 0x6 - the fourth
} WIRE;
#pragma pack(pop)
static_assert(sizeof(WIRE) == 8, "Bad size for WIRE");

typedef enum {
    WIRE_ROPE = 0,      // hung from, hand over hand (Player_Wire)
    WIRE_1 = 1,         // never caught (Player_CollWire)
    WIRE_ZIPLINE = 2,   // slid down (Player_Zipline)
} WireType;

obj_tag * Wire_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

#endif // WIRE_H_
