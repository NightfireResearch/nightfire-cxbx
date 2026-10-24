#ifndef COPTER_H_
#define COPTER_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

// COPTERPATH::kind
enum CopterPathKind {
    COPTERPATH_WITH_COPTER = 0, // the placement makes the helicopter as well
    COPTERPATH_EXIT = 2,        // the way out
};

// COPTER::parts, named by the script's event 15 (Copter_Register): shots pass through all but the body
#define COPTER_PARTS 9
#define COPTER_PART_BODY 2

// A COPTER::channel that is no channel: the helicopter never attacks
#define COPTER_NEVER_ATTACKS 100

#pragma pack(push, 1)

// A helicopter's path (on CopterPaths): every placement of the type makes one; those of a group are the routes its
// helicopter goes round, chosen by index, and the one of kind 2 is the way out
typedef struct COPTERPATH {
    LLNODE_tag node;
    void *spline;               // 0x08 - the placement's
    float speed;                // 0x0c - frames of the path a frame
    ushort frames;              // 0x10 - one less than its points
    ushort group;               // 0x12 - key 1
    ushort start;               // 0x14 - the frame a helicopter starts it at (key 3)
    ushort index;               // 0x16 - key 2
    ushort kind;                // 0x18 - key 0: CopterPathKind
    ushort _pad1a;
} COPTERPATH;

LAYOUT_32(sizeof(COPTERPATH) == 0x1c, "COPTERPATH is 0x1c bytes");

typedef struct {
    LLNODE_tag node;
    obj_tag *obj;             // 0x08
    char pad0c[0x1c - 0x0c];
    quaternion_tag quat;      // 0x1c - Spline_Interp's
    obj_tag* scriptPlayer;    // 0x2c
    union {
        // 0x30 - set by its script (Copter_Register); an explosion that hurts any of them hurts the helicopter
        obj_tag *parts[COPTER_PARTS];
        struct {
            obj_tag *_parts30[2];
            obj_tag* body;    // 0x38
            obj_tag *smoke;   // 0x3c - smokes once badly hurt
            obj_tag *_parts40;
            obj_tag *gun;     // 0x44
            obj_tag *flash;   // 0x48 - the gun's
            obj_tag *launchers[2]; // 0x4c
        };
    };
    COPTERPATH *path;         // 0x54
    COPTERPATH *exitPath;     // 0x58
    float pathFrame;          // 0x5c
    char pad60[4];
    float damage;             // 0x64 - from shots and explosions
    float maxDamage;          // 0x68 - more and it leaves
    float attackDamage;       // 0x6c - this attack's
    ushort enableChannel;     // 0x70 - shown, and flying, once it is on
    ushort doneChannel;       // 0x72 - set when it leaves or is destroyed
    ushort channel;           // 0x74 - set after delay; COPTER_NEVER_ATTACKS: it goes at its path's end
    ushort delay;             // 0x76 - frames
    ushort paths;             // 0x78 - its group's routes
    char pad7a[2];
} COPTER;

LAYOUT_32(sizeof(COPTER) == 0x7c, "Copter size incorrect");
LAYOUT_32(offsetof(COPTER, quat) == 0x1c, "Offset of quat incorrect");
LAYOUT_32(offsetof(COPTER, scriptPlayer) == 0x2c, "Offset of scriptPlayer incorrect");
LAYOUT_32(offsetof(COPTER, body) == 0x38, "Offset of body incorrect");
LAYOUT_32(offsetof(COPTER, launchers) == 0x4c, "Offset of launchers incorrect");
LAYOUT_32(offsetof(COPTER, path) == 0x54, "Offset of path incorrect");
LAYOUT_32(offsetof(COPTER, attackDamage) == 0x6c, "Offset of attackDamage incorrect");
LAYOUT_32(offsetof(COPTER, enableChannel) == 0x70, "Offset of enableChannel incorrect");
LAYOUT_32(offsetof(COPTER, paths) == 0x78, "Offset of paths incorrect");

// What a shot does to each part: the bullets that pass it by, and its damage's scale
typedef struct COPTERBIT {
    ushort ignore;              // Collide_FilterBullets' mask, 0 for none
    ushort _pad;
    float scale;
} COPTERBIT;

static_assert(sizeof(COPTERBIT) == 8, "COPTERBIT is 8 bytes");

// A helicopter path's placement keys (level_tag)
typedef struct CopterPlacement {
    PlacementHeader header;     // 0x00
    uint kind;                  // 0x2c - key 0
    uint group;                 // 0x30
    uint index;                 // 0x34
    uint start;                 // 0x38
    HASHCODE script0;           // 0x3c
    HASHCODE script1;           // 0x40
    uint enableChannel;         // 0x44
    uint doneChannel;           // 0x48
    uint channel;               // 0x4c
    uint delay;                 // 0x50
    uint length2;               // 0x54 - key 10: frames, 0 for the placement's own
} CopterPlacement;

static_assert(offsetof(CopterPlacement, kind) == 0x2c, "Bad offset of CopterPlacement.kind");
static_assert(offsetof(CopterPlacement, length2) == 0x54, "Bad offset of CopterPlacement.length2");

#pragma pack(pop)

void Copter_Delete(obj_tag *obj);
obj_tag * Copter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
obj_tag* Copter_GetBody(COPTER* copter);
void Copter_Init(void);
void Copter_Register(obj_tag *obj, ushort part, void *context);
void Copter_Update(obj_tag *obj);
void Copter_InitConeCos(void);
void Copter_InitConeSin(void);
COPTERPATH* Copter_CreatePath(CopterPlacement *keys);
COPTERPATH* Copter_NextPath(COPTER *copter);
bool Copter_Move(COPTER *copter, obj_tag *obj, char positionOnly);
bool Copter_FireGun(COPTER *copter, obj_tag *obj);
bool Copter_FireMissile(COPTER *copter, obj_tag *obj);
bool Copter_SeesPlayer(obj_tag *player, obj_tag *obj);

extern LLISTINFO_tag CopterList;
extern LLISTINFO_tag CopterPaths;
// The cone the helicopters see the player in, 45 degrees about their direction (Copter_InitConeCos, _Sin)
extern float CopterConeCos;
extern float CopterConeSin;
// Odd: the attack is with the gun; even, the missiles (Copter_Update)
extern int16_t CopterAttackMode;
// The difficulty's scale of a helicopter's endurance and how long it attacks
extern float DiffMod;
extern COPTERBIT CopterBits[9];

#endif // COPTER_H_
