#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "CharacterShadow.h"

#include "../anim/Actor.h"
#include "../anim/AnimationDatabase.h"
#include "../anim/Character.h"
#include "../anim/Events.h"
#include "../anim/Manager.h"
#include "../eagl/anim/AnimChannels.h"
#include "../eagl/anim/AnimObjects.h"
#include "../eagl/anim/FnAnim.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../../common/xbeOriginal.h"
#include "../render/Lights.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_CHARACTERSHADOW=1, once on the first simulation tick: anim/AnimationDatabase.cpp and
// anim/Character.cpp against the originals.
//
// Two passes run the same tests, calling everything at the original addresses: the first with the originals of
// 0x00013c00-0x000157e0 swapped back in, the second with our jumps. Each pass writes a log (floats as bits,
// records as bytes, pointers into the game's data as addresses, fresh allocations by content); the logs must
// match line for line. Everything a call writes is a copy or a buffer of the test's, or is put back after it.
//
//   - every loaded bank's FnDefaultAnimBank methods over every anim (by index and by name) and unknown names;
//   - PrivateData::GetAnimation and ActAnimationDatabase::GetAnimation over the lookup table's valid entries;
//   - ActAnimGroup's constructors, ChangeAnimation and destructor on those entries (the FnAnims by content);
//   - ActCharacterInfo on the live characters' descriptions and made-up ones (every type letter, one to three
//     digit numbers and scales, an unknown letter), its file name getters and destructor;
//   - CharacterDrawOptions::Set, LightBlock' GetLight and SetLight, SetBrightness, MakeCoord4, MinFloat on random
//     and special values (NaN, infinities, -0, out of range);
//   - on copies of the live characters: SetAlpha, GetScaleFactors, GetShadowTriangle, CalculateMuzzleFlashIntensity
//     with perturbed flash values; InheritWeaponLightingFromCar on the live weapons with the car's brightness
//     perturbed (both put back);
//   - the default and physics-off handlers (on a stand-in actor), SetCurrentActor, ActEventResolver.
// Loading (the database's constructor, BankInfo::Load), ChangeCharacter, LoadCharacter, Draw, the weapon calls
// and the fire and drop-weapon handlers change the game's state and are tested in game.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_faults;
bool g_counting;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    std::string s = what;
    s += ' ';
    char h[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(h, sizeof(h), "%02x", static_cast<const uint8_t *>(p)[i]);
        s += h;
    }
    Logf("%s", s.c_str());
}

void Case() {
    if (g_counting)
        g_cases++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
};

// Special floats the random ones are mixed with
const uint32_t kSpecials[] = { 0x00000000, 0x80000000, 0x3f800000, 0x3f800001, 0x3f7fffff, 0xbf800000, 0x7f800000,
                               0xff800000, 0x7fc00000, 0xffc00000, 0x00000001, 0x437f0000, 0x3b808081, 0xc2c80000 };

float SomeFloat(Rng *rng, float lo, float hi) {
    if (rng->Range(0, 3) == 0)
        return FromBits(kSpecials[rng->Range(0, int(sizeof(kSpecials) / sizeof(kSpecials[0])) - 1)]);
    return rng->Uniform(lo, hi);
}

// ---- the originals' addresses (our jumps in the second pass)

typedef int (__fastcall *BankIntFn)(FnDefaultAnimBank *, int);
typedef uint8_t *(__fastcall *BankAnimFn)(FnDefaultAnimBank *, int, int);
typedef const char *(__fastcall *BankNameFn)(FnDefaultAnimBank *, int, int);
typedef uint8_t *(__fastcall *BankAnimByNameFn)(FnDefaultAnimBank *, int, const char *);
typedef int (__fastcall *BankIndexFn)(FnDefaultAnimBank *, int, const char *);
typedef void (__fastcall *BankInitFn)(FnDefaultAnimBank *, int, AnimBank *);
typedef FnDefaultAnimBank *(__fastcall *BankDeleteFn)(FnDefaultAnimBank *, int, unsigned);
typedef void (__fastcall *GetAnimationFn)(void *, int, int, int, uint8_t **, uint8_t **);
typedef ActAnimGroup *(__fastcall *GroupConstructFn)(ActAnimGroup *, int, int, int);
typedef ActAnimGroup *(__fastcall *GroupCopyFn)(ActAnimGroup *, int, const ActAnimGroup *);
typedef void (__fastcall *GroupDestructFn)(ActAnimGroup *, int);
typedef void (__fastcall *GroupChangeFn)(ActAnimGroup *, int, int, int);
typedef void (__fastcall *SetBrightnessFn)(void *, int, float);
typedef Coord4 *(__cdecl *MakeCoord4Fn)(Coord4 *, float, float, float, float);
typedef const float *(__cdecl *MinFloatFn)(const float *, const float *);
typedef void (__fastcall *GetLightFn)(LightBlock *, int, int, Coord4 *, Coord4 *);
typedef void (__fastcall *SetLightFn)(LightBlock *, int, int, const Coord4 *, const Coord4 *);
typedef void (__fastcall *OptionsSetFn)(CharacterDrawOptions *, int, const CharacterDrawOptions *, int);
typedef void (__fastcall *SetAlphaFn)(ActCharacter *, int, float);
typedef void (__fastcall *ScaleFactorsFn)(ActCharacter *, int, float *, float *);
typedef Coord3 *(__fastcall *ShadowTriangleFn)(ActCharacter *, int);
typedef void (__fastcall *MuzzleFlashFn)(ActCharacter *, int, int);
typedef void (__fastcall *InheritLightingFn)(ActCharacter *, int);
typedef ActCharacterInfo *(__fastcall *InfoConstructFn)(ActCharacterInfo *, int, const char *);
typedef void (__fastcall *InfoDestructFn)(ActCharacterInfo *, int);
typedef char *(__fastcall *InfoFileNameFn)(ActCharacterInfo *, int, int);
typedef void (__fastcall *HandlerFn)(EventHandler *, int, float, RawEvent *, void *);
typedef void (__fastcall *SetCurrentActorFn)(ActEvents *, int, ActActor *);
typedef ActEventResolver *(__fastcall *ResolverConstructFn)(ActEventResolver *, int);
typedef void (__fastcall *ResolverDestructFn)(ActEventResolver *, int);

#define AT(type, address) reinterpret_cast<type>(uintptr_t(address))

#define ShadowActManager (*(ActManager **)0x001dd9cc)
#define ShadowActorDatabase (*(ActActorDatabase **)0x001dd9a0)
#define ShadowCurrentActor (*(ActActor **)0x001dd9c4)
#define ShadowPlayerCar (**(PhysicsObject ***)0x00234e40)

PrivateData *Banks() {
    if (ShadowActManager == NULL || ShadowActManager->animations == NULL)
        return NULL;
    return ShadowActManager->animations->data;
}

// The anim indices an entry of the lookup names are within its bank (or -1)
bool ValidEntry(PrivateData *data, int bank, int index) {
    if (data->lookup == NULL || data->banks[bank].anims == NULL)
        return false;
    int32_t words = int32_t(data->lookupSize / 4);
    int32_t at = data->lookup[bank] + index * 2;
    if (at < 0 || at + 1 >= words)
        return false;
    int count = data->banks[bank].animCount;
    for (int i = 0; i < 2; i++) {
        int32_t anim = data->lookup[at + i];
        if (anim != -1 && (anim < 0 || anim >= count))
            return false;
    }
    return true;
}

// ---- the banks

