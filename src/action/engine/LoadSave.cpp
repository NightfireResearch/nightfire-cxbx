#include "LoadSave.h"

#include "psiSave.h"
#include "Text.h"
#include "../input.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/PlayerStats.h"
#include "../sound/SFX.h"
#include "../ui/Menu.h"

#include <string.h>

// A codename's save (docs/savestate/). Each block is a header - its label and its length in bytes - and then its
// fields, packed a bit at a time (BIN_PushBits) from bit 0x40. A save is the blocks one after another, in the
// table's order; a load finds each by its label.

extern uint32_t SubtitlesEnabled;
extern uint32_t IsWidescreen;


#define SAVE_LABEL(a, b, c, d) ((uint)(a) | (uint)(b) << 8 | (uint)(c) << 16 | (uint)(d) << 24)
#define SAVE_FIELDS_START 0x40   // the first field's bit, after the header

#pragma pack(push, 1)
typedef struct SaveBlockHeader {
    uint label;     // four characters, as they read in the file
    uint length;    // in bytes, the header included
} SaveBlockHeader;
#pragma pack(pop)

typedef uchar *(*SaveBlockMake)(uint label, ushort slot, ushort *length);
typedef void (*SaveBlockLoad)(uchar *block, ushort slot);

typedef struct SaveIFFBlock {
    uint bitfield;          // SAVE_*
    uint label;
    SaveBlockMake make;     // builds the block in its own buffer
    SaveBlockLoad load;     // applies a block found in a save
} SaveIFFBlock;

// XBE_GLOBAL(0x0017c238, 0x60)
static const SaveIFFBlock SaveIFFBlocks[] = {
    {SAVE_PLRS, SAVE_LABEL('P', 'L', 'R', 'S'), LS_MakePlrSettings, LS_LoadPlrSettings},
    {SAVE_MSSN, SAVE_LABEL('M', 'S', 'S', 'N'), LS_MakeMission, LS_LoadMission},
    {SAVE_MPSG, SAVE_LABEL('M', 'P', 'S', 'G'), LS_MakeMPSettings, LS_LoadMPSettings},
    {SAVE_GSET, SAVE_LABEL('G', 'S', 'E', 'T'), LS_MakeGlobalSettings, LS_LoadGlobalSettings},
    {SAVE_CHET, SAVE_LABEL('C', 'H', 'E', 'T'), LS_MakeCheats, LS_LoadCheats},
    {SAVE_BNUS, SAVE_LABEL('B', 'N', 'U', 'S'), LS_MakeBonus, LS_LoadBonus},
};
// XBE_GLOBAL(0x0017c230, 0x4)
static const ushort NumSaveBlocks = ARRAY_SIZE(SaveIFFBlocks);

// The save being written, and the last one loaded (or saved), per slot. The slot is a multiplayer load's player.
// XBE_GLOBAL(0x00215790, 0x4000)
uchar DstData[LS_SLOTS][LS_SLOT_SIZE];
// XBE_GLOBAL(0x00219790, 0x4000)
uchar InBufData[LS_SLOTS][LS_SLOT_SIZE];
// XBE_GLOBAL(0x0021d790, 0x2)
ushort Load_State;
// XBE_GLOBAL(0x0021d794, 0x2)
ushort Save_State;
// The slot a load in progress is for
// XBE_GLOBAL(0x002237b4, 0x2)
ushort LoadSlot;

// Each block is built in a buffer of its own
// XBE_GLOBAL(0x0021d798, 0x1000)
static uchar PlrSettingsBlock[0x1000];
// XBE_GLOBAL(0x0021e798, 0x1000)
static uchar CheatsBlock[0x1000];
// XBE_GLOBAL(0x0021f798, 0x1000)
static uchar MissionBlock[0x1000];
// XBE_GLOBAL(0x00220798, 0x1000)
static uchar BonusBlock[0x1000];
// XBE_GLOBAL(0x00221798, 0x1000)
static uchar MPSettingsBlock[0x1000];
// XBE_GLOBAL(0x00222798, 0x1000)
static uchar GlobalSettingsBlock[0x1000];

// AUTOINJECT
void LS_FlushStates(void) {
    Load_State = LS_STATE_IDLE;
    Save_State = LS_STATE_IDLE;
}

