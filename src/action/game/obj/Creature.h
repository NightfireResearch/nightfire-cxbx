#ifndef CREATURE_H_
#define CREATURE_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"
#include "../../math/math.h"
#include "../../util/LList.h"

#define CREATURE_WAYPOINTS 6

// Creature::kind
#define CREATURE_SWIMS 0
#define CREATURE_FISH  1    // only in a cel of kind 0xc023, and listed in FishList

// Creature::state
#define CREATURE_PLAN   0   // new waypoints, then moving
#define CREATURE_MOVE   1
#define CREATURE_DIE    3
#define CREATURE_DEAD   4

#pragma pack(push, 1)
// A waypoint: where, and turned along the way there
typedef struct CreatureWaypoint {
    _VECTOR pos;
    quaternion_tag quat;
} CreatureWaypoint;

// A creature's extra data (OBJECTTYPE_CREATURE): it follows a spline through six waypoints, the first where it is and
// the rest planned from there, then plans again
typedef struct Creature {
    LLNODE_tag node;                                // 0x00 - in FishList
    CreatureWaypoint waypoints[CREATURE_WAYPOINTS]; // 0x08
    _VECTOR home;                                   // 0xb0 - where it was placed
    int splineMode;                                 // 0xbc - Spline_Interp's: 2 for fish, turned along the way
    obj_tag *gameObj;                               // 0xc0
    float range;                                    // 0xc4 - key 1: how far from home it goes
    float t;                                        // 0xc8 - along the spline, 0 to 5
    float speed;                                    // 0xcc - a frame
    float speedRange;                               // 0xd0 - key 2, hundredths
    float size;                                     // 0xd4 - its model's furthest z
    ushort kind;                                    // 0xd8 - key 0: CREATURE_*
    short state;                                    // 0xda - CREATURE_PLAN and on
    char seeks;                                     // 0xdc - key 3: heads for player 1 when he is within 4 sizes and 1.1
    char hurts;                                     // 0xdd - key 4: hurts players it touches
    char padDE[2];
} Creature;
#pragma pack(pop)

LAYOUT_32(sizeof(Creature) == 0xe0, "Creature is 0xe0 bytes");
LAYOUT_32(offsetof(Creature, gameObj) == 0xc0, "Bad offset of Creature.gameObj");

// The placement's keys (level_tag)
typedef struct CreaturePlacement {
    char _unknown00[0x2c];
    uint kind;                  // 0x2c
    uint range;                 // 0x30
    uint speed;                 // 0x34
    uint seeks;                 // 0x38
    uint hurts;                 // 0x3c
} CreaturePlacement;

void Creature_Init(void);
void Creature_Delete(obj_tag *obj);
obj_tag * Creature_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, level_tag *lvl);
void Creature_CalcWaypoints(obj_tag *obj, Creature *creature);
void Creature_Update(obj_tag *obj);
void Creature_HurtPlayers(obj_tag *obj);
void Creature_CalcPoint(_VECTOR *out, float distance, float size, _VECTOR *angles, _VECTOR *dir, _VECTOR *from,
                        obj_tag *obj);
bool Creature_SeekPlayer(Creature *creature, obj_tag *obj, _VECTOR *dir);
void Creature_Move(obj_tag *obj, Creature *creature);

#endif // CREATURE_H_
