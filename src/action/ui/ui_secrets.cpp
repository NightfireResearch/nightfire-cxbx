#include "ui.h"
#include "Manager.h"
#include "Menu.h"

#include "../engine/Text.h"

#include <string.h>

// Secret Unlocks (docs/ui/secrets.md): item 0 of the codename menu opens the codename name page, P_CNNAME, in
// secret mode, and a code typed on its keyboard is checked against a table of 52 when Done is pressed. The same
// page and keyboard name a new codename otherwise.


// AUTOGEN
void __stdcall PlrStats_ResetScoring(void);
// AUTOGEN
undefined4 Menu_ValidateCodename(char *name);
// AUTOGEN
uint Menu_CodenameExists(byte *name);

// XBE_GLOBAL(0x00245200, 0x1)
#define kbd_text               ((char *)0x00245200)    // the text being typed: 8 characters in secret mode
// XBE_GLOBAL(0x0017d608, 0x1)
static uint8_t kbd_clear_on_key = 1; // the next key (other than Done) clears the text first
#define cn_bonus               (*(uint64_t *)0x0025d6e8) // the codename's rewards, a bit each (docs/ui/secrets.md)
// XBE_GLOBAL(0x0025ed68, 0x8)
#define kbd_accent_keys        ((ushort *)0x0025ed68)  // the strings of keys 100-103: A-umlaut, A-circumflex, AE, A-grave

#define KEY_DELETE 1000
#define KEY_SPACE  1001
#define KEY_DONE   1002
#define KEY_ACCENT_FIRST 100
#define SECRET_CODE_MAX 8

// The level whose rewards are in slot 'index' of the rewards table, as sp_level lists them.
static HASHCODE LevelAt(uint index) {
    for (uint i = 0; i < ARRAY_SIZE(sp_level); i++)
        if (i == index)
            return (HASHCODE)sp_level[i].identifier;
    return (HASHCODE)0xffffffff;
}

// The bonus with at least 'count' of object objId's upgrades in it: counts the upgrade rewards already owned,
// then sets unowned ones, level by level, until there are enough.
// AUTOINJECT
uint64_t Menu_UpgradeCheat(uint64_t bonus, uint objId, byte count) {
    REWARDINFO_tag info = {};
    byte owned = 0;
    for (uint level = 0; level < 12; level++)
        for (uint slot = 1; slot < 5; slot++) {
            PlrStarts_ProcessRewardCounter((ulong *)&bonus, LevelAt(level), slot, 0, &info);
            if (info.hasMedal && info.objType == 4 && info.objId == objId)
                owned++;
        }
    if (owned < count)
        for (uint level = 0; level < 12; level++)
            for (uint slot = 1; slot < 5; slot++) {
                PlrStarts_ProcessRewardCounter((ulong *)&bonus, LevelAt(level), slot, 0, &info);
                if (info.objType == 4) {
                    if (!info.hasMedal && info.objId == objId) {
                        PlrStarts_ProcessRewardCounter((ulong *)&bonus, LevelAt(level), slot, 1, &info);
                        owned++;
                    }
                    if (owned == count)
                        return bonus;
                }
            }
    return bonus;
}

typedef enum {
    SECRET_MISSIONS_ALL,    // every mission open
    SECRET_MISSION,         // one mission, toggled
    SECRET_REWARDS,         // rewards (high word of the bonus), set
    SECRET_REWARD,          // one reward (high word of the bonus), toggled
    SECRET_UPGRADES,        // weapon/gadget upgrades, added
} SecretKind;

typedef struct { byte objId, count; } SecretUpgrade;

typedef struct {
    const char *code;
    SecretKind kind;
    uint value;                 // the mission bit or reward bits
    SecretUpgrade upgrades[9];  // SECRET_UPGRADES: (object, how many of its upgrades), count 0 ends the list
} SecretCode;

// Missions are sp_level indices; rewards are bits 32 and up of the bonus (see docs/ui/secrets.md for which).
// The first match wins; there are no duplicate codes, but a few pairs do the same thing.
static const SecretCode secret_codes[] = {
    { "PASSPORT", SECRET_MISSIONS_ALL, 0xffffffff },
    { "POWDER",   SECRET_MISSION, 1 << 2 },      // Alpine Escape
    { "TRACTION", SECRET_MISSION, 1 << 3 },      // Enemies Vanquished
    { "BONSAI",   SECRET_MISSION, 1 << 4 },      // Double Cross
    { "HIGHRISE", SECRET_MISSION, 1 << 5 },      // Night Shift
    { "MELTDOWN", SECRET_MISSION, 1 << 6 },      // Chain Reaction
    { "FLAME",    SECRET_MISSION, 1 << 7 },      // Phoenix Fire
    { "AQUA",     SECRET_MISSION, 1 << 8 },      // Deep Descent
    { "PARADISE", SECRET_MISSION, 1 << 9 },      // Island Infiltration
    { "BLASTOFF", SECRET_MISSION, 1 << 10 },     // Countdown
    { "VACUUM",   SECRET_MISSION, 1 << 11 },     // Equinox
    { "PARTY",    SECRET_REWARDS, 0x00ffefc0 },  // every multiplayer character
    { "BOWLER",   SECRET_REWARD, 0x40 },         // Oddjob
    { "BLACKTIE", SECRET_REWARD, 0x80 },         // Bond (tuxedo)
    { "MARTIAL",  SECRET_REWARD, 0x100 },        // Wai Lin
    { "BITESIZE", SECRET_REWARD, 0x200 },        // Nick Nack
    { "JOELWADE", SECRET_REWARD, 0x200 },        // Nick Nack
    { "ASSASSIN", SECRET_REWARD, 0x400 },        // Scaramanga
    { "DENTAL",   SECRET_REWARD, 0x800 },        // Jaws
    { "NUMBER 1", SECRET_REWARD, 0x400000 },     // Drake (suit)
    { "VOODOO",   SECRET_REWARD, 0x2000 },       // Baron Samedi
    { "JANUS",    SECRET_REWARD, 0x4000 },       // Xenia Onatopp
    { "MIDAS",    SECRET_REWARD, 0x8000 },       // Goldfinger
    { "BADGIRL",  SECRET_REWARD, 0x10000 },      // May Day
    { "SLICK",    SECRET_REWARD, 0x20000 },      // Elektra King
    { "NUCLEAR",  SECRET_REWARD, 0x40000 },      // Christmas Jones
    { "CIRCUS",   SECRET_REWARD, 0x80000 },      // Pussy Galore
    { "HEADCASE", SECRET_REWARD, 0x100000 },     // Renard
    { "BLIMP",    SECRET_REWARD, 0x200000 },     // Max Zorin
    { "HUGE EGO", SECRET_REWARD, 0x200000 },     // Max Zorin
    { "ZERO G",   SECRET_REWARD, 0x800000 },     // Bond (space suit)
    { "GAMEROOM", SECRET_REWARDS, 0x3f000000 },  // every multiplayer scenario
    { "TARGET",   SECRET_REWARD, 0x1000000 },    // Assassination
    { "TRANSMIT", SECRET_REWARD, 0x2000000 },    // Uplink
    { "TEAMWORK", SECRET_REWARD, 0x4000000 },    // Team King of the Hill
    { "TNT",      SECRET_REWARD, 0x8000000 },    // Demolition
    { "GUARDIAN", SECRET_REWARD, 0x10000000 },   // Protection
    { "ORBIT",    SECRET_REWARD, 0x20000000 },   // GoldenEye Strike
    { "BOOM",     SECRET_REWARD, 0x40000000 },   // Explosive Scenery
    { "Q LAB",    SECRET_UPGRADES, 0, { { 9, 2 }, { 24, 1 }, { 21, 1 }, { 18, 1 }, { 15, 1 }, { 0, 3 }, { 12, 1 }, { 6, 1 }, { 3, 1 } } },
    { "AU PP7",   SECRET_UPGRADES, 0, { { 0, 1 } } },    // pistol, first upgrade
    { "LIFTOFF",  SECRET_UPGRADES, 0, { { 3, 1 } } },    // grapple
    { "SHUTTER",  SECRET_UPGRADES, 0, { { 6, 1 } } },    // camera
    { "SCOPE",    SECRET_UPGRADES, 0, { { 9, 1 } } },    // sniper rifle, first upgrade
    { "P2000",    SECRET_UPGRADES, 0, { { 0, 2 } } },    // pistol, second upgrade
    { "SLEEPY",   SECRET_UPGRADES, 0, { { 12, 1 } } },   // dart gun
    { "AU P2K",   SECRET_UPGRADES, 0, { { 0, 3 } } },    // pistol, third upgrade
    { "SESAME",   SECRET_UPGRADES, 0, { { 15, 1 } } },   // decryptor
    { "ZAP",      SECRET_UPGRADES, 0, { { 18, 1 } } },   // stunner
    { "PHOTON",   SECRET_UPGRADES, 0, { { 21, 1 } } },   // laser
    { "MAGAZINE", SECRET_UPGRADES, 0, { { 9, 2 } } },    // sniper rifle, second upgrade
    { "LAUNCH",   SECRET_UPGRADES, 0, { { 24, 1 } } },   // missile
};
static_assert(ARRAY_SIZE(secret_codes) == 52, "the game has 52 secret codes");

