// Shadow tests for the script player's functions that run on scratch state (engine/Script.cpp,
// game/obj/ScriptPlayer.cpp): each is run, the original and ours, on the same made-up inputs, and what each answers
// and writes is compared bit for bit. Run at start with MenuShadowTests=on (MenuProbe.cpp).
//
// - Script_FindKey, Script_GetLinearWeights and GetSplineWeights on random keys and frames;
// - Script_Set2Start on a random script instance;
// - SP_Hit on script players whose streams' objects carry made-up hit lists. Not with requiredBulletType 0 and
//   triggerMask 2, where it filters the hit lists through Collide_FilterBullets, which frees the game's records.
//
// Not here: Script_FreeFromHashTable needs the game's hashtable, which does not exist yet when this runs, and
// Player_Cam2Mode and Drone_CoderCreate need a player or the drone system in play. The time demos do not reach
// them either (docs/nightfire-port-lifts.md).

#include "ScriptShadow.h"
#include "AllOriginal.h"

#include "../../common/xbeOriginal.h"
#include "../engine/Script.h"
#include "../game/obj/ScriptPlayer.h"
#include "../game/obj/bullet.h"
#include "../game.h"
#include "../memory.h"

// The number of entries in the game's hashtable
#define HashtableCount U32_AT(XbeOurs(0x001fe684))

#include <stdio.h>
#include <string.h>

namespace {

const unsigned kFindKey = 0x000c11b0;
const unsigned kGetLinearWeights = 0x000c1210;
const unsigned kGetSplineWeights = 0x000c12b0;
const unsigned kSet2Start = 0x000c1850;
const unsigned kSPHit = 0x000c4100;

unsigned s_seed = 2024;
unsigned NextRandom() {
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
float RandomFloat(float lo, float hi) {
    return lo + (hi - lo) * (float)(NextRandom() & 0xffff) / 65535.0f;
}

int s_cases, s_checks, s_differ;

void Check(const char *what, int trial, const void *ours, const void *theirs, size_t size) {
    s_checks++;
    if (memcmp(ours, theirs, size) == 0)
        return;
    if (s_differ++ < 10)
        printf("[script] %s case %d differs\n", what, trial);
}

// The originals take registers: these put them there
__declspec(naked) int __cdecl OriginalFindKey(KEYED_POSROT *keys, int count, float frame) {
    _asm {
        push ebx
        mov ebx, [esp + 8]
        mov eax, [esp + 12]
        push dword ptr [esp + 16]
        mov ecx, 0x000c11b0
        call ecx
        add esp, 4
        pop ebx
        ret
    }
}

__declspec(naked) ushort __cdecl OriginalSPHit(SCRIPTPLAYER *scriptPlayer) {
    _asm {
        push ebx
        mov ebx, [esp + 8]
        mov eax, 0x000c4100
        call eax
        pop ebx
        ret
    }
}

typedef void (__cdecl *WeightsFn)(int *keys, float *weights, float time, KEYED_POSROT *base, int count);

void RandomKeys(KEYED_POSROT *keys, int count) {
    float frame = RandomFloat(0.0f, 3.0f);
    for (int i = 0; i < count; i++) {
        memset(&keys[i], 0, sizeof(keys[i]));
        keys[i].frame = frame;
        frame += (NextRandom() % 4 == 0) ? 1.0f : RandomFloat(0.5f, 12.0f);
    }
}

void TestWeights() {
    KEYED_POSROT keys[40];
    for (int trial = 0; trial < 400; trial++) {
        int count = 1 + NextRandom() % 39;
        RandomKeys(keys, count + 1);
        float time = RandomFloat(-5.0f, keys[count].frame + 5.0f);
        if (trial % 7 == 0)
            time = keys[NextRandom() % (count + 1)].frame;   // exactly on a key
        s_cases++;

        int ours = Script_FindKey(keys, count, time);
        int theirs;
        {
            XbeOriginalScope original(kFindKey);
            theirs = OriginalFindKey(keys, count, time);
        }
        Check("Script_FindKey", trial, &ours, &theirs, sizeof(ours));

        int oursKeys[4], theirKeys[4];
        float oursWeights[4], theirWeights[4];
        memset(oursKeys, 0xcd, sizeof(oursKeys));
        memset(theirKeys, 0xcd, sizeof(theirKeys));
        memset(oursWeights, 0xcd, sizeof(oursWeights));
        memset(theirWeights, 0xcd, sizeof(theirWeights));
        Script_GetLinearWeights(oursKeys, oursWeights, time, keys, count);
        {
            XbeOriginalScope original(kGetLinearWeights);
            ((WeightsFn)(uintptr_t)kGetLinearWeights)(theirKeys, theirWeights, time, keys, count);
        }
        Check("Script_GetLinearWeights keys", trial, oursKeys, theirKeys, sizeof(oursKeys));
        Check("Script_GetLinearWeights weights", trial, oursWeights, theirWeights, sizeof(oursWeights));

        GetSplineWeights(oursKeys, oursWeights, time, keys, count);
        {
            XbeOriginalScope original(kGetSplineWeights);
            ((WeightsFn)(uintptr_t)kGetSplineWeights)(theirKeys, theirWeights, time, keys, count);
        }
        Check("GetSplineWeights keys", trial, oursKeys, theirKeys, sizeof(oursKeys));
        Check("GetSplineWeights weights", trial, oursWeights, theirWeights, sizeof(oursWeights));
    }
}

void RandomScript(SCRIPTINFO *scriptInfo) {
    unsigned char *bytes = (unsigned char *)scriptInfo;
    for (size_t i = 0; i < sizeof(*scriptInfo); i++)
        bytes[i] = (unsigned char)NextRandom();
    scriptInfo->numElements = NextRandom() % 65;
}

void TestSet2Start() {
    static SCRIPTINFO before, ours, theirs;
    for (int trial = 0; trial < 50; trial++) {
        RandomScript(&before);
        s_cases++;
        ours = before;
        theirs = before;
        Script_Set2Start(&ours);
        {
            XbeOriginalScope original(kSet2Start);
            ((void (__cdecl *)(SCRIPTINFO *))(uintptr_t)kSet2Start)(&theirs);
        }
        Check("Script_Set2Start", trial, &ours, &theirs, sizeof(ours));
    }
}

// Objects of the kinds SP_Hit looks for, each with a hit list of others
struct HitWorld {
    obj_tag objects[8];
    HITDATA_tag hits[24];
    BU_tag bullets[2];
    weapon_definition_tag weapons[2];
    SCRIPTINFO scriptInfo;
    SCRIPTPLAYER scriptPlayer;
};

void BuildHitWorld(HitWorld *world) {
    memset(world, 0, sizeof(*world));
    static const char kTypes[8] = { OBJECTTYPE_DRONE, OBJECTTYPE_PLAYER, OBJECTTYPE_BULLET, OBJECTTYPE_BULLET,
                                    OBJECTTYPE_GFX, OBJECTTYPE_DOOR, 0, OBJECTTYPE_DRONE };
    for (int i = 0; i < 8; i++)
        world->objects[i].objectType = kTypes[i];
    for (int b = 0; b < 2; b++) {
        world->weapons[b].weaponVariantNum = (short)(1 + NextRandom() % 3);
        world->bullets[b].wpnDef = &world->weapons[b];
        world->objects[2 + b].extraObjectData = &world->bullets[b];
    }

    // The first four objects are the script's: each gets a short list of hits by any object, or by nothing
    int nextHit = 0;
    for (int i = 0; i < 4; i++) {
        int count = NextRandom() % 5;
        HITDATA_tag *previous = NULL;
        for (int h = 0; h < count && nextHit < 24; h++) {
            HITDATA_tag *hit = &world->hits[nextHit++];
            hit->hitObj = (NextRandom() % 6 == 0) ? NULL : &world->objects[NextRandom() % 8];
            if (previous == NULL)
                world->objects[i].hitList = hit;
            else
                previous->next = hit;
            previous = hit;
        }
    }

    SCRIPTINFO *scriptInfo = &world->scriptInfo;
    scriptInfo->numElements = 1 + NextRandom() % 6;
    for (int s = 0; s < scriptInfo->numElements; s++) {
        scriptInfo->streamDataBuffer[s].whatKindOfInterpolation = (uchar)(NextRandom() % 8);
        scriptInfo->streamDataBuffer[s].linkedGameOrSoundObject =
            (NextRandom() % 5 == 0) ? NULL : &world->objects[NextRandom() % 4];
    }

    SCRIPTPLAYER *scriptPlayer = &world->scriptPlayer;
    scriptPlayer->nthScript = NextRandom() % 2;
    scriptPlayer->scriptInfos[scriptPlayer->nthScript] = (NextRandom() % 8 == 0) ? NULL : scriptInfo;
    scriptPlayer->switchChannel = (NextRandom() % 3 == 0) ? 0 : 1 + NextRandom() % 255;
    scriptPlayer->requiredBulletType = (char)(NextRandom() % 4);
    do {
        scriptPlayer->triggerMask = (ushort)(NextRandom() & 0x1f);
    } while (scriptPlayer->requiredBulletType == 0 && scriptPlayer->triggerMask == SP_Trigger_BulletHit);
}

void TestSPHit() {
    static HitWorld world, before, after;
    for (int trial = 0; trial < 200; trial++) {
        BuildHitWorld(&world);
        before = world;
        s_cases++;
        ushort theirs;
        {
            XbeOriginalScope original(kSPHit);
            theirs = OriginalSPHit(&world.scriptPlayer);
        }
        after = world;
        ushort ours = SP_Hit(&world.scriptPlayer);
        Check("SP_Hit", trial, &ours, &theirs, sizeof(ours));
        Check("SP_Hit (original) writes", trial, &after, &before, sizeof(world));
        Check("SP_Hit writes", trial, &world, &before, sizeof(world));
    }
}

} // namespace

void ScriptShadow_Run(void) {
    s_cases = s_checks = s_differ = 0;
    TestWeights();
    TestSet2Start();
    TestSPHit();
    printf("[script] Script/SP shadow: %d cases, %d checks, %d differ\n", s_cases, s_checks, s_differ);
}
