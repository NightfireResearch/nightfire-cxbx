#ifndef HUD_H
#define HUD_H

#include "../actionhelpers.h"

typedef enum {
    Ammo = 0,
    Health,
    MsgMissionStatus,
    MsgObjectiveStatus,
    MsgInfoStatus,
    Air,
    Sight,
    NightSight,
    LensFlare,
    Redeemer,
    RCCar,
    Camera,
    Blood, 
    MPScore,
    Radar,
    Xray,
    SecCam,
    OICW,
    Ronin,
    Laser,
    Space,
    MsgPickupStatus
  } HUD_PANE_IND;

  
#define NUM_PANES 22


#pragma pack(push, 1)

// Advance definitions of the structs
struct HUDPANECREATE_tag;
struct HUDPANE_tag;
struct HUDINFO_tag;
typedef struct HUDPANECREATE_tag HUDPANECREATE_tag;
typedef struct HUDPANE_tag HUDPANE_tag;
typedef struct HUDINFO_tag HUDINFO_tag;

// Create function will take BLData*, HUDPANE_tag*, HUDPANECREATE_tag*, obj_tag* and return nothing
// Update function will take BLData*, HUDPANE_tag*, obj_tag* and return nothing
typedef void (*HUDPANE_createFunc)(BLData*, HUDPANE_tag*, HUDPANECREATE_tag*, obj_tag*);
typedef void (*HUDPANE_updateFunc)(BLData*, HUDPANE_tag*, obj_tag*);

typedef struct HUDPANECREATE_tag {
    char pad[8];
    HUDPANE_createFunc createFunc;
    HUDPANE_updateFunc updateFunc;
    void* spriteInfo; // FIXME
    char pad2[8];
} HUDPANECREATE_tag;

typedef struct HUDPANE_tag {
    HUDPANECREATE_tag *base;
    char pad1[22];
    ushort state;
    bool enabled;
    char pad2[2];
    char maybeCanBeEnabled;
} HUDPANE_tag;

static_assert(sizeof(HUDPANE_tag) == 0x20, "Size of HUDPANE_tag is incorrect");
static_assert(offsetof(HUDPANE_tag, state) == 0x1a, "Offset of state is incorrect");
static_assert(offsetof(HUDPANE_tag, enabled) == 0x1c, "Offset of enabled is incorrect");
static_assert(offsetof(HUDPANE_tag, maybeCanBeEnabled) == 0x1f, "Offset of maybeCanBeEnabled is incorrect");

typedef struct HUDINFO_tag {
    void* crosshairSprite; // FIXME sprite*
    int maybeUnused;
    HUDPANE_tag pane[NUM_PANES];
} HUDINFO_tag;

static_assert(sizeof(HUDINFO_tag) == 0x2c8, "Size of HUDINFO_tag is incorrect");
static_assert(offsetof(HUDINFO_tag, pane) == 0x8, "Offset of pane is incorrect");


#pragma pack(pop)


void HUD_Enable(HUDINFO_tag *param_1, HUD_PANE_IND idx, char enable, ushort state);
void HUD_Reset(BLData *param_1);
void HUD_DisableAll(BLData *param_1);
ushort HUD_State(HUDINFO_tag *param_1, HUD_PANE_IND idx);


#endif // HUD_H