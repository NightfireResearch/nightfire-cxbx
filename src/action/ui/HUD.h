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
    short posX;
    short posY;
    short width;
    short height;
    HUDPANE_createFunc createFunc;
    HUDPANE_updateFunc updateFunc;
    SpriteInfo *spriteInfo;
    ushort numSprites;
    ushort numExtraItems;
    ushort flags;           // HUDPANE_* for HUD_ValidateXY
    char pad1;
    char pad2;
} HUDPANECREATE_tag;
static_assert(offsetof(HUDPANECREATE_tag, width) == 4, "HUDPANECREATE_tag.width is at 4");
static_assert(sizeof(HUDPANECREATE_tag) == 0x1c, "HUDPANECREATE_tag is 0x1c bytes");

// HUDPANECREATE_tag::flags: how HUD_ValidateXY keeps a pane in its player's view
#define HUDPANE_KEEP_IN_X   0x0008
#define HUDPANE_KEEP_IN_Y   0x0010
#define HUDPANE_VIEW_X      0x0080  // the position is from the view's corner
#define HUDPANE_VIEW_Y      0x0100

// SpriteInfo::maybeFlags: HUD_CreateDefault cuts the sprite to the view, and HUD_CalcWidthHeight leaves it out
#define SPRITEINFO_CLIP_TO_VIEW 0x1000

// The C++ dynamic initialisers of the message panes' widths (HUD.cpp), run by _cinit.
void HUD_InitObjectiveStatusPaneWidth(void);
void HUD_InitInfoStatusPaneWidth(void);
void HUD_InitPickupStatusPaneWidth(void);

typedef struct HUDPANE_tag {
    HUDPANECREATE_tag *base;
    void** extraItems; // Pointer to an array of pointers
    sprite** spriteList;
    HUDPANE_updateFunc updateFunction;
    short posX;             // 0x10 the layout's position, kept to the view
    short posY;
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
void HUD_CreateShrink(BLData *playerInfo, HUDPANE_tag *pane, HUDPANECREATE_tag *param_3, obj_tag *param_4);

// A thing on the multiplayer radar (MP_GetRadarObjects)
typedef struct RADAROBJ {
    _VECTOR pos;
    uint colour;        // 0x0c
    ushort icon;        // 0x10 into RadarIcons
    ushort pad;
} RADAROBJ;
static_assert(sizeof(RADAROBJ) == 0x14, "RADAROBJ is 0x14 bytes");

void _HUD_UpdateCrossHair(BLData *player, sprite *spr);
void HUD_UpdateCrossHair(void);
void _HUD_MonitorNightSight(BLData *player);
void HUD_MonitorNightSight(void);
void HUD_CalcWidthHeight(HUDPANECREATE_tag *create, short *width, short *height);
void HUD_ValidateXY(short *x, short *y, ushort width, ushort height, ushort player, uint flags);
void HUD_CreateDefault(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *unused);
void _HUD_CreateShift(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateShift(void);
void HUD_FixMP(sprite *spr, short player);
void _HUD_PlaceLensFlare(sprite *ring, _VECTOR sun, float cx, float cy, float along, float scale, uchar alpha);
void HUD_PlaceLensFlare(void);

// Implementations of individual HUD panes
void HUD_CreateRedeemer(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateMissionStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateObjectiveStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateInfoStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreatePickupStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateMPHealthPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateMPScorePane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_CreateRadar(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj);
void HUD_UpdateBloodPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateAmmoPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateAirPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateCameraPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateNightSightPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused);
void HUD_UpdateXRayPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused);
void HUD_UpdateSecCamPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused);
void HUD_UpdateLensFlarePane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateMPHealthPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_MPUpdateStatusPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused);
void HUD_MPUpdatePane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_RadarUpdate(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_CreateAmmoPane(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj);
void HUD_CreateHealthPane(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj);
void HUD_CreateOICWPane(BLData *playerInfo,HUDPANE_tag *pane,HUDPANECREATE_tag *param_3,obj_tag *param_4);

void HUD_UpdateOICWPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateStatusPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused);
void HUD_UpdateHealthPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateCarPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateSpacePane(BLData *param_1, HUDPANE_tag *pane, obj_tag *obj);
void HUD_UpdateRedeemerPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

#endif // HUD_H