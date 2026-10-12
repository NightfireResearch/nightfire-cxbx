#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameFrontEndShadowA.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <string>

#include "../camera/Camera.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/GameLoop.h"
#include "../engine/UMemory.hpp"
#include "../game/Gallery.h"
#include "../game/Hud.h"
#include "../game/LoadingScreen.h"
#include "../game/MissionManager.h"
#include "../game/WeaponCameras.h"
#include "../game/WeaponManager.h"
#include "../render/Renderer.h"
#include "../world/Targeting.h"
#include "../world/World.h"
#include "../Scheduler.hpp"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GFE_A (GHud part 1), run once from the first simulation tick when
// NIGHTFIRE_GAMEFRONTENDSHADOWA is set. Each case copies the live HUD into a buffer, perturbs it (times, fades, the
// letterbox, the lock, the gadget, stylin, the messages, the pause menu, the cheat input...) and the state it reads
// (the mission manager's health and objective count, the selected weapon, the renderer's screen mode and frame
// count, the launch page), then runs the ORIGINAL (the package's entries swapped back) and the PORT on the same
// state, put back between the two. Every call out of the package is a recording fake on both sides: the gallery's
// draws and states (with the gallery's colour and scale at each draw), Draw's boxes, sprites and model matrix, the
// materials' blend and depth modes, the menu sounds, the locale's symbols (fixed strings, some long, some with
// button markers), the subtitles, the clock, the target picker, the mission manager and its timers, the input
// configurations, the events, the attribute lookups, the relaunch. Compared: the call logs, the HUD's bytes after
// the call, and the globals the package writes (the fade, the launch page's flags, cheats and end state, the
// scheduler's time scale, the sniper zoom's flag, and DrawHealth's, DrawFlyByWire's and dbMessage's statics - the
// game's for the original, ours for the port).
//
// Not run here: the constructor and destructor (they build the gallery's puppets and the views; every boot runs
// them before the first tick).
//
// A mutation the test sees: DrawLetterBox clamping at > kLetterBoxSteps instead of >= (cases put the bars at 29
// while opening, so the mode and the HUD's bytes differ), or DrawHealth's health colour chosen at >= 0.5 (cases
// put the health at exactly a half: the SpriteDraw2D colours differ).
//
// One summary line: [gamefrontendshadowa] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gamefrontendshadowa] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x6d2b79f5;
uint32_t Random() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
uint32_t Random(uint32_t n) {
    return n == 0 ? 0 : Random() % n;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random() & 0xffff) * (1.0f / 65536.0f);
}
template <class T, size_t N>
T Pick(const T (&values)[N]) {
    return values[Random(N)];
}

uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

void Append(std::string &log, const char *format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log += line;
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

// The package's entries, swapped back to the originals for the original's run
const unsigned kEntries[] = {
    0x000d7c10, 0x000d7c20, 0x000d7c30, 0x000d7c60, 0x000d7c70, 0x000d7cd0, 0x000d7d40, 0x000d7d50, 0x000d7d60,
    0x000d7d90, 0x000d7da0, 0x000d8170, 0x000d81d0, 0x000d87a0, 0x000d87f0, 0x000d8800, 0x000d8810, 0x000d8820,
    0x000d8970, 0x000d8ae0, 0x000d8c00, 0x000d8ea0, 0x000d9040, 0x000d9050, 0x000d90e0, 0x000d9140, 0x000d9370,
    0x000d93d0, 0x000d9570, 0x000d9740, 0x000d98d0, 0x000d9970, 0x000d9a50, 0x000d9ad0, 0x000d9b30, 0x000d9b70,
    0x000d9b90, 0x000d9ba0, 0x000d9bd0, 0x000d9bf0, 0x000d9c30, 0x000d9c50, 0x000d9e30, 0x000d9f50, 0x000d9fb0,
    0x000da030, 0x000da0a0, 0x000da180, 0x000da1d0, 0x000da260, 0x000da280, 0x000da2e0, 0x000da540, 0x000da8e0,
    0x000dc4a0, 0x000dc590, 0x000dc7d0, 0x000dc990, 0x000dcae0, 0x000dcc10,
};

struct Originals {
    Originals() {
        for (unsigned at : kEntries)
            XbeOriginal_Restore(at, true);
    }
    ~Originals() {
        for (unsigned at : kEntries)
            XbeOriginal_Restore(at, false);
    }
};

// ---- hooks: a jump over an entry, and over our port's entry behind it

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[128];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// ---- the state a case sets, and the fakes' answers

#define GameGruntIndex I32_AT(0x0023f564)
#define GameFlyByWireFrame I32_AT(0x0023f568)
#define GameDebugMessage ((char *)0x0023f414)
#define GameDebugMessageEnd I32_AT(0x0023f454)
#define GameScreenFade I16_AT(0x00243a74)
#define GameScreenFadeLevel FLOAT_AT(0x00243a70)
#define GameLoadState I32_AT(0x0023f574)
#define ShLaunch (*(LaunchPage *)0x00243b90)
#define ShScheduler (*(Scheduler **)0x001e520c)
#define ShMission (*(SMissionManager **)0x00239220)

struct Fakes {
    int32_t tick;
    int32_t tickStep;
    int language;
    bool subtitlesApp;
    bool subtitlesShown;
    int32_t timerValue;
    bool timerReverse;
    bool programmerEvent;
    bool skipping;
    int numConfigs;
    int currentConfig;
    bool inverted;
    int lookupInt;
    bool lookupString;
    bool missionEnded;
    ScreenPos target;
    int objectiveStates[8];
};

struct State {
    uint8_t hud[sizeof(GHud)];
    uint8_t gallery[sizeof(GGallery)];
    uint8_t launch[sizeof(LaunchPage)];
    float health[4];
    uint32_t objectiveCount;
    int32_t secondary;
    WeaponRecord weapons[kWeaponCount];
    uint8_t widescreen;
    int32_t videoMode;
    uint32_t frameCount;
    int16_t screenFade;
    float screenFadeLevel;
    int32_t loadDots;
    float timeScale;
    uint8_t sniperActive;
    int32_t grunt;
    int32_t flyByWire;
    char debugMessage[33];
    int32_t debugEnd;
    Fakes fakes;
};

State g_base, g_case;
alignas(16) uint8_t g_hudBuffer[sizeof(GHud)];
GHud *const g_hud = reinterpret_cast<GHud *>(g_hudBuffer);
GHud *g_live;
Fakes g_fakes;
std::string *g_log;
int32_t g_eventBuffer[16];
WTargetable g_target;
MissionObjective g_objectives[8];

void Take(State &s) {
    memcpy(s.hud, g_live, sizeof(GHud));
    memcpy(s.gallery, g_live->gallery, sizeof(GGallery));
    memcpy(s.launch, &ShLaunch, sizeof(LaunchPage));
    s.health[0] = ShMission->playerHealth;
    s.health[1] = ShMission->playerMaxHealth;
    s.health[2] = ShMission->playerShield;
    s.health[3] = ShMission->playerMaxShield;
    s.objectiveCount = ShMission->objectives.size;
    s.secondary = WeaponManager->secondary;
    memcpy(s.weapons, WeaponManager->weapons, sizeof(s.weapons));
    s.widescreen = fgRenderer->widescreen;
    s.videoMode = fgRenderer->videoMode;
    s.frameCount = fgRenderer->frameCount;
    s.screenFade = ScreenFade;
    s.screenFadeLevel = GameScreenFadeLevel;
    s.loadDots = LoadState;
    s.timeScale = ShScheduler->timeScale;
    s.sniperActive = SniperZoom != NULL ? SniperZoom->active : 0;
    s.grunt = GameGruntIndex;
    s.flyByWire = GameFlyByWireFrame;
    memcpy(s.debugMessage, GameDebugMessage, sizeof(s.debugMessage));
    s.debugEnd = GameDebugMessageEnd;
    memset(&s.fakes, 0, sizeof(s.fakes));
}

// Puts a state on the copy and the live game; the package's statics into both the game's and ours
void Apply(const State &s) {
    memcpy(g_hudBuffer, s.hud, sizeof(GHud));
    memcpy(g_live->gallery, s.gallery, sizeof(GGallery));
    memcpy(&ShLaunch, s.launch, sizeof(LaunchPage));
    ShMission->playerHealth = s.health[0];
    ShMission->playerMaxHealth = s.health[1];
    ShMission->playerShield = s.health[2];
    ShMission->playerMaxShield = s.health[3];
    ShMission->objectives.size = s.objectiveCount;
    WeaponManager->secondary = s.secondary;
    memcpy(WeaponManager->weapons, s.weapons, sizeof(s.weapons));
    fgRenderer->widescreen = s.widescreen;
    fgRenderer->videoMode = s.videoMode;
    fgRenderer->frameCount = s.frameCount;
    GameScreenFade = s.screenFade;
    ScreenFade = s.screenFade;
    GameScreenFadeLevel = s.screenFadeLevel;
    ScreenFadeLevel = s.screenFadeLevel;
    GameLoadState = s.loadDots;
    LoadState = s.loadDots;
    ShScheduler->timeScale = s.timeScale;
    if (SniperZoom != NULL)
        SniperZoom->active = s.sniperActive;
    GameGruntIndex = s.grunt;
    HudGruntIndex = s.grunt;
    GameFlyByWireFrame = s.flyByWire;
    HudFlyByWireFrame = s.flyByWire;
    memcpy(GameDebugMessage, s.debugMessage, sizeof(s.debugMessage));
    memcpy(HudDebugMessage, s.debugMessage, sizeof(s.debugMessage));
    GameDebugMessageEnd = s.debugEnd;
    HudDebugMessageEndStep = s.debugEnd;
    g_fakes = s.fakes;
    g_target.screenPos = s.fakes.target;
    for (int i = 0; i < 8; i++)
        g_objectives[i].state = s.fakes.objectiveStates[i];
}

// What a run leaves: the HUD's bytes (an allocated ATargeting as a flag), the gallery's state, the globals
void AppendResult(std::string &log, bool original) {
    GHud copy;
    memcpy(&copy, g_hudBuffer, sizeof(GHud));
    const ATargeting *base = reinterpret_cast<const GHud *>(g_case.hud)->targeting;
    if (copy.targeting != base) {
        Append(log, " newTargeting(%d)", copy.targeting != NULL);
        if (copy.targeting != NULL)
            OperatorDelete(copy.targeting);
        copy.targeting = NULL;
        g_hud->targeting = NULL;
    }
    log += " hud:";
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&copy);
    for (size_t i = 0; i < sizeof(GHud); i++)
        Append(log, "%02x", bytes[i]);
    GGallery *gallery = g_live->gallery;
    Append(log, " gallery %08x %08x %08x %08x", gallery->colour, Bits(gallery->scale.x), Bits(gallery->scale.y),
           Bits(gallery->scale.z));
    Append(log, " fade %d %08x launch %08x %08x %d scale %08x sniper %d", original ? GameScreenFade : ScreenFade,
           Bits(original ? GameScreenFadeLevel : ScreenFadeLevel),
           ShLaunch.flags, ShLaunch.cheats, ShLaunch.unknown978, Bits(ShScheduler->timeScale),
           SniperZoom != NULL ? SniperZoom->active : -1);
    if (original)
        Append(log, " statics %d %d %s %d", GameGruntIndex, GameFlyByWireFrame, GameDebugMessage, GameDebugMessageEnd);
    else
        Append(log, " statics %d %d %s %d", HudGruntIndex, HudFlyByWireFrame, HudDebugMessage, HudDebugMessageEndStep);
}

template <class Call>
void RunCase(const char *what, const Call &call) {
    std::string results[2];
    for (int side = 0; side < 2; side++) {
        bool original = side == 0;
        Apply(g_case);
        std::string &log = results[side];
        g_log = &log;
        bool ok;
        if (original) {
            Originals window;
            ok = Guarded([&] { call(true); });
        } else {
            ok = Guarded([&] { call(false); });
        }
        g_log = NULL;
        if (!ok)
            log += "<fault>";
        AppendResult(log, original);
    }
    g_cases++;
    Check(results[0] == results[1], what, g_cases);
}

#define LOG(...) do { if (g_log != NULL) Append(*g_log, __VA_ARGS__); } while (0)

// ---- the fakes

const char *const kSymbols[] = {
    "OBJECTIVE COMPLETE", "Press *A* to continue", "A",
    "A MESSAGE LONG ENOUGH TO PASS THE THIRTY-TWO CHARACTER MARK",
    "Hold *G* and *P*", "", "Plug a controller into port *D*", "Weapon",
};
const char *__cdecl FakeLocaleSymbol(int symbol) {
    LOG("sym(%d) ", symbol);
    return kSymbols[uint32_t(symbol * 2654435761u) >> 29];
}
float FakeWidthOf(const char *text) {
    return float(strlen(text)) * 9.25f + 0.375f;
}
float __fastcall FakeFontWidth(GGallery *, int, const char *text) {
    LOG("width(%s) ", text);
    return FakeWidthOf(text);
}
float __fastcall FakeFontHeight(GGallery *, int, const char *text) {
    LOG("height(%s) ", text);
    return 13.5f + float(strlen(text) & 3);
}
void __fastcall FakeFontSetState(GGallery *, int, const GalleryKey *puppet) {
    LOG("fontState(%p) ", (const void *)puppet);
}
void __fastcall FakeFontSetScale(GGallery *, int, float x, float y) {
    LOG("fontScale(%08x,%08x) ", Bits(x), Bits(y));
}
void __fastcall FakeFontDrawBlock(GGallery *gallery, int, const void *block, int unknown) {
    LOG("fontBlock(%p,%d,%08x) ", block, unknown, gallery->colour);
}
void __fastcall FakeFontDraw4(GGallery *gallery, int, const char *text, float x, float y, uint32_t colour) {
    LOG("font4(%s,%08x,%08x,%08x,%08x) ", text, Bits(x), Bits(y), colour, gallery->colour);
}
void __fastcall FakeFontDraw3(GGallery *gallery, int, const char *text, float x, float y) {
    LOG("font3(%s,%08x,%08x,%08x) ", text, Bits(x), Bits(y), gallery->colour);
}
void __fastcall FakeSpriteSetState(GGallery *, int, const GalleryKey *puppet) {
    LOG("spriteState(%p) ", (const void *)puppet);
}
void __fastcall FakeSpriteDraw(GGallery *gallery, int, int x, int y, const GalleryKey *puppet,
                               const float *rectangle) {
    LOG("sprite(%d,%d,%p,%p,%08x,%08x,%08x,%08x) ", x, y, (const void *)puppet, (const void *)rectangle,
        gallery->colour, Bits(gallery->scale.x), Bits(gallery->scale.y), Bits(gallery->scale.z));
}
void __fastcall FakeSpriteDrawAt(GGallery *gallery, int, const GalleryKey *puppet) {
    LOG("spriteAt(%p,%08x,%08x,%08x) ", (const void *)puppet, gallery->colour, Bits(gallery->scale.x),
        Bits(gallery->scale.y));
}
void __fastcall FakeBitmapDraw(GGallery *gallery, int, const GalleryKey *puppet) {
    LOG("bitmap(%p,%08x) ", (const void *)puppet, gallery->colour);
}
void __fastcall FakeQuadSetState(GGallery *, int, const GalleryKey *puppet) {
    LOG("quadState(%p) ", (const void *)puppet);
}
void __fastcall FakeQuadDraw(GGallery *, int, const GalleryKey *puppet, float alpha) {
    LOG("quad(%p,%08x) ", (const void *)puppet, Bits(alpha));
}
void __fastcall FakeQuadDraw2D(GGallery *, int, float x, float y, float w, float h, uint32_t colour) {
    LOG("quad2d(%08x,%08x,%08x,%08x,%08x) ", Bits(x), Bits(y), Bits(w), Bits(h), colour);
}
void __fastcall FakeLineStrip(GGallery *, int, const float *points, const uint32_t *colours, int count) {
    LOG("strip(%d", count);
    for (int i = 0; i < count; i++)
        LOG(" %08x,%08x,%08x,%08x/%08x", Bits(points[i * 4]), Bits(points[i * 4 + 1]), Bits(points[i * 4 + 2]),
            Bits(points[i * 4 + 3]), colours[i]);
    LOG(") ");
}
void __cdecl FakeDrawBox(float x, float y, float w, float h, uint32_t colour) {
    LOG("box(%08x,%08x,%08x,%08x,%08x) ", Bits(x), Bits(y), Bits(w), Bits(h), colour);
}
void __cdecl FakeDrawSprite(float x, float y, float w, float h, uint32_t colour, const float *coords, void *tar) {
    LOG("drawSprite(%08x,%08x,%08x,%08x,%08x,%p", Bits(x), Bits(y), Bits(w), Bits(h), colour, tar);
    for (int i = 0; i < 8; i++)
        LOG(",%08x", Bits(coords[i]));
    LOG(") ");
}
void __cdecl FakeSetModelMatrix(const float *matrix) {
    LOG("matrix(");
    if (matrix == NULL)
        LOG("null");
    else
        for (int i = 0; i < 16; i++)
            LOG("%08x,", Bits(matrix[i]));
    LOG(") ");
}
bool __fastcall FakeBlendMode(void *material, int, uint32_t mode) {
    LOG("blend(%p,%u) ", material, mode);
    return true;
}
bool __fastcall FakeDepthTest(void *material, int, uint32_t method) {
    LOG("depth(%p,%x) ", material, method);
    return true;
}
void __cdecl FakeMenuSound(int bank, const char *patch, const char *mix, int view) {
    LOG("sound(%d,%s,%s,%d) ", bank, patch, mix, view);
}
int g_fakeSubtitles;
void *__cdecl FakeSubtitlesApp() {
    LOG("subtitlesApp ");
    return g_fakes.subtitlesApp ? &g_fakeSubtitles : NULL;
}
bool __fastcall FakeSubtitlesShown(void *, int) {
    LOG("subtitlesShown ");
    return g_fakes.subtitlesShown;
}
void __fastcall FakeSubtitlesClear(void *, int) {
    LOG("subtitlesClear ");
}
int __cdecl FakeTick() {
    LOG("tick ");
    int tick = g_fakes.tick;
    g_fakes.tick += g_fakes.tickStep;
    return tick;
}
WTargetable *__fastcall FakeSelectedTarget(void *, int) {
    LOG("selected ");
    return &g_target;
}
int __fastcall FakeTimerValue(SMissionTimer *timer, int) {
    int index = int(timer - ShMission->timers);
    LOG("timerValue(%d) ", index);
    return g_fakes.timerValue + index * 7;
}
bool __fastcall FakeTimerReverse(SMissionTimer *timer, int) {
    LOG("timerReverse(%d) ", int(timer - ShMission->timers));
    return g_fakes.timerReverse;
}
bool __fastcall FakeProgrammerEvent(void *, int, int event) {
    LOG("event(%d) ", event);
    return g_fakes.programmerEvent;
}
bool __fastcall FakeGoingToSkip(void *, int) {
    LOG("goingToSkip ");
    return g_fakes.skipping;
}
MissionObjective *__fastcall FakeObjectiveByIndex(void *, int, int index) {
    LOG("objective(%d) ", index);
    return index >= 0 && index < 8 ? &g_objectives[index] : NULL;
}
void __fastcall FakeRestoreLives(void *, int) {
    LOG("restoreLives ");
}
void __fastcall FakeShieldTimes(void *, int, int times) {
    LOG("shieldTimes(%d) ", times);
}
void __fastcall FakeTargetingState(void *, int, int state) {
    LOG("targetingState(%d) ", state);
}
void *__fastcall FakeTargetingConstruct(void *self, int) {
    LOG("targetingConstruct ");
    memset(self, 0, 0xc);
    return self;
}
void *__cdecl FakeEventNew(size_t size) {
    LOG("eventNew(%u) ", unsigned(size));
    return g_eventBuffer;
}
void *__fastcall FakePause(void *self, int) {
    LOG("EPause(%d) ", self == g_eventBuffer);
    return self;
}
void *__fastcall FakeResume(void *self, int) {
    LOG("EResume(%d) ", self == g_eventBuffer);
    return self;
}
int __fastcall FakeCurrentConfig(void *, int, int type) {
    LOG("currentConfig(%d) ", type);
    return g_fakes.currentConfig;
}
int __fastcall FakeNumConfigs(void *, int, int type) {
    LOG("numConfigs(%d) ", type);
    return g_fakes.numConfigs;
}
bool __fastcall FakeIsInverted(void *, int) {
    LOG("isInverted ");
    return g_fakes.inverted;
}
void __fastcall FakeSetInverted(void *, int, bool inverted) {
    LOG("setInverted(%d) ", inverted);
}
void __fastcall FakeSetConfig(void *, int, int type, int config) {
    LOG("setConfig(%d,%d) ", type, config);
}
int32_t __fastcall FakeLookupInt(void *, int, const char *key, bool *) {
    LOG("lookupInt(%s) ", key);
    return g_fakes.lookupInt;
}
const char *__fastcall FakeLookupString(void *, int, const char *key, bool *) {
    LOG("lookupString(%s) ", key);
    return g_fakes.lookupString ? "chained_mission" : NULL;
}
void __cdecl FakeExitToDriving(int mission, const char *name, bool clear) {
    LOG("exitToDriving(%d,%s,%d) ", mission, name, clear);
}
bool __fastcall FakeMissionEnded(void *, int) {
    LOG("missionEnded ");
    return g_fakes.missionEnded;
}
int __cdecl FakeLanguage() {
    LOG("language ");
    return g_fakes.language;
}
void __cdecl FakeKillGirl() {
    LOG("killGirl ");
}
void __fastcall FakeFlush(void *, int) {
    LOG("flush ");
}