// The block with that label in the slot's last save, and its length; NULL (length 0) if there is none. The walk
// ends at a zero label or the end of the slot; a block of length 0 would hold it in place.
uchar* _LS_FindBlockByLabel(ushort slot, uint label, ushort *length) {
    uchar *at = InBufData[slot];
    for (uint offset = 0; offset < LS_SLOT_SIZE;) {
        const SaveBlockHeader *block = (const SaveBlockHeader *)at;
        if (block->label == label) {
            *length = (ushort)block->length;
            return at;
        }
        if (block->label == 0)
            break;
        offset += block->length;
        at += block->length;
    }
    *length = 0;
    return NULL;
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the slot in AX and the label in EDI.
// AUTOLTCG
void __declspec(naked) LS_FindBlockByLabel(void) {
    _asm {
        push dword ptr [esp + 4]
        push edi
        movzx eax, ax
        push eax
        call _LS_FindBlockByLabel
        add esp, 0xc
        ret
    }
}
#endif

// Takes a buffer handed over from outside (Boot_LoadPTPData's) as slot 0's last save, if its first word is not
// zero. The length is not checked against the slot.
// AUTOINJECT
int LS_LoadFromBuffer(uchar *data, uint length) {
    if (*(uint *)data == 0)
        return 0;
    memset(InBufData[0], 0, sizeof(InBufData[0]));
    memcpy(InBufData[0], data, length);
    return 1;
}

// Builds the slot's save in DstData: the masked blocks afresh, the others copied from its last save (where they
// are in it). Returns its size.
// AUTOINJECT
ushort LS_GetSaveStuff(ushort mask, ushort slot) {
    ushort size = 0;
    for (ushort i = 0; i < NumSaveBlocks; i++) {
        const SaveIFFBlock *entry = &SaveIFFBlocks[i];
        ushort length = 0;
        uchar *block;
        if (entry->bitfield & mask)
            block = entry->make(entry->label, slot, &length);
        else
            block = _LS_FindBlockByLabel(slot, entry->label, &length);
        if (block != NULL) {
            memcpy(&DstData[slot][size], block, length);
            size += length;
        }
    }
    return size;
}

// Applies the masked blocks the slot's last save has.
void _LS_GetLoadStuff(ushort mask, ushort slot) {
    for (ushort i = 0; i < NumSaveBlocks; i++) {
        const SaveIFFBlock *entry = &SaveIFFBlocks[i];
        if (entry->bitfield & mask) {
            ushort length;
            uchar *block = _LS_FindBlockByLabel(slot, entry->label, &length);
            if (block != NULL)
                entry->load(block, slot);
        }
    }
}

#ifndef NIGHTFIRE_STANDALONE
// The original takes the slot in BX.
// AUTOLTCG
void __declspec(naked) LS_GetLoadStuff(void) {
    _asm {
        movzx eax, bx
        push eax
        push dword ptr [esp + 8]
        call _LS_GetLoadStuff
        add esp, 8
        ret
    }
}
#endif

// Saves the slot's masked blocks under the codename. Returns the state it found, so the caller sees the result a
// call later.
// AUTOINJECT
uint LS_Save(char *codename, uint slot, ushort mask) {
    uint state = Save_State;
    if (state == LS_STATE_IDLE) {
        ushort saveSlot = slot;
        uint size = LS_GetSaveStuff(mask, slot);
        psiSaveData(DstData[saveSlot], size, codename);
        memcpy(InBufData[saveSlot], DstData[saveSlot], size);   // whether or not the save worked
        Save_State = LS_STATE_SAVING;
    } else if (state != LS_STATE_SAVING) {
        return state;
    }
    int result = psiSavingData();
    if (result == 2)
        Save_State = LS_STATE_DONE;
    else if (result == 4)
        Save_State = LS_STATE_FAILED;
    return state;
}

// Loads the codename's save into the slot and applies its masked blocks. Returns the state it found.
// AUTOINJECT
uint LS_Load(char *codename, ushort slot, ushort mask) {
    uint state = Load_State;
    if (state == LS_STATE_IDLE) {
        LoadSlot = slot;
        uint size = LS_GetSaveStuff(0xff, 0);   // what a full save comes to now: as much as is read
        memset(InBufData[LoadSlot], 0, sizeof(InBufData[LoadSlot]));
        psiLoadData(InBufData[LoadSlot], &size, codename);
        Load_State = LS_STATE_LOADING;
    } else if (state != LS_STATE_LOADING) {
        return state;
    }
    int result = psiLoadingData();
    if (result == 2) {
        Load_State = LS_STATE_DONE;
        _LS_GetLoadStuff(mask, LoadSlot);
    } else if (result == 4) {
        Load_State = LS_STATE_FAILED;
    }
    return state;
}

// ---- the blocks

static void StartBlock(uchar *block, uint label, uint *cursor) {
    ((SaveBlockHeader *)block)->label = label;
    *cursor = SAVE_FIELDS_START;
}

static uchar *EndBlock(uchar *block, uint cursor, ushort *length) {
    uint bytes = (cursor + 7) / 8;
    ((SaveBlockHeader *)block)->length = bytes;
    *length = bytes;
    return block;
}

// AUTOINJECT
uchar* LS_MakePlrSettings(uint label, ushort slot, ushort *length) {
    const PlayerInput *input = &PlayerInputs[slot];
    uint cursor;
    StartBlock(PlrSettingsBlock, label, &cursor);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->inverted, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->controlStyle, 4);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->controlStyleDriving, 4);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->autoAim, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->autoAimMultiplayer, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->manualAimToggle, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->autoSwitchBetterWeapon, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->crouchToggle, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->vibrationEnabled, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->crosshairsEnabled, 1);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->flashingObjects, 2);
    BIN_PushBits(PlrSettingsBlock, &cursor, input->hudVisibility, 1);
    return EndBlock(PlrSettingsBlock, cursor, length);
}

