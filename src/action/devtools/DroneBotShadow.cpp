// Shadow tests for the multiplayer bots' drone states (NDrone2_Bot.cpp): each state, the original and ours, takes
// the same made-up message on the same made-up bots, with every callee (the state machine, anims, impacts, the bot
// goal and weapon code, the multiplayer calls) going to a logging fake. Run at start with MenuShadowTests=on
// (MenuProbe.cpp), before any level exists; NIGHTFIRE_SHADOW_ONLY=dronebots runs them alone.
//
// The fakes are generic: one naked stub per callee hands its arguments to DroneBotShadow_Fake, which logs them as the
// callee's table entry says (pointers as offsets into the world, bytes and words masked as the callee reads them) and
// answers from a sequence drawn per case. What the callees would do in a match is not tested here.

#include "DroneBotShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../game/drone/BOT.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/NDrone2_Bot.h"
#include "../game/drone/DroneVision_Alerts.h"
#include "../assets.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Drone_AIHints.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" uint32_t __cdecl DroneBotShadow_Fake(int index, uint32_t callerReturn, uint32_t firstArg);

namespace {

// The game's RNG state (Rand_Random)
#define RandWords ((uint32_t *)0x0018cdf8)

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))

const int DRONES = 3;

struct World {
    obj_tag objs[DRONES];
    Drone_tag drones[DRONES];
    BOT_vars_t bots[DRONES];
    AnimState anims[DRONES];
    obj_tag player;
    CoverNode_tag nodes[2];
    HITDATA_tag hit;
    DroneImpact_tag impact;
    DCVars_tag dcv;
    MsgObject msg;
    Drone_tag *droneArg;
    obj_tag *gameObjArg;
    uint uintArg;
    char charArg;
    float floatArg;
    obj_tag objective;
    celglist_tag objectiveModel;
    DroneAlert_tag alert;
    short shortArg;
    obj_tag *objArg;

    // What the fakes answer: a sequence drawn per call, the same for both runs (the world is restored)
    uint32_t answers;
    uint answerBias;
};
World world;

ShadowRegions regions;

uint32_t Answer(uint32_t n) {
    world.answers = world.answers * 1103515245u + 12345u;
    return (world.answers >> 16) % n;
}

bool Yes() {
    return Answer(4) < world.answerBias;
}

template <class T, size_t N> T Pick(const T (&values)[N]) {
    return values[ShadowRand(N)];
}

// A pointer as an offset into the world or NPCGlobals; small values as they are
uint32_t Who(uint32_t p) {
    const uint8_t *b = (const uint8_t *)(uintptr_t)p;
    if (b >= (const uint8_t *)&world && b < (const uint8_t *)&world + sizeof(world))
        return 0x10000000 + (uint32_t)(b - (const uint8_t *)&world);
    if (b >= (const uint8_t *)&NPCGlobals && b < (const uint8_t *)&NPCGlobals + sizeof(NPCGlobals))
        return 0x20000000 + (uint32_t)(b - (const uint8_t *)&NPCGlobals);
    if (p < 0x100000)
        return p;
    return 0x7fffffff;
}

// ---- the fakes: per callee, its address, how to log each argument and what to answer
//   p a pointer (or a small value)   d a dword   f a float   b a byte   w a 16-bit value
//   answers: v none (0)   b a bool   s a bot state or 0   u 0, 1 or 2   n NULL

struct FakeSpec {
    unsigned at;
    const char *name;
    const char *args;
    char answer;
};