void InstallFakes() {
    HookInstall(0x000e3af0, (const void *)&FakeLocaleSymbol);
    HookInstall(0x000d5ad0, (const void *)&FakeFontWidth);
    HookInstall(0x000d5b00, (const void *)&FakeFontHeight);
    HookInstall(0x000d6eb0, (const void *)&FakeFontSetState);
    HookInstall(0x000d61e0, (const void *)&FakeFontSetScale);
    HookInstall(0x000d6200, (const void *)&FakeFontDrawBlock);
    HookInstall(0x000d63a0, (const void *)&FakeFontDraw4);
    HookInstall(0x000d6520, (const void *)&FakeFontDraw3);
    HookInstall(0x000d5a00, (const void *)&FakeSpriteSetState);
    HookInstall(0x000d69a0, (const void *)&FakeSpriteDraw);
    HookInstall(0x000d6d70, (const void *)&FakeSpriteDrawAt);
    HookInstall(0x000d5f50, (const void *)&FakeBitmapDraw);
    HookInstall(0x000d5b30, (const void *)&FakeQuadSetState);
    HookInstall(0x000d68b0, (const void *)&FakeQuadDraw);
    HookInstall(0x000d68c0, (const void *)&FakeQuadDraw2D);
    HookInstall(0x000d66a0, (const void *)&FakeLineStrip);
    HookInstall(0x000760e0, (const void *)&FakeDrawBox);
    HookInstall(0x00075f60, (const void *)&FakeDrawSprite);
    HookInstall(0x00075f30, (const void *)&FakeSetModelMatrix);
    HookInstall(0x000eecf0, (const void *)&FakeBlendMode);
    HookInstall(0x000eecd0, (const void *)&FakeDepthTest);
    HookInstall(0x0011e010, (const void *)&FakeMenuSound);
    HookInstall(0x000e3180, (const void *)&FakeSubtitlesApp);
    HookInstall(0x000e3190, (const void *)&FakeSubtitlesShown);
    HookInstall(0x000e3630, (const void *)&FakeSubtitlesClear);
    HookInstall(0x0010a6c0, (const void *)&FakeTick);
    HookInstall(0x000cdfb0, (const void *)&FakeSelectedTarget);
    HookInstall(0x000b9630, (const void *)&FakeTimerValue);
    HookInstall(0x000b96a0, (const void *)&FakeTimerReverse);
    HookInstall(0x000b7300, (const void *)&FakeProgrammerEvent);
    HookInstall(0x000b6590, (const void *)&FakeGoingToSkip);
    HookInstall(0x000b8460, (const void *)&FakeObjectiveByIndex);
    HookInstall(0x000b63c0, (const void *)&FakeRestoreLives);
    HookInstall(0x000b6690, (const void *)&FakeShieldTimes);
    HookInstall(0x0012e120, (const void *)&FakeTargetingState);
    HookInstall(0x0012e0f0, (const void *)&FakeTargetingConstruct);
    HookInstall(0x0005a5a0, (const void *)&FakeEventNew);
    HookInstall(0x00042050, (const void *)&FakePause);
    HookInstall(0x00043490, (const void *)&FakeResume);
    HookInstall(0x00050180, (const void *)&FakeCurrentConfig);
    HookInstall(0x00050170, (const void *)&FakeNumConfigs);
    HookInstall(0x00050160, (const void *)&FakeIsInverted);
    HookInstall(0x00050140, (const void *)&FakeSetInverted);
    HookInstall(0x00050720, (const void *)&FakeSetConfig);
    HookInstall(0x00058180, (const void *)&FakeLookupInt);
    HookInstall(0x000583a0, (const void *)&FakeLookupString);
    HookInstall(0x00059bf0, (const void *)&FakeExitToDriving);
    HookInstall(0x000d7bd0, (const void *)&FakeMissionEnded);
    HookInstall(0x000e30d0, (const void *)&FakeLanguage);
    HookInstall(0x000d6f20, (const void *)&FakeKillGirl);
    HookInstall(0x0004f280, (const void *)&FakeFlush);
}

// ---- the originals

#define O_SetLevelLoadText ((void (__fastcall *)(GHud *, int))0x000d7c20)
#define O_EnableLavaDamageEffect ((void (__fastcall *)(GHud *, int, bool))0x000d7c30)
#define O_LavaDamageEffectEnabled ((bool (__fastcall *)(GHud *, int))0x000d7c60)
#define O_FadeInFromBlack ((void (__fastcall *)(GHud *, int, int))0x000d7c70)
#define O_FadeOutToBlack ((void (__fastcall *)(GHud *, int, int))0x000d7cd0)
#define O_GetBlackFadeAnimating ((bool (__fastcall *)(GHud *, int))0x000d7d40)
#define O_GetBlackFadePct ((float (__fastcall *)(GHud *, int))0x000d7d50)
#define O_Stylin ((void (__fastcall *)(GHud *, int))0x000d7d60)
#define O_RadarRadiusInMeters ((float (__fastcall *)(GHud *, int))0x000d7d90)
#define O_Reset ((void (__fastcall *)(GHud *, int))0x000d7da0)
#define O_IsTargetPointOnScreen ((bool (__fastcall *)(GHud *, int, const ScreenPos *))0x000d8170)
#define O_DrawHealth ((void (__fastcall *)(GHud *, int))0x000d81d0)
#define O_TriggerDamageFlash ((void (__fastcall *)(GHud *, int, float))0x000d87a0)
#define O_TriggerBloodCurtain ((void (__fastcall *)(GHud *, int))0x000d87f0)
#define O_TriggerTimerOn ((void (__fastcall *)(GHud *, int, int))0x000d8800)
#define O_TriggerTimerOff ((void (__fastcall *)(GHud *, int, int))0x000d8810)
#define O_DrawScreenHits ((void (__fastcall *)(GHud *, int))0x000d8820)
#define O_DrawScreenFades ((void (__fastcall *)(GHud *, int))0x000d8970)
#define O_DrawFlyByWire ((void (__fastcall *)(GHud *, int))0x000d8ae0)
#define O_DrawZoomingCrossHairs ((uint32_t (__fastcall *)(GHud *, int, GalleryKey *))0x000d8c00)
#define O_DrawFadingCrossHairs ((void (__fastcall *)(GHud *, int, GalleryKey *))0x000d8ea0)
#define O_SetAutoDrive ((void (__fastcall *)(GHud *, int, bool))0x000d9040)
#define O_PauseOff ((void (__fastcall *)(GHud *, int))0x000d9050)
#define O_CheckCheatInput ((bool (__fastcall *)(GHud *, int, const int32_t *))0x000d90e0)
#define O_CheatMsgHandler ((void (__fastcall *)(GHud *, int, int))0x000d9140)
#define O_GetCenter ((double (__fastcall *)(GHud *, int, GalleryKey *, float, const char *))0x000d9370)
#define O_DrawPauseErrors ((void (__fastcall *)(GHud *, int))0x000d93d0)
#define O_DrawBlinkingArrow ((void (__fastcall *)(GHud *, int, float, float))0x000d9570)
#define O_WordWrap ((int (*)(const char *, unsigned, char ***))0x000d9740)
#define O_DrawFontAndShadow ((void (__fastcall *)(GHud *, int, GalleryKey *, float, float, uint32_t, uint32_t, float, const char *))0x000d98d0)
#define O_DrawFontCentered ((void (__fastcall *)(GHud *, int, GalleryKey *, float, float, float, float, float, uint32_t, unsigned, char **))0x000d9970)
#define O_DrawFontLines ((void (__fastcall *)(GHud *, int, GalleryKey *, float, float, uint32_t, float, char **, int))0x000d9a50)
#define O_DrawFont ((void (__fastcall *)(GHud *, int, GalleryKey *, float, float, uint32_t, float, const char *))0x000d9ad0)
#define O_LetterBoxOn ((void (__fastcall *)(GHud *, int, bool))0x000d9b30)
#define O_LetterBoxOff ((void (__fastcall *)(GHud *, int, bool))0x000d9b70)
#define O_GetLetterBoxMode ((int (__fastcall *)(GHud *, int))0x000d9b90)
#define O_AimOn ((void (__fastcall *)(GHud *, int))0x000d9ba0)
#define O_AimOff ((void (__fastcall *)(GHud *, int))0x000d9bd0)
#define O_GadgetOn ((void (__fastcall *)(GHud *, int, int, float))0x000d9bf0)
#define O_GadgetOff ((void (__fastcall *)(GHud *, int))0x000d9c30)
#define O_DrawGadget ((void (__fastcall *)(GHud *, int))0x000d9c50)
#define O_DrawLetterBox ((void (__fastcall *)(GHud *, int))0x000d9e30)
#define O_SetTarget ((void (__fastcall *)(GHud *, int, const ScreenPos *))0x000d9f50)
#define O_SetTargetLockState ((void (__fastcall *)(GHud *, int, int))0x000d9fb0)
#define O_TriggerMissionMsg ((void (__fastcall *)(GHud *, int, int, float))0x000da030)
#define O_TriggerObjectiveMsg ((void (__fastcall *)(GHud *, int, int, int, bool))0x000da0a0)
#define O_TriggerButtonMsg ((void (__fastcall *)(GHud *, int, int, int, int, int, float))0x000da180)
#define O_GetButton ((int (__fastcall *)(GHud *, int, const char *, char *, char *))0x000da1d0)
#define O_KillButtonMessage ((void (__fastcall *)(GHud *, int))0x000da260)
#define O_dbMessage ((void (*)(const char *, int))0x000da280)
#define O_DrawMessage ((void (__fastcall *)(GHud *, int))0x000da2e0)
#define O_DrawStylin ((void (__fastcall *)(GHud *, int))0x000da540)
#define O_dbDrawTarget ((void (__fastcall *)(GHud *, int, const ScreenPos *, const char *, float, uint32_t, const char *))0x000da8e0)
#define O_GetScreenCoords ((void (__fastcall *)(GHud *, int, const Coord3 *, bool *, ScreenPos *))0x000dc4a0)
#define O_DrawBloodCurtain ((void (__fastcall *)(GHud *, int))0x000dc590)
#define O_DrawTimer ((void (__fastcall *)(GHud *, int))0x000dc7d0)
#define O_DrawWeapon ((void (__fastcall *)(GHud *, int))0x000dc990)
#define O_PauseOn ((void (__fastcall *)(GHud *, int))0x000dcae0)
#define O_PauseMenuMsgHandler ((void (__fastcall *)(GHud *, int, int))0x000dcc10)

// ---- the perturbations

const float kTimes[] = {0.0f, -0.0f, 0.25f, 0.5f, 1.0f, 3.0f, -0.125f, 0.0333f, 2.9f, 8.0f};

float Time() {
    return Random(3) == 0 ? Pick(kTimes) : Uniform(-0.5f, 4.0f);
}

void PerturbHud(GHud &h) {
    h.paused = Random(4) == 0;
    h.autoDrive = Random(2) != 0;
    h.superEasy = Random(5) == 0;
    h.unknown431 = Random(4) == 0;
    h.letterBoxMode = int32_t(Random(4));
    h.letterBoxSize = Random(3) == 0 ? int32_t(Random(2)) * 29 + 1 : int32_t(Random(33)) - 1;
    h.arrowAlpha = Random(3) == 0 ? Pick(kTimes) : Uniform(-0.2f, 1.2f);
    h.arrowTick = float(Random(100000));
    h.arrowTime = Uniform(0.0f, 1.5f);
    h.arrowState = int32_t(Random(3));
    h.timerLastSecond = int32_t(Random(12));
    h.healthShowTime = Time();
    h.healthFadeTime = Time();
    h.healthBlinkTime = Time();
    h.stylinTime = Time();
    h.stylinState = int32_t(Random(3));
    h.stylinScale = Uniform(0.0f, 1.2f);
    h.stylinFade = Time();
    h.buttonMsgTime = Time();
    h.objectiveMsgTime = Time();
    h.objectiveMsgId = int32_t(Random(4));
    h.objectiveMsgKind = int32_t(Random(4));
    h.missionResultText = Random(3) == 0 ? kTextMissionFailed : Random(2) == 0 ? kTextMissionPassed : -1;
    h.missionResultState = Random(3) == 0 ? int32_t(Random(6)) : 0;
    h.damageFlash = int32_t(Random(8)) - 1;
    h.damageFlashLength = int32_t(Random(24)) - 1;
    h.lavaDamageStep = int32_t(Random(2));
    h.blackFadePct = Uniform(-0.2f, 1.2f);
    h.blackFadeAnimating = Random(2) != 0;
    h.blackFadeStartStep = int32_t(Random(2000));
    h.blackFadeStart = Random(2) ? 1.0f : 0.0f;
    h.blackFadeRate = Uniform(-0.05f, 0.05f);
    h.steps = int32_t(Random(4));
    h.elapsed = Random(3) == 0 ? 0.0333333351f : Uniform(0.0f, 0.2f);
    h.shownHealth = Uniform(0.0f, 1.0f);
    h.shownShield = Uniform(0.0f, 1.0f);
    h.targetLockState = int32_t(Random(3));
    h.lockFrames = int32_t(Random(9));
    h.lockFramesShown = int32_t(Random(9));
    h.lockX = Uniform(0.0f, 640.0f);
    h.lockY = Uniform(0.0f, 480.0f);
    h.missionMsg = int32_t(Random(64));
    h.missionMsgTime = Time();
    h.missionMsgFade = Time();
    h.missionMsgLong = Random(2) != 0;
    h.gadget = int32_t(Random(9)) - 1;
    h.gadgetTime = Time();
    h.gadgetGrow = Uniform(-0.1f, 0.5f);
    h.gadgetPulse = Uniform(-0.1f, 0.5f);
    h.gadgetPulses = int32_t(Random(4)) - 1;
    h.bloodCurtainTime = Random(4) == 0 ? -1.0f : Uniform(-0.1f, 8.0f);
    h.timer = int32_t(Random(11)) - 1;
    h.targetScaleX = Random(2) ? 1.0f : Uniform(0.5f, 1.5f);
    h.targetScaleY = Random(2) ? 1.0f : Uniform(0.5f, 1.5f);
    h.targetOffsetX = Random(2) ? 0.0f : Uniform(-30.0f, 30.0f);
    h.targetOffsetY = Random(2) ? 0.0f : Uniform(-30.0f, 30.0f);
    h.pausePage = Random(5) == 0 ? kPauseFailed : int32_t(Random(4));
    h.pauseSelection = int32_t(Random(4));
    h.pauseConfirm = int32_t(Random(3));
    h.pauseObjective = int32_t(Random(9));
    h.pauseObjectivesBrowse = int32_t(Random(2));
    h.pauseConfig = int32_t(Random(5)) - 1;
    for (int i = 0; i < 16; i++)
        h.cheatInput[i] = kCheatDpadLeft + int32_t(Random(20));
    h.targeting = NULL;
    static const float kFrameRates[] = {25.0f, 30.0f, 60.0f};
    h.frameRate = Pick(kFrameRates);
}

void PerturbState(State &s) {
    PerturbHud(*reinterpret_cast<GHud *>(s.hud));
    const float healths[] = {0.0f, 0.25f, 0.5f, 1.0f, 0.75f, 0.125f};
    float maxHealth = Random(2) ? 3000.0f : 1000.0f;
    s.health[1] = maxHealth;
    s.health[0] = Random(3) == 0 ? Pick(healths) * maxHealth : Uniform(-50.0f, maxHealth);
    s.health[3] = 1000.0f;
    s.health[2] = Random(3) == 0 ? Pick(healths) * 1000.0f : Uniform(0.0f, 1000.0f);
    s.objectiveCount = Random(7);
    s.secondary = int32_t(Random(kWeaponCount));
    WeaponRecord &weapon = s.weapons[s.secondary];
    static const int32_t kIds[] = {kWeaponGun, kWeaponMissile, kWeaponTorpedo, kWeaponUltralightRocket,
                                   kWeaponFodRocket, kWeaponTimeMine};
    static const int32_t kAmmo[] = {-1, 0, 1, 12, 250};
    weapon.id = Pick(kIds);
    weapon.textId = Random(3) == 0 ? 0x64b : int32_t(Random(100));
    weapon.ammo = Pick(kAmmo);
    s.widescreen = Random(2) != 0;
    s.videoMode = int32_t(Random(3));
    s.frameCount = Random(5);
    s.loadDots = Random(5) == 0;
    LaunchPage &launch = *reinterpret_cast<LaunchPage *>(s.launch);
    launch.flags = Random() & 0x31ff;
    launch.cheats = Random(4);
    launch.controllerPort = int32_t(Random(6));
    s.grunt = int32_t(Random(3));
    s.flyByWire = int32_t(Random(32));
    Fakes &f = s.fakes;
    f.tick = int32_t(Random(100000));
    f.tickStep = int32_t(Random(40));
    f.language = int32_t(Random(8));
    f.subtitlesApp = Random(4) != 0;
    f.subtitlesShown = Random(4) == 0;
    f.timerValue = int32_t(Random(3000)) - 100;
    f.timerReverse = Random(2) != 0;
    f.programmerEvent = Random(3) == 0;
    f.skipping = Random(2) != 0;
    f.numConfigs = int32_t(Random(4));
    f.currentConfig = int32_t(Random(3));
    f.inverted = Random(2) != 0;
    f.lookupInt = int32_t(Random(9));
    f.lookupString = Random(2) != 0;
    f.missionEnded = Random(2) != 0;
    f.target.x = Uniform(-20.0f, 660.0f);
    f.target.y = Uniform(-20.0f, 500.0f);
    for (int i = 0; i < 8; i++)
        f.objectiveStates[i] = int32_t(Random(4));
}

// ---- the cases

void TestState() {
    for (int i = 0; i < 200; i++) {
        g_case = g_base;
        PerturbState(g_case);
        int which = i % 26;
        int a = int32_t(Random(40)) - 5;
        float t = Time() * 10.0f;
        bool flag = Random(2) != 0;
        ScreenPos point = {Uniform(-20.0f, 660.0f), Uniform(-20.0f, 500.0f)};
        static const char *const kMissions[] = {"junglea_mis13a", "SNOW2A_MIS4", "uw_mis11", "paris"};
        const char *mission = Pick(kMissions);
        int32_t code[17];
        code[0] = Random(2) ? 0 : 1 + (a & 15);
        const GHud &caseHud = *reinterpret_cast<const GHud *>(g_case.hud);
        for (int n = 0; n < 16; n++)
            code[1 + n] = Random(3) == 0 ? kCheatDpadLeft : caseHud.cheatInput[n];
        static const char *kNames[] = {
            "SetLevelLoadText", "EnableLavaDamageEffect", "LavaDamageEffectEnabled", "FadeInFromBlack",
            "FadeOutToBlack", "GetBlackFade", "Stylin", "RadarRadiusInMeters", "Reset", "IsTargetPointOnScreen",
            "TriggerDamageFlash", "TriggerBloodCurtain", "TriggerTimer", "SetAutoDrive", "LetterBox",
            "GetLetterBoxMode", "AimOn/Off", "Gadget", "SetTarget", "SetTargetLockState", "TriggerMissionMsg",
            "TriggerObjectiveMsg", "TriggerButtonMsg", "KillButtonMessage", "PauseOn/Off", "CheckCheatInput",
        };
        RunCase(kNames[which], [&](bool o) {
            GHud *h = g_hud;
            switch (which) {
            case 0: o ? O_SetLevelLoadText(h, 0) : h->SetLevelLoadText(); break;
            case 1: o ? O_EnableLavaDamageEffect(h, 0, flag) : h->EnableLavaDamageEffect(flag); break;
            case 2: LOG("%d", o ? O_LavaDamageEffectEnabled(h, 0) : h->LavaDamageEffectEnabled()); break;
            case 3: o ? O_FadeInFromBlack(h, 0, a) : h->FadeInFromBlack(a); break;
            case 4: o ? O_FadeOutToBlack(h, 0, a) : h->FadeOutToBlack(a); break;
            case 5:
                LOG("%d %08x", o ? O_GetBlackFadeAnimating(h, 0) : h->GetBlackFadeAnimating(),
                    Bits(o ? O_GetBlackFadePct(h, 0) : h->GetBlackFadePct()));
                break;
            case 6: o ? O_Stylin(h, 0) : h->Stylin(); break;
            case 7: LOG("%08x", Bits(o ? O_RadarRadiusInMeters(h, 0) : h->RadarRadiusInMeters())); break;
            case 8:
                strcpy(ShLaunch.missionName, mission);
                o ? O_Reset(h, 0) : h->Reset();
                break;
            case 9: LOG("%d", o ? O_IsTargetPointOnScreen(h, 0, &point) : h->IsTargetPointOnScreen(&point)); break;
            case 10: o ? O_TriggerDamageFlash(h, 0, t) : h->TriggerDamageFlash(t); break;
            case 11: o ? O_TriggerBloodCurtain(h, 0) : h->TriggerBloodCurtain(); break;
            case 12:
                if (flag)
                    o ? O_TriggerTimerOn(h, 0, a) : h->TriggerTimerOn(a);
                else
                    o ? O_TriggerTimerOff(h, 0, a) : h->TriggerTimerOff(a);
                break;
            case 13: o ? O_SetAutoDrive(h, 0, flag) : h->SetAutoDrive(flag); break;
            case 14:
                if (a & 1)
                    o ? O_LetterBoxOn(h, 0, flag) : h->LetterBoxOn(flag);
                else
                    o ? O_LetterBoxOff(h, 0, flag) : h->LetterBoxOff(flag);
                break;
            case 15: LOG("%d", o ? O_GetLetterBoxMode(h, 0) : h->GetLetterBoxMode()); break;
            case 16:
                if (flag)
                    o ? O_AimOn(h, 0) : h->AimOn();
                else
                    o ? O_AimOff(h, 0) : h->AimOff();
                break;
            case 17:
                if (flag)
                    o ? O_GadgetOn(h, 0, a, t) : h->GadgetOn(a, t);
                else
                    o ? O_GadgetOff(h, 0) : h->GadgetOff();
                break;
            case 18: o ? O_SetTarget(h, 0, &point) : h->SetTarget(&point); break;
            case 19:
                h->targeting = flag ? reinterpret_cast<ATargeting *>(g_eventBuffer) : NULL;
                o ? O_SetTargetLockState(h, 0, a & 3) : h->SetTargetLockState(a & 3);
                h->targeting = NULL;
                break;
            case 20: o ? O_TriggerMissionMsg(h, 0, a, t) : h->TriggerMissionMsg(a, t); break;
            case 21:
                o ? O_TriggerObjectiveMsg(h, 0, a & 3, (a >> 2) & 3, flag)
                  : h->TriggerObjectiveMsg(a & 3, (a >> 2) & 3, flag);
                break;
            case 22: o ? O_TriggerButtonMsg(h, 0, a, 7, 8, a * 3, t) : h->TriggerButtonMsg(a, 7, 8, a * 3, t); break;
            case 23: o ? O_KillButtonMessage(h, 0) : h->KillButtonMessage(); break;
            case 24:
                if (flag)
                    o ? O_PauseOn(h, 0) : h->PauseOn();
                else
                    o ? O_PauseOff(h, 0) : h->PauseOff();
                break;
            case 25: LOG("%d", o ? O_CheckCheatInput(h, 0, code) : h->CheckCheatInput(code)); break;
            }
        });
    }
}

// The pause menu's actions over every page, selection and question
void TestPauseMenu() {
    for (int i = 0; i < 400; i++) {
        g_case = g_base;
        PerturbState(g_case);
        int action = kFrontEndUp + int32_t(Random(9));
        RunCase("PauseMenuMsgHandler", [&](bool o) {
            o ? O_PauseMenuMsgHandler(g_hud, 0, action) : g_hud->PauseMenuMsgHandler(action);
        });
    }
}

// The cheats: the codes fed whole (and with noise), on and off the track that has two of them
void TestCheats() {
    static const int32_t kCodes[][17] = {
        {14, 97, 89, 90, 91, 92, 93, 94, 89, 90, 91, 92, 93, 94, 98},
        {10, 97, 89, 90, 93, 94, 93, 94, 89, 90, 98},
        {12, 97, 93, 94, 93, 94, 89, 90, 89, 90, 91, 92, 98},
        {12, 97, 89, 90, 93, 94, 91, 92, 89, 90, 91, 92, 98},
        {12, 97, 93, 94, 91, 92, 89, 90, 93, 94, 93, 94, 98},
        {14, 97, 93, 94, 91, 92, 89, 90, 93, 94, 93, 94, 93, 94, 98},
        {16, 97, 93, 94, 91, 92, 89, 90, 93, 94, 93, 94, 93, 94, 93, 94, 98},
        {10, 97, 93, 94, 93, 94, 93, 94, 93, 94, 98},
        {14, 97, 89, 90, 91, 92, 91, 92, 89, 90, 91, 92, 93, 94, 98},
    };
    char *trackName = fgWorld != NULL ? fgWorld->trackName : NULL;
    for (int i = 0; i < 120; i++) {
        g_case = g_base;
        PerturbState(g_case);
        GHud &h = *reinterpret_cast<GHud *>(g_case.hud);
        const int32_t *code = kCodes[i % 9];
        int length = code[0];
        // all of the code but its last action already in, the last fed (sometimes a wrong one)
        for (int n = 0; n < length - 1; n++)
            h.cheatInput[16 - (length - 1) + n] = code[1 + n];
        int action = Random(5) == 0 ? int32_t(Random(120)) : code[length];
        static char snowTrack[] = "snow2a_mis4";
        bool snow = (i / 9) % 2 == 0;
        RunCase("CheatMsgHandler", [&](bool o) {
            if (fgWorld != NULL)
                fgWorld->trackName = snow ? snowTrack : trackName;
            o ? O_CheatMsgHandler(g_hud, 0, action) : g_hud->CheatMsgHandler(action);
            if (fgWorld != NULL)
                fgWorld->trackName = trackName;
        });
    }
}

// The draws
void TestDraws() {
    static const char *kNames[] = {
        "DrawHealth", "DrawScreenHits", "DrawScreenFades", "DrawFlyByWire", "DrawZoomingCrossHairs",
        "DrawFadingCrossHairs", "DrawPauseErrors", "DrawBlinkingArrow", "DrawGadget", "DrawLetterBox",
        "DrawStylin", "DrawBloodCurtain", "DrawTimer", "DrawWeapon", "DrawMessage", "dbDrawTarget",
    };
    for (int i = 0; i < 800; i++) {
        g_case = g_base;
        PerturbState(g_case);
        int which = i % 16;
        float x = Uniform(0.0f, 640.0f);
        float y = Uniform(0.0f, 480.0f);
        GalleryKey *puppet = Random(2) ? g_live->crossTorpNormPuppet : g_live->crossNormPuppet;
        ScreenPos position = {x, y};
        uint32_t colour = Random();
        bool second = Random(2) != 0;
        RunCase(kNames[which], [&](bool o) {
            GHud *h = g_hud;
            switch (which) {
            case 0: o ? O_DrawHealth(h, 0) : h->DrawHealth(); break;
            case 1: o ? O_DrawScreenHits(h, 0) : h->DrawScreenHits(); break;
            case 2: o ? O_DrawScreenFades(h, 0) : h->DrawScreenFades(); break;
            case 3: o ? O_DrawFlyByWire(h, 0) : h->DrawFlyByWire(); break;
            case 4: LOG("%08x", o ? O_DrawZoomingCrossHairs(h, 0, puppet) : h->DrawZoomingCrossHairs(puppet)); break;
            case 5: o ? O_DrawFadingCrossHairs(h, 0, puppet) : h->DrawFadingCrossHairs(puppet); break;
            case 6: o ? O_DrawPauseErrors(h, 0) : h->DrawPauseErrors(); break;
            case 7: o ? O_DrawBlinkingArrow(h, 0, x, y) : h->DrawBlinkingArrow(x, y); break;
            case 8: o ? O_DrawGadget(h, 0) : h->DrawGadget(); break;
            case 9: o ? O_DrawLetterBox(h, 0) : h->DrawLetterBox(); break;
            case 10: o ? O_DrawStylin(h, 0) : h->DrawStylin(); break;
            case 11: o ? O_DrawBloodCurtain(h, 0) : h->DrawBloodCurtain(); break;
            case 12: o ? O_DrawTimer(h, 0) : h->DrawTimer(); break;
            case 13: o ? O_DrawWeapon(h, 0) : h->DrawWeapon(); break;
            case 14: o ? O_DrawMessage(h, 0) : h->DrawMessage(); break;
            case 15:
                o ? O_dbDrawTarget(h, 0, &position, "target", 5.0f, colour, second ? "second" : NULL)
                  : h->dbDrawTarget(&position, "target", 5.0f, colour, second ? "second" : NULL);
                break;
            }
        });
    }
}

// The text helpers, WordWrap, GetButton, dbMessage and GetScreenCoords
void TestText() {
    static const char *kTexts[] = {
        "", "short", "a few words that wrap round the width", "nospacesatallinthislongtextwhichcannotbreak",
        "  leading spaces and   gaps  between words that go on and on and on and on and on and on and on",
        "Press *A* to go", "*G*", "before *P* after *D* end", "no marker",
    };
    for (int i = 0; i < 300; i++) {
        g_case = g_base;
        PerturbState(g_case);
        int which = i % 8;
        const char *text = Pick(kTexts);
        float x = Uniform(0.0f, 640.0f);
        float y = Uniform(0.0f, 480.0f);
        float scale = Uniform(0.5f, 1.5f);
        uint32_t colour = Random();
        unsigned width = Random(30);
        Coord3 position = {Uniform(-200.0f, 200.0f), Uniform(-20.0f, 40.0f), Uniform(-200.0f, 200.0f)};
        static const char *kNames[] = {"GetCenter", "DrawFontAndShadow", "DrawFontCentered", "DrawFont lines",
                                       "DrawFont", "WordWrap", "GetButton/dbMessage", "GetScreenCoords"};
        const char *maybe = Random(4) == 0 ? NULL : text;
        bool startOnScreen = Random(2) != 0;
        RunCase(kNames[which], [&](bool o) {
            GHud *h = g_hud;
            GalleryKey *puppet = g_live->txtPuppet;
            switch (which) {
            case 0: {
                double center = o ? O_GetCenter(h, 0, puppet, scale, maybe) : h->GetCenter(puppet, scale, maybe);
                LOG("%016llx", std::bit_cast<unsigned long long>(center));
                break;
            }
            case 1:
                o ? O_DrawFontAndShadow(h, 0, puppet, x, y, colour, ~colour, scale, maybe)
                  : h->DrawFontAndShadow(puppet, x, y, colour, ~colour, scale, maybe);
                break;
            case 2:
            case 3:
            case 5: {
                char **lines;
                int count = o ? O_WordWrap(text, width, &lines) : WordWrap(text, width, &lines);
                LOG("lines %d:", count);
                for (int n = 0; n < 5; n++)
                    LOG(" [%s]", lines[n] != NULL ? lines[n] : "(null)");
                if (which == 2)
                    o ? O_DrawFontCentered(h, 0, puppet, x, y, x + 200.0f, y + 120.0f, scale, colour, count, lines)
                      : h->DrawFontCentered(puppet, x, y, x + 200.0f, y + 120.0f, scale, colour, count, lines);
                else if (which == 3)
                    o ? O_DrawFontLines(h, 0, puppet, x, y, colour, scale, lines, count)
                      : h->DrawFont(puppet, x, y, colour, scale, lines, count);
                for (int n = 0; n < 5; n++)
                    if (lines[n] != NULL)
                        OperatorDelete(lines[n]);
                OperatorDelete(lines);
                break;
            }
            case 4:
                o ? O_DrawFont(h, 0, puppet, x, y, colour, scale, maybe) : h->DrawFont(puppet, x, y, colour, scale, maybe);
                break;
            case 6: {
                char before[96], after[96];
                memset(before, 'x', sizeof(before));
                memset(after, 'y', sizeof(after));
                before[95] = after[95] = '\0';
                int button = o ? O_GetButton(h, 0, text, before, after) : h->GetButton(text, before, after);
                LOG("%d [%s] [%s] ", button, before, after);
                o ? O_dbMessage(text, int(width)) : GHud::dbMessage(text, int(width));
                break;
            }
            case 7: {
                bool onScreen = startOnScreen;
                ScreenPos screen = {-1.0f, -1.0f};
                o ? O_GetScreenCoords(h, 0, &position, &onScreen, &screen)
                  : h->GetScreenCoords(&position, &onScreen, &screen);
                LOG("%d %08x %08x", onScreen, Bits(screen.x), Bits(screen.y));
                break;
            }
            }
        });
    }
}

} // namespace

void GameFrontEndShadowA_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEFRONTENDSHADOWA");
    if (env == NULL || atoi(env) == 0)
        return;
    g_live = GHud::TheApp();
    if (g_live == NULL || ShMission == NULL || WeaponManager == NULL || fgRenderer == NULL || ShScheduler == NULL) {
        printf("[gamefrontendshadowa] no HUD, mission manager, weapon manager, renderer or scheduler: skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    Take(g_base);
    InstallFakes();
    TestState();
    TestPauseMenu();
    TestCheats();
    TestDraws();
    TestText();
    HooksRemove();
    Apply(g_base);
    printf("[gamefrontendshadowa] GHud part 1 (state, pause menu, cheats, draws, text): %ld cases, %ld checks, "
           "%ld differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
