#ifndef SUB_H_
#define SUB_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

typedef struct light_tag light_tag;

#pragma pack(push, 1)
// A sub's extra data (OBJECTTYPE_SUB): it follows its path while its switch channel is on, rams, fires missiles and
// torpedoes (and a laser, with a beam), and once destroyed sinks to the seabed
typedef struct SubInfo {
    void *path;             // 0x00 - the placement's
    float t;                // 0x04 - along the path, in keys
    float speed;            // 0x08 - keys a frame
    float sinkY;            // 0x0c - destroyed, the height it sinks to
    float health;           // 0x10 - 300 to start
    float height;           // 0x14 - its anim's first root height
    float seabedGap;        // 0x18 - -3: how far above the seabed it comes to rest
    float missileTime;      // 0x1c - seconds since its last missile
    float laserTime;        // 0x20
    float torpedoTime;      // 0x24
    char _pad28[0x34 - 0x28];
    ushort lastKey;         // 0x34 - the path's last key
    ushort channel;         // 0x36 - key 0: a switch channel it waits for, or 0
    int lastHitFrame;       // 0x38 - the frame it was last damaged, once a frame
    obj_tag *beam;          // 0x3c - the laser's; none is made
    light_tag *light;       // 0x40 - its cyan light
} SubInfo;
#pragma pack(pop)

LAYOUT_32(sizeof(SubInfo) == 0x44, "SubInfo is 0x44 bytes");

obj_tag * Sub_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
void Sub_Update(obj_tag *obj);
void Sub_ApplyDamage(obj_tag *obj, float damage);
void Sub_Follow(obj_tag *obj, SubInfo *sub);
void Sub_Ram(obj_tag *obj, SubInfo *sub);
void Sub_Laser(obj_tag *obj, SubInfo *sub);
void Sub_FireMissiles(obj_tag *obj, SubInfo *sub);
void Sub_DeployTorpedoes(obj_tag *obj, SubInfo *sub);

#endif // SUB_H_