void TestBanks(Rng *rng) {
    PrivateData *data = Banks();
    if (data == NULL) {
        Logf("no animation database");
        return;
    }
    for (int b = 0; b < PrivateData::kMaxBanks; b++) {
        FnDefaultAnimBank *bank = data->banks[b].anims;
        if (bank == NULL)
            continue;
        Case();
        int count = AT(BankIntFn, 0x00013c90)(bank, 0);
        Logf("bank %d count %d unknown08 %08x", b, count, AT(BankIntFn, 0x00013d00)(bank, 0));
        for (int i = 0; i < count; i++) {
            Case();
            uint8_t *anim = AT(BankAnimFn, 0x00013ca0)(bank, 0, i);
            const char *name = AT(BankNameFn, 0x00013cb0)(bank, 0, i);
            uint8_t *byName = AT(BankAnimByNameFn, 0x00013cc0)(bank, 0, name);
            int index = AT(BankIndexFn, 0x00013cf0)(bank, 0, name);
            Logf(" %d %p %p %p %d", i, anim, name, byName, index);
        }
        const char *unknown[] = { "", "zzzz", "a", "~", "driving" };
        for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++) {
            Case();
            Logf(" '%s' %p %d", unknown[i], AT(BankAnimByNameFn, 0x00013cc0)(bank, 0, unknown[i]),
                 AT(BankIndexFn, 0x00013cf0)(bank, 0, unknown[i]));
        }
        // Init and the deleting destructor (flags 0: nothing freed) on a stand-in
        Case();
        FnDefaultAnimBank standIn;
        memset(&standIn, 0x5a, sizeof(standIn));
        AT(BankInitFn, 0x00013c80)(&standIn, 0, data->banks[b].bank);
        FnDefaultAnimBank *answer = AT(BankDeleteFn, 0x00013d10)(&standIn, 0, 0u);
        Logf(" stand-in %d", answer == &standIn);
        LogBytes(" stand-in", &standIn, sizeof(standIn));
    }
    (void)rng;
}

void TestLookups(Rng *rng) {
    PrivateData *data = Banks();
    if (data == NULL)
        return;
    for (int b = 0; b < PrivateData::kMaxBanks; b++) {
        for (int i = 0; i < 2000 && ValidEntry(data, b, i); i++) {
            Case();
            uint8_t *anim = reinterpret_cast<uint8_t *>(uintptr_t(0xdeadbeef));
            uint8_t *second = anim;
            AT(GetAnimationFn, 0x00013c00)(data, 0, b, i, &anim, &second);
            uint8_t *anim2 = NULL, *second2 = NULL;
            AT(GetAnimationFn, 0x00013c70)(ShadowActManager->animations, 0, b, i, &anim2, &second2);
            Logf("lookup %d %d %p %p %p %p", b, i, anim, second, anim2, second2);
        }
    }
    (void)rng;
}

void LogChannel(const char *what, const FnAnim *anim) {
    if (anim == NULL) {
        Logf("%s none", what);
        return;
    }
    LogBytes(what, anim, anim->type == kCompound ? sizeof(FnCompoundChannel) : sizeof(FnAnimMemoryMap));
}

void LogGroup(const char *what, const ActAnimGroup *group) {
    Logf("%s %08x %08x %p %p %d %d", what, Bits(group->length), Bits(group->secondLength), group->animData,
         group->secondData, group->index, group->bank);
    LogChannel(" anim", group->animData != NULL ? group->anim : NULL);
    LogChannel(" second", group->secondData != NULL ? group->second : NULL);
}

void TestGroups(Rng *rng) {
    PrivateData *data = Banks();
    if (data == NULL)
        return;
    std::vector<int> pairs;
    for (int b = 0; b < PrivateData::kMaxBanks; b++)
        for (int i = 0; i < 2000 && ValidEntry(data, b, i); i += 1 + rng->Range(0, 6))
            pairs.push_back(b * 10000 + i);
    for (size_t p = 0; p < pairs.size(); p++) {
        int bank = pairs[p] / 10000, index = pairs[p] % 10000;
        Case();
        ActAnimGroup group;
        memset(&group, 0x5a, sizeof(group));
        AT(GroupConstructFn, 0x00014360)(&group, 0, bank, index);
        LogGroup("group", &group);
        ActAnimGroup copy;
        memset(&copy, 0x5a, sizeof(copy));
        AT(GroupCopyFn, 0x000143a0)(&copy, 0, &group);
        LogGroup("copy", &copy);
        int other = pairs[size_t(rng->Range(0, int(pairs.size()) - 1))];
        AT(GroupChangeFn, 0x00014240)(&copy, 0, other / 10000, other % 10000);
        LogGroup("changed", &copy);
        AT(GroupDestructFn, 0x00014310)(&copy, 0);
        AT(GroupDestructFn, 0x00014310)(&group, 0);
    }
    // A negative index: no animation
    Case();
    ActAnimGroup empty;
    memset(&empty, 0x5a, sizeof(empty));
    AT(GroupConstructFn, 0x00014360)(&empty, 0, 3, -1);
    LogBytes("empty", &empty, sizeof(empty));
    AT(GroupDestructFn, 0x00014310)(&empty, 0);
}

// ---- ActCharacterInfo

void LogInfo(const ActCharacterInfo *info) {
    const char *base = info->description;
    char *const tokens[] = { info->numberToken, info->name, info->texture, info->texture1, info->texture2,
                             info->scaleToken };
    std::string offsets;
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) {
        char text[16];
        snprintf(text, sizeof(text), " %d", tokens[i] != NULL ? int(tokens[i] - base) : -1);
        offsets += text;
    }
    Logf("info %d %d%s %08x %08x suffix %d", info->number, info->type, offsets.c_str(), Bits(info->scale),
         Bits(info->inverseScale), int(info->lodSuffix - info->modelPath));
    LogBytes(" description", info->description, sizeof(info->description));
    LogBytes(" model", info->modelPath, sizeof(info->modelPath));
    const char *const paths[] = { info->texturePath, info->texture1Path, info->texture2Path };
    for (int i = 0; i < 3; i++)
        Logf(" path %s", paths[i] != NULL ? paths[i] : "(none)");
}

void TestDescription(const char *text) {
    Case();
    ActCharacterInfo info;
    memset(&info, 0xcd, sizeof(info));
    AT(InfoConstructFn, 0x00015260)(&info, 0, text);
    Logf("description '%s'", text);
    LogInfo(&info);
    for (int lod = 0; lod < 3; lod++) {
        char *dat = AT(InfoFileNameFn, 0x00015640)(&info, 0, lod);
        Logf(" dat %d %s", int(dat - info.modelPath), dat);
        char *rel = AT(InfoFileNameFn, 0x00015680)(&info, 0, lod);
        Logf(" rel %d %s", int(rel - info.modelPath), rel);
    }
    AT(InfoDestructFn, 0x00015600)(&info, 0);
}

// A live character's description, from its tokens
std::string Description(const ActCharacterInfo *info) {
    std::string text = info->numberToken;
    text += '.';
    text += info->name;
    if (info->texture != NULL) {
        text += '.';
        text += info->texture;
    } else {
        text += '.';
        text += info->texture1 != NULL ? info->texture1 : "";
        text += '.';
        text += info->texture2 != NULL ? info->texture2 : "";
    }
    text += '.';
    text += info->scaleToken;
    return text;
}

std::vector<ActCharacter *> LiveCharacters() {
    std::vector<ActCharacter *> characters;
    ActActorDatabase *db = ShadowActorDatabase;
    if (db == NULL || db->head == NULL)
        return characters;
    for (PointerListNode *node = db->head->next; node != db->head && characters.size() < 64; node = node->next) {
        ActActor *actor = static_cast<ActActor *>(node->value);
        if (actor != NULL && actor->character != NULL && actor->character->info != NULL)
            characters.push_back(actor->character);
    }
    return characters;
}

void TestInfos(Rng *rng) {
    std::vector<ActCharacter *> characters = LiveCharacters();
    Logf("characters %u", unsigned(characters.size()));
    for (size_t i = 0; i < characters.size(); i++)
        TestDescription(Description(characters[i]->info).c_str());
    const char *made[] = {
        "5.hbond.skina.skinb.100", "12.Hguard.t1.t2.95", "7.pdriver.skin.105", "3.Pman.tex.5", "1.cbody.a.b.10",
        "8.Cthing.x.y.33", "9.aped.coat.120", "0.Aworker.t.1", "4.zunknown.t.17", "45.p.t.999", "123.h.a.b.20",
        "6.pmodel.tex.1000", "2.pmodel.tex.07", "5.hbond.skina.skinb.46",
    };
    for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); i++)
        TestDescription(made[i]);
    for (int n = 0; n < 40; n++) {
        char text[64];
        const char letters[] = "hpcaHPCAx";
        char letter = letters[rng->Range(0, 8)];
        int scaleDigits = rng->Range(1, 3), scale = rng->Range(0, 999);
        char scaleText[8];
        snprintf(scaleText, sizeof(scaleText), "%0*d", scaleDigits, scale % (scaleDigits == 1 ? 10 :
                                                                            scaleDigits == 2 ? 100 : 1000));
        if (letter == 'h' || letter == 'c' || letter == 'H' || letter == 'C')
            snprintf(text, sizeof(text), "%d.%cm%d.ta%d.tb%d.%s", rng->Range(0, 99), letter, n, n, n, scaleText);
        else
            snprintf(text, sizeof(text), "%d.%cm%d.t%d.%s", rng->Range(0, 99), letter, n, n, scaleText);
        TestDescription(text);
    }
}

// ---- the helpers and the options

void TestHelpers(Rng *rng) {
    for (int n = 0; n < 200; n++) {
        Case();
        uint8_t object[0x40];
        memset(object, 0x5a, sizeof(object));
        float level = SomeFloat(rng, -0.5f, 1.5f);
        AT(SetBrightnessFn, 0x000143e0)(object, 0, level);
        Logf("brightness %08x", Bits(level));
        LogBytes(" object", object, sizeof(object));

        Case();
        Coord4 made;
        memset(&made, 0x5a, sizeof(made));
        float v[4];
        for (int i = 0; i < 4; i++)
            v[i] = SomeFloat(rng, -100.0f, 100.0f);
        Coord4 *answer = AT(MakeCoord4Fn, 0x00014440)(&made, v[0], v[1], v[2], v[3]);
        Logf("coord4 %d", answer == &made);
        LogBytes(" made", &made, sizeof(made));

        Case();
        float pair[2] = { SomeFloat(rng, -2.0f, 2.0f), SomeFloat(rng, -2.0f, 2.0f) };
        const float *min = AT(MinFloatFn, 0x00014880)(&pair[0], &pair[1]);
        Logf("min %08x %08x %d", Bits(pair[0]), Bits(pair[1]), int(min - pair));
    }

    for (int n = 0; n < 100; n++) {
        LightBlock lights;
        for (int i = 0; i < 12; i++)
            lights.directions[i / 4][i % 4] = SomeFloat(rng, -1.0f, 1.0f);
        for (int i = 0; i < 4; i++) {
            lights.colours[i].x = SomeFloat(rng, 0.0f, 1.0f);
            lights.colours[i].y = SomeFloat(rng, 0.0f, 1.0f);
            lights.colours[i].z = SomeFloat(rng, 0.0f, 1.0f);
            lights.colours[i].w = SomeFloat(rng, 0.0f, 1.0f);
        }
        int light = rng->Range(0, 3);
        Case();
        Coord4 out[2];
        memset(out, 0x5a, sizeof(out));
        AT(GetLightFn, 0x000148a0)(&lights, 0, light, &out[0], &out[1]);
        LogBytes("get light", out, sizeof(out));
        Case();
        Coord4 in[2];
        for (int i = 0; i < 2; i++) {
            in[i].x = SomeFloat(rng, -1.0f, 1.0f);
            in[i].y = SomeFloat(rng, -1.0f, 1.0f);
            in[i].z = SomeFloat(rng, -1.0f, 1.0f);
            in[i].w = SomeFloat(rng, -1.0f, 1.0f);
        }
        AT(SetLightFn, 0x000148f0)(&lights, 0, light, &in[0], &in[1]);
        LogBytes("set light", &lights, sizeof(lights));
    }

    for (int n = 0; n < 200; n++) {
        Case();
        CharacterDrawOptions from, to;
        uint32_t *words = reinterpret_cast<uint32_t *>(&from);
        for (size_t i = 0; i < sizeof(from) / 4; i++)
            words[i] = rng->Next() ^ (rng->Next() << 8);
        const int32_t alphas[] = { 0, 1, 7, 8, 128, 254, 255, 256, -1, -300, 100000, 0x7fffffff };
        from.alpha = rng->Range(0, 2) == 0 ? alphas[rng->Range(0, 11)] : rng->Range(-10, 300);
        from.shadowColour[0] = SomeFloat(rng, 0.0f, 1.0f);
        memset(&to, 0x5a, sizeof(to));
        int shadow = rng->Range(0, 3) == 0 ? rng->Range(-2, 5) : rng->Range(0, 1);
        AT(OptionsSetFn, 0x00014940)(&to, 0, &from, shadow);
        Logf("options %d %d", from.alpha, shadow);
        LogBytes(" to", &to, sizeof(to));
    }
}

// ---- the characters, on copies

void TestCharacters(Rng *rng) {
    std::vector<ActCharacter *> characters = LiveCharacters();
    for (size_t c = 0; c < characters.size(); c++) {
        ActCharacter *live = characters[c];
        for (int n = 0; n < 8; n++) {
            Case();
            ActCharacter copy = *live;
            CharacterDrawOptions options = *live->options;
            copy.options = &options;
            float alpha = SomeFloat(rng, -0.2f, 1.2f);
            AT(SetAlphaFn, 0x00014bf0)(&copy, 0, alpha);
            Logf("alpha %08x %d", Bits(alpha), options.alpha);
            float scale = 0.0f, inverse = 0.0f;
            AT(ScaleFactorsFn, 0x00014c20)(&copy, 0, &scale, &inverse);
            Coord3 *triangle = AT(ShadowTriangleFn, 0x00014870)(&copy, 0);
            Logf(" scales %08x %08x triangle %d", Bits(scale), Bits(inverse),
                 int(reinterpret_cast<uint8_t *>(triangle) - reinterpret_cast<uint8_t *>(&options)));

            const uint32_t flashes[] = { 0x00000000, 0x3e99999a, 0x3f666666, 0x3f800000, 0x3fc00000, 0x7fc00000 };
            for (int weapon = -1; weapon < ActCharacter::kWeapons; weapon++) {
                if (weapon >= 0 && live->weapons[weapon] == NULL)
                    continue;
                Case();
                ActCharacter flash = *live;
                flash.muzzleFlash = rng->Range(0, 1) == 0 ? FromBits(flashes[rng->Range(0, 5)])
                                                          : rng->Uniform(0.0f, 1.5f);
                AT(MuzzleFlashFn, 0x00015010)(&flash, 0, weapon);
                Logf(" flash %d", weapon);
                LogBytes(" character", &flash, sizeof(flash));
            }
        }

        if (ShadowPlayerCar == NULL || ShadowPlayerCar->renderObject == NULL)
            continue;
        uint8_t carBrightness = ShadowPlayerCar->renderObject->brightness;
        uint8_t saved[ActCharacter::kWeapons] = {};
        for (int i = 0; i < ActCharacter::kWeapons; i++)
            if (live->weapons[i] != NULL)
                saved[i] = reinterpret_cast<RSceneObj *>(live->weapons[i])->brightness;
        const uint8_t levels[] = { carBrightness, 0, 1, 2, 127, 128, 200, 254, 255 };
        for (size_t l = 0; l < sizeof(levels); l++) {
            Case();
            ShadowPlayerCar->renderObject->brightness = levels[l];
            AT(InheritLightingFn, 0x00015180)(live, 0);
            for (int i = 0; i < ActCharacter::kWeapons; i++)
                Logf(" lighting %u weapon %d %d", levels[l], i, live->weapons[i] != NULL ?
                     reinterpret_cast<RSceneObj *>(live->weapons[i])->brightness : -1);
        }
        ShadowPlayerCar->renderObject->brightness = carBrightness;
        for (int i = 0; i < ActCharacter::kWeapons; i++)
            if (live->weapons[i] != NULL)
                reinterpret_cast<RSceneObj *>(live->weapons[i])->brightness = saved[i];
    }
}

