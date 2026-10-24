// The cheat menu's checkboxes.
#include "ui.h"
#include "../sound/SFX.h"
#include "../engine/Camera.h"
#include "../engine/viewFov.h"
#include "../game.h"
#include "../input.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Player.h"
#include "HUD.h"

extern uint32_t SoundInfo; // defined in game.cpp: the music volume the cheat menu toggles between 0 and 100 (low word)
extern uint32_t switch_ForceDrawAll; // defined in view.cpp: draw every object, not just the visible cels'

// Music on/off: the checkbox shows whether the volume is non-zero, and selecting it sets 100 or 0.
// AUTOINJECT
bool C_CHCHMUSIC_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        ushort volume = __Menu_SendMessage(control, MessageType_GetValue, 0, 0) ? 100 : 0;
        SoundInfo = (SoundInfo & 0xffff0000) | volume;   // the original writes only the low word
        SFXMusicSetVolume(volume);
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, (SoundInfo & 0xffff) != 0, 0);
    }
    return true;
}

// Draw everything: the checkbox is the flag.
// AUTOINJECT
bool C_CHCHDRAWALL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        switch_ForceDrawAll = __Menu_SendMessage(control, MessageType_GetValue, 0, 0);
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, switch_ForceDrawAll, 0);
    }
    return true;
}

// The widescreen switches: the menu's, and the copy the cameras read (Camera_CalcViewAngles).
extern uint32_t switch_CONFIG_WIDESCREEN;
extern uint32_t IsWidescreen;

// Widescreen: the checkbox is the switch, and every viewer's projection is worked out again for the new shape -
// the players' at their field of view, the rest at the game's own.
// AUTOINJECT
bool C_CHCHWS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        switch_CONFIG_WIDESCREEN = __Menu_SendMessage(control, MessageType_GetValue, 0, 0);
        IsWidescreen = switch_CONFIG_WIDESCREEN;
        for (ushort viewer = 0; viewer < 9; viewer++)
            Camera_CalcViewAngles(viewer, ViewFov_ForViewer(viewer));
    } else if (message == MessageType_ControlCreated) {
        __Menu_SendMessage(control, MessageType_SetValue, switch_CONFIG_WIDESCREEN, 0);
    }
    return true;
}

// The other switches the checkboxes set. A switch only the cheat menu reads is ours.
extern uint32_t switch_NO_DRONES;
extern uint32_t switch_BLIND_DRONES;
extern int switch_SHOW_COORDS;
extern char Drone_DummyPlayer;
extern char switch_allowFreeze;  // Game_Run: the game may be frozen
extern char menu_unlock_everything;
extern uint8_t debug_scanmode;
extern uint8_t debug_clipmode;
// XBE_GLOBAL(0x0017d5f8, 0x1)
char hud_state = 1;
// XBE_GLOBAL(0x001dfa28, 0x1)
char Drone_DebugSpr;
// XBE_GLOBAL(0x0025d822, 0x1)
char zero_gravity;     // name ours
// XBE_GLOBAL(0x0017d5fc, 0x4)
int bDebugLockup = 1;
// XBE_GLOBAL(0x0025d7a4, 0x4)
int bTweakBrightness;
// XBE_GLOBAL(0x0017d600, 0x4)
int bDebugControls = 1;

// The movement substates the cheats put player 0 in (names ours)
#define PLAYER_SUBSTATE_NORMAL 0
#define PLAYER_SUBSTATE_FLY    5   // Player_SSScanMode
#define PLAYER_SUBSTATE_ZEROG  9

#define WEAPON_CHEAT_FIRST_WEAPON 0x01
#define WEAPON_CHEAT_LAST_WEAPON  0x47
#define WEAPON_CHEAT_FIRST_GADGET 0x4a
#define WEAPON_CHEAT_LAST_GADGET  0x5e
#define WEAPON_CHEAT_AMMO 999
#define WEAPON_CHEAT_ARMOR 50.0f

static int Checked(M_CONTROL *control) {
    return __Menu_SendMessage(control, MessageType_GetValue, 0, 0);
}

static void ShowChecked(M_CONTROL *control, int value) {
    __Menu_SendMessage(control, MessageType_SetValue, value, 0);
}