const FakeSpec kFakes[] = {
    {0x0001aca0, "BOT_soundEffect", "pd", 'v'},
    {0x0001bb30, "BOTSTATE_setNearestNavNode", "p", 'v'},
    {0x0001bb50, "BOTSTATE_uninitGoal", "pd", 'v'},
    {0x0001bef0, "BOTSTATE_setGoalPickPrefs", "pdffffbd", 'v'},
    {0x0001c2d0, "BOTSTATE_stopAnim", "p", 'v'},
    {0x0001b970, "BOTSTATE_chooseUnarmedAttackAnim", "p", 's'},
    {0x0001e3f0, "BOTSTATE_changeWeapon", "pdbb", 'u'},
    {0x0001c8c0, "BOTSTATE_chooseRandomMove", "pd", 's'},
    {0x0001c820, "BOTSTATE_chooseCombatMove", "p", 's'},
    {0x0001bab0, "BOTSTATE_EvasiveMove", "p", 's'},
    {0x0001c240, "BOTSTATE_validateRoute", "pd", 'b'},
    {0x0001e5b0, "BOTWEAP_canReload", "pd", 'b'},
    {0x0001eeb0, "BOTWEAP_hasReloadAnim", "w", 'b'},
    {0x00064910, "NDrone2_ReactToOpponentSighted", "pw", 's'},
    {0x00046d90, "NDrone2_MoveToObject", "ppfb", 'r'},
    {0x00046a10, "NDrone2_CoverAvailable", "pb", 'b'},
    {0x000459a0, "NDrone2_CanBackoff", "p", 'b'},
    {0x00043c60, "NDrone2_SetAngleToObj", "ppfb", 'v'},
    {0x000675f0, "DroneWeap_Fire", "bp", 'v'},
    {0x000330c0, "DroneAnim_CanDoAnimState", "pww", 'b'},
    {0x0003cd80, "NDrone2_SendAttackMessage", "p", 'v'},
    {0x00045720, "NDrone2_CanStrafeLeft", "p", 'b'},
    {0x000457c0, "NDrone2_CanStrafeRight", "p", 'b'},
    {0x00043fd0, "NDrone2_AtDest", "p", 'b'},
    {0x0003cdf0, "NDrone2_NearDrone", "pf", 'b'},
    {0x00034750, "DroneAnim_CoverAnim", "pbw", 'b'},
    {0x00043b20, "NDrone2_SlideToPosition", "pfbp", 'v'},
    {0x00043e00, "NDrone2_SetAngleToCover", "pf", 'b'},
    {0x00044310, "NDrone2_CoverNodeOK", "ppwb", 'u'},
    {0x000445a0, "NDrone2_GetCoverPosForTarget", "pdppb", 'b'},
    {0x0003e6a0, "DroneFunc_HandleImpact", "ppdb", 'b'},
    {0x00038670, "DroneFunc_ConsiderExplosive", "ppw", 'v'},
    {0x00044210, "NDrone2_DisownCover", "p", 'v'},
    {0x00046f30, "NDrone2_MoveToGoalPosition", "ppfw", 'r'},
    {0x00066640, "DroneWeap_OpponentIsAimingAtMe", "pf", 'b'},
    {0x00043ee0, "NDrone2_BondIsFacingMe", "pf", 'b'},
    {0x00047030, "NDrone2_MoveToAlertPosition", "p", 'r'},
    {0x0001bed0, "BOTSTATE_getActiveGoal", "p", 'g'},
    {0x0001c550, "BOTSTATE_setPickupVisitTime", "pp", 'v'},
    {0x0001bbc0, "BOTSTATE_initGoal", "pdbppb", 'v'},
    {0x0001bc30, "BOTSTATE_gotoGoal", "pdd", 'b'},
    {0x0001d750, "BOTSTATE_combatWeaponChangeChoice", "ppbbb", 'u'},
    {0x0001da80, "BOTSTATE_getPreferredTraitOpponentObjIndex", "p", 'o'},
    {0x000a1600, "MP_getProtectionObj_NotExt", "", 'O'},
    {0x000a1610, "MP_getDemolitionObj_NotExt", "", 'O'},
    {0x0009ec80, "MP_PlayerOrBotInd", "p", 'i'},
    {0x0002d8a0, "Control_Plr2Ind", "p", 'i'},
    {0x00039f10, "NDrone2_DoorIsOpen", "p", 'b'},
    {0x00039ec0, "NDrone2_OpenDoor", "p", 'v'},
    {0x00044e70, "NDrone2_DistanceToAIPoint", "pp", 'F'},
    {0x00035f90, "NDrone2_Player", "", 'P'},
    {0x00038bd0, "NDrone2_CheckSurrender", "p", 'b'},
    {0x00045e50, "NDrone2_ChooseCombatMove", "p", 's'},
    {0x0001a260, "BOT_reactToDroneAlertMsg", "dpp", 'b'},
    {0x0009df20, "MP_areObjectsOnSameTeam", "pp", 'b'},
    {0x00036dc0, "NDrone2_FeetPos", "pp", 'b'},
    {0x0009e430, "MP_sendTeamBotMessage", "dddpp", 'v'},
    {0x0001b4f0, "BOT_respawn", "pdb", 'b'},
    {0x00037a10, "NDrone2_SetOpponent", "pp", 'v'},
    {0x0003a1d0, "NDrone2_LogDroneDeath", "p", 'v'},
    {0x000a1fc0, "MP_PlayerKilled", "p", 'v'},
    {0x00067690, "DroneWeap_DropWeapon", "p", 'v'},
    {0x00034b90, "DroneAnim_LocationDeathAnim", "pw", 'b'},
    {0x00029560, "Coll_AddHitToList", "pfpp", 'n'},
    {0x0003a2b0, "NDrone2_ExplosionDir", "pp", 'u'},
    {0x000341c0, "DroneAnim_CallAnim", "ddwdfdp", 'b'},
    {0x0004e320, "Drone_SM_SetState", "pdp", 'b'},
    {0x0001cb20, "BOTSTATE_pickGoal", "pd", 'b'},
    {0x00043fa0, "NDrone2_InvalidateAttackRoute", "p", 'v'},
    {0x0009e850, "MP_ResetBotPickupTimes", "w", 'v'},
    {0x00038df0, "NDrone2_SetAsDead", "p", 'v'},
    {0x00038f40, "NDrone2_DroneAlertToObject", "pdpfff", 'v'},
    {0x0003e220, "NDrone2_BulletImpact", "ppdb", 'b'},
    {0x0003e3f0, "NDrone2_ExplosiveImpact", "ppdb", 'b'},
    {0x0003e010, "NDrone2_PunchImpact", "ppdb", 'b'},
    {0x00066700, "DroneWeap_AimTarget_IsOpponent", "bbp", 'b'},
    {0x0003a690, "NDrone2_HasOpponent", "p", 'b'},
};
const int FAKES = sizeof(kFakes) / sizeof(kFakes[0]);

uint32_t Respond(const FakeSpec &fake, const uint32_t *args) {
    char line[256];
    int n = snprintf(line, sizeof(line), "%s(", fake.name);
    for (int i = 0; fake.args[i] != 0 && n < (int)sizeof(line) - 16; i++) {
        uint32_t v = args[i];
        switch (fake.args[i]) {
        case 'p':
            v = Who(v);
            break;
        case 'b':
            v &= 0xff;
            break;
        case 'w':
            v &= 0xffff;
            break;
        default:
            break;
        }
        n += snprintf(line + n, sizeof(line) - n, i == 0 ? "%x" : ",%x", v);
    }
    ShadowLog("%s)", line);
    switch (fake.answer) {
    case 'b':
        return Yes();
    case 's': {
        static const uint16_t states[] = {0, 0, DSTATE_BotAttackStrafeAimLeft, DSTATE_BotAttackRollRightCrouch,
                                          DSTATE_BotAttackStepAimLeft, 0x7f};
        return states[Answer(6)] | 0xabcd0000u;    // only AX is the answer
    }
    case 'u':
        return Answer(3);
    case 'r':
        return Answer(12);  // an AIROUTE status
    case 'g':
        return Answer(4) ? (uint32_t)(uintptr_t)&world.bots[0].goals[Answer(2)] : 0;
    case 'o':
        return Answer(2) ? 0xff : Answer(8);
    case 'O':
        return Answer(5) ? (uint32_t)(uintptr_t)&world.objective : 0;
    case 'i':
        return Answer(4) ? Answer(NUM_AGENTS) : 0xffff;
    case 'P':
        return Answer(2) ? (uint32_t)(uintptr_t)&world.player : (uint32_t)(uintptr_t)&world.objs[2];
    case 'F': {
        float f = 3.5f + Answer(4);
        uint32_t u;
        memcpy(&u, &f, 4);
        return u;
    }
    default:
        return 0;
    }
}

} // namespace

uint32_t __cdecl DroneBotShadow_Fake(int index, uint32_t callerReturn, uint32_t firstArg) {
    return Respond(kFakes[index], &firstArg);
}

namespace {

#define FAKE_STUB(i)                                                                                                  \
    __declspec(naked) void FakeStub##i() {                                                                            \
        __asm push i                                                                                                  \
        __asm call DroneBotShadow_Fake                                                                                \
        __asm add esp, 4                                                                                              \
        __asm ret                                                                                                     \
    }
FAKE_STUB(0) FAKE_STUB(1) FAKE_STUB(2) FAKE_STUB(3) FAKE_STUB(4) FAKE_STUB(5) FAKE_STUB(6) FAKE_STUB(7)
FAKE_STUB(8) FAKE_STUB(9) FAKE_STUB(10) FAKE_STUB(11) FAKE_STUB(12) FAKE_STUB(13) FAKE_STUB(14) FAKE_STUB(15)
FAKE_STUB(16) FAKE_STUB(17) FAKE_STUB(18) FAKE_STUB(19) FAKE_STUB(20) FAKE_STUB(21) FAKE_STUB(22) FAKE_STUB(23)
FAKE_STUB(24) FAKE_STUB(25) FAKE_STUB(26) FAKE_STUB(27) FAKE_STUB(28) FAKE_STUB(29) FAKE_STUB(30) FAKE_STUB(31)
FAKE_STUB(32) FAKE_STUB(33) FAKE_STUB(34) FAKE_STUB(35) FAKE_STUB(36) FAKE_STUB(37) FAKE_STUB(38) FAKE_STUB(39)
FAKE_STUB(40) FAKE_STUB(41) FAKE_STUB(42) FAKE_STUB(43) FAKE_STUB(44) FAKE_STUB(45) FAKE_STUB(46) FAKE_STUB(47)
FAKE_STUB(48) FAKE_STUB(49) FAKE_STUB(50) FAKE_STUB(51) FAKE_STUB(52) FAKE_STUB(53) FAKE_STUB(54) FAKE_STUB(55)
FAKE_STUB(56) FAKE_STUB(57) FAKE_STUB(58) FAKE_STUB(59) FAKE_STUB(60) FAKE_STUB(61) FAKE_STUB(62) FAKE_STUB(63)
FAKE_STUB(64) FAKE_STUB(65) FAKE_STUB(66) FAKE_STUB(67) FAKE_STUB(68) FAKE_STUB(69) FAKE_STUB(70) FAKE_STUB(71)
FAKE_STUB(72) FAKE_STUB(73) FAKE_STUB(74) FAKE_STUB(75) FAKE_STUB(76) FAKE_STUB(77) FAKE_STUB(78) FAKE_STUB(79)
FAKE_STUB(80) FAKE_STUB(81) FAKE_STUB(82) FAKE_STUB(83) FAKE_STUB(84) FAKE_STUB(85) FAKE_STUB(86) FAKE_STUB(87)
FAKE_STUB(88) FAKE_STUB(89) FAKE_STUB(90) FAKE_STUB(91) FAKE_STUB(92) FAKE_STUB(93) FAKE_STUB(94) FAKE_STUB(95)
FAKE_STUB(96) FAKE_STUB(97) FAKE_STUB(98) FAKE_STUB(99) FAKE_STUB(100) FAKE_STUB(101) FAKE_STUB(102) FAKE_STUB(103)
FAKE_STUB(104) FAKE_STUB(105) FAKE_STUB(106) FAKE_STUB(107) FAKE_STUB(108) FAKE_STUB(109) FAKE_STUB(110) FAKE_STUB(111)

// The same for a callee returning in ST0: the answer's bits loaded as a float
#define FAKE_FSTUB(i)                                                                                                 \
    __declspec(naked) void FakeFStub##i() {                                                                           \
        __asm push i                                                                                                  \
        __asm call DroneBotShadow_Fake                                                                                \
        __asm add esp, 4                                                                                              \
        __asm push eax                                                                                                \
        __asm fld dword ptr [esp]                                                                                     \
        __asm add esp, 4                                                                                              \
        __asm ret                                                                                                     \
    }
FAKE_FSTUB(0) FAKE_FSTUB(1) FAKE_FSTUB(2) FAKE_FSTUB(3) FAKE_FSTUB(4) FAKE_FSTUB(5) FAKE_FSTUB(6) FAKE_FSTUB(7)
FAKE_FSTUB(8) FAKE_FSTUB(9) FAKE_FSTUB(10) FAKE_FSTUB(11) FAKE_FSTUB(12) FAKE_FSTUB(13) FAKE_FSTUB(14) FAKE_FSTUB(15)
FAKE_FSTUB(16) FAKE_FSTUB(17) FAKE_FSTUB(18) FAKE_FSTUB(19) FAKE_FSTUB(20) FAKE_FSTUB(21) FAKE_FSTUB(22) FAKE_FSTUB(23)
FAKE_FSTUB(24) FAKE_FSTUB(25) FAKE_FSTUB(26) FAKE_FSTUB(27) FAKE_FSTUB(28) FAKE_FSTUB(29) FAKE_FSTUB(30) FAKE_FSTUB(31)
FAKE_FSTUB(32) FAKE_FSTUB(33) FAKE_FSTUB(34) FAKE_FSTUB(35) FAKE_FSTUB(36) FAKE_FSTUB(37) FAKE_FSTUB(38) FAKE_FSTUB(39)
FAKE_FSTUB(40) FAKE_FSTUB(41) FAKE_FSTUB(42) FAKE_FSTUB(43) FAKE_FSTUB(44) FAKE_FSTUB(45) FAKE_FSTUB(46) FAKE_FSTUB(47)
FAKE_FSTUB(48) FAKE_FSTUB(49) FAKE_FSTUB(50) FAKE_FSTUB(51) FAKE_FSTUB(52) FAKE_FSTUB(53) FAKE_FSTUB(54) FAKE_FSTUB(55)
FAKE_FSTUB(56) FAKE_FSTUB(57) FAKE_FSTUB(58) FAKE_FSTUB(59) FAKE_FSTUB(60) FAKE_FSTUB(61) FAKE_FSTUB(62) FAKE_FSTUB(63)
FAKE_FSTUB(64) FAKE_FSTUB(65) FAKE_FSTUB(66) FAKE_FSTUB(67) FAKE_FSTUB(68) FAKE_FSTUB(69) FAKE_FSTUB(70) FAKE_FSTUB(71)
FAKE_FSTUB(72) FAKE_FSTUB(73) FAKE_FSTUB(74) FAKE_FSTUB(75) FAKE_FSTUB(76) FAKE_FSTUB(77) FAKE_FSTUB(78) FAKE_FSTUB(79)
FAKE_FSTUB(80) FAKE_FSTUB(81) FAKE_FSTUB(82) FAKE_FSTUB(83) FAKE_FSTUB(84) FAKE_FSTUB(85) FAKE_FSTUB(86) FAKE_FSTUB(87)
FAKE_FSTUB(88) FAKE_FSTUB(89) FAKE_FSTUB(90) FAKE_FSTUB(91) FAKE_FSTUB(92) FAKE_FSTUB(93) FAKE_FSTUB(94) FAKE_FSTUB(95)
FAKE_FSTUB(96) FAKE_FSTUB(97) FAKE_FSTUB(98) FAKE_FSTUB(99) FAKE_FSTUB(100) FAKE_FSTUB(101) FAKE_FSTUB(102) FAKE_FSTUB(103)
FAKE_FSTUB(104) FAKE_FSTUB(105) FAKE_FSTUB(106) FAKE_FSTUB(107) FAKE_FSTUB(108) FAKE_FSTUB(109) FAKE_FSTUB(110) FAKE_FSTUB(111)

void (*const kFStubs[])() = {
    FakeFStub0, FakeFStub1, FakeFStub2, FakeFStub3, FakeFStub4, FakeFStub5, FakeFStub6, FakeFStub7,
    FakeFStub8, FakeFStub9, FakeFStub10, FakeFStub11, FakeFStub12, FakeFStub13, FakeFStub14, FakeFStub15,
    FakeFStub16, FakeFStub17, FakeFStub18, FakeFStub19, FakeFStub20, FakeFStub21, FakeFStub22, FakeFStub23,
    FakeFStub24, FakeFStub25, FakeFStub26, FakeFStub27, FakeFStub28, FakeFStub29, FakeFStub30, FakeFStub31,
    FakeFStub32, FakeFStub33, FakeFStub34, FakeFStub35, FakeFStub36, FakeFStub37, FakeFStub38, FakeFStub39,
    FakeFStub40, FakeFStub41, FakeFStub42, FakeFStub43, FakeFStub44, FakeFStub45, FakeFStub46, FakeFStub47,
    FakeFStub48, FakeFStub49, FakeFStub50, FakeFStub51, FakeFStub52, FakeFStub53, FakeFStub54, FakeFStub55,
    FakeFStub56, FakeFStub57, FakeFStub58, FakeFStub59, FakeFStub60, FakeFStub61, FakeFStub62, FakeFStub63,
    FakeFStub64, FakeFStub65, FakeFStub66, FakeFStub67, FakeFStub68, FakeFStub69, FakeFStub70, FakeFStub71,
    FakeFStub72, FakeFStub73, FakeFStub74, FakeFStub75, FakeFStub76, FakeFStub77, FakeFStub78, FakeFStub79,
    FakeFStub80, FakeFStub81, FakeFStub82, FakeFStub83, FakeFStub84, FakeFStub85, FakeFStub86, FakeFStub87,
    FakeFStub88, FakeFStub89, FakeFStub90, FakeFStub91, FakeFStub92, FakeFStub93, FakeFStub94, FakeFStub95,
    FakeFStub96, FakeFStub97, FakeFStub98, FakeFStub99, FakeFStub100, FakeFStub101, FakeFStub102, FakeFStub103,
    FakeFStub104, FakeFStub105, FakeFStub106, FakeFStub107, FakeFStub108, FakeFStub109, FakeFStub110, FakeFStub111,
};

void (*const kStubs[])() = {
    FakeStub0, FakeStub1, FakeStub2, FakeStub3, FakeStub4, FakeStub5, FakeStub6, FakeStub7,
    FakeStub8, FakeStub9, FakeStub10, FakeStub11, FakeStub12, FakeStub13, FakeStub14, FakeStub15,
    FakeStub16, FakeStub17, FakeStub18, FakeStub19, FakeStub20, FakeStub21, FakeStub22, FakeStub23,
    FakeStub24, FakeStub25, FakeStub26, FakeStub27, FakeStub28, FakeStub29, FakeStub30, FakeStub31,
    FakeStub32, FakeStub33, FakeStub34, FakeStub35, FakeStub36, FakeStub37, FakeStub38, FakeStub39,
    FakeStub40, FakeStub41, FakeStub42, FakeStub43, FakeStub44, FakeStub45, FakeStub46, FakeStub47,
    FakeStub48, FakeStub49, FakeStub50, FakeStub51, FakeStub52, FakeStub53, FakeStub54, FakeStub55,
    FakeStub56, FakeStub57, FakeStub58, FakeStub59, FakeStub60, FakeStub61, FakeStub62, FakeStub63,
    FakeStub64, FakeStub65, FakeStub66, FakeStub67, FakeStub68, FakeStub69, FakeStub70, FakeStub71,
    FakeStub72, FakeStub73, FakeStub74, FakeStub75, FakeStub76, FakeStub77, FakeStub78, FakeStub79,
    FakeStub80, FakeStub81, FakeStub82, FakeStub83, FakeStub84, FakeStub85, FakeStub86, FakeStub87,
    FakeStub88, FakeStub89, FakeStub90, FakeStub91, FakeStub92, FakeStub93, FakeStub94, FakeStub95,
    FakeStub96, FakeStub97, FakeStub98, FakeStub99, FakeStub100, FakeStub101, FakeStub102, FakeStub103,
    FakeStub104, FakeStub105, FakeStub106, FakeStub107, FakeStub108, FakeStub109, FakeStub110, FakeStub111,
};
static_assert(sizeof(kStubs) / sizeof(kStubs[0]) >= FAKES, "more fakes than stubs");

// Every fake but those of the functions a group tests (a hook covers our port too)
void InstallFakes(const unsigned *tested = NULL, int count = 0) {
    for (int i = 0; i < FAKES; i++) {
        bool skip = false;
        for (int k = 0; k < count; k++)
            skip |= kFakes[i].at == tested[k];
        if (skip)
            continue;
        bool inST0 = kFakes[i].answer == 'F';
        if (inST0 && kFStubs[i] == NULL)
            printf("[dronebots] fake %s cannot return in ST0\n", kFakes[i].name);
        ShadowHook_Install(kFakes[i].at, inST0 ? (const void *)kFStubs[i] : (const void *)kStubs[i]);
    }
}

// ---- the made-up world

float RandomFloat() {
    static const float edges[] = {0.0f, -0.0f, 1.5f, 1.4999999f, 1.5000001f, 3.0f, 4.0f, 1.0f, NAN, 0.7853982f,
                                  1.5707964f, -1.5707964f};
    switch (ShadowRand(3)) {
    case 0:
        return Pick(edges);
    case 1:
        return ShadowRandFloat(-4.0f, 4.0f);
    default:
        return ShadowRandFloat(0.0f, 12.0f);
    }
}

void RandomDrone(int i) {
    Drone_tag *d = &world.drones[i];
    obj_tag *o = &world.objs[i];
    BOT_vars_t *bot = &world.bots[i];
    memset(d, 0, sizeof(*d));
    memset(o, 0, sizeof(*o));
    memset(bot, 0, sizeof(*bot));
    memset(&world.anims[i], 0, sizeof(world.anims[i]));
    o->extraObjectData = d;
    o->animState = &world.anims[i];
    o->position.x = ShadowRandFloat(-10.0f, 10.0f);
    o->position.y = ShadowRandFloat(-2.0f, 2.0f);
    o->position.z = ShadowRandFloat(-10.0f, 10.0f);
    o->inCel = (cel_tag *)(uintptr_t)(0x1000 + i);
    d->gameObj = o;
    d->botVars = bot;
    d->sm.id = 100 + i;
    d->sm.curState = DSTATE_BotIdle - ShadowRand(55);
    d->sm.prevState = ShadowRand(2) ? DSTATE_BotImpactBullet : DSTATE_BotIdle - ShadowRand(55);
    d->sm.stateParam = ShadowRand(2) ? (int)(uintptr_t)&world.impact : 0;
    d->dtype = DTYPE_Bot;
    d->side = (uchar)ShadowRand(4);
    d->flags = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    d->flags2 = ShadowRand(0x100);
    d->health = RandomFloat();
    d->distanceToTarget = RandomFloat();
    d->opponentBearing = RandomFloat();
    d->recover = (uchar)ShadowRand(256);
    d->headShot = (uchar)ShadowRand(2);
    d->firing = (uchar)ShadowRand(2);
    d->burstShots = (short)ShadowRand(10);
    d->initialState = (short)ShadowRand(250);
    d->opponent = ShadowRand(4) ? &world.player : NULL;
    for (int w = 0; w < 3; w++)
        d->behaviour1[w] = ShadowRand(4) ? ShadowRand(0x10000) | (ShadowRand(0x10000) << 16) : 0;
    d->currentBehaviour = d->behaviour1;
    d->coverNode = &world.nodes[ShadowRand(2)];
    d->coverMode = (uchar)ShadowRand(5);
    d->coverFlags = (ushort)ShadowRand(0x10000);
    d->stateScratch = ShadowRand(5);
    d->unknown854 = ShadowRand(3) ? 0.5f : RandomFloat();
    d->aiPoint.set = (uchar)ShadowRand(2);
    d->aiPoint.radius = ShadowRandFloat(0.2f, 2.0f);
    d->aiPoint.pos.x = o->position.x + (ShadowRand(3) ? ShadowRandFloat(-1.0f, 1.0f) : 1.5f);
    d->aiPoint.pos.y = o->position.y;
    d->aiPoint.pos.z = o->position.z + (ShadowRand(3) ? ShadowRandFloat(-1.0f, 1.0f) : 0.0f);
    d->targetSlot = (uchar)ShadowRand(MAX_DRONE_TARGETS);
    bot->drone = d;
    bot->playerIndex = (short)(4 + i);
    bot->botIndex = (short)i;
    bot->activeGoal = ShadowRand(3) ? (uchar)ShadowRand(2) : 0xff;
    bot->stateClass = (uchar)ShadowRand(14);
    bot->weapon = (uchar)ShadowRand(0x53);
    bot->nextWeapon = (uchar)ShadowRand(0x53);
    bot->flags = ShadowRand(8);
    bot->stats.traitFlags = (uchar)ShadowRand(0x20);
    for (int g = 0; g < 2; g++) {
        bot->goals[g].kind = (uchar)ShadowRand(4);
        bot->goals[g].target = ShadowRand(2) ? (void *)&world.player : NULL;
        bot->goals[g].lastRouteStatus = (uchar)ShadowRand(12);
    }
    bot->goalReturnState = ShadowRand(2) ? 0 : DSTATE_BotAttack + ShadowRand(10);
    bot->guardFriend = ShadowRand(4) ? &world.objs[2] : NULL;
    for (int p = 0; p < NUM_AGENTS; p++) {
        bot->players[p].flags = (uchar)ShadowRand(16);
        bot->players[p].distanceSq = ShadowRand(4) ? ShadowRandFloat(0.0f, 40.0f) : 20.25f;
    }
    bot->insideObjective = (uchar)ShadowRand(2);
    bot->atObjective = 0;
    bot->preferredOpponent = (uchar)ShadowRand(10);
    d->aiPoint.distance = ShadowRandFloat(0.0f, 20.0f);
    if (ShadowRand(4) == 0) {
        d->aiPoint.radius = 0.5f;
        d->aiPoint.distance = 8.5f;     // exactly 8 beyond reach
    }
    d->baseAlertness1 = ShadowRand(3) ? ShadowRandFloat(0.0f, 1.2f) : 0.66f;
    d->alertness = ShadowRand(3) ? ShadowRandFloat(0.0f, 1.2f) : (ShadowRand(2) ? 0.66f : 1.0f);
    d->unknown38 = (uchar)ShadowRand(2);
    if (ShadowRand(4) == 0)
        bot->weapon = 1;
    for (int r = 0; r < 5; r++)
        d->combatRanges[r] = ShadowRand(4) ? ShadowRandFloat(0.5f, 12.0f) : RandomFloat();
    if (ShadowRand(4) == 0)
        d->distanceToTarget = d->combatRanges[ShadowRand(2)];
    d->unseenFrames = 13 + ShadowRand(5);
    d->sightFlags = ShadowRand(0x10);
    d->burstOver = (uchar)ShadowRand(2);
    static const short anims[] = {4, 5, 8, 0xa, 0xc, 0xf, 0x1e, 0x52, 0x5a};
    d->animState = Pick(anims);
    d->route1.remaining = RandomFloat();
    d->moveTarget.x = o->position.x + ShadowRandFloat(-3.0f, 3.0f);
    d->moveTarget.y = o->position.y;
    d->moveTarget.z = o->position.z + ShadowRandFloat(-3.0f, 3.0f);
    d->unknown3f0 = ShadowRand(4) ? 0 : 0x8000000;
    d->angleToSomePoint = 0.0f;
    d->nodeReturnState = 0;
}

void RandomWorld() {
    world.answers = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    world.answerBias = 1 + ShadowRand(3);
    for (int i = 0; i < DRONES; i++)
        RandomDrone(i);
    for (int i = 0; i < DRONES; i++) {
        world.drones[i].next = i + 1 < DRONES ? &world.drones[i + 1] : NULL;
        world.drones[i].prev = i > 0 ? &world.drones[i - 1] : NULL;
    }
    memset(&world.player, 0, sizeof(world.player));
    static const uchar types[] = {OBJECTTYPE_PLAYER, OBJECTTYPE_DRONE, OBJECTTYPE_DEAD_DRONE};
    world.player.objectType = Pick(types);
    memset(world.nodes, 0, sizeof(world.nodes));
    world.nodes[0].type = COVER_CORNER;
    world.nodes[1].type = (uchar)(ShadowRand(2) ? COVER_LOW : 2);
    memset(&world.hit, 0, sizeof(world.hit));
    memset(&world.impact, 0, sizeof(world.impact));
    world.impact.source = ShadowRand(2) ? &world.player : &world.objs[2];

    world.dcv.gameObj = &world.objs[0];
    world.dcv.drone = &world.drones[0];
    world.dcv.cel = NULL;
    world.dcv.aiStateMachine = &world.drones[0].sm;
    world.droneArg = ShadowRand(5) ? &world.drones[0] : &world.drones[1];
    world.gameObjArg = &world.objs[0];
    static const uint anims[] = {4, 5, 8, 0xa, 0xc, 0xf, 0x1e, 0x52, 0x5a, 0x1000a};
    world.uintArg = Pick(anims);
    world.charArg = (char)(ShadowRand(3) == 0 ? 0 : ShadowRand(3) == 0 ? 0x41 : 1);
    world.floatArg = RandomFloat();

    GameState.NumFramesUnpaused = ShadowRand(100000);
    NPCGlobals.unknown138c = ShadowRand(3) ? 0.5f : ShadowRandFloat(0.0f, 2.0f);
    static const MultiplayerGameMode modes[] = {GM_PROTECTION, GM_DEMOLITION, GM_CTF};
    MPSettings.GameMode = Pick(modes);
    memset(&world.objective, 0, sizeof(world.objective));
    memset(&world.objectiveModel, 0, sizeof(world.objectiveModel));
    world.objective.objGraphics = ShadowRand(5) ? &world.objectiveModel : NULL;
    float *m = (float *)&world.objective.transformMatrix;
    for (size_t k = 0; k < sizeof(world.objective.transformMatrix) / sizeof(float); k++)
        m[k] = ShadowRandFloat(-4.0f, 4.0f);
    world.objectiveModel.extentMin = {-1.0f, -1.0f, -1.0f};
    world.objectiveModel.extentMax = {1.0f, 1.0f, 1.0f};
    world.objs[0].centrePoint.x = ShadowRandFloat(-6.0f, 6.0f);
    world.objs[0].centrePoint.y = ShadowRandFloat(-6.0f, 6.0f);
    world.objs[0].centrePoint.z = ShadowRandFloat(-6.0f, 6.0f);
    FRAME_RATE_INT = ShadowRand(2) ? 60 : 50;
    RandWords[0] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
    RandWords[1] = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

void RandomMessage() {
    memset(&world.msg, 0, sizeof(world.msg));
    // the messages the bot states handle, more often than the rest
    static const uint types[] = {0, 1, 1, 1, 2, 3, 3, 3, 3, 4, 5, 5, 6, 7, 8, 9, 0xc, 0xc, 0xd, 0xd, 0xf, 0x14, 0x17,
                                 0x18, 0x19, 0x1b, 0x1c, 0x20, 0x21, 0x2e, 0x3a};
    world.msg.msgType = ShadowRand(10) ? Pick(types) : ShadowRand(0x50);
    world.msg.sender = 101;
    world.msg.receiver = 100;
    static void *const extras[] = {&world.hit, &world.impact};
    world.msg.extraData = Pick(extras);
}

// ---- running the two

enum Kind {
    K_STATE,            // (dcv, droneArg, gameObjArg, &msg), AL
    K_DCV_SHORT,        // (dcv, shortArg)
    K_DCV_MSG_SHORT,    // (dcv, &msg, shortArg)
    K_DCV_OBJ,          // (dcv, objArg)
    K_DCV_UINT_CHAR,    // (dcv, uintArg, charArg)
    K_DCV_FLOAT,        // (dcv, floatArg)
};

struct Case {
    const char *name;
    unsigned at;
    const void *ours;
    Kind kind;
    uint32_t mask;      // of the return register compared
};

const Case *current;

uint32_t Call(const void *f) {
    World &w = world;
    switch (current->kind) {
    case K_DCV_UINT_CHAR:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, uint, char))(&w.dcv, w.uintArg, w.charArg);
    case K_DCV_FLOAT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, float))(&w.dcv, w.floatArg);
    case K_DCV_SHORT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, short))(&w.dcv, w.shortArg);
    case K_DCV_MSG_SHORT:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, MsgObject *, short))(&w.dcv, &w.msg, w.shortArg);
    case K_DCV_OBJ:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, obj_tag *))(&w.dcv, w.objArg);
    default:
        return ORIGINAL(f, uint32_t(__cdecl *)(DCVars_tag *, Drone_tag *, obj_tag *, MsgObject *))(
            &w.dcv, w.droneArg, w.gameObjArg, &w.msg);
    }
}

uint32_t RunTheirs() {
    XbeOriginalScope o(current->at);
    return Call((const void *)(uintptr_t)current->at) & current->mask;
}

uint32_t RunOurs() {
    return Call(current->ours) & current->mask;
}

#define CASE(name, at) {#name, at, (const void *)&name, K_STATE, 0xff}
#define HELPER(name, at, kind, mask) {#name, at, (const void *)&name, kind, mask}

const Case kLife[] = {
    CASE(NDrone2_DSTATE_BotInit, 0x00060390),
    CASE(NDrone2_DSTATE_BotRespawn, 0x0004dcf0),
    CASE(NDrone2_DSTATE_BotDeathAnim, 0x0004e050),
    CASE(NDrone2_DSTATE_BotDeathByExplosion, 0x0004e0d0),
    CASE(NDrone2_DSTATE_BotDead, 0x00063a60),
    CASE(NDrone2_DSTATE_BotImpactBullet, 0x00063790),
    CASE(NDrone2_DSTATE_BotImpactExplosive, 0x000638b0),
    CASE(NDrone2_DSTATE_BotImpactPunch, 0x00063930),
    CASE(NDrone2_DSTATE_BotImpactStunGrenade, 0x000639c0),
    CASE(NDrone2_DSTATE_BotCollector, 0x000613f0),
    CASE(NDrone2_DSTATE_BotStuck, 0x00061470),
    CASE(NDrone2_DSTATE_BotAttackNoRoute, 0x000618b0),
    CASE(NDrone2_DSTATE_BotSeenDroneShot, 0x00063760),
    CASE(NDrone2_DSTATE_BotHeardNoise, 0x0004e040),
    CASE(NDrone2_DSTATE_BotAttackRollLeftCrouch, 0x000624c0),
    CASE(NDrone2_DSTATE_BotAttackRollRightCrouch, 0x00062560),
    CASE(NDrone2_DSTATE_BotAttackChangeWeapon, 0x00062900),
    CASE(NDrone2_DSTATE_BotAttackUnarmed, 0x000629a0),
};

const Case kAttack[] = {
    CASE(NDrone2_DSTATE_BotAttack, 0x000614a0),
    CASE(NDrone2_DSTATE_BotAttackRun, 0x00061690),
    CASE(NDrone2_DSTATE_BotAttackFire, 0x000618f0),
    CASE(NDrone2_DSTATE_BotAttackStrafeAimLeft, 0x00061b30),
    CASE(NDrone2_DSTATE_BotAttackStrafeAimRight, 0x00061ca0),
    CASE(NDrone2_DSTATE_BotAttackRunChangePosition, 0x00061e10),
    CASE(NDrone2_DSTATE_BotAttackBackoff, 0x00061f40),
    CASE(NDrone2_DSTATE_BotAttackCrouch, 0x00062190),
    CASE(NDrone2_DSTATE_BotAttackStepAimLeft, 0x00062600),
    CASE(NDrone2_DSTATE_BotAttackStepAimRight, 0x000626f0),
    CASE(NDrone2_DSTATE_BotAttackReload, 0x000627e0),
    HELPER(NDrone2_SetRunAnim, 0x00043900, K_DCV_UINT_CHAR, 0),
    HELPER(NDrone2_ChooseRunAnim, 0x00043960, K_DCV_FLOAT, 0xffffffffu),
    HELPER(NDrone2_FaceMoveTarget, 0x00043cf0, K_DCV_FLOAT, 0),
};

const Case kCover[] = {
    CASE(NDrone2_DSTATE_BotCoverRunTo, 0x00062a20),
    CASE(NDrone2_DSTATE_BotCoverInit, 0x0004dd30),
    CASE(NDrone2_DSTATE_BotCoverIdle, 0x00062bc0),
    CASE(NDrone2_DSTATE_BotCoverAim, 0x0004de70),
    CASE(NDrone2_DSTATE_BotCoverFire, 0x00062ee0),
    CASE(NDrone2_DSTATE_BotCoverReturn, 0x00063070),
    CASE(NDrone2_DSTATE_BotCoverTypeChange, 0x000631c0),
    CASE(NDrone2_DSTATE_BotCoverLeave, 0x000632c0),
    CASE(NDrone2_DSTATE_BotCoverLeaveNow, 0x0004df50),
};

const Case kPlaces[] = {
    CASE(NDrone2_DSTATE_BotAlertToPosition, 0x000633e0),
    CASE(NDrone2_DSTATE_BotGotoGoalPosition, 0x000635d0),
    CASE(NDrone2_DSTATE_BotDoorOpen, 0x00063c10),
    CASE(NDrone2_DSTATE_BotGuardFriendIdle, 0x00063e00),
    CASE(NDrone2_DSTATE_BotGuardFriendFollow, 0x00063ec0),
    CASE(NDrone2_DSTATE_BotIdle, 0x00064100),
};

const Case kReactions[] = {
    HELPER(NDrone2_InShoutingRange, 0x00064e90, K_DCV_OBJ, 0xff),
    HELPER(NDrone2_HandleDroneAlert, 0x00038ff0, K_DCV_MSG_SHORT, 0xff),
    HELPER(NDrone2_ReactToOpponentSighted, 0x00064910, K_DCV_SHORT, 0xffff),
};

// The drones' alerts: a sender (a drone on the same path or another, the drone itself, the player, none), and the
// drones made the kinds the handlers tell apart
void RandomAlerts() {
    static obj_tag *const senders[] = {&world.objs[1], &world.objs[1], &world.objs[1], &world.objs[0], &world.player,
                                       NULL};
    memset(&world.alert, 0, sizeof(world.alert));
    world.alert.sender = Pick(senders);
    static const uint types[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x1e, 0x11, 0x12, 0x13, 0x15, 0x16, 0x3};
    world.msg.msgType = Pick(types);
    world.msg.extraData = ShadowRand(8) ? &world.alert : NULL;
    static const uchar dtypes[] = {DTYPE_Normal, DTYPE_Civilian, DTYPE_CivilianScared, DTYPE_PartyGirl,
                                   DTYPE_CivilianGuard, DTYPE_CivDoorGuard, DTYPE_Bot, DTYPE_Bot, DTYPE_RunToPoint,
                                   DTYPE_AlarmRaiser, DTYPE_Interogator, DTYPE_Guard};
    for (int i = 0; i < DRONES; i++) {
        Drone_tag *d = &world.drones[i];
        d->dtype = Pick(dtypes);
        d->route1.path = (AIPath_tag *)(uintptr_t)(ShadowRand(4) ? 0x5000 : 0x5000 + i);
        d->flags = (ShadowRand(3) ? 0 : 0x800) | (ShadowRand(3) ? 0 : 0x200000) | (ShadowRand(4) ? 0 : 0x400000) |
                   (ShadowRand(4) ? 0 : 0x800000);
        static const float alertness[] = {0.0f, 0.5f, 0.66f, 0.65999997f, 0.7f, 0.70000005f, 0.75f, 0.75000006f, 1.0f,
                                          NAN};
        d->alertness = Pick(alertness);
        d->sightFlags = ShadowRand(0x40);
        d->unknown33 = (uchar)(ShadowRand(3) == 0);
        d->explosiveSeen = (uchar)(ShadowRand(3) == 0);
        world.objs[i].curState = (ushort)(ShadowRand(4) ? ShadowRand(4) : 0x63);
        world.objs[i].objectType = OBJECTTYPE_DRONE;
        world.objs[i].inCel = (cel_tag *)(uintptr_t)(0x1000 + ShadowRand(2));
    }
    static const float distances[] = {0.0f, 14.0f, 15.0f, 16.0f, 19.0f, 20.0f, 21.0f, 39.0f, 40.0f, 41.0f, 99.0f,
                                      100.0f, 101.0f, 49.0f, 50.0f, 51.0f};
    world.objs[1].position = world.objs[0].position;
    world.objs[1].position.x += Pick(distances);
    world.player.position = world.objs[0].position;
    world.player.position.z -= Pick(distances);
    world.drones[0].distanceToTarget = Pick(distances);
    static const short states[] = {0, 0, DSTATE_BotAttack, DSTATE_Attack, DSTATE_BotAttackFire};
    world.shortArg = Pick(states);
    static obj_tag *const objs[] = {&world.objs[1], &world.player, &world.objs[2]};
    world.objArg = Pick(objs);
    static const uint levels[] = {HT_Level_TowerC, HT_Level_PowerStationA1, HT_Level_PowerStationA2,
                                  HT_Level_Cut_Level1, HT_Level_Cut_Level2, HT_Level_Cut_Level3};
    GameState.CurrentLevelHashcode = (HASHCODE)Pick(levels);
    static const uint special[] = {DSTATE_Interogate, DSTATE_InterogateWalk, DSTATE_EnemyRunToPoint,
                                   DSTATE_GoToGoalPosition};
    world.dcv.aiStateMachine->curState = ShadowRand(3) ? DSTATE_BotIdle - ShadowRand(5) : Pick(special);
    // the global alarm reads a bot's sender as a drone, whatever it is (as the original)
    if (world.msg.msgType == DRONE_MSG_GlobalAlarm) {
        world.alert.sender = &world.objs[1];
        world.msg.extraData = &world.alert;
    }
}

int Test(const Case *cases, int count, int trials, int *differ) {
    int run = 0;
    for (int c = 0; c < count; c++) {
        current = &cases[c];
        for (int trial = 0; trial < trials; trial++) {
            RandomWorld();
            RandomMessage();
            if (cases == kReactions)
                RandomAlerts();
            run++;
            if (!ShadowCompare(regions, "dronebots", current->name, trial, RunTheirs, RunOurs))
                (*differ)++;
        }
    }
    return run;
}

} // namespace

void DroneBotShadow_Run(void) {
    static uint8_t gameState[sizeof(GameState_t)], npc[sizeof(NPCGlobals_t)];
    static uint32_t rand[2];
    uint32_t frameRate = FRAME_RATE_INT;
    memcpy(gameState, &GameState, sizeof(gameState));
    memcpy(npc, &NPCGlobals, sizeof(npc));
    memcpy(rand, RandWords, sizeof(rand));

    regions.Clear();
    regions.Add(&world, sizeof(world));
    regions.Add(&GameState, sizeof(GameState_t));
    regions.Add(&NPCGlobals, sizeof(NPCGlobals_t));
    regions.Add(RandWords, 8);
    regions.Add(&FRAME_RATE_INT, 4);
    regions.Add(&MPSettings.GameMode, 4);
    MultiplayerGameMode gameMode = MPSettings.GameMode;

    InstallFakes();
    NfWarnMuted++;

    struct Group {
        const char *what;
        const Case *cases;
        int count;
        int trials;
    };
    const Group groups[] = {
        {"life, death and impacts", kLife, sizeof(kLife) / sizeof(kLife[0]), 3000},
        {"attack", kAttack, sizeof(kAttack) / sizeof(kAttack[0]), 6000},
        {"cover", kCover, sizeof(kCover) / sizeof(kCover[0]), 6000},
        {"going places, guarding and idling", kPlaces, sizeof(kPlaces) / sizeof(kPlaces[0]), 8000},
        {"alerts and reactions", kReactions, sizeof(kReactions) / sizeof(kReactions[0]), 20000},
    };
    const int n = sizeof(groups) / sizeof(groups[0]);
    int cases[n], differ[n];
    for (int g = 0; g < n; g++) {
        if (groups[g].cases == kReactions) {
            ShadowHook_RemoveAll();
            unsigned tested[sizeof(kReactions) / sizeof(kReactions[0])];
            for (int c = 0; c < groups[g].count; c++)
                tested[c] = kReactions[c].at;
            InstallFakes(tested, groups[g].count);
        }
        differ[g] = 0;
        cases[g] = Test(groups[g].cases, groups[g].count, groups[g].trials, &differ[g]);
    }

    NfWarnMuted--;
    ShadowHook_RemoveAll();
    memcpy(&GameState, gameState, sizeof(gameState));
    memcpy(&NPCGlobals, npc, sizeof(npc));
    memcpy(RandWords, rand, sizeof(rand));
    FRAME_RATE_INT = frameRate;
    MPSettings.GameMode = gameMode;

    for (int g = 0; g < n; g++)
        printf("[dronebots] %s: %d cases, %d differ\n", groups[g].what, cases[g], differ[g]);
}