// ---- the events

void TestEvents(Rng *rng) {
    ActActor *current = ShadowCurrentActor;

    Case();
    EventHandler handler = { reinterpret_cast<const void *>(uintptr_t(0x0018a050)), NULL };
    RawEvent event = { 3, 0.5f, { 0x3f000000, 0x3f800000 } };
    AT(HandlerFn, 0x00015770)(&handler, 0, 1.0f, &event, NULL);
    LogBytes("default", &handler, sizeof(handler));

    Case();
    uint8_t actor[sizeof(ActActor)];
    memset(actor, 0x5a, sizeof(actor));
    ShadowCurrentActor = reinterpret_cast<ActActor *>(actor);
    AT(HandlerFn, 0x000157d0)(&handler, 0, 1.0f, &event, NULL);
    LogBytes("physics off", actor, sizeof(actor));

    Case();
    uint8_t events[sizeof(ActEvents)];
    memset(events, 0x5a, sizeof(events));
    ActActor *someone = reinterpret_cast<ActActor *>(uintptr_t(0x12345678 + rng->Range(0, 255) * 4));
    AT(SetCurrentActorFn, 0x000156f0)(reinterpret_cast<ActEvents *>(events), 0, someone);
    LogBytes("events", events, sizeof(events));
    Logf(" current %d", ShadowCurrentActor == someone);
    ShadowCurrentActor = current;

    Case();
    ActEventResolver resolver;
    memset(&resolver, 0x5a, sizeof(resolver));
    AT(ResolverConstructFn, 0x00015710)(&resolver, 0);
    if (resolver.target != NULL)
        LogBytes("resolver", resolver.target, sizeof(EventTarget));
    else
        Logf("resolver none");
    AT(ResolverDestructFn, 0x00015750)(&resolver, 0);
}

bool Safe(void (*test)(Rng *), Rng *rng) {
#ifdef _MSC_VER
    __try {
        test(rng);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        return false;
    }
#else
    test(rng);
    return true;
#endif
}

void RunPass(std::string *log, bool original) {
    g_log = log;
    if (original)
        XbeOriginal_RestoreRange(0x00013c00, 0x000157e0, true);
    void (*const tests[])(Rng *) = {TestBanks, TestLookups, TestGroups, TestInfos, TestHelpers, TestCharacters,
                                    TestEvents};
    uint32_t seed = 0x5c4a7bu;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        Rng rng = {seed + uint32_t(i) * 7919u};
        if (!Safe(tests[i], &rng))
            Logf("fault in test %u", unsigned(i));
    }
    if (original)
        XbeOriginal_RestoreRange(0x00013c00, 0x000157e0, false);
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

void CharacterShadow_Run(void) {
    const char *setting = getenv("NIGHTFIRE_CHARACTERSHADOW");
    if (setting == NULL || atoi(setting) == 0)
        return;
    if (ShadowActManager == NULL) {
        printf("[character] no ActManager yet - skipped\n");
        fflush(stdout);
        return;
    }

    std::string original, ours;
    g_counting = true;
    RunPass(&original, true);
    g_counting = false;
    RunPass(&ours, false);

    std::vector<std::string> a, b;
    SplitLines(original, &a);
    SplitLines(ours, &b);
    size_t lines = a.size() > b.size() ? a.size() : b.size();
    int differ = 0;
    for (size_t i = 0; i < lines; i++) {
        const std::string *x = i < a.size() ? &a[i] : NULL;
        const std::string *y = i < b.size() ? &b[i] : NULL;
        if (x != NULL && y != NULL && *x == *y)
            continue;
        if (differ < 10)
            printf("[character]   line %u: original \"%.200s\" ours \"%.200s\"\n", unsigned(i),
                   x != NULL ? x->c_str() : "(none)", y != NULL ? y->c_str() : "(none)");
        differ++;
    }
    printf("[character] banks, lookups, anim groups, character infos, helpers, characters, events: %d cases, %u "
           "checks, %d differ (%d faults)\n", g_cases, unsigned(lines), differ, g_faults);
    fflush(stdout);
}
