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
    Redeemer, // Sentinel Missile?
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
    MsgPickupStatus,
    NUM_PANES
  } HUD_PANE_IND;

#pragma pack(push, 1)

// Create function will take BLData*, HUDPANE_tag*, HUDPANECREATE_tag*, obj_tag* and return nothing
// Update function will take BLData*, HUDPANE_tag*, obj_tag* and return nothing
typedef void (*HUDPANE_createFunc)(BLData*, HUDPANE_tag*, HUDPANECREATE_tag*, obj_tag*);
typedef void (*HUDPANE_updateFunc)(BLData*, HUDPANE_tag*, obj_tag*);

typedef struct HUDPANECREATE_tag {
    char pad[8];
    HUDPANE_createFunc createFunc;
    HUDPANE_updateFunc updateFunc;
    SpriteInfo *spriteInfo;
    ushort numSprites;
    char pad2[6];
} HUDPANECREATE_tag;

typedef struct HUDPANE_tag {
    HUDPANECREATE_tag *base;
    void** extraItems; // Pointer to an array of pointers
    sprite** spriteList;
    HUDPANE_updateFunc updateFunction;
    short unknown2[2];
    short width;
    short height;
    short numSprites;
    ushort state;
    bool enabled;
    char field_0x1d;
    char field_0x1e;
    char maybeCanBeEnabled;
} HUDPANE_tag;

static_assert(sizeof(HUDPANE_tag) == 0x20, "Size of HUDPANE_tag is incorrect");
static_assert(offsetof(HUDPANE_tag, state) == 0x1a, "Offset of state is incorrect");
static_assert(offsetof(HUDPANE_tag, enabled) == 0x1c, "Offset of enabled is incorrect");
static_assert(offsetof(HUDPANE_tag, maybeCanBeEnabled) == 0x1f, "Offset of maybeCanBeEnabled is incorrect");

typedef struct HUDINFO_tag {
    sprite* crosshairSprite;
    int maybeUnused;
    HUDPANE_tag pane[NUM_PANES]; // Index is HUD_PANE_IND::...
} HUDINFO_tag;

static_assert(sizeof(HUDINFO_tag) == 0x2c8, "Size of HUDINFO_tag is incorrect");
static_assert(offsetof(HUDINFO_tag, pane) == 0x8, "Offset of pane is incorrect");


#pragma pack(pop)

void HUD_Init(BLData *player, obj_tag *obj);
void HUD_Enable(HUDINFO_tag *param_1, HUD_PANE_IND idx, char enable, ushort state);
void HUD_Reset(BLData *param_1);
void HUD_DisableAll(BLData *param_1);
ushort HUD_State(HUDINFO_tag *param_1, HUD_PANE_IND idx);
void HUD_Update(BLData *playerInfo, obj_tag *obj);

// Implementations of individual HUD panes
void HUD_CreateOICWPane(BLData *playerInfo,HUDPANE_tag *pane,HUDPANECREATE_tag *param_3,obj_tag *param_4);
void HUD_UpdateOICWPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateCarPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateSpacePane(BLData *param_1, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateRedeemerPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

#endif // HUD_H