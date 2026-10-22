#include "actionhelpers.h"

void Input_Update(void);
void Input_UpdateWithMouse(void);
void Input_RumbleStop(ushort playerNum);
void _StickCompensation2(float *x, float *y);
void StickCompensation2(void);
void __fastcall StickCompensation3(float *x, float *y);
unsigned short Input_Action(short player, GameActions_tag action,unsigned char flags);
float __cdecl Input_Actionf(short playerNum, GameActions_tag action,unsigned char flags);
void __cdecl Input_ClearAction(short playerNum, GameActions_tag action);
void __cdecl Input_SetAction(short playerNum, GameActions_tag action,unsigned char val);
bool Input_ChangeControllerStyle(ushort playerNum, int controllerStyle);
void Input_ClearAllActions(short playerNum);
void Input_Ready(void);
void Input_Init(void);
void Input_RumbleStart(ushort playerNum, int time, int intensity);

typedef struct {
    byte inverted; /* Inverts fChannel[4,5] */
    undefined autoAim;          // 0x1 single player (PS2 name)
    undefined autoAimMultiplayer; // 0x2
    undefined manualAimToggle; // 0x3 - the "aim: toggle/hold" option; non-zero = toggle
    undefined crouchToggle;     // 0x4 (PS2 name)
    undefined field5_0x5;
    undefined rememberZoom;     // 0x6 - the scope comes up at the zoom it was left at (Player_ResetZoom)
    undefined levelViewOnPace; // 0x7 - a change of walking pace sets the view levelling (Ghidra: someSettingRelatedToCrouch)
    undefined crosshairsEnabled;
    undefined1 vibrationEnabled; /* Created by retype action */
    undefined1 autoSwitchBetterWeapon; /* Created by retype action */
    undefined hudVisibility;    // 0xb - Ghidra's name; on, the health bars fade out (Player_Update)
    undefined flashingObjects;  // 0xc - a codename option: objects that want it flash near the view (view.cpp)
    undefined field13_0xd;
    short controlStyle;
    short controlStyleDriving; // 0x10 - the driving engine's control style (Boot_GetPTPData hands it over at +0xa14)
    undefined field17_0x12;
    undefined field18_0x13;
    float fChannels[40];
    ushort holdFrames[40];      // 0xb4 - frames each action has been held (to 0x7fff)
    uchar actions[40]; /* Created by retype action */
    uchar actionLock[40];       // 0x12c - ACTIONLOCK_: an action is not reported while its lock holds
    undefined anyInput;         // 0x154 - some action has a value this frame (Input_Update)
    uchar controllerIsActive;   // 0x155 - psiInput_MapInputs (the PlayerInput_tag view of these records)
    undefined1 controllerPort; // 0x156 - the pad's port
    undefined field144_0x157;
} PlayerInput;

static_assert(sizeof(PlayerInput) == 0x158, "Bad size for PlayerInput");
static_assert(offsetof(PlayerInput, holdFrames) == 0xb4, "Bad offset of PlayerInput.holdFrames");
static_assert(offsetof(PlayerInput, actionLock) == 0x12c, "Bad offset of PlayerInput.actionLock");
static_assert(offsetof(PlayerInput, anyInput) == 0x154, "Bad offset of PlayerInput.anyInput");

// PlayerInputs is an array of 4 PlayerInput structs, located at 001fe6d0
extern PlayerInput PlayerInputs[4];

void Input_ProcessEvents(void);