// AUTOINJECT
void LS_LoadPlrSettings(uchar *block, ushort slot) {
    PlayerInput *input = &PlayerInputs[slot];
    uint cursor = SAVE_FIELDS_START;
    BIN_PullBits_U8(block, &cursor, &input->inverted, 1);
    BIN_PullBits_U16(block, &cursor, (ushort *)&input->controlStyle, 4);
    BIN_PullBits_U16(block, &cursor, (ushort *)&input->controlStyleDriving, 4);
    BIN_PullBits_U8(block, &cursor, &input->autoAim, 1);
    BIN_PullBits_U8(block, &cursor, &input->autoAimMultiplayer, 1);
    BIN_PullBits_U8(block, &cursor, &input->manualAimToggle, 1);
    BIN_PullBits_U8(block, &cursor, &input->autoSwitchBetterWeapon, 1);
    BIN_PullBits_U8(block, &cursor, &input->crouchToggle, 1);
    BIN_PullBits_U8(block, &cursor, &input->vibrationEnabled, 1);
    BIN_PullBits_U8(block, &cursor, &input->crosshairsEnabled, 1);
    BIN_PullBits_U8(block, &cursor, &input->flashingObjects, 2);
    BIN_PullBits_U8(block, &cursor, &input->hudVisibility, 1);
}

// The missions open, then each level's best score and Bond moments.
// AUTOINJECT
uchar* LS_MakeMission(uint label, ushort slot, ushort *length) {
    uint cursor;
    StartBlock(MissionBlock, label, &cursor);
    BIN_PushBits(MissionBlock, &cursor, Menu_GetNightfireStatus(), 32);
    uint count;
    const SCORETABLE *table = PlrStats_GetScoreTable(&count);
    BIN_PushBits(MissionBlock, &cursor, count, 8);
    for (uint i = 0; i < count; i++) {
        BIN_PushBits(MissionBlock, &cursor, table[i].bestScore, 32);
        BIN_PushBits(MissionBlock, &cursor, table[i].statsTable[SCORE_BOND_MOMENTS].achieved, 4);
    }
    BIN_PushBits(MissionBlock, &cursor, GameState.WeaponUpgradeRelated, 1);
    return EndBlock(MissionBlock, cursor, length);
}

// The scores go in through PlrStats_SetupScoreTable, from tables that hold only the best score and Bond moments.
// The count is not checked against the 20 the tables have room for.
// AUTOINJECT
void LS_LoadMission(uchar *block, ushort slot) {
    uint cursor = SAVE_FIELDS_START;
    uint status;
    BIN_PullBits_U32(block, &cursor, &status, 32);
    Menu_SetNightfireStatus(status);

    SCORETABLE tables[20];
    ScoreCategory categories[20][SCORE_NUM_CATEGORIES];
    memset(tables, 0, sizeof(tables));
    memset(categories, 0, sizeof(categories));
    uint count;
    BIN_PullBits_U32(block, &cursor, &count, 8);
    for (uint i = 0; i < count; i++) {
        tables[i].statsTable = categories[i];
        BIN_PullBits_U32(block, &cursor, &tables[i].bestScore, 32);
        BIN_PullBits_U32(block, &cursor, &tables[i].statsTable[SCORE_BOND_MOMENTS].achieved, 4);
    }
    BIN_PullBits_U8(block, &cursor, &GameState.WeaponUpgradeRelated, 1);
    PlrStats_SetupScoreTable(tables, count);
}

// AUTOINJECT
uchar* LS_MakeMPSettings(uint label, ushort slot, ushort *length) {
    const MPSettings_PerPlayer *player = &MPSettings.Player[slot];
    uint cursor;
    StartBlock(MPSettingsBlock, label, &cursor);
    BIN_PushBits(MPSettingsBlock, &cursor, player->SomeField2, 1);
    BIN_PushBits(MPSettingsBlock, &cursor, player->HealthModifier, 32);
    return EndBlock(MPSettingsBlock, cursor, length);
}

// AUTOINJECT
void LS_LoadMPSettings(uchar *block, ushort slot) {
    MPSettings_PerPlayer *player = &MPSettings.Player[slot];
    uint cursor = SAVE_FIELDS_START;
    BIN_PullBits_U32(block, &cursor, &player->SomeField2, 1);
    BIN_PullBits_U32(block, &cursor, &player->HealthModifier, 32);
}