// Applies a secret code to the codename being edited: its missions (sp_level[].enabled) and its bonus. Returns
// true in EAX with EDX clear, as the original (its caller tests both); false leaves everything as it was.
// AUTOINJECT
uint64_t Menu_SpecialCodenameCheck(byte *code) {
    uint64_t bonus = cn_bonus;
    uint missions = Menu_GetNightfireStatus();

    for (uint i = 0; i < ARRAY_SIZE(secret_codes); i++) {
        const SecretCode *s = &secret_codes[i];
        if (strcmp((const char *)code, s->code) != 0)
            continue;
        switch (s->kind) {
        case SECRET_MISSIONS_ALL: missions = s->value; break;
        case SECRET_MISSION:      missions ^= s->value; break;
        case SECRET_REWARDS:      bonus |= (uint64_t)s->value << 32; break;
        case SECRET_REWARD:       bonus ^= (uint64_t)s->value << 32; break;
        case SECRET_UPGRADES: {
            // each object's upgrades are worked out from the bonus as it was, then all are combined
            uint64_t combined = bonus;
            for (int u = 0; u < ARRAY_SIZE(s->upgrades) && s->upgrades[u].count != 0; u++)
                combined |= Menu_UpgradeCheat(bonus, s->upgrades[u].objId, s->upgrades[u].count);
            bonus = combined;
            break;
        }
        }
        // Every code sets both: a code that closes the last open mission leaves the first two open.
        if (missions == 0)
            missions = 3;
        for (uint level = 0; level < ARRAY_SIZE(sp_level); level++)
            ITEM_ENABLED(sp_level[level]) = (missions >> level) & 1;
        Menu_SetBonus((uint)bonus, (uint)(bonus >> 32), 0, 0);
        return 1;
    }
    return 0;
}

// Sets up a save of the codename just named (a new one, or overwriting one of that name), which the message box
// on P_CNNAME then runs.
static void SaveNewCodename(uchar managerNum) {
    PlrStats_ResetScoring();
    ls.busy = 1;
    ls.slot = 999;
    ls.returnPage = P_CNMENU;
    ls.field3_0xc = 1;
    ls.operation = LS_OPERATION_SAVE;
    ls.field9_0x3a = 0;
    ls.field5_0x14 = 0;
    ls.field17_0x42 = 0;
    Menu_UpdateMessageBox(managerNum, 0xff, 0);
}