// Fly: player 0 moves freely through the level.
// AUTOINJECT
bool C_CHCHFLY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        uchar on = Checked(control);
        debug_scanmode = on;
        debug_clipmode = on;
        Player_ChangeSubState(glb_players[0], on ? PLAYER_SUBSTATE_FLY : PLAYER_SUBSTATE_NORMAL);
    } else if (message == MessageType_ControlCreated) {
        ShowChecked(control, debug_scanmode);
    }
    return true;
}

// Drones: ticked, they run; unticked sets switch_NO_DRONES.
// AUTOINJECT
bool C_CHCHDRONES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        switch_NO_DRONES = Checked(control) == 0;
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, switch_NO_DRONES == 0);
    return true;
}

// AUTOINJECT
bool C_CHCHBLIND_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        switch_BLIND_DRONES = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, switch_BLIND_DRONES);
    return true;
}

// All weapons: ticked, player 0 is given every weapon and gadget with full ammo, and armour. The checkbox sets
// the unlimited-ammo cheat too.
// AUTOINJECT
bool C_CHCHWEAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        CheatInfo.UnlimitedAmmo = Checked(control);
        CheatInfo.AllWeapons = Checked(control);
        if (CheatInfo.AllWeapons) {
            for (short weapon = WEAPON_CHEAT_FIRST_WEAPON; weapon <= WEAPON_CHEAT_LAST_WEAPON; weapon++)
                Player_EquipWeapon(glb_blokes[0], weapon, WEAPON_CHEAT_AMMO);
            for (short gadget = WEAPON_CHEAT_FIRST_GADGET; gadget <= WEAPON_CHEAT_LAST_GADGET; gadget++)
                Player_EquipWeapon(glb_blokes[0], gadget, WEAPON_CHEAT_AMMO);
            glb_blokes[0]->armor = WEAPON_CHEAT_ARMOR;
        }
        Player_CheckWeaponsLoaded(glb_blokes[0]);
    } else if (message == MessageType_ControlCreated) {
        ShowChecked(control, CheatInfo.AllWeapons);
    }
    return true;
}

// AUTOINJECT
bool C_CHCHHEALTH_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        CheatInfo.Immortal = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, CheatInfo.Immortal);
    return true;
}

// HUD: each player's crosshair option, its multiplayer copy and all the player's HUD panes follow the checkbox.
// AUTOINJECT
bool C_CHCHHUD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        hud_state = Checked(control);
        for (int player = 0; player < 4; player++) {
            if (glb_blokes[player] == NULL)
                continue;
            PlayerInputs[player].crosshairsEnabled = hud_state;
            MPSettings.Player[player].radar = hud_state;
            for (int pane = 0; pane < NUM_PANES; pane++)
                HUD_Enable(glb_blokes[player]->hudInfo, (HUD_PANE_IND)pane, hud_state, 0);
        }
    } else if (message == MessageType_ControlCreated) {
        ShowChecked(control, hud_state);
    }
    return true;
}

// AUTOINJECT
bool C_CHCHDEBUG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        Drone_DebugSpr = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, Drone_DebugSpr);
    return true;
}

// AUTOINJECT
bool C_CHCHCOORDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        switch_SHOW_COORDS = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, switch_SHOW_COORDS);
    return true;
}

// AUTOINJECT
bool C_CHCHALLOWFREEZE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        switch_allowFreeze = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, switch_allowFreeze);
    return true;
}

// AUTOINJECT
bool C_CHCHDUMMY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        Drone_DummyPlayer = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, Drone_DummyPlayer);
    return true;
}

// Zero gravity: player 0 floats.
// AUTOINJECT
bool C_CHCHZEROG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        zero_gravity = Checked(control);
        Player_ChangeSubState(glb_players[0], zero_gravity ? PLAYER_SUBSTATE_ZEROG : PLAYER_SUBSTATE_NORMAL);
    } else if (message == MessageType_ControlCreated) {
        ShowChecked(control, zero_gravity);
    }
    return true;
}

// AUTOINJECT
bool C_CHCHLOCKUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        bDebugLockup = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, bDebugLockup);
    return true;
}

// AUTOINJECT
bool C_CHCHBRIGHT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        bTweakBrightness = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, bTweakBrightness);
    return true;
}

// AUTOINJECT
bool C_CHCHUNLOCK_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        menu_unlock_everything = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, menu_unlock_everything);
    return true;
}

// AUTOINJECT
bool C_CHCHCONTROLS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select)
        bDebugControls = Checked(control);
    else if (message == MessageType_ControlCreated)
        ShowChecked(control, bDebugControls);
    return true;
}
