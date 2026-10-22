#ifndef MINISUB_H_
#define MINISUB_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"
#include "../../engine/AINetwork.h"

#pragma pack(push, 1)
// A mini sub's extra data (OBJECTTYPE_MINISUB): it hunts player 1 over the AI network, turning towards where it is
// going, and fires torpedoes at him once it is close
typedef struct MiniSubInfo {
    obj_tag *gameObj;               // 0x000
    float health;                   // 0x004 - 100
    float height;                   // 0x008 - its anim's first root height
    float turnRate;                 // 0x00c - of the gap to its facing, a frame
    float unknown10;                // 0x010 - 0.03
    int channel;                    // 0x014 - key 0: a switch channel it waits for, or 0
    float unknown18[2];             // 0x018
    float speed;                    // 0x020 - 0.03 a frame
    float unknown24[3];             // 0x024
    obj_tag *target;                // 0x030 - player 1
    char _pad034[0xc4 - 0x34];
    uint nextFireFrame;             // 0x0c4
    uint lastFireFrame;             // 0x0c8
    char _pad0cc[0xdc - 0xcc];
    float moveDistance;             // 0x0dc - to moveTarget, when it was set
    float moveReach;                // 0x0e0 - 0.3
    CelPos_tag moveTarget;          // 0x0e4
    _VECTOR facing;                 // 0x0f4 - the way to moveTarget, as angles
    char _pad100[0x128 - 0x100];
    AIPoint_tag point;              // 0x128
    char _pad150[0x184 - 0x150];
    AITarget_tag aiTarget;          // 0x184
    char _pad1b4[0x1bc - 0x1b4];
    AIRoute_tag *activeRoute;       // 0x1bc
    AIRoute_tag route;              // 0x1c0
    char _pad260[4];
} MiniSubInfo;
#pragma pack(pop)

LAYOUT_32(sizeof(MiniSubInfo) == 0x264, "MiniSubInfo is 0x264 bytes");
LAYOUT_32(offsetof(MiniSubInfo, route) == 0x1c0, "Bad offset of MiniSubInfo.route");

obj_tag * MiniSub_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
void MiniSub_Update(obj_tag *obj);
// The register-argument originals' adaptors (AUTOLTCG), and the cores behind them
void _MiniSub_SetTarget(MiniSubInfo *sub, CelPos_tag *at);
void MiniSub_SetTarget(void);       // EAX the sub's data, EBX where
void _MiniSubMove_Turn(MiniSubInfo *sub);
void MiniSubMove_Turn(void);        // ESI the sub's data
void _MiniSubMove_Forward(MiniSubInfo *sub);
void MiniSubMove_Forward(void);     // EAX the sub's data
uint _MiniSub_Route(MiniSubInfo *sub, obj_tag *targetObj, float radius);
void MiniSub_Route(void);           // ESI the sub's data; target, radius
void _MiniSub_FireTorpedo(MiniSubInfo *sub);
void MiniSub_FireTorpedo(void);     // EAX the sub's data
void _MiniSubState_Attack(MiniSubInfo *sub);
void MiniSubState_Attack(void);     // EAX the sub's data
void _MiniSubState_Init(MiniSubInfo *sub);
void MiniSubState_Init(void);       // ESI the sub's data

#endif // MINISUB_H_