// AUTOINJECT
bool P_CNNAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        cn_secret_mode = (HASHCODE)arg2 == P_CNMENU;
        ls.busy = 0;
        kbd_clear_on_key = 1;
        if (cn_secret_mode) {
            kbd_text[0] = 0;
            LABEL_SET_TEXT(managerNum, SUB_P_CNNAME_TITLE, Txt_BindLabel(SECRET_UNLOCKS, 0));
        } else {
            LABEL_SET_TEXT(managerNum, SUB_P_CNNAME_TITLE, Txt_BindLabel(ENTER_NEW_CODENAME, 0));
            memcpy(kbd_text, "BOND\0\0\0", 8);
        }
        LABEL_SET_TEXT(managerNum, SUB_P_CNNAME_TEXT, kbd_text);
        // the cursor starts on Done
        int done = __Menu_SendEx(managerNum, C_KEYBOARD, KEY_DONE, MessageType_GetControl, 0, 0);
        Manager_SendMessage(&manager[managerNum], MessageType_SetCursor, 0, done);
        kbd_accent_keys[0] = 0xc4;
        kbd_accent_keys[1] = 0xc2;
        kbd_accent_keys[2] = 0xc6;
        kbd_accent_keys[3] = 0xc0;
        for (int k = 0; k < 4; k++)
            __Menu_SendEx(managerNum, C_KEYBOARD, KEY_ACCENT_FIRST + k, MessageType_SetText, (int)&kbd_accent_keys[k], 0);
    } else if (message == MessageType_PageUpdate) {
        Menu_UpdateMessageBox(managerNum, 0xff, 0);
        undefined4 box = 0;
        undefined4 answer = Menu_UpdateOptionBox(&box);
        if (box == OPTIONBOX_OVERWRITE_CODENAME) {
            if (answer == 1)
                SaveNewCodename(managerNum);
        } else if (box == OPTIONBOX_UNLOCK_SUCCESS && answer != 0) {
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
        }
    }
    return true;
}

// The on-screen keyboard: every key is a button whose id is its character, or one of the KEY_* commands.
// AUTOINJECT
bool C_KEYBOARD_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message != MessageType_Select && message != MessageType_SelectRepeat)
        return true;

    int key = control->id;
    if (key != KEY_DONE && kbd_clear_on_key) {
        // the first key typed replaces the text the page started with
        kbd_text[0] = 0;
        kbd_clear_on_key = 0;
        if (key == KEY_SPACE)
            return true;
    }
    switch (key) {
    case KEY_DELETE:
        // as the original: with nothing typed, this clears the byte before the buffer
        kbd_text[strlen(kbd_text) - 1] = 0;
        break;
    case KEY_SPACE:
        if (strlen(kbd_text) < SECRET_CODE_MAX)
            strcat(kbd_text, " ");
        break;
    case KEY_DONE:
        if (cn_secret_mode) {
            if (Menu_SpecialCodenameCheck((byte *)kbd_text)) {
                Menu_CreateOptionBoxLabel(managerNum, UNLOCK_SUCCESS, OPTIONBOX_UNLOCK_SUCCESS, 1, 0);
                cn_modified = 1;
            }
            break;   // a wrong code does nothing
        }
        {
            Action_TranslatedText problem = (Action_TranslatedText)Menu_ValidateCodename(kbd_text);
            if (problem != TXT_NULL) {
                Menu_CreateOptionBoxLabel(managerNum, problem, OPTIONBOX_BAD_CODENAME, 1, 0);
                kbd_clear_on_key = 1;
                break;
            }
            ls.slot = 999;
            strcpy(ls.codename, kbd_text);
            if ((uchar)Menu_CodenameExists((byte *)kbd_text)) {
                Menu_CreateOptionBoxLabel(managerNum, CODENAME_EXISTS_OVERWRITE, OPTIONBOX_OVERWRITE_CODENAME, 0, 0);
                break;
            }
            SaveNewCodename(managerNum);
        }
        break;
    default:
        if (strlen(kbd_text) < SECRET_CODE_MAX) {
            const char *character = (const char *)__Menu_SendMessage(control, MessageType_GetText, 0, 0);
            strncat(kbd_text, character, 1);
        }
        break;
    }
    return true;
}
