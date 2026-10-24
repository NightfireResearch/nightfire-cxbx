#include "Mission.h"
#include "SwitchChannels.h"
#include "../mp/multiplayer.h"
#include "../../sound/music.h"
#include "../../ui/Menu.h"
#include "../../ui/MenuManager.h"
#include "../../game/sp/PlayerStats.h"

#include <stdio.h>

// XBE_GLOBAL(0x0017e548, 0x4)
static Action_TranslatedText FailLabel = Action_TranslatedText_NULLVALUE;
// XBE_GLOBAL(0x0025fe20, 0x4)
static uint32_t BaseMap;
// XBE_GLOBAL(0x0025fe24, 0x4)
static uint32_t ThisOrderNum;
// XBE_GLOBAL(0x0017e544, 0x4)
static MissionStatus MissionState = MISSION_STATUS_NONE;

// The objectives each mission part points at: still the game's own arrays (their status is written at run time).
// XBE_GLOBAL(0x0017dc58, 0x18)
static Objective Objectives_1_CastleExterior[1] = {
    {(Action_TranslatedText)0x04000000, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400005b,
     70, 0, 1, 69, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dc70, 0x18)
static Objective Objectives_1_CastleCourtyard[1] = {
    {(Action_TranslatedText)0x04000001, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400005c,
     2, 0, 1, 70, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dc88, 0x30)
static Objective Objectives_2_CastleIndoors[2] = {
    {(Action_TranslatedText)0x04000002, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400005d,
     72, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000003, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400005e,
     73, 0, 1, 72, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dcb8, 0x30)
static Objective Objectives_2_CastleIndoors2[2] = {
    {(Action_TranslatedText)0x04000004, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400005f,
     74, 0, 1, 73, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000005, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000060,
     75, 0, 1, 74, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dce8, 0x18)
static Objective Objectives_1_HendersonA[1] = {
    {(Action_TranslatedText)0x04000006, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400000e,
     71, 0, 0, 0, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dd00, 0x48)
static Objective Objectives_3_HendersonB[3] = {
    {(Action_TranslatedText)0x04000007, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400000f,
     72, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000008, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000010,
     75, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000009, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000011,
     77, 0, 1, 76, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dd48, 0x48)
static Objective Objectives_3_HendersonC[3] = {
    {(Action_TranslatedText)0x0400000a, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000012,
     78, 0, 1, 77, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400000b, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000013,
     81, 0, 1, 77, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400000c, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000014,
     83, 0, 1, 82, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dd90, 0x18)
static Objective Objectives_1_HendersonD[1] = {
    {(Action_TranslatedText)0x0400000d, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000015,
     84, 0, 1, 83, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dda8, 0x48)
static Objective Objectives_3_TowerA[3] = {
    {(Action_TranslatedText)0x04000016, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000021,
     97, 0, 0, 0, 2, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000017, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000022,
     70, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000018, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000023,
     71, 0, 1, 70, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017ddf0, 0x48)
static Objective Objectives_3_TowerB[3] = {
    {(Action_TranslatedText)0x04000019, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000024,
     72, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400001a, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000025,
     73, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400001b, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000026,
     75, 0, 1, 74, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017de38, 0x78)
static Objective Objectives_5_TowerC[5] = {
    {(Action_TranslatedText)0x0400001c, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000027,
     76, 0, 1, 75, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400001d, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000028,
     77, 0, 1, 75, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400001e, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000029,
     78, 0, 1, 77, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400001f, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400002a,
     89, 0, 1, 79, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000020, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400002b,
     90, 0, 1, 89, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017deb0, 0x48)
static Objective Objectives_3_PowerStationA1[3] = {
    {(Action_TranslatedText)0x0400002c, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000061,
     27, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400002d, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000062,
     28, 0, 1, 27, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400002e, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000063,
     35, 0, 1, 28, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017def8, 0x48)
static Objective Objectives_3_PowerStationA2[3] = {
    {(Action_TranslatedText)0x0400002f, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000064,
     29, 0, 1, 35, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000030, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000065,
     30, 0, 1, 29, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000031, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000066,
     31, 0, 1, 29, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017df40, 0x60)
static Objective PowerStation_B1[4] = {
    {(Action_TranslatedText)0x04000041, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     15, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000042, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     10, 0, 1, 7, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000043, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     11, 0, 1, 8, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000044, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     12, 0, 1, 9, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dfa0, 0x18)
static Objective PowerStation_B2[1] = {
    {(Action_TranslatedText)0x04000045, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     0, 0, 1, 0, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dfb8, 0x30)
static Objective Tower_2A[2] = {
    {(Action_TranslatedText)0x04000035, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400003b,
     100, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000036, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400003c,
     72, 0, 0, 0, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017dfe8, 0x30)
static Objective Tower_2B[2] = {
    {(Action_TranslatedText)0x04000037, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400003d,
     73, 0, 1, 72, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000038, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400003e,
     77, 0, 1, 73, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e018, 0x18)
static Objective Tower_2Elevator[1] = {
    {(Action_TranslatedText)0x04000039, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400003f,
     74, 0, 1, 77, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e030, 0x18)
static Objective Tower_2C[1] = {
    {(Action_TranslatedText)0x0400003a, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000040,
     80, 0, 1, 74, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e048, 0x48)
static Objective Caves[3] = {
    {(Action_TranslatedText)0x04000046, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     0, 0, 1, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000047, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     0, 0, 1, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000048, Action_TranslatedText_NULLVALUE, Action_TranslatedText_NULLVALUE,
     0, 0, 1, 0, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e090, 0x78)
static Objective Evil_Base[5] = {
    {(Action_TranslatedText)0x04000049, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000050,
     71, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400004a, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000051,
     72, 0, 1, 71, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400004b, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000052,
     74, 0, 1, 72, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400004c, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000053,
     84, 0, 1, 74, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x04000069, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x0400006a,
     206, 0, 1, 84, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e108, 0x18)
static Objective Evil_Silo[1] = {
    {(Action_TranslatedText)0x0400004d, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000054,
     142, 0, 1, 75, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e120, 0x30)
static Objective EvilBase_ShuttleBay[2] = {
    {(Action_TranslatedText)0x0400004e, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000055,
     143, 0, 1, 142, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400004f, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000056,
     144, 0, 1, 143, 0, {0, 0, 0}, 0},
};
// XBE_GLOBAL(0x0017e150, 0x30)
static Objective Space_Station_D[2] = {
    {(Action_TranslatedText)0x04000059, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000067,
     9, 0, 0, 0, 0, {0, 0, 0}, 0},
    {(Action_TranslatedText)0x0400005a, Action_TranslatedText_NULLVALUE, (Action_TranslatedText)0x04000068,
     11, 0, 1, 9, 0, {0, 0, 0}, 0},
};

// XBE_GLOBAL(0x0017e180, 0x3c0)
static Mission MissionData[24] = {
    {HT_Level_CastleExterior, HT_Level_CastleExterior, 0, Objectives_1_CastleExterior, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_CastleCourtyard, HT_Level_CastleExterior, 1, Objectives_1_CastleCourtyard, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_CastleIndoors1, HT_Level_CastleExterior, 2, Objectives_2_CastleIndoors, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_CastleIndoors2, HT_Level_CastleExterior, 3, Objectives_2_CastleIndoors2, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonA, HT_Level_HendersonA, 0, Objectives_1_HendersonA, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonB, HT_Level_HendersonA, 1, Objectives_3_HendersonB, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonC, HT_Level_HendersonA, 2, Objectives_3_HendersonC, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}},
    {HT_Level_HendersonD, HT_Level_HendersonA, 3, Objectives_1_HendersonD, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_TowerA, HT_Level_TowerA, 0, Objectives_3_TowerA, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_TowerB, HT_Level_TowerA, 1, Objectives_3_TowerB, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_TowerC, HT_Level_TowerA, 2, Objectives_5_TowerC, 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}},
    {HT_Level_PowerStationA1, HT_Level_PowerStationA1, 0, Objectives_3_PowerStationA1, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_PowerStationA2, HT_Level_PowerStationA1, 1, Objectives_3_PowerStationA2, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level1, HT_Level_Cut_Level1, 0, PowerStation_B1, 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level2, HT_Level_Cut_Level1, 1, PowerStation_B2, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2A, HT_Level_Tower2A, 0, Tower_2A, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2B, HT_Level_Tower2A, 1, Tower_2B, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2Elevator, HT_Level_Tower2A, 2, Tower_2Elevator, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Tower2C, HT_Level_Tower2A, 3, Tower_2C, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_Cut_Level3, HT_Level_Cut_Level3, 0, Caves, 3, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_EvilBase, HT_Level_EvilBase, 0, Evil_Base, 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
    {HT_Level_EvilSilo, HT_Level_EvilBase, 1, Evil_Silo, 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_EvilBaseC, HT_Level_EvilBase, 2, EvilBase_ShuttleBay, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {HT_Level_SpaceStationD, HT_Level_SpaceStationD, 0, Space_Station_D, 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

// AUTOINJECT
Action_TranslatedText Mission_FailLabel(void) {
    return FailLabel;
}

// AUTOINJECT
void Mission_SetFailLabel(Action_TranslatedText text) {
    FailLabel = text;
}

// AUTOINJECT
void Mission_SetMapHCode(HASHCODE hashcode) {
  BaseMap = hashcode;
}

// AUTOINJECT
HASHCODE Mission_BaseMapHCode(void) {
  if(!MPSettings.isMultiplayer) {
    return (HASHCODE)BaseMap;
  }
  return GameState.CurrentLevelHashcode;
}

// AUTOINJECT
void Mission_SetStatus(MissionStatus status) {
  MissionState = status;
}

// AUTOINJECT
MissionStatus Mission_Status(void) {
  return MissionState;
}

// AUTOINJECT
int Mission_NumVisObjectives(void) {

    int count = 0;

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we reach a later mission then we are done
            return count;

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            int objState = MissionData[i].objectives[j].status;

            if(objState != OBJSTATUS_NOT_SHOWN && objState != OBJSTATUS_HIDDEN)
                count++;
        }

        
    }

    return count;
}

// AUTOINJECT
void Mission_ObjectiveState(OBJ_STATE *state, short objectiveNum) {

    int objWithinLevel = 0;

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        if(ThisOrderNum < MissionData[i].idxInOrder) // The missions are sorted, if we haven't found it then the index is bad somehow
            return;

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            Objective *o = &MissionData[i].objectives[j];

            if(o->status != OBJSTATUS_NOT_SHOWN && o->status != OBJSTATUS_HIDDEN) {
                if(objWithinLevel == objectiveNum) {
                    state->statusText = (
                        (o->status == OBJSTATUS_IN_PROGRESS) ? TXT_OBJECTIVE_IN_PROGRESS :
                        (o->status == OBJSTATUS_INCOMPLETE) ? TXT_OBJECTIVE_INCOMPLETE :
                        (o->status == OBJSTATUS_COMPLETE) ? TXT_COMPLETE :
                        TXT_FAIL );
                    state->name = o->name;
                    state->description = o->description;
                    state->status = o->status;
                    return;
                }
                objWithinLevel++;
            }
        }

    }

}

// AUTOINJECT
void Mission_MonitorObjectives(void) {

    // ANDed with the raw channel byte, so a channel holding an even value clears it
    uint8_t allObjectivesCompleted = 1;

    // Evaluate objectives and send notifications
    for(int i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        bool fromPreviousPart = (ThisOrderNum <= MissionData[i].idxInOrder);

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {

            Objective *o = &MissionData[i].objectives[j];
            char* str = Txt_GetStringFromHeap(0);

            bool isFailCondition = (o->flags & OBJECTIVE_FAIL_CONDITION);

            uint8_t thisObjectiveMet = isFailCondition ? switch_channels[o->completedChannel] == 0
                                                       : switch_channels[o->completedChannel];
            allObjectivesCompleted &= thisObjectiveMet;

            switch(o->status) {
                case OBJSTATUS_NOT_SHOWN:
                    // shown once the current level's hashcode is not below the part's
                    if(MissionData[i].level <= GameState.CurrentLevelHashcode) {
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, TXTMSG_OBJECTIVE, str, 0, 300);
                        o->status = isFailCondition ? OBJSTATUS_IN_PROGRESS : OBJSTATUS_INCOMPLETE;
                    }
                    break;
                case OBJSTATUS_HIDDEN:
                    if (((o->revealedChannel != 0) && (o->revealedChannel != 0xff)) && (switch_channels[o->revealedChannel] != '\0')) {
                        sprintf(str,"%s",Txt_BindLabel(o->name,0));
                        Text_AddMsg(0, fromPreviousPart, TXTMSG_OBJECTIVE, str, 0, 300);
                        o->status = isFailCondition ? OBJSTATUS_IN_PROGRESS : OBJSTATUS_INCOMPLETE;
                    }
                    break;
                case OBJSTATUS_IN_PROGRESS:
                    if (!thisObjectiveMet) {
                        switch_channels[SWITCH_MISSION_FAILED] = true;
                        switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
                        sprintf(str,"%s: %s",Txt_BindLabel(TXT_FAIL, 0), Txt_BindLabel(o->name, 0));
                        Text_AddMsg(0, fromPreviousPart, TXTMSG_OBJECTIVE, str, 0, 180);
                        o->status = OBJSTATUS_FAILED;
                    }
                    break;
                case OBJSTATUS_INCOMPLETE:
                    if(thisObjectiveMet) {
                        sprintf(str, "%s", Txt_BindLabel(TXT_NOTIF_OBJECTIVE_COMPLETE, 0));
                        Text_AddMsg(0, fromPreviousPart, TXTMSG_OBJECTIVE, str, 0, 180);
                        o->status = OBJSTATUS_COMPLETE;
                    }
                    break;

                case OBJSTATUS_FAILED:
                    if(o->failLabel != Action_TranslatedText_NULLVALUE)
                        FailLabel = o->failLabel;
                    break;

                case OBJSTATUS_COMPLETE:
                default:
                    // Label has been created, no need to do anything more
                    break;

                
            }

        }
        
    }

    // Check conditions for mission failure or success: every one is tested, and success can be set beside a failure

    // Mission failure - Alarm triggered
    if(switch_channels[SWITCH_FAIL_ALARM]) {
        FailLabel = TXT_MISSION_FAIL_ALARM_TRIGGERED;
        switch_channels[SWITCH_MISSION_FAILED] = true;
        switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
    }

    // Mission failure - Civilian was killed
    if(switch_channels[SWITCH_FAIL_CIVILIAN_KILLED]) {
        FailLabel = TXT_MISSION_FAIL_KILLED_CIVILIAN;
        switch_channels[SWITCH_MISSION_FAILED] = true;
        switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
    }

    // Mission failure - Kiko escaped in Evil Base C
    if(switch_channels[SWITCH_EVILBASEC_FAIL_KIKO] && GameState.CurrentLevelHashcode == HT_Level_EvilBaseC) {
        FailLabel = TXT_MISSION_FAIL_KIKO_ESCAPE;
        switch_channels[SWITCH_MISSION_FAILED] = true;
        switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
    }

    // Mission failure - Missile launched in Space Station D
    if(switch_channels[SWITCH_SPACESTATIOND_FAIL_MISSILE] && GameState.CurrentLevelHashcode == HT_Level_SpaceStationD) {
        FailLabel = TXT_MISSION_FAIL_MISSILE_LAUNCHED;
        switch_channels[SWITCH_MISSION_FAILED] = true;
        switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
    }

    // Mission success
    if(allObjectivesCompleted) {
        switch_channels[SWITCH_MISSION_WON] = 1;
        switch_channels_time[SWITCH_MISSION_WON] = GameState.NumFramesUnpaused;
        Music_Event(MUSICEV_END,1);
        printf("--- Completed mission 0x%08x in %i frames\n", GameState.CurrentLevelHashcode, GameState.NumFramesUnpaused);
        return;
    }

}

// The level a won mission part ends to (ours: the original has the switch in Mission_Update)
HASHCODE Mission_GetEndTo(HASHCODE level) {
    switch(level) {
        default:
            return HT_Level_Menu_Pre;

        case HT_Level_HendersonD:
            return HT_Level_HendersonD_Outro;

        case HT_Level_CastleIndoors2:
            return HT_Level_CastleIndoors2_Outro;

        case HT_Level_TowerC:
            return HT_Level_TowerC_Outro;

        case HT_Level_PowerStationA2:
            return HT_Level_PowerStationA2_Outro;

        case HT_Level_Tower2C:
            return HT_Level_Tower2C_Outro;
        
        case HT_Level_EvilBaseC:
            return HT_Level_EvilBaseC_Outro;

        case HT_Level_SpaceStationD:
            return HT_Level_SpaceStationD_Outro;

        case HT_Level_Cut_Level2:
            return HT_Level_Cut_Level2_Outro;
    }
}

// Mission_Update's state
enum MissionUpdateState : uint32_t {
    MISSION_UPDATE_RUNNING = 1,
    MISSION_UPDATE_FAILED = 2,          // shows the fail text
    MISSION_UPDATE_DIED = 3,
    MISSION_UPDATE_TIMED_OUT = 4,       // the results after a win, else the retry menu
    MISSION_UPDATE_WAITING = 5,         // on the game flow or the menu
    MISSION_UPDATE_WON = 6,
    MISSION_UPDATE_COUNTDOWN = 7,       // until TimeOut_148 runs out
};

// XBE_GLOBAL(0x0017e540, 0x4)
static MissionUpdateState InternalState = MISSION_UPDATE_WAITING;
// XBE_GLOBAL(0x0017e550, 0x4)
static uint32_t FadeClr_147 = 0xff;
// XBE_GLOBAL(0x0025fe28, 0x4)
static float TimeOut_148;
// XBE_GLOBAL(0x0017e54c, 0x4)
static HASHCODE LevelToEndTo = HT_Level_Menu_Pre;

// AUTOINJECT
void Mission_Update(void) {
    
    if(GameState.CurrentLevelHashcode == HT_Level_Menu_Pre)
        return;

    if(MPSettings.isMultiplayer)
        return;

    switch(InternalState) {
        case MISSION_UPDATE_RUNNING:

            if(switch_channels[SWITCH_PLAYER_FINISHED_DYING]) {

                Music_Event(MUSICEV_DEATH,1);
                InternalState = MISSION_UPDATE_DIED;
                FadeClr_147 = 0xff0000ff;

                if (glb_players[0] != NULL)
                    Player_SetHealth((BLData*)glb_players[0]->extraObjectData, 0.0f);

                return;

            } else {

                Mission_MonitorObjectives();

                if (switch_channels[SWITCH_MISSION_FAILED]) {
                    Music_Event(MUSICEV_VICTORY,1);
                    InternalState = MISSION_UPDATE_FAILED;
                    return;
                }
                
                if (switch_channels[SWITCH_MISSION_WON]) {
                    Music_Event(MUSICEV_END,1);
                    InternalState = MISSION_UPDATE_WON;
                    FadeClr_147 = 0xff;
                    return;
                }
                

            }

            break;

        case MISSION_UPDATE_FAILED:
            GameState.LevelEnding = true;
            MissionState = MISSION_STATUS_FAILED;
            LevelToEndTo = HT_Level_Menu_Pre;
            InternalState = MISSION_UPDATE_COUNTDOWN;
            Text_AddMsg(0, 0, TXTMSG_MISSION, (char*)Txt_BindLabel(TXT_MISSION_FAIL, 0), 0,
                        4 * VIDEO_FRAME_RATE); // 4 seconds
            TimeOut_148 = 5 * VIDEO_FRAME_RATE;

            break;

        case MISSION_UPDATE_DIED:
            TimeOut_148 = 1.0f;
            GameState.LevelEnding = true;
            MissionState = MISSION_STATUS_QUIT;
            InternalState = MISSION_UPDATE_COUNTDOWN;
            LevelToEndTo = HT_Level_Menu_Pre;
            break;

        case MISSION_UPDATE_TIMED_OUT:
            if(MissionState == MISSION_STATUS_COMPLETE) {
                int missionIndex = Menu_GetLevelIndex(Mission_BaseMapHCode());
                int savedIndex = Menu_GetLevelIndex((HASHCODE)GameState.BaseMapHashCode);
                if (savedIndex < missionIndex) {
                  GameState.BaseMapHashCode = Mission_BaseMapHCode();
                }
                GameState.ReloadMenupage = P_NFRESULTS;
                ResetMap_LevelToLoad(LevelToEndTo, 0, 0);
                GameFlow_PushState(GAMEFLOW_FADE, 60.0, FadeClr_147);
                InternalState = MISSION_UPDATE_WAITING;
            } else {
                MenuManager_Create(HT_Menu_MissionEndMenu, P_ENDMISSION, 0, '\0', MENU_NO_CLOSE_ACTION, 0,
                                   MENU_STATUS_MISSION_END);
                GS_PauseGame(true);
                InternalState = MISSION_UPDATE_WAITING;
            }

        break;

        case MISSION_UPDATE_WAITING:
            break;

        case MISSION_UPDATE_WON:
            MissionState = MISSION_STATUS_COMPLETE;
            InternalState = MISSION_UPDATE_COUNTDOWN;
            GameState.LevelEnding = true;
            LevelToEndTo = Mission_GetEndTo(GameState.CurrentLevelHashcode);
            Text_AddMsg(0, 0, TXTMSG_MISSION, (char*)Txt_BindLabel(TXT_MISSION_COMPLETE, 0), 0,
                        4 * (short)VIDEO_FRAME_RATE); // 4 seconds
            TimeOut_148 = 4 * VIDEO_FRAME_RATE;
            break;
        
        case MISSION_UPDATE_COUNTDOWN:
            GS_PausePlayer(1, 0);
            TimeOut_148 -= FRAME_RATE_MUL;
            if (TimeOut_148 <= 0) {
                GS_PauseGame(true);
                InternalState = MISSION_UPDATE_TIMED_OUT;
            }
            break;

        default:
            InternalState = MISSION_UPDATE_RUNNING;
            break;
    }

}

// AUTOINJECT
void Mission_Init(HASHCODE hashcode, short warmReset) {

    LevelToEndTo = HT_Level_Menu_Pre;

    if(hashcode == HT_Level_Menu_Pre)
        return;

    if(!warmReset)
        BaseMap = 0xFFFFFFFF;
    
    FailLabel = Action_TranslatedText_NULLVALUE;
    ThisOrderNum = 0xFFFFFFFF;
    InternalState = MISSION_UPDATE_RUNNING;
    Mission_SetStatus(MISSION_STATUS_RUNNING);
    GS_PauseGame(false);

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].level == hashcode) {
            BaseMap = MissionData[i].baseLevel;
            ThisOrderNum = MissionData[i].idxInOrder;
            break;
        }
    }

    if(BaseMap == 0xFFFFFFFF) {
        InternalState = MISSION_UPDATE_WAITING;
        Mission_SetStatus(MISSION_STATUS_NONE);
        return;
    }

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {
        if(MissionData[i].level == BaseMap) {
            if(MissionData[i].level == hashcode) { // If we're (re)starting from the start of the mission, reset all objectives
                PlrStat_ResetForMission();
            }
            break; // Otherwise, we don't need to do anything
        }
    }

    for(uint i = 0; i < ARRAY_SIZE(MissionData); i++) {

        if(MissionData[i].baseLevel != BaseMap)
            continue;

        // 0 a later part, 1 an earlier one (4 on a warm reset), 2 this one
        int partOrder;
        if(MissionData[i].idxInOrder < ThisOrderNum) {
            partOrder = warmReset ? 4 : 1;
        } else if (MissionData[i].idxInOrder == ThisOrderNum) {
            partOrder = 2;
        } else {
            partOrder = 0;
        }

        for(uint j = 0; j < MissionData[i].numObjectives; j++) {
            Objective* o = &MissionData[i].objectives[j];
            if((partOrder == 0) || (partOrder == 2)) {
                
                o->status = (o->revealedChannel ? OBJSTATUS_HIDDEN : OBJSTATUS_NOT_SHOWN);

                if(o->completedChannel) {
                    switch_channels[o->completedChannel] = o->markCompletionTimeAtInit;

                    if(o->markCompletionTimeAtInit) {
                        switch_channels_time[o->completedChannel] = GameState.NumFramesUnpaused;
                    }
                }

            }
        }
    }
    

}