#ifndef COPTER_H_
#define COPTER_H_

#include "../../actionhelpers.h"


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
    ushort kind;                // 0x18 - key 0: 0 makes the helicopter as well, 2 the way out
    ushort _pad1a;
} COPTERPATH;

static_assert(sizeof(COPTERPATH) == 0x1c, "COPTERPATH is 0x1c bytes");

typedef struct {
    LLNODE_tag node;
    obj_tag *obj;             // 0x08
    char pad0c[0x1c - 0x0c];
    quaternion_tag quat;      // 0x1c - Spline_Interp's
    obj_tag* scriptPlayer;    // 0x2c
    union {
        obj_tag *parts[9];    // 0x30 - set by its script (Copter_Register); an explosion that hurts any of them hurts
                              //        the helicopter
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
    ushort channel;           // 0x74 - set after delay; 100: it never attacks, and goes at its path's end
    ushort delay;             // 0x76 - frames
    ushort paths;             // 0x78 - its group's routes
    char pad7a[2];
} COPTER;

static_assert(sizeof(COPTER) == 0x7c, "Copter size incorrect");
static_assert(offsetof(COPTER, quat) == 0x1c, "Offset of quat incorrect");
static_assert(offsetof(COPTER, scriptPlayer) == 0x2c, "Offset of scriptPlayer incorrect");
static_assert(offsetof(COPTER, body) == 0x38, "Offset of body incorrect");
static_assert(offsetof(COPTER, launchers) == 0x4c, "Offset of launchers incorrect");
static_assert(offsetof(COPTER, path) == 0x54, "Offset of path incorrect");
static_assert(offsetof(COPTER, attackDamage) == 0x6c, "Offset of attackDamage incorrect");
static_assert(offsetof(COPTER, enableChannel) == 0x70, "Offset of enableChannel incorrect");
static_assert(offsetof(COPTER, paths) == 0x78, "Offset of paths incorrect");

// What a shot does to each part: the bullets that pass it by, and its damage's scale
typedef struct COPTERBIT {
    ushort ignore;              // Collide_FilterBullets' mask, 0 for none
    ushort _pad;
    float scale;
} COPTERBIT;

static_assert(sizeof(COPTERBIT) == 8, "COPTERBIT is 8 bytes");

// A helicopter path's placement keys (level_tag)
typedef struct CopterPlacement {
    char _unknown00[2];
    ushort points;              // 0x02 - the spline's
    ushort length;              // 0x04 - frames, when key 10 is 0
    char _unknown06[2];
    void *spline;               // 0x08
    char _unknown0c[0x2c - 0x0c];
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
// The register-argument originals' entries, and the C++ under them
COPTERPATH* _Copter_CreatePath(CopterPlacement *keys);
void Copter_CreatePath(void);
COPTERPATH* _Copter_NextPath(COPTER *copter);
void Copter_NextPath(void);
bool _Copter_Move(COPTER *copter, obj_tag *obj, char positionOnly);
void Copter_Move(void);
bool _Copter_FireGun(COPTER *copter, obj_tag *obj);
void Copter_FireGun(void);
bool _Copter_FireMissile(COPTER *copter, obj_tag *obj);
void Copter_FireMissile(void);
bool _Copter_SeesPlayer(obj_tag *player, obj_tag *obj);
void Copter_SeesPlayer(void);

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
