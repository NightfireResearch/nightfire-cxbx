// Shadow tests for the drone system's leaf functions (docs/drone/README.md, step 1): each original and ours run on
// the same inputs - scratch drones, state machines, routes and emitters, and synthetic behaviour blocks - and must
// return the same and leave the same bytes behind. Run at start with MenuShadowTests=on (MenuProbe.cpp), before any
// level exists, so nothing live is touched; every global a test changes (the RNG, the frame rate, the level
// hashcode, the drone counter, the heap) is put back.
//
// The message senders (Drone_SM_SendMsg, SendMsgSelf, BroadcastMsg, Drone_Message) deliver into the live system and
// are left to replays; Drone_SM_InitObject's Enter message and the state changes here address no drone, so they go
// nowhere.

#include "DroneShadow.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../game.h"
#include "../memory.h"
#include "../game/drone/Behaviour.h"
#include "../game/drone/BOT.h"
#include "../game/drone/NDrone2.h"
#include "../game/drone/DroneTables.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// memory.cpp's heap globals (for the emitter test's scratch heap)
extern uint32_t PtrHeap, HeapByteSize, MallocMethod, QuickBlock;

// The game's RNG state (Rand_Random)
#define RandWord0 U32_AT(0x0018cdf8)
#define RandWord1 U32_AT(0x0018cdfc)

static int runs, mismatches, reported;
static const char *current;

static void Mismatch(const char *fmt, uint a, uint b, uint c) {
    mismatches++;
    if (reported++ < 20) {
        printf("[drone] %s: ", current);
        printf(fmt, a, b, c);
        printf("\n");
    }
}

static uint32_t lcg = 0xd0e5eed;
static uint32_t Rnd(void) {
    lcg = lcg * 1664525u + 1013904223u;
    return lcg;
}

static uint FloatBits(float f) {
    uint u;
    memcpy(&u, &f, 4);
    return u;
}

// ---------------------------------------------------------------------------------------------------------------- behaviour

static void TestProperties(void) {
    typedef uint(__cdecl * GetFn)(int, uint *);
    typedef void(__cdecl * SetFn)(int, uint *, int);
    static const uint seeds[] = {0, 0xffffffff, 0xaaaaaaaa, 0x55555555};
    static const int values[] = {0, 1, 2, 3, 7, (int)0x80000000, -1};
    for (int trial = 0; trial < 204; trial++) {
        uint start[8];
        for (int w = 0; w < 8; w++)
            start[w] = trial < 4 ? seeds[trial] : Rnd();
        for (int w = 3; w < 8; w++)
            start[w] = 0xcdcdcdcd;
        current = "behaviour_util_getProperty";
        for (int id = 0; id < NUM_DRONE_PROPERTIES; id++) {
            uint a;
            {
                XbeOriginalScope o(0x00019440);
                a = ((GetFn)0x00019440)(id, start);
            }
            uint b = behaviour_util_getProperty(id, start);
            runs++;
            if (a != b)
                Mismatch("id 0x%x: 0x%x vs 0x%x", id, a, b);
        }
        current = "behaviour_util_setProperty";
        for (int id = 0; id < NUM_DRONE_PROPERTIES; id++)
            for (int v = 0; v < 7; v++) {
                uint wa[8], wb[8];
                memcpy(wa, start, sizeof(wa));
                memcpy(wb, start, sizeof(wb));
                {
                    XbeOriginalScope o(0x00019470);
                    ((SetFn)0x00019470)(id, wa, values[v]);
                }
                behaviour_util_setProperty(id, wb, values[v]);
                runs++;
                if (memcmp(wa, wb, sizeof(wa)) != 0)
                    Mismatch("id 0x%x value 0x%x: words differ (first 0x%x)", id, values[v], wa[0] ^ wb[0]);
            }
    }

    current = "behaviour_util_getStats";
    typedef DroneStatsRecord *(__cdecl * StatsFn)(int, int);
    for (int cls = -1; cls <= 20; cls++)
        for (int j = 0; j < 6; j++) {
            DroneStatsRecord *a;
            {
                XbeOriginalScope o(0x000197b0);
                a = ((StatsFn)0x000197b0)(cls, j);
            }
            runs++;
            if (a != behaviour_util_getStats(cls, j))
                Mismatch("class %d j %d: %p", cls, j, (uint)(uintptr_t)a);
        }
}

// A behaviour block as placement keys 15-32 hold it: class, then per behaviour {mode, header (count | props << 16),
// count words}, then the stats header and its words
static int BuildBlock(uint *out, uint cls, uint mode1, uint count1, uint props1, uint mode2, uint count2, uint props2,
                      uint statsHeader, uint nstats) {
    int n = 0;
    out[n++] = cls;
    out[n++] = mode1;
    out[n++] = count1 | (props1 << 16);
    for (uint i = 0; i < count1 && i < 8; i++)
        out[n++] = Rnd();
    out[n++] = mode2;
    out[n++] = count2 | (props2 << 16);
    for (uint i = 0; i < count2 && i < 8; i++)
        out[n++] = Rnd();
    out[n++] = statsHeader;
    for (uint i = 0; i < nstats && n < 60; i++)
        out[n++] = Rnd();
    while (n < 64)
        out[n++] = Rnd();
    return n;
}

static void TestBehaviourGet(void) {
    typedef uint(__cdecl * GetFn)(BehaviourStruct *, uint *);
    current = "behaviour_util_get";
    static uchar saved[sizeof(drone_stats)], start[sizeof(drone_stats)], theirs[sizeof(drone_stats)];
    memcpy(saved, &drone_stats, sizeof(saved));
    for (size_t i = 0; i < sizeof(start); i++)
        start[i] = (uchar)Rnd();

    struct Case { uint cls, mode1, count1, props1, mode2, count2, props2, statsHeader, nstats; bool valid; };
    static const Case cases[] = {
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010006, 6, true},
        {0x11, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010006, 6, false},
        {0xffffffff, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 6, 3, 0x5b, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 0, 0x5b, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 4, 0x5b, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 3, 0, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 3, 0x5c, 2, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 3, 0x5b, 6, 3, 0x5b, 0x80010006, 6, false},
        {3, 1, 3, 0x5b, 2, 0, 0x5b, 0x80010006, 6, false},
        {3, 1, 3, 0x5b, 2, 3, 0, 0x80010006, 6, false},
        {3, 1, 3, 0x5b, 2, 3, 0x5c, 0x80010006, 6, false},
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x00010006, 6, true},
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x80020006, 6, true},
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010000, 0, true},
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010003, 3, true},
        {3, 1, 3, 0x5b, 2, 3, 0x5b, 0x80010009, 9, true},
        {3, 1, 1, 0x20, 2, 2, 0x40, 0x80010006, 6, true},
    };
    NfWarnMuted = 1;   // ours reports the invalid blocks
    for (int c = 0; c < (int)(sizeof(cases) / sizeof(cases[0])) + 17 * 4; c++) {
        Case k = c < (int)(sizeof(cases) / sizeof(cases[0])) ? cases[c] : cases[0];
        if (c >= (int)(sizeof(cases) / sizeof(cases[0])))
            k.cls = (c - sizeof(cases) / sizeof(cases[0])) % 17;   // every class, several random blocks
        uint block[64];
        BuildBlock(block, k.cls, k.mode1, k.count1, k.props1, k.mode2, k.count2, k.props2, k.statsHeader, k.nstats);
        for (int nulls = 0; nulls < (k.valid ? 4 : 1); nulls++) {
            uint wordsA1[32], wordsA2[32], wordsB1[32], wordsB2[32];
            memset(wordsA1, 0xcd, sizeof(wordsA1));
            memset(wordsA2, 0xcd, sizeof(wordsA2));
            memset(wordsB1, 0xcd, sizeof(wordsB1));
            memset(wordsB2, 0xcd, sizeof(wordsB2));
            BehaviourStruct a, b;
            memset(&a, 0xcd, sizeof(a));
            memset(&b, 0xcd, sizeof(b));
            a.words1 = (nulls & 1) ? NULL : wordsA1;
            a.words2 = (nulls & 2) ? NULL : wordsA2;
            b.words1 = (nulls & 1) ? NULL : wordsB1;
            b.words2 = (nulls & 2) ? NULL : wordsB2;

            memcpy(&drone_stats, start, sizeof(start));
            uint ra;
            {
                XbeOriginalScope o(0x000194d0);
                ra = ((GetFn)0x000194d0)(&a, block);
            }
            memcpy(theirs, &drone_stats, sizeof(theirs));
            memcpy(&drone_stats, start, sizeof(start));
            uint rb = behaviour_util_get(&b, block);
            runs++;
            if ((ra & 0xff) != (uint)(uchar)rb)
                Mismatch("case %d: returned %d vs %d", c, ra & 0xff, rb);
            if (memcmp(&a, &b, 0xc) != 0 || memcmp((char *)&a + 0x10, (char *)&b + 0x10, 8) != 0)
                Mismatch("case %d: BehaviourStruct differs (nulls %d)", c, nulls, 0);
            if (memcmp(wordsA1, wordsB1, sizeof(wordsA1)) != 0 || memcmp(wordsA2, wordsB2, sizeof(wordsA2)) != 0)
                Mismatch("case %d: behaviour words differ (nulls %d)", c, nulls, 0);
            if (memcmp(theirs, &drone_stats, sizeof(theirs)) != 0)
                Mismatch("case %d: drone_stats differs", c, 0, 0);
        }
    }
    NfWarnMuted = 0;
    memcpy(&drone_stats, saved, sizeof(saved));
}

// ---------------------------------------------------------------------------------------------------------------- timers

static void TestRecoverTime(void) {
    typedef uint(__cdecl * Fn)(DCVars_tag *, HITDATA_tag *);
    current = "DroneFunc_RecoverTime";
    static Drone_tag drone;
    memset(&drone, 0, sizeof(drone));
    DCVars_tag dcv = {NULL, &drone, NULL, &drone.sm};
    static uchar hit[0x100], obj[0x200], bullet[0x100], weapon[0x10c];
    memset(hit, 0, sizeof(hit));
    memset(obj, 0, sizeof(obj));
    memset(bullet, 0, sizeof(bullet));
    memset(weapon, 0, sizeof(weapon));
    *(void **)(hit + 0x4c) = obj;           // HITDATA_tag.hitObj
    *(void **)(obj + 0x9c) = NULL;
    *(void **)(bullet + 0x38) = weapon;     // BU_tag.wpnDef
    uint savedRate = FRAME_RATE_INT;
    static const uint rates[] = {60, 50, 30, 1, 0, 0x80000001};
    for (int r = 0; r < 6; r++) {
        FRAME_RATE_INT = rates[r];
        for (int kind = 0; kind < 5; kind++) {
            HITDATA_tag *h = kind == 0 ? NULL : (HITDATA_tag *)hit;
            *(void **)(hit + 0x4c) = kind == 1 ? NULL : obj;
            obj[0xdb] = kind == 2 ? 2 : kind == 3 ? 3 : 5;          // obj_tag.objectType
            ((obj_tag *)obj)->extraObjectData = bullet;
            static const int variants[] = {0, 0x35, 0x43, 0x44, 0x4a, 0x4c, 0x4b, 0x72, 0x135, 0xffff};
            for (int v = 0; v < (kind == 4 ? 10 : 1); v++) {
                *(ushort *)weapon = (ushort)variants[v];                 // weaponVariantNum
                for (int rec = 0; rec < 256; rec++) {
                    drone.recover = (uchar)rec;
                    uint a;
                    {
                        XbeOriginalScope o(0x0003a4d0);
                        a = ((Fn)0x0003a4d0)(&dcv, h);
                    }
                    uint b = DroneFunc_RecoverTime(&dcv, h);
                    runs++;
                    if (a != b)
                        Mismatch("recover %d: %u vs %u", rec, a, b);
                }
            }
        }
    }
    FRAME_RATE_INT = savedRate;
    // The x87 precision question (docs): recover 80, no hit, 60 fps -> 4319 at double precision, 4320 at 24-bit
    drone.recover = 80;
    uint probe;
    {
        XbeOriginalScope o(0x0003a4d0);
        probe = ((Fn)0x0003a4d0)(&dcv, NULL);
    }
    printf("[drone] RecoverTime(recover 80, no hit) = %u from the original (4319 = double precision x87)\n", probe);
}

static void TestReactionTime(void) {
    typedef uint(__cdecl * Fn)(Drone_tag *);
    current = "DroneFunc_ReactionTime";
    static Drone_tag drone;
    memset(&drone, 0, sizeof(drone));
    uint savedLevel = GameState.CurrentLevelHashcode;
    float savedDiv = FRAME_RATE_DIV;
    static const uint levels[] = {0x7000001, 0x7000002, 0x7000003, 0x7000004, 0x7000005, 0x7000006, 0x7000007,
                                  0x7000008, 0x7000009, 0x700000a, 0x700000b, 0x700000c, 0, 0x7000011};
    static const float divs[] = {1.0f, 0.5f, 2.0f, 1.0f / 3.0f};
    static const uint flags[] = {0, 4, 8, 0xfffffff7, 0xffffffff};
    float alerts[24] = {0, 0.25f, 0.5f, 0.66f, 0.75f, 0.9999999f, 1.0f, 1.5f, -0.5f, -0.0f};
    uint nan = 0x7fc00000, inf = 0x7f800000;
    memcpy(&alerts[10], &nan, 4);
    memcpy(&alerts[11], &inf, 4);
    for (int l = 0; l < 14; l++) {
        GameState.CurrentLevelHashcode = (HASHCODE)levels[l];
        for (int d = 0; d < 4; d++) {
            FRAME_RATE_DIV = divs[d];
            for (int f = 0; f < 5; f++) {
                drone.sightFlags = flags[f];
                for (int al = 0; al < 24; al++) {
                    if (al >= 12)
                        alerts[al] = (float)(Rnd() & 0xffffff) / 16777216.0f;
                    drone.alertness = alerts[al];
                    for (int rea = 0; rea < 256; rea += (l == 1 || l == 4) ? 1 : 51) {
                        drone.reaction = (uchar)rea;
                        uint a;
                        {
                            XbeOriginalScope o(0x0003a5d0);
                            a = ((Fn)0x0003a5d0)(&drone);
                        }
                        uint b = DroneFunc_ReactionTime(&drone);
                        runs++;
                        if (a != b)
                            Mismatch("alertness bits 0x%x reaction %d: %u vs ...", FloatBits(drone.alertness), rea, a);
                    }
                }
            }
        }
    }
    GameState.CurrentLevelHashcode = (HASHCODE)savedLevel;
    FRAME_RATE_DIV = savedDiv;
}

static void TestIdleTimeOut(void) {
    typedef void(__cdecl * Fn)(DCVars_tag *, int, uint);
    current = "NDrone2_SetIdleTimeOut";
    static Drone_tag drone;
    memset(&drone, 0, sizeof(drone));
    DCVars_tag dcv = {NULL, &drone, NULL, &drone.sm};
    uint saved0 = RandWord0, saved1 = RandWord1;
    static const int mins[] = {0, 1, 5, 30, 45, -3, 0x7fffffff};
    static const uint rands[] = {0, 1, 2, 5, 10, 100, 0xffffffff};
    for (int seed = 0; seed < 8; seed++) {
        uint s0 = seed == 0 ? saved0 : seed == 1 ? 0x1f123bb5 : Rnd();
        uint s1 = seed == 0 ? saved1 : seed == 1 ? 0x159a55e5 : Rnd();
        for (int m = 0; m < 7; m++)
            for (int r = 0; r < 7; r++) {
                RandWord0 = s0;
                RandWord1 = s1;
                drone.stateTimeoutFrame = 0xcdcdcdcd;
                {
                    XbeOriginalScope o(0x00038e70);
                    ((Fn)0x00038e70)(&dcv, mins[m], rands[r]);
                }
                uint a = drone.stateTimeoutFrame, a0 = RandWord0, a1 = RandWord1;
                RandWord0 = s0;
                RandWord1 = s1;
                drone.stateTimeoutFrame = 0xcdcdcdcd;
                NDrone2_SetIdleTimeOut(&dcv, mins[m], rands[r]);
                runs++;
                if (a != drone.stateTimeoutFrame || a0 != RandWord0 || a1 != RandWord1)
                    Mismatch("min %d rand %u: frame 0x%x", mins[m], rands[r], a);
            }
    }
    RandWord0 = saved0;
    RandWord1 = saved1;
}

static void TestAlertStatus(void) {
    typedef void(__cdecl * Fn)(char, DCVars_tag *);
    current = "Drone_AlertStatusSet";
    static Drone_tag a, b;
    DCVars_tag da = {NULL, &a, NULL, &a.sm}, db = {NULL, &b, NULL, &b.sm};
    static const int statuses[] = {0, 1, 2, 3, 4, 5, 0xff};
    for (int s = 0; s < 7; s++)
        for (int n = 0; n < 7; n++) {
            memset(&a, 0xcd, sizeof(a));
            memset(&b, 0xcd, sizeof(b));
            a.alertStatus = b.alertStatus = (uchar)statuses[s];
            {
                XbeOriginalScope o(0x00031d30);
                ((Fn)0x00031d30)((char)statuses[n], &da);
            }
            Drone_AlertStatusSet((char)statuses[n], &db);
            runs++;
            if (memcmp(&a, &b, sizeof(a)) != 0)
                Mismatch("status %d -> %d: drone differs", statuses[s], statuses[n], 0);
        }
}

// ---------------------------------------------------------------------------------------------------------------- state machine

static void TestStateMachine(void) {
    current = "Drone_SM_SetState";
    typedef bool(__cdecl * SetFn)(StateMachineInfo_tag *, DSTATE, int);
    for (int state = 0; state < 300; state++)
        for (int p = 0; p < 3; p++) {
            StateMachineInfo_tag a, b;
            memset(&a, 0xcd, sizeof(a));
            a.id = 0x7fff1234;   // no drone has it
            b = a;
            static const int params[] = {0, 1, -7};
            bool ra;
            {
                XbeOriginalScope o(0x0004e320);
                ra = ((SetFn)0x0004e320)(&a, (DSTATE)state, params[p]);
            }
            bool rb = Drone_SM_SetState(&b, (DSTATE)state, params[p]);
            runs++;
            if (ra != rb || memcmp(&a, &b, sizeof(a)) != 0)
                Mismatch("state %d: returned %d vs %d", state, ra, rb);
        }

    current = "Drone_SM_InitObject";
    typedef bool(__cdecl * InitFn)(obj_tag *);
    ushort savedCount = NPCGlobals.NumDrones;
    static const ushort counts[] = {0, 1, 5, 0xfffe, 0xffff};
    static const short states[] = {0, 4, 6, 0x56, -1};
    for (int c = 0; c < 5; c++)
        for (int s = 0; s < 5; s++) {
            static uchar objA[0x200], objB[0x200];
            static Drone_tag droneA, droneB;
            memset(objA, 0, sizeof(objA));
            memset(&droneA, 0xcd, sizeof(droneA));
            ((obj_tag *)objA)->extraObjectData = &droneA;
            ((obj_tag *)objA)->objectType = OBJECTTYPE_DRONE;
            ((obj_tag *)objA)->curState = states[s];
            memcpy(objB, objA, sizeof(objB));
            memcpy(&droneB, &droneA, sizeof(droneB));
            ((obj_tag *)objB)->extraObjectData = &droneB;

            NPCGlobals.NumDrones = counts[c];
            bool ra;
            {
                XbeOriginalScope o(0x0004e2a0);
                ra = ((InitFn)0x0004e2a0)((obj_tag *)objA);
            }
            ushort ca = NPCGlobals.NumDrones;
            NPCGlobals.NumDrones = counts[c];
            bool rb = Drone_SM_InitObject((obj_tag *)objB);
            // The original stores its own NDrone2_ProcessStateMachine (0x4e180), ours ours: the same code, since the
            // original's entry jumps to ours
            if (droneB.processFunction == (void *)NDrone2_ProcessStateMachine)
                droneB.processFunction = (void *)0x0004e180;
            runs++;
            if (ra != rb || ca != NPCGlobals.NumDrones || memcmp(&droneA, &droneB, sizeof(droneA)) != 0)
                Mismatch("count 0x%x state %d: returned %d", counts[c], states[s], ra);
        }
    NPCGlobals.NumDrones = savedCount;
    {
        bool ra;
        {
            XbeOriginalScope o(0x0004e2a0);
            ra = ((InitFn)0x0004e2a0)(NULL);
        }
        runs++;
        if (ra != Drone_SM_InitObject(NULL))
            Mismatch("NULL object", 0, 0, 0);
    }

    current = "DroneAnim_SetEndAIState";
    typedef void(__cdecl * EndFn)(Drone_tag *, short, uint);
    static const short endStates[] = {0, 4, 0x56, 249, -1};
    for (int s = 0; s < 5; s++) {
        static uchar obj[0x200];
        static Drone_tag a, b;
        memset(obj, 0, sizeof(obj));
        memset(&a, 0xcd, sizeof(a));
        a.gameObj = (obj_tag *)obj;
        a.sm.id = 0x7fff1234;
        ((obj_tag *)obj)->objectType = OBJECTTYPE_DRONE;
        memcpy(&b, &a, sizeof(b));
        ((obj_tag *)obj)->extraObjectData = &a;
        {
            XbeOriginalScope o(0x00032e70);
            ((EndFn)0x00032e70)(&a, endStates[s], 0);   // no message: that would be delivered
        }
        ((obj_tag *)obj)->extraObjectData = &b;
        DroneAnim_SetEndAIState(&b, endStates[s], 0);
        runs++;
        if (memcmp(&a.sm, &b.sm, sizeof(a.sm)) != 0)
            Mismatch("state %d: state machine differs", endStates[s], 0, 0);
    }
}

// ---------------------------------------------------------------------------------------------------------------- bots, routes

static void TestBots(void) {
    current = "BOT_getDefaultStats";
    typedef BOT_stats_t *(__cdecl * StatsFn)(uint);
    for (uint id = 0; id < 40; id++) {
        BOT_stats_t *a;
        {
            XbeOriginalScope o(0x0001a250);
            a = ((StatsFn)0x0001a250)(id);
        }
        runs++;
        if (a != BOT_getDefaultStats(id))
            Mismatch("id %u", id, 0, 0);
    }

    static Drone_tag drone;
    memset(&drone, 0, sizeof(drone));
    static uchar opponent[0x200];

    current = "BOT_getAggressionMul";
    typedef double(__cdecl * AggFn)(Drone_tag *);
    static const float distances[] = {0.0f, 1.0f, 2.0f, 2.4999f, 2.5f, 3.0f, 100.0f, -1.0f};
    for (int ag = 0; ag < 8; ag++)
        for (int opp = 0; opp < 2; opp++)
            for (int d = 0; d < 8; d++) {
                drone.aggression = (uchar)ag;
                drone.opponent = opp ? (obj_tag *)opponent : NULL;
                drone.distanceToTarget = distances[d];
                double a;
                {
                    XbeOriginalScope o(0x0001ab70);
                    a = ((AggFn)0x0001ab70)(&drone);
                }
                double b = BOT_getAggressionMul(&drone);
                runs++;
                if (memcmp(&a, &b, sizeof(a)) != 0)
                    Mismatch("aggression %d opponent %d distance %d", ag, opp, d);
            }

    current = "BOT_getMovementSpeedMul";
    typedef float(__cdecl * SpeedFn)(Drone_tag *);
    for (int sp = 0; sp < 256; sp++) {
        drone.speed = (uchar)sp;
        float a;
        {
            XbeOriginalScope o(0x0001ac30);
            a = ((SpeedFn)0x0001ac30)(&drone);
        }
        runs++;
        if (FloatBits(a) != FloatBits(BOT_getMovementSpeedMul(&drone)))
            Mismatch("speed %d", sp, 0, 0);
    }

    current = "BOT_getMovePossibility";
    typedef bool(__cdecl * MoveFn)(Drone_tag *, int);
    uint saved0 = RandWord0, saved1 = RandWord1;
    for (int acc = 0; acc < 10; acc++)
        for (int sp = 0; sp < 4; sp++)
            for (int odds = 0; odds < 12; odds++) {
                drone.accuracy = (uchar)acc;
                drone.speed = (uchar)sp;
                uint s0 = Rnd(), s1 = Rnd();
                RandWord0 = s0;
                RandWord1 = s1;
                bool a;
                {
                    XbeOriginalScope o(0x0001ac70);
                    a = ((MoveFn)0x0001ac70)(&drone, odds);
                }
                uint a0 = RandWord0, a1 = RandWord1;
                RandWord0 = s0;
                RandWord1 = s1;
                bool b = BOT_getMovePossibility(&drone, odds);
                runs++;
                if (a != b || a0 != RandWord0 || a1 != RandWord1)
                    Mismatch("accuracy %d speed %d odds %d", acc, sp, odds);
            }
    RandWord0 = saved0;
    RandWord1 = saved1;

    current = "BOTSTATE_getStateType";
    typedef uint(__cdecl * TypeFn)(uint);
    for (uint st = 0; st < 300; st++) {
        uint a;
        {
            XbeOriginalScope o(0x0001b950);
            a = ((TypeFn)0x0001b950)(st);
        }
        runs++;
        if (a != BOTSTATE_getStateType(st))
            Mismatch("state %u: %u", st, a, 0);
    }
}

static void TestRoutesAndEmitters(void) {
    current = "AINetwork_RouteIsValid";
    typedef uchar(__cdecl * ValidFn)(AIRoute_tag *);
    static AIRoute_tag route;
    memset(&route, 0, sizeof(route));
    for (int f = 0; f < 0x10000; f += 7) {
        route.flags = (ushort)f;
        uchar a;
        {
            XbeOriginalScope o(0x000479d0);
            a = ((ValidFn)0x000479d0)(&route);
        }
        runs++;
        if (a != AINetwork_RouteIsValid(&route))
            Mismatch("flags 0x%x", f, 0, 0);
    }
    {
        uchar a;
        {
            XbeOriginalScope o(0x000479d0);
            a = ((ValidFn)0x000479d0)(NULL);
        }
        runs++;
        if (a != AINetwork_RouteIsValid(NULL))
            Mismatch("NULL route", 0, 0, 0);
    }

    // The emitters allocate from the heap (Mem_Malloc, ours for both): on one scratch heap, freshly initialised for
    // each side so that every address (and so every block header's links) is the same, allocate and free with the
    // originals, then with ours, and compare the emitter and the whole heap after each step
    current = "AINetwork_AllocEmitter / FreeEmitter";
    typedef void(__cdecl * AllocFn)(AIEmitter_tag *, uint);
    typedef void(__cdecl * FreeFn)(AIEmitter_tag *);
    const uint heapSize = 0x10000;
    static uchar heap[0x10000], snapAlloc[2][0x10000], snapFree[2][0x10000];
    uint saved[4] = {PtrHeap, HeapByteSize, MallocMethod, QuickBlock};
    static const uint sizes[] = {1, 7, 8, 100, 0x400, 0x1000};
    for (int s = 0; s < 6; s++) {
        AIEmitter_tag e[2], eAlloc[2];
        for (int side = 0; side < 2; side++) {
            PtrHeap = (uint32_t)(uintptr_t)heap;
            HeapByteSize = heapSize;
            Mem_Init();
            memset(&e[side], 0xcd, sizeof(AIEmitter_tag));
            if (side == 0) {
                XbeOriginalScope o(0x00049d10);
                ((AllocFn)0x00049d10)(&e[side], sizes[s]);
            } else {
                AINetwork_AllocEmitter(&e[side], sizes[s]);
            }
            eAlloc[side] = e[side];
            memcpy(snapAlloc[side], heap, heapSize);
            if (side == 0) {
                XbeOriginalScope o(0x00049d40);
                ((FreeFn)0x00049d40)(&e[side]);
            } else {
                AINetwork_FreeEmitter(&e[side]);
            }
            memcpy(snapFree[side], heap, heapSize);
        }
        runs++;
        if (memcmp(&eAlloc[0], &eAlloc[1], sizeof(AIEmitter_tag)) != 0)
            Mismatch("size %u: emitters differ after the allocation", sizes[s], 0, 0);
        if (memcmp(snapAlloc[0], snapAlloc[1], heapSize) != 0 || memcmp(snapFree[0], snapFree[1], heapSize) != 0)
            Mismatch("size %u: heaps differ", sizes[s], 0, 0);
        if (memcmp(&e[0], &e[1], sizeof(AIEmitter_tag)) != 0)
            Mismatch("size %u: emitters differ after the free", sizes[s], 0, 0);
    }
    // An emitter with no data, and NULL, are left alone
    for (int side = 0; side < 2; side++) {
        AIEmitter_tag e;
        memset(&e, 0xcd, sizeof(e));
        e.data = NULL;
        AIEmitter_tag before = e;
        if (side == 0) {
            XbeOriginalScope o(0x00049d40);
            ((FreeFn)0x00049d40)(&e);
            ((FreeFn)0x00049d40)(NULL);
        } else {
            AINetwork_FreeEmitter(&e);
            AINetwork_FreeEmitter(NULL);
        }
        runs++;
        if (memcmp(&e, &before, sizeof(e)) != 0)
            Mismatch("side %d: an emitter with no data was changed", side, 0, 0);
    }
    PtrHeap = saved[0];
    HeapByteSize = saved[1];
    MallocMethod = saved[2];
    QuickBlock = saved[3];
}

void DroneShadow_Run(void) {
    runs = mismatches = reported = 0;
    TestProperties();
    TestBehaviourGet();
    TestRecoverTime();
    TestReactionTime();
    TestIdleTimeOut();
    TestAlertStatus();
    TestStateMachine();
    TestBots();
    TestRoutesAndEmitters();
    printf("[drone] leaf functions: %d runs, %d mismatches\n", runs, mismatches);
}
