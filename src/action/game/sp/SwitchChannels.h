#ifndef SWITCHCHANNELS_H_
#define SWITCHCHANNELS_H_

#include "../../actionhelpers.h"

// 256 bytes of per-level boolean state, cleared by Init_SwitchChannels on a cold level load - the wiring that
// most of a level's scripting is built out of. Nearly every use takes its channel number from the map
// data, so the number means whatever that level says it means; a few dozen are hard-coded in the
// engine instead (SwitchChannel below).
extern char switch_channels[256];
extern char switch_channels_hold[256];
extern char switch_channels_prev[256];
extern uint32_t switch_channels_time[256];
extern ushort switch_channels_MusicVars[256]; // frames each channel has been on (DroneFunc_CheckAlarmRaised)

// The switch channels the engine itself reads or writes. The authors numbered channels per level, and a mission's
// parts share them (a warm reset keeps them, and Player_RamLoad restores them), so only those with one meaning
// game-wide have plain names; those the code uses only under a level guard are named for that level; the rest stay
// numbers at their uses. docs/switch-channels.md has the census. 0x60 to 0x63 each also stop a level-load trigger.
typedef enum {
    SWITCH_SPACESTATIOND_HUD_BLINK0 = 0x09,     // HUD_UpdateSpacePane: the pane's sprite 0 blinks while on
    SWITCH_SPACE_DRAKE = 0x0a,                  // DSTATE_SpaceDrake: the drake takes bullet impacts, and is deleted
    SWITCH_SPACESTATIOND_HUD_BLINK9 = 0x18,     // HUD_UpdateSpacePane: sprite 9 blinks while on
    SWITCH_SPACESTATIOND_HUD_BLINK10 = 0x1c,    // HUD_UpdateSpacePane: sprite 10 blinks while on
    SWITCH_REMOVE_DYING = 0x35,                 // dying drones whose death channel is 0x88, 0x29 or 0x2a are removed
    SWITCH_CASTLE_ALARM = 0x37,                 // CastleExterior, CastleCourtyard: ten seconds of the alarm music
    SWITCH_CASTLEINDOORS1_ARMED = 0x48,         // until on: 0.9 speed, no jump, unarmed weapon cycle; P2K as it goes on
    SWITCH_CASTLEINDOORS2_ZOE_LAST_LINE = 0x4a, // Zoe says line 0x9e and stops talking
    SWITCH_CASTLEINDOORS2_STONE_DEBRIS = 0x53,  // explosions of materials 8, 10 and 11 throw stone debris
    SWITCH_PLAYER_DIED = 0x5e,                  // set as the player dies in single player, cleared by Player_Init
    SWITCH_PLAYER_NO_PAIN = 0x5f,               // Player_HandlePain does nothing
    SWITCH_FAIL_CIVILIAN_KILLED = 0x60,         // fails the mission (TXT_MISSION_FAIL_KILLED_CIVILIAN)
    SWITCH_FAIL_ALARM = 0x61,                   // fails the mission (TXT_MISSION_FAIL_ALARM_TRIGGERED), alarm music;
                                                // the player can't flip a switch on it
    SWITCH_PLAYER_FINISHED_DYING = 0x62,        // set once the blood pane has run and skip is pressed: the fail screen
    SWITCH_MISSION_FAILED = 0x63,               // Mission_Update fails the mission
    SWITCH_MISSION_WON = 0x64,                  // Mission_Update ends the mission won
    SWITCH_BOND_MOMENT_FIRST = 0x65,            // ten bond moments, each scored while its channel is on
    SWITCH_BOND_MOMENT_LAST = 0x6e,
    SWITCH_TOWERC_SSYS_SPEAKS = 0x76,           // SSys_Msg is quiet until on
    SWITCH_EVILBASEC_FAIL_KIKO = 0x7b,          // fails the mission (TXT_MISSION_FAIL_KIKO_ESCAPE)
    SWITCH_TOWER2B_FELL = 0x7c,                 // set when the player is below y = -10
    SWITCH_LEVEL7001500_TOUCH = 0x7d,           // a touch trigger sets it only in HT_Level_Level7001500
    SWITCH_GLOBAL_ALARM = 0x96,                 // DRONE_MSG_GlobalAlarm every 30 frames (once in EvilBase)
    SWITCH_ENEMY_ALERTED_NEAR_LATCHED = 0x97,   // set once an alerted enemy drone is within 30 m of its opponent
    SWITCH_ENEMY_ALERTED_NEAR = 0x98,           // whether one is, each frame; the Powerstation loudspeakers need it
    SWITCH_MISSION_WAIT = 0x99,                 // until on, a mission wait node holds the ally
    SWITCH_ZOE_LEASH = 0x9b,                    // while on, the player more than 10 m from Zoe fails the mission
    SWITCH_CASTLEEXTERIOR_ALARM = 0xa4,         // a second of the castle alarm music; sets 0x6f
    SWITCH_SPACESTATIOND_FAIL_MISSILE = 0xc7,   // fails the mission (TXT_MISSION_FAIL_MISSILE_LAUNCHED)
    SWITCH_MP_SCORE_REACHED = 0xfd,             // multiplayer: a score has reached the limit, the game ends
    SWITCH_MP_TIME_UP = 0xfe,                   // multiplayer: the time limit has passed, the game ends
} SwitchChannel;

void Init_SwitchChannels(void);

void SwitchChannel_SetActive(int ch);
bool SwitchChannel_IsActive(int ch);

#endif // SWITCHCHANNELS_H_