#define GSET_RESERVED_WORDS 7
#define GSET_VERSION 4

// AUTOINJECT
uchar* LS_MakeGlobalSettings(uint label, ushort slot, ushort *length) {
    uint cursor;
    StartBlock(GlobalSettingsBlock, label, &cursor);
    uint language = GetLanguage();
    uint music = SFXMusicGetVolume();
    uint sfx = SFXGetVolume();
    uint mode = SFXGetMode();
    BIN_PushBits(GlobalSettingsBlock, &cursor, music, 7);
    BIN_PushBits(GlobalSettingsBlock, &cursor, sfx, 7);
    BIN_PushBits(GlobalSettingsBlock, &cursor, language, 7);
    BIN_PushBits(GlobalSettingsBlock, &cursor, SubtitlesEnabled, 1);
    BIN_PushBits(GlobalSettingsBlock, &cursor, mode, 32);
    BIN_PushBits(GlobalSettingsBlock, &cursor, IsWidescreen, 1);
    BIN_PushBits(GlobalSettingsBlock, &cursor, MultiplayerLayout_LeftRightOrTopBtm, 32);
    for (int i = 0; i < GSET_RESERVED_WORDS; i++)
        BIN_PushBits(GlobalSettingsBlock, &cursor, 0, 32);
    BIN_PushBits(GlobalSettingsBlock, &cursor, GSET_VERSION, 16);
    return EndBlock(GlobalSettingsBlock, cursor, length);
}

// Only the volumes, subtitles and layout are applied: the language, sound mode and widescreen are read past.
// AUTOINJECT
void LS_LoadGlobalSettings(uchar *block, ushort slot) {
    uint cursor = SAVE_FIELDS_START;
    uint music, sfx, language, mode, widescreen, reserved;
    ushort version;
    BIN_PullBits_U32(block, &cursor, &music, 7);
    BIN_PullBits_U32(block, &cursor, &sfx, 7);
    BIN_PullBits_U32(block, &cursor, &language, 7);
    BIN_PullBits_U32(block, &cursor, &SubtitlesEnabled, 1);
    BIN_PullBits_U32(block, &cursor, &mode, 32);
    BIN_PullBits_U32(block, &cursor, &widescreen, 1);
    BIN_PullBits_U32(block, &cursor, &MultiplayerLayout_LeftRightOrTopBtm, 32);
    for (int i = 0; i < GSET_RESERVED_WORDS; i++)
        BIN_PullBits_U32(block, &cursor, &reserved, 32);
    BIN_PullBits_U16(block, &cursor, &version, 16);
    SFXMusicSetVolume(music);
    SFXSetVolume(sfx);
}

// AUTOINJECT
uchar* LS_MakeCheats(uint label, ushort slot, ushort *length) {
    uint cursor;
    StartBlock(CheatsBlock, label, &cursor);
    BIN_PushBits(CheatsBlock, &cursor, CheatInfo.Immortal, 1);
    BIN_PushBits(CheatsBlock, &cursor, CheatInfo.AllWeapons, 1);
    BIN_PushBits(CheatsBlock, &cursor, CheatInfo.UnlimitedAmmo, 1);
    return EndBlock(CheatsBlock, cursor, length);
}

// AUTOINJECT
void LS_LoadCheats(uchar *block, ushort slot) {
    uint cursor = SAVE_FIELDS_START;
    BIN_PullBits_U32(block, &cursor, &CheatInfo.Immortal, 1);
    BIN_PullBits_U32(block, &cursor, &CheatInfo.AllWeapons, 1);
    BIN_PullBits_U32(block, &cursor, &CheatInfo.UnlimitedAmmo, 1);
}

// Always player 0's bonus, whatever the slot; the high word first.
// AUTOINJECT
uchar* LS_MakeBonus(uint label, ushort slot, ushort *length) {
    *length = 0;
    uint cursor;
    StartBlock(BonusBlock, label, &cursor);
    uint64_t bonus = Menu_GetBonus(0);
    BIN_PushBits(BonusBlock, &cursor, (uint)(bonus >> 32), 32);
    BIN_PushBits(BonusBlock, &cursor, (uint)bonus, 32);
    return EndBlock(BonusBlock, cursor, length);
}

// Replaces the slot's player's bonus.
// AUTOINJECT
void LS_LoadBonus(uchar *block, ushort slot) {
    uint cursor = SAVE_FIELDS_START;
    uint hi, lo;
    BIN_PullBits_U32(block, &cursor, &hi, 32);
    BIN_PullBits_U32(block, &cursor, &lo, 32);
    Menu_SetBonus(lo, hi, (byte)slot, 0);
}
