#ifndef PSIINPUT_H_
#define PSIINPUT_H_

#include "../actionhelpers.h"

#include "xinput_xbox.h" // Differs from regular PC in at least one place...

#pragma pack(push, 1)


typedef struct {
    uint controllerIndex;
    XINPUT_STATE controllerState;
    char pad[2];
    float Joystick_LX;
    float Joystick_LY;
    float Joystick_RX;
    float Joystick_RY;
    uint buttons;
    XINPUT_VIBRATION vibrationState;
    // 15x additional vibration samples perhaps?
    char pad_2[60];
    char pad_3[2];
    ushort scaledRumbleA;
    ushort scaledRumbleB;
    ushort lastRumbleA;
    ushort rumbleA;
    ushort lastRumbleB;
    ushort rumbleB;
    char pad_4[2];
    uint prevButtons;
    char pad_5[4];
    int unknown[8]; // Angle-Magnitude or some other representations of joysticks / D-Pads?
} ControllerStateStruct;

static_assert(sizeof(ControllerStateStruct) == 0xa8, "Bad size for ControllerStateStruct");

typedef struct {
    bool Initialised; 
    char pad[3];
    ControllerStateStruct Controllers[4];
} XboxInputs_struct;

static_assert(sizeof(XboxInputs_struct) == 0x2a4, "Bad size for XboxInputs struct");

typedef enum {
    // TODO: Inferred from P_PAUSE_HANDLER / P_CNCONTROLS_Handler but needs checking
    CONTROLSTYLE_NIGHTFIRE,
    CONTROLSTYLE_MOONRAKER,
    CONTROLSTYLE_OCTOPUSSY,
    CONTROLSTYLE_GOLDFINGER,
    CONTROLSTYLE_DRNO,
    CONTROLSTYLE_THUNDERBALL,
    CONTROLSTYLE_GOLDENEYE,
    CONTROLSTYLE_CLASSICBOND,

    CONTROLSTYLE_FORCE_U16 = 0x7fff
} GameContStyle_tag;


typedef struct {
    bool inverted;
    char unknown0[7];
    bool maybeCrosshairEnable;
    bool vibrationEnabled;
    bool autoSwitchBetterWeapon;
    char unknown00[3];
    short controlStyle; // GameContStyle_tag
    char unknown[4];
    float fChannels[60]; // Index is ActionAxes_t, unclear if this is the right number of items but it seems OK from a cross-reference perspective
    unsigned char actions[80]; // Unclear if this is the right number of items but it seems OK from a cross-reference perspective
    char unknown3;
    bool controllerIsActive;
    char controllerPort;
    char unknown4;
} PlayerInput_tag;

static_assert(offsetof(PlayerInput_tag, controlStyle) == 0xe, "Bad offset of controlStyle in PlayerInput_tag");
static_assert(offsetof(PlayerInput_tag, fChannels) == 0x14, "Bad offset of fChannels in PlayerInput_tag");
static_assert(offsetof(PlayerInput_tag, actions) == 0x104, "Bad offset of actions in PlayerInput_tag");
static_assert(offsetof(PlayerInput_tag, controllerPort) == 0x156, "Bad offset of controllerPort in PlayerInput_tag");

static_assert(sizeof(PlayerInput_tag) == 0x158, "Bad size for PlayerInput_tag");

typedef enum {
    ACTION_AIM_L_R,
    ACTION_WALK_L_R,
    ACTION_WALK_F_B,
    ACTION_3,
    ACTION_4,
    ACTION_AIM_U_D,
    ACTION_SCOPE_ZOOM,
    ACTION_7,
    ACTION_8,
    ACTION_FIRE,
    ACTION_GADGET_PREV,
    ACTION_GADGET_NEXT,
    ACTION_ALTFIRE,
    ACTION_13,
    ACTION_RELOAD,
    ACTION_WEAPON_PREV,
    ACTION_WEAPON_NEXT,
    ACTION_17,
    ACTION_18,
    ACTION_AIM_ZOOM_SCOPE,
    ACTION_TMP_ZOOMOUT,
    ACTION_TMP_ZOOMIN,
    ACTION_22,
    ACTION_23,
    ACTION_24,
    ACTION_SKIP_CUTSCENE,
    ACTION_MENU_DIR_UP,
    ACTION_MENU_DIR_DOWN,
    ACTION_MENU_DIR_LEFT,
    ACTION_MENU_DIR_RIGHT,
    ACTION_PAUSE,
    ACTION_MENU_SELECT,
    ACTION_32,
    ACTION_33,
    ACTION_MENU_BACK,

} ActionAxes_t;

#pragma pack(pop)

#define XboxInputs (*(XboxInputs_struct*)(0x002ff498))

float psiInput_GetJoystickLX(uint i);
float psiInput_GetJoystickLY(uint i);
float psiInput_GetJoystickRX(uint i);
float psiInput_GetJoystickRY(uint i);
uint psiInput_GetButtons(uint i);

void psiInput_ResetInputState(unsigned int i);
void psiInput_RumbleStart(ushort controllerNum, int time, int intensity);
void psiInput_RumbleUpdate(void);
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b);
void psiInputReset(void);
void psiInput_ResetRumble(unsigned int i);
bool psiInput_ControllerIsActive(unsigned int i);
void psiInput_MapInputs(PlayerInput_tag* playerInputs, int maxPlayers);


#endif // PSIINPUT_H_