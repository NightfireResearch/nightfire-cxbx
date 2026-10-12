#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameFrontEndShadowB.h"
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

#include "../camera/CameraIniLoader.h"
#include "../camera/PlayerCamera.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/GameLoop.h"
#include "../engine/UMemory.hpp"
#include "../game/Gallery.h"
#include "../game/Hud.h"
#include "../game/HudCrossHairs.h"
#include "../game/HudMenus.h"
#include "../game/MissionManager.h"
#include "../game/Vehicle.h"
#include "../game/WeaponCameras.h"
#include "../game/WeaponManager.h"
#include "../render/RenderHigh.h"
#include "../render/Renderer.h"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GFE_B (GHud part 2: the large draws), run once from the first simulation tick when
// NIGHTFIRE_GAMEFRONTENDSHADOWB is set. Each case copies the live HUD into a buffer and perturbs it (the pause
// menu's page, selection, question and objective, the mission result's state, step and fades, the button and
// objective messages, the lock, the aim, the auto-drive zoom, the frame's step...) and the state it reads (the
// selected weapon, the target picker's sorted targets, the mission manager's objective count, auto-drive,
// cinematic mode and result, the launch page and mission name, the renderer's screen mode, the missile camera,
// the HUD switch, the simulation state), then runs the ORIGINAL (the function under test swapped back) and the
// PORT on the same state, put back between the two. Every call out of the function is a recording fake on both
// sides: the gallery (its quad state written as QUAD_SetState writes it, since the original inlines that call
// in places; the quads interpolated and keys interpolated deterministically), Draw's model matrix and depth,
// the render context, the locale, the menu sounds, the subtitles, the girl, the target picker, the mission
// manager and its scores, the input configurations, the attribute lookups, the streams and mixes, the action
// queue (a scripted list of actions), the HUD view's Render, GHud part 1's pause, cheat, arrow, crosshair and
// screen-coordinate functions, the player's car's sound (a fake car and sound object behind the player's
// pointer: its aim flag, its zoom's InTheZone, the recharge's vtable slot 9), the hands' aim, the camera's zoom,
// the Simulation's rigid body and tracked instances, the scene objects, and for Draw every draw it calls. The
// helpers of part 1 that only draw (GetCenter, DrawFont*, GetButton, WordWrap) run as themselves.
// Compared: the call logs, the HUD's bytes after the call, the gallery's bytes, the globals written (the sniper
// zoom's flag and alpha, the launch page's end state, outro flag and lives, the fake sound's aim flag, the
// crosshairs' auto-aim frame - the game's for the original, ours for the port).
//
// Not run here: the scalar deleting destructor (it destroys the HUD).
//
// A mutation the test sees: DrawMissionResult's opening ending at missionResultStep > frames instead of >= (cases
// put the step one short of the key's frame: the state and step in the HUD's bytes differ), or DrawCrossHairs'
// sniper ticks multiplied in the other order (the model matrices logged differ).
//
// One summary line: [gamefrontendshadowb] ...: N cases, M checks, D differ.
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
        printf("[gamefrontendshadowb] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x2545f491;
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

// ---- hooks: a jump over an entry, and over our port's entry behind it

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[192];
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

// Takes the hooks off down to `mark` of them
void HooksRemove(int mark) {
    for (int i = g_hookCount; i-- > mark;) {
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
    g_hookCount = mark;
}

// ---- the state a case sets, and the fakes' answers

#define GameAutoAimFrame I32_AT(0x0023f56c)
#define ShLaunch (*(LaunchPage *)0x00243b90)
#define ShLives I32_AT(0x002440cc)
#define ShMission (*(SMissionManager **)0x00239220)
#define ShMissionState I32_AT((uintptr_t(ShMission) + 0x474))
#define ShMissionEndTime I32_AT((uintptr_t(ShMission) + 0x70c))
#define ShPicker (*(WTargetPicker *)0x0023e1b0)
#define GameHudOn U8_AT(0x001b7055)           // the game's copies of gHudOn and gDAudio
#define GameDAudio U8_AT(0x001e4761)
#define ShSimState I32_AT(0x00234e24)
#define ShPlayerList PTR_AT(0x00234e40)

struct Fakes {
    int selected;                       // the target GetSelectedTarget answers, -1 for none
    bool lookupBool[16];                // by target
    int lookupInt;
    int numConfigs;
    bool inverted;
    bool objectiveNull[8];
    int objectiveStates[8];
    int objectiveIds[8];
    double health;
    double zoom;
    float aim;
    bool streamAnswers[2][4];           // music, speech: not found, over, paused, loop
    bool speechInMix;
    int queue[8];                       // action ids, 0 for a null action, -1 the end
    bool currObjective;
    bool objectiveVisible;
    Coord4 objective;
    double arrowHeight;
    uint32_t comment;
    int trackedCount;
    int32_t trackedKinds[4];
    bool trackedVisible[4];
    Coord3 trackedPositions[4];
    Coord4 trackedDimensions[4];
    bool trackedData[4];
    float trackedDescriptions[4];
    bool onScreen[8];                   // GetScreenCoords' answers, in turn
    ScreenPos screens[8];
    Coord3 player;
    uint32_t zoomingColour;
    uint32_t scoreSeed;
};

struct State {
    uint8_t hud[sizeof(GHud)];
    uint8_t gallery[sizeof(GGallery)];
    uint8_t launch[sizeof(LaunchPage)];
    uint8_t picker[sizeof(WTargetPicker)];
    WTargetable targets[16];
    uint32_t objectiveCount;
    uint8_t autoDrive;
    int32_t cinematicMode;
    int32_t missionState;
    int32_t missionEndTime;
    int32_t secondary;
    bool doubleShot;
    WeaponRecord weapons[kWeaponCount];
    uint8_t widescreen;
    int32_t videoMode;
    int cameraMode;
    uint8_t hudOn;
    uint8_t dAudio;
    int32_t simState;
    uint8_t sniperActive;
    int32_t sniperAlpha;
    int32_t autoAimFrame;
    uint8_t audioAiming;
    Fakes fakes;
};

State g_base, g_case;
alignas(16) uint8_t g_hudBuffer[sizeof(GHud)];
GHud *const g_hud = reinterpret_cast<GHud *>(g_hudBuffer);
GHud *g_live;
Fakes g_fakes;
std::string *g_log;
WTargetable g_targets[16];
uint8_t g_targetObjects[16][0x70];      // the targets' physics objects (their attributes at +0x60)
MissionObjective g_objectives[8];
int g_queuePos;
ActionData g_actions[8];
int g_screenCall;

// The player's car behind the player pointer: its GetAudio (slot 7) answers the sound object, whose slot 9 is the
// recharge
alignas(16) uint8_t g_fakeCar[0x400];
alignas(16) uint8_t g_fakeAudio[0x200];
uint8_t g_fakeZoom[0x10];
void *g_carVtable[48];
void *g_audioVtable[16];
void *g_playerList[1];
void *g_savedPlayerList;

// The streams, mixes, rigid body, tracked instances and scene objects the fakes answer
alignas(16) uint8_t g_streams[2][0x100];
uint8_t g_mixes[2][0x10];
alignas(16) uint8_t g_rigidBody[0x100];
struct FakeTracked {
    int32_t kind;
    void *instance;
    bool visible;
};
FakeTracked g_tracked[4];
FakeTracked *g_trackedPointers[4];
GameVector<FakeTracked *> g_trackedList;
uint8_t g_instances[4][0x40];
alignas(16) uint8_t g_sceneObjects[4][0x100];
uint8_t g_baseDescs[4][100];
uint8_t g_physicsData[4][0x30];

uint8_t &AudioAiming() {
    return g_fakeAudio[0xc4];
}

void Take(State &s) {
    memcpy(s.hud, g_live, sizeof(GHud));
    memcpy(s.gallery, g_live->gallery, sizeof(GGallery));
    memcpy(s.launch, &ShLaunch, sizeof(LaunchPage));
    memcpy(s.picker, &ShPicker, sizeof(WTargetPicker));
    memset(s.targets, 0, sizeof(s.targets));
    s.objectiveCount = ShMission->objectives.size;
    s.autoDrive = ShMission->autoDrive;
    s.cinematicMode = ShMission->cinematicMode;
    s.missionState = ShMissionState;
    s.missionEndTime = ShMissionEndTime;
    s.secondary = WeaponManager->secondary;
    s.doubleShot = WeaponManager->doubleShot;
    memcpy(s.weapons, WeaponManager->weapons, sizeof(s.weapons));
    s.widescreen = fgRenderer->widescreen;
    s.videoMode = fgRenderer->videoMode;
    s.cameraMode = fgRenderHigh->views[0].camera->cameraMode;
    s.hudOn = gHudOn;
    s.dAudio = gDAudio;
    s.simState = ShSimState;
    s.sniperActive = SniperZoom->active;
    s.sniperAlpha = SniperZoom->alpha;
    s.autoAimFrame = GameAutoAimFrame;
    s.audioAiming = 0;
    memset(&s.fakes, 0, sizeof(s.fakes));
}

// Puts a state on the copy and the live game; the package's static into both the game's and ours
void Apply(const State &s) {
    memcpy(g_hudBuffer, s.hud, sizeof(GHud));
    memcpy(g_live->gallery, s.gallery, sizeof(GGallery));
    memcpy(&ShLaunch, s.launch, sizeof(LaunchPage));
    memcpy(&ShPicker, s.picker, sizeof(WTargetPicker));
    memcpy(g_targets, s.targets, sizeof(g_targets));
    ShMission->objectives.size = s.objectiveCount;
    ShMission->autoDrive = s.autoDrive;
    ShMission->cinematicMode = s.cinematicMode;
    ShMissionState = s.missionState;
    ShMissionEndTime = s.missionEndTime;
    WeaponManager->secondary = s.secondary;
    WeaponManager->doubleShot = s.doubleShot;
    memcpy(WeaponManager->weapons, s.weapons, sizeof(s.weapons));
    fgRenderer->widescreen = s.widescreen;
    fgRenderer->videoMode = s.videoMode;
    fgRenderHigh->views[0].camera->cameraMode = s.cameraMode;
    GameHudOn = s.hudOn;
    gHudOn = s.hudOn != 0;
    GameDAudio = s.dAudio;
    gDAudio = s.dAudio != 0;
    ShSimState = s.simState;
    SniperZoom->active = s.sniperActive;
    SniperZoom->alpha = s.sniperAlpha;
    GameAutoAimFrame = s.autoAimFrame;
    HudAutoAimFrame = s.autoAimFrame;
    AudioAiming() = s.audioAiming;
    g_fakes = s.fakes;
    g_queuePos = 0;
    g_screenCall = 0;
    for (int i = 0; i < 8; i++) {
        g_objectives[i].id = s.fakes.objectiveIds[i];
        g_objectives[i].hint = s.fakes.objectiveIds[i] * 3 + 1;
        g_objectives[i].state = s.fakes.objectiveStates[i];
        g_actions[i].action = s.fakes.queue[i];
        g_actions[i].source = 0;
        g_actions[i].value = 0.0f;
    }
    *reinterpret_cast<void **>(g_streams[0] + 0x94) = g_mixes[0];
    *reinterpret_cast<void **>(g_streams[1] + 0x94) = s.fakes.speechInMix ? g_mixes[1] : g_mixes[0];
    memcpy(g_rigidBody + 0x10, &s.fakes.player, sizeof(Coord3));
    g_trackedList.first = g_trackedPointers;
    g_trackedList.last = g_trackedPointers + s.fakes.trackedCount;
    for (int i = 0; i < 4; i++) {
        g_tracked[i].kind = s.fakes.trackedKinds[i];
        g_tracked[i].instance = g_instances[i];
        g_tracked[i].visible = s.fakes.trackedVisible[i];
        g_trackedPointers[i] = &g_tracked[i];
        *reinterpret_cast<void **>(g_sceneObjects[i] + 0x10) = g_baseDescs[i];
        *reinterpret_cast<void **>(g_baseDescs[i] + 0x0c) = s.fakes.trackedData[i] ? g_physicsData[i] : NULL;
        memcpy(g_physicsData[i] + 0x28, &s.fakes.trackedDescriptions[i], 4);
    }
}

// What a run leaves: the HUD's bytes, the gallery's, the globals
void AppendResult(std::string &log, bool original) {
    log += " hud:";
    for (size_t i = 0; i < sizeof(GHud); i++)
        Append(log, "%02x", g_hudBuffer[i]);
    log += " gallery:";
    const uint8_t *gallery = reinterpret_cast<const uint8_t *>(g_live->gallery);
    for (size_t i = 0; i < sizeof(GGallery); i++)
        Append(log, "%02x", gallery[i]);
    Append(log, " sniper %d %d launch %d %d lives %d aiming %d frame %d", SniperZoom->active, SniperZoom->alpha,
           ShLaunch.unknown978, ShLaunch.unknown5d8, ShLives, AudioAiming(),
           original ? GameAutoAimFrame : HudAutoAimFrame);
}

#define LOG(...) do { if (g_log != NULL) Append(*g_log, __VA_ARGS__); } while (0)

// Runs the case on both sides: the original with the entry `at` swapped back
template <class Call>
void RunCase(const char *what, unsigned at, const Call &call) {
    std::string results[2];
    for (int side = 0; side < 2; side++) {
        bool original = side == 0;
        Apply(g_case);
        std::string &log = results[side];
        g_log = &log;
        bool ok;
        if (original) {
            XbeOriginalScope window(at);
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

// ---- the fakes

const char *const kSymbols[] = {
    "OBJECTIVE COMPLETE", "Press *A* to continue", "A", "Hold *G* to quit the hint",
    "A MESSAGE LONG ENOUGH TO PASS THE THIRTY-TWO CHARACTER MARK AND THEN SOME MORE WORDS TO WRAP",
    "Use *P* and *D*", "", "Weapon",
};
const char *__cdecl FakeLocaleSymbol(int symbol) {
    LOG("sym(%d) ", symbol);
    if (symbol == g_live->hintQuitPuppet->text.text)
        return kSymbols[3];     // the objectives' hint page needs its button marker
    return kSymbols[uint32_t(symbol * 2654435761u) >> 29];
}
float __fastcall FakeFontWidth(GGallery *, int, const char *text) {
    LOG("width(%s) ", text);
    return float(strlen(text)) * 9.25f + 0.375f;
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
    LOG("spriteAt(%p,%08x) ", (const void *)puppet, gallery->colour);
}
void __fastcall FakeBitmapDraw(GGallery *gallery, int, const GalleryKey *puppet) {
    LOG("bitmap(%p,%08x) ", (const void *)puppet, gallery->colour);
}
void __fastcall FakeSpriteFlush(GGallery *, int) {
    LOG("flush ");
}
// QUAD_SetState's writes, not logged (the original inlines them in places); the draws log the state
void __fastcall FakeQuadSetState(GGallery *gallery, int, const GalleryKey *puppet) {
    uint8_t *g = reinterpret_cast<uint8_t *>(gallery);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(puppet);
    memcpy(g + 0x60, p, 12);
    memcpy(g + 0x70, p + 0x10, 16);
    gallery->colour = 0;
}
void LogQuadState(const GGallery *gallery) {
    const uint32_t *g = reinterpret_cast<const uint32_t *>(gallery);
    LOG("[%08x %08x %08x %08x %08x %08x %08x %08x]", gallery->colour, g[0x60 / 4], g[0x64 / 4], g[0x68 / 4],
        g[0x70 / 4], g[0x74 / 4], g[0x78 / 4], g[0x7c / 4]);
}
void __fastcall FakeQuadDraw(GGallery *gallery, int, const GalleryKey *puppet, float alpha) {
    LOG("quad(%p,%08x", (const void *)puppet, Bits(alpha));
    LogQuadState(gallery);
    LOG(") ");
}
void __fastcall FakeQuadDrawBlock(GGallery *gallery, int, const uint32_t *block, float alpha) {
    LOG("quadBlock(%08x", Bits(alpha));
    for (int i = 0; i < 12; i++)
        LOG(",%08x", block[i]);
    LogQuadState(gallery);
    LOG(") ");
}
void __fastcall FakeQuadDraw2D(GGallery *, int, float x, float y, float w, float h, uint32_t colour) {
    LOG("quad2d(%08x,%08x,%08x,%08x,%08x) ", Bits(x), Bits(y), Bits(w), Bits(h), colour);
}
void __fastcall FakeQuadInterpolate(GGallery *, int, const float *from, const float *to, float *out, float t) {
    LOG("quadLerp(%p,%p,%08x) ", (const void *)from, (const void *)to, Bits(t));
    const uint32_t *a = reinterpret_cast<const uint32_t *>(from);
    const uint32_t *b = reinterpret_cast<const uint32_t *>(to);
    uint32_t *o = reinterpret_cast<uint32_t *>(out);
    for (int i = 0; i < 4; i++)
        o[i] = t > 0.5f ? b[i] : a[i];
    for (int i = 4; i < 12; i++)
        out[i] = from[i] + (to[i] - from[i]) * t;
}
void *__fastcall FakeInterpolatedKey(GGallery *, int, uint8_t *result, const uint8_t *from, const uint8_t *to,
                                     float t) {
    LOG("keyLerp(%p,%p,%08x) ", (const void *)from, (const void *)to, Bits(t));
    memset(result, 0, 0x34);
    float *r = reinterpret_cast<float *>(result);
    const float *a = reinterpret_cast<const float *>(from);
    const float *b = reinterpret_cast<const float *>(to);
    for (int i = 0; i < 3; i++)
        r[i] = a[i] + (b[i] - a[i]) * t;
    memcpy(result + 0x10, from + 0x10, 0x12);
    return result;
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
void __cdecl FakeZDepthNear() {
    LOG("zNear ");
}
bool __fastcall FakeZWrites(void *, int, uint8_t enable) {
    LOG("zWrites(%d) ", enable);
    return true;
}
void __cdecl FakeMenuSound(int bank, const char *patch, const char *mix, int view) {
    LOG("sound(%d,%s,%s,%d) ", bank, patch, mix, view);
}
int g_fakeSubtitles;
void *__cdecl FakeSubtitlesApp() {
    LOG("subtitlesApp ");
    return &g_fakeSubtitles;
}
void __fastcall FakeDrawSubtitles(void *self, int) {
    LOG("drawSubtitles(%d) ", self == &g_fakeSubtitles);
}
void __cdecl FakeDoGirl() {
    LOG("doGirl ");
}
WTargetable *__fastcall FakeSelectedTarget(void *, int) {
    LOG("selected ");
    return g_fakes.selected >= 0 ? &g_targets[g_fakes.selected] : NULL;
}
MissionObjective *__fastcall FakeObjectiveByIndex(void *, int, int index) {
    LOG("objective(%d) ", index);
    if (index < 0 || index >= 8 || g_fakes.objectiveNull[index])
        return NULL;
    return &g_objectives[index];
}
void __fastcall FakeRestoreLives(void *, int) {
    LOG("restoreLives ");
}
void __fastcall FakeSkipCinematic(void *, int) {
    LOG("skipCinematic ");
}
const Coord4 *__fastcall FakeCurrObjective(void *, int, int slot) {
    LOG("currObjective(%d) ", slot);
    return g_fakes.currObjective ? &g_fakes.objective : NULL;
}
bool __fastcall FakeObjectiveVisible(void *, int, int slot) {
    LOG("objectiveVisible(%d) ", slot);
    return g_fakes.objectiveVisible;
}
double __fastcall FakeArrowHeight(void *, int, int slot) {
    LOG("arrowHeight(%d) ", slot);
    return g_fakes.arrowHeight;
}
uint32_t __fastcall FakeObjectiveComment(void *, int, int slot) {
    LOG("comment(%d) ", slot);
    return g_fakes.comment;
}
void __fastcall FakeClearScores(void *, int, LaunchPage *) {
    LOG("clearScores ");
}
void __fastcall FakeTallyScores(void *, int, LaunchPage *page, bool end) {
    LOG("tally(%d) ", end);
    int32_t *summary = reinterpret_cast<int32_t *>(page->summary);
    for (int i = 0; i < 27; i++)
        summary[i] = int32_t((g_fakes.scoreSeed * (i + 7)) % 5000) - 200;
}
double __fastcall FakePlayerHealth(void *, int, bool fraction) {
    LOG("health(%d) ", fraction);
    return g_fakes.health;
}
int __fastcall FakeNumConfigs(void *, int, int type) {
    LOG("numConfigs(%d) ", type);
    return g_fakes.numConfigs;
}
bool __fastcall FakeIsInverted(void *, int) {
    LOG("isInverted ");
    return g_fakes.inverted;
}
int __fastcall FakeLocaleID(void *, int, int type, int config, int slot) {
    LOG("localeID(%d,%d,%d) ", type, config, slot);
    return type * 100 + config * 20 + slot;
}
int32_t __fastcall FakeLookupInt(void *, int, const char *key, bool *) {
    LOG("lookupInt(%s) ", key);
    return g_fakes.lookupInt;
}
uint8_t __fastcall FakeLookupBool(void *attributes, int, const char *key, bool *) {
    int index = int((static_cast<uint8_t *>(attributes) - 0x60 - &g_targetObjects[0][0]) / 0x70);
    LOG("lookupBool(%d,%s) ", index, key);
    return index >= 0 && index < 16 && g_fakes.lookupBool[index];
}
// GHud part 1's
void __fastcall FakePauseOn(GHud *, int) {
    LOG("pauseOn ");
}
void __fastcall FakePauseOff(GHud *, int) {
    LOG("pauseOff ");
}
void __fastcall FakePauseMenuMsg(GHud *, int, int action) {
    LOG("pauseMsg(%d) ", action);
}
void __fastcall FakeCheatMsg(GHud *, int, int action) {
    LOG("cheatMsg(%d) ", action);
}
void __fastcall FakeBlinkingArrow(GHud *, int, float x, float y) {
    LOG("arrow(%08x,%08x) ", Bits(x), Bits(y));
}
uint32_t __fastcall FakeZooming(GHud *, int, GalleryKey *puppet) {
    LOG("zooming(%p) ", (const void *)puppet);
    return g_fakes.zoomingColour;
}
void __fastcall FakeFading(GHud *, int, GalleryKey *puppet) {
    LOG("fading(%p) ", (const void *)puppet);
}
void __fastcall FakeScreenCoords(GHud *, int, const Coord3 *position, bool *onScreen, ScreenPos *screen) {
    LOG("screenCoords(%08x,%08x,%08x) ", Bits(position->x), Bits(position->y), Bits(position->z));
    int n = g_screenCall++ & 7;
    *onScreen = g_fakes.onScreen[n];
    if (*onScreen)
        *screen = g_fakes.screens[n];
}
// The sounds and the car
void *__fastcall FakeGetAudio(void *, int) {
    LOG("getAudio ");
    return g_fakeAudio;
}
void __fastcall FakeSetRecharge(void *, int, float percent) {
    LOG("recharge(%08x) ", Bits(percent));
}
void __fastcall FakeInTheZone(void *self, int, bool entering) {
    LOG("inTheZone(%d,%d) ", self == g_fakeZoom, entering);
}
float __cdecl FakeAimFraction() {
    LOG("aimFraction ");
    return g_fakes.aim;
}
double __fastcall FakeZoomPercent(void *, int) {
    LOG("zoomPercent ");
    return g_fakes.zoom;
}
void *__cdecl FakeStreamGet(const char *name) {
    LOG("stream(%s) ", name);
    return strcmp(name, "Music") == 0 ? g_streams[0] : g_streams[1];
}
void *__cdecl FakeMixGet(const char *name) {
    LOG("mix(%s) ", name);
    return g_mixes[1];
}
int StreamIndex(void *stream) {
    return stream == g_streams[0] ? 0 : 1;
}
void __fastcall FakeStreamStop(void *stream, int) {
    LOG("stop(%d) ", StreamIndex(stream));
}
bool __fastcall FakeIsNotFound(void *stream, int) {
    LOG("notFound(%d) ", StreamIndex(stream));
    return g_fakes.streamAnswers[StreamIndex(stream)][0];
}
bool __fastcall FakeIsOver(void *stream, int) {
    LOG("over(%d) ", StreamIndex(stream));
    return g_fakes.streamAnswers[StreamIndex(stream)][1];
}
bool __fastcall FakeIsPaused(void *stream, int) {
    LOG("paused(%d) ", StreamIndex(stream));
    return g_fakes.streamAnswers[StreamIndex(stream)][2];
}
bool __fastcall FakeIsLoop(void *stream, int) {
    LOG("loop(%d) ", StreamIndex(stream));
    return g_fakes.streamAnswers[StreamIndex(stream)][3];
}
// The action queue: the scripted actions
bool __fastcall FakeQueueEmpty(void *, int) {
    LOG("empty ");
    return g_queuePos >= 8 || g_fakes.queue[g_queuePos] < 0;
}
ActionRef *__fastcall FakeGetAction(void *, int, ActionRef *result) {
    LOG("getAction ");
    result->data = g_fakes.queue[g_queuePos] == 0 ? NULL : &g_actions[g_queuePos];
    return result;
}
void __fastcall FakePopAction(void *, int) {
    LOG("pop ");
    g_queuePos++;
}
void __fastcall FakeViewRender(void *, int) {
    LOG("viewRender ");
}
// The Simulation and the world
void *__fastcall FakeRigidBody(void *, int, int slot) {
    LOG("rigidBody(%d) ", slot);
    return g_rigidBody;
}
void *__fastcall FakeTrackedList(void *, int) {
    LOG("trackedList ");
    return &g_trackedList;
}
void *__fastcall FakeSceneObj(void *, int, const uint8_t *instance) {
    int index = int((instance - &g_instances[0][0]) / 0x40);
    LOG("sceneObj(%d) ", index);
    return g_sceneObjects[index & 3];
}
int SceneIndex(const void *object) {
    return int((static_cast<const uint8_t *>(object) - &g_sceneObjects[0][0]) / 0x100) & 3;
}
void __fastcall FakeBoundingDimensions(void *self, int, Coord4 *out) {
    LOG("dimensions(%d) ", SceneIndex(self));
    *out = g_fakes.trackedDimensions[SceneIndex(self)];
}
void __fastcall FakePosition(void *self, int, Coord3 *out) {
    LOG("position(%d) ", SceneIndex(self));
    *out = g_fakes.trackedPositions[SceneIndex(self)];
}
void __cdecl FakeDAudioTargets() {
    LOG("dAudioTargets ");
}
void __cdecl FakeDAudioUpdate() {
    LOG("dAudioUpdate ");
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
    HookInstall(0x000d6da0, (const void *)&FakeSpriteFlush);
    HookInstall(0x000d5b30, (const void *)&FakeQuadSetState);
    HookInstall(0x000d68b0, (const void *)&FakeQuadDraw);
    HookInstall(0x000d6750, (const void *)&FakeQuadDrawBlock);
    HookInstall(0x000d68c0, (const void *)&FakeQuadDraw2D);
    HookInstall(0x000d5b70, (const void *)&FakeQuadInterpolate);
    HookInstall(0x000d5910, (const void *)&FakeInterpolatedKey);
    HookInstall(0x00075f30, (const void *)&FakeSetModelMatrix);
    HookInstall(0x00075f50, (const void *)&FakeZDepthNear);
    HookInstall(0x000e73f0, (const void *)&FakeZWrites);
    HookInstall(0x0011e010, (const void *)&FakeMenuSound);
    HookInstall(0x000e3180, (const void *)&FakeSubtitlesApp);
    HookInstall(0x000e39b0, (const void *)&FakeDrawSubtitles);
    HookInstall(0x000d7a50, (const void *)&FakeDoGirl);
    HookInstall(0x000cdfb0, (const void *)&FakeSelectedTarget);
    HookInstall(0x000b8460, (const void *)&FakeObjectiveByIndex);
    HookInstall(0x000b63c0, (const void *)&FakeRestoreLives);
    HookInstall(0x000b79d0, (const void *)&FakeSkipCinematic);
    HookInstall(0x000b6c70, (const void *)&FakeCurrObjective);
    HookInstall(0x000b6d40, (const void *)&FakeObjectiveVisible);
    HookInstall(0x000b6cd0, (const void *)&FakeArrowHeight);
    HookInstall(0x000b6cc0, (const void *)&FakeObjectiveComment);
    HookInstall(0x000b63f0, (const void *)&FakeClearScores);
    HookInstall(0x000b7470, (const void *)&FakeTallyScores);
    HookInstall(0x000b67d0, (const void *)&FakePlayerHealth);
    HookInstall(0x00050170, (const void *)&FakeNumConfigs);
    HookInstall(0x00050160, (const void *)&FakeIsInverted);
    HookInstall(0x000501a0, (const void *)&FakeLocaleID);
    HookInstall(0x00058180, (const void *)&FakeLookupInt);
    HookInstall(0x000580e0, (const void *)&FakeLookupBool);
    HookInstall(0x000dcae0, (const void *)&FakePauseOn);
    HookInstall(0x000d9050, (const void *)&FakePauseOff);
    HookInstall(0x000dcc10, (const void *)&FakePauseMenuMsg);
    HookInstall(0x000d9140, (const void *)&FakeCheatMsg);
    HookInstall(0x000d9570, (const void *)&FakeBlinkingArrow);
    HookInstall(0x000d8c00, (const void *)&FakeZooming);
    HookInstall(0x000d8ea0, (const void *)&FakeFading);
    HookInstall(0x000dc4a0, (const void *)&FakeScreenCoords);
    HookInstall(0x00127ed0, (const void *)&FakeInTheZone);
    HookInstall(0x000227a0, (const void *)&FakeAimFraction);
    HookInstall(0x00080e20, (const void *)&FakeZoomPercent);
    HookInstall(0x001237d0, (const void *)&FakeStreamGet);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x00122430, (const void *)&FakeStreamStop);
    HookInstall(0x00121f50, (const void *)&FakeIsNotFound);
    HookInstall(0x00121f40, (const void *)&FakeIsOver);
    HookInstall(0x00121f60, (const void *)&FakeIsPaused);
    HookInstall(0x00121f70, (const void *)&FakeIsLoop);
    HookInstall(0x0004f220, (const void *)&FakeQueueEmpty);
    HookInstall(0x0004f290, (const void *)&FakeGetAction);
    HookInstall(0x0004f250, (const void *)&FakePopAction);
    HookInstall(0x00097060, (const void *)&FakeViewRender);
    HookInstall(0x000b2700, (const void *)&FakeRigidBody);
    HookInstall(0x000b2900, (const void *)&FakeTrackedList);
    HookInstall(0x000d14a0, (const void *)&FakeSceneObj);
    HookInstall(0x0008dfd0, (const void *)&FakeBoundingDimensions);
    HookInstall(0x0008dc90, (const void *)&FakePosition);
    HookInstall(0x00036c90, (const void *)&FakeDAudioTargets);
    HookInstall(0x00037750, (const void *)&FakeDAudioUpdate);
}

// Draw's callees, for its own cases
#define DRAW_FAKE(name, address) \
    void __fastcall Fake##name(GHud *, int) { LOG(#name " "); }
DRAW_FAKE(DrawFlyByWire, 0x000d8ae0)
DRAW_FAKE(DrawStylin, 0x000da540)
DRAW_FAKE(DrawWeapon, 0x000dc990)
DRAW_FAKE(DrawHealth, 0x000d81d0)
DRAW_FAKE(DrawMessage, 0x000da2e0)
DRAW_FAKE(DrawGadget, 0x000d9c50)
DRAW_FAKE(DrawTimer, 0x000dc7d0)
DRAW_FAKE(DrawPauseErrors, 0x000d93d0)
DRAW_FAKE(DrawPauseMenu, 0x000dd340)
DRAW_FAKE(DrawMissionResult, 0x000df2e0)
DRAW_FAKE(DrawObjectiveMessage, 0x000dfc30)
DRAW_FAKE(DrawButtonMessage, 0x000def90)
DRAW_FAKE(DrawCrossHairs, 0x000e0440)

void InstallDrawFakes() {
    HookInstall(0x000d8ae0, (const void *)&FakeDrawFlyByWire);
    HookInstall(0x000da540, (const void *)&FakeDrawStylin);
    HookInstall(0x000dc990, (const void *)&FakeDrawWeapon);
    HookInstall(0x000d81d0, (const void *)&FakeDrawHealth);
    HookInstall(0x000da2e0, (const void *)&FakeDrawMessage);
    HookInstall(0x000d9c50, (const void *)&FakeDrawGadget);
    HookInstall(0x000dc7d0, (const void *)&FakeDrawTimer);
    HookInstall(0x000d93d0, (const void *)&FakeDrawPauseErrors);
    HookInstall(0x000dd340, (const void *)&FakeDrawPauseMenu);
    HookInstall(0x000df2e0, (const void *)&FakeDrawMissionResult);
    HookInstall(0x000dfc30, (const void *)&FakeDrawObjectiveMessage);
    HookInstall(0x000def90, (const void *)&FakeDrawButtonMessage);
    HookInstall(0x000e0440, (const void *)&FakeDrawCrossHairs);
}

// ---- the originals

#define O_DrawPauseMenu ((void (__fastcall *)(GHud *, int))0x000dd340)
#define O_SetMissionPassed ((void (__fastcall *)(GHud *, int, bool))0x000dee80)
#define O_SetMissionFailed ((void (__fastcall *)(GHud *, int, bool))0x000deee0)
#define O_DrawButtonMessage ((void (__fastcall *)(GHud *, int))0x000def90)
#define O_DrawMissionResult ((void (__fastcall *)(GHud *, int))0x000df2e0)
#define O_DrawObjectiveMessage ((void (__fastcall *)(GHud *, int))0x000dfc30)
#define O_SetControllerUnplugged ((void (__fastcall *)(GHud *, int, bool))0x000e00f0)
#define O_Render ((int (__fastcall *)(GHud *, int))0x000e0140)
#define O_DrawCrossHairs ((void (__fastcall *)(GHud *, int))0x000e0440)
#define O_Draw ((void (__fastcall *)(GHud *, int))0x000e1970)
#define O_UpdateTargets ((void (__fastcall *)(GHud *, int))0x000e1a90)

// ---- the perturbations

const float kTimes[] = {0.0f, -0.0f, 0.25f, 0.5f, 1.0f, 1.5f, 3.0f, -0.125f, 0.0333f, 0.3f, 0.1f};

float Time() {
    return Random(3) == 0 ? Pick(kTimes) : Uniform(-0.5f, 4.0f);
}

char g_buttonText[] = "Push *G* now";

void PerturbHud(GHud &h, const GHud &live) {
    h.paused = Random(3) == 0;
    h.controllerUnplugged = Random(3) == 0;
    h.autoDrive = Random(2) != 0;
    h.superEasy = Random(5) == 0;
    h.unknown432 = Random(5) == 0;
    h.aimOn = Random(2) != 0;
    h.letterBoxMode = Random(3) == 0 ? int32_t(Random(4)) : kLetterBoxOff;
    h.missionResultText = Random(3) == 0 ? kTextMissionFailed : Random(2) == 0 ? kTextMissionPassed : -1;
    h.missionResultState = Random(3) == 0 ? 0 : int32_t(Random(7));
    // around the box's key frames, one short of each sometimes
    int frames0 = int32_t(live.statusBackKeys[0]->time);
    int frames1 = int32_t(live.statusBackKeys[1]->time) - frames0;
    int frames = h.missionResultState == kResultWidening ? frames1 : frames0;
    h.missionResultStep = Random(2) ? frames - 1 : int32_t(Random(uint32_t(frames > 0 ? frames + 2 : 3)));
    h.missionResultFadeIn = Time();
    h.missionResultHold = Time();
    h.missionResultDelay = Random(3) == 0 ? Uniform(0.0f, 0.4f) : Time();
    h.bloodCurtainTime = Random(3) == 0 ? -1.0f : Time();
    h.elapsed = Random(3) == 0 ? 0.0333333351f : Uniform(0.0f, 0.4f);
    h.lastStep = Random(3) == 0 ? 0 : int32_t(Random(1200));
    h.buttonMsgTime = Time();
    h.buttonMsgFade = Time();
    h.buttonMsgUnknown = Random(2) ? int32_t(Random(64)) : -int32_t(Random(3));
    h.buttonMsg = Random(4) == 0 ? 0 : int32_t(uintptr_t(Random(2) ? g_buttonText : "no marker here"));
    h.objectiveMsgTime = Time();
    h.objectiveMsgId = int32_t(Random(64));
    h.objectiveMsgKind = int32_t(Random(5)) - 1;
    h.nextObjectiveMsgId = Random(2) ? -1 : int32_t(Random(64));
    h.nextObjectiveMsgKind = int32_t(Random(5)) - 1;
    h.objectivePulses = int32_t(Random(4));
    h.objectivePulseTime = Uniform(-0.1f, 0.7f);
    h.objectiveMsgFlag = Random(2) != 0;
    h.targetLockState = int32_t(Random(3));
    h.target.x = Uniform(0.0f, 640.0f);
    h.target.y = Uniform(0.0f, 480.0f);
    h.targetScaleX = Random(2) ? 1.0f : Uniform(0.5f, 1.5f);
    h.targetScaleY = Random(2) ? 1.0f : Uniform(0.5f, 1.5f);
    h.targetOffsetX = Random(2) ? 0.0f : Uniform(-30.0f, 30.0f);
    h.targetOffsetY = Random(2) ? 0.0f : Uniform(-30.0f, 30.0f);
    h.zoomPercent = Random(3) == 0 ? Pick(kTimes) : Uniform(0.0f, 1.0f);
    h.pausePage = Random(6) == 0 ? int32_t(Random(8)) : Random(5) == 0 ? kPauseFailed : int32_t(Random(4));
    h.pauseSelection = int32_t(Random(4));
    h.pauseConfirm = int32_t(Random(3));
    h.pauseObjective = int32_t(Random(9));
    h.pauseObjectivesBrowse = int32_t(Random(3));
    h.pauseConfig = int32_t(Random(4));
    h.pauseError = int32_t(Random(2));
    h.unknown47C = int32_t(Random(3));
}

void PerturbState(State &s) {
    PerturbHud(*reinterpret_cast<GHud *>(s.hud), *g_live);
    s.objectiveCount = Random(8);
    s.autoDrive = Random(2) != 0;
    s.cinematicMode = Random(3) == 0;
    s.missionState = int32_t(Random(5));
    s.missionEndTime = Random(2) ? 0 : int32_t(Random(300));
    s.secondary = int32_t(Random(kWeaponCount));
    s.doubleShot = Random(2) != 0;
    WeaponRecord &weapon = s.weapons[s.secondary];
    static const int32_t kIds[] = {kWeaponGun, kWeaponMissile, kWeaponTorpedo, kWeaponUltralightRocket,
                                   kWeaponFodRocket, kWeaponFodGun, kWeaponSnowmobileGun, kWeaponSnowmobileRocket,
                                   kWeaponSniperRifle, kWeaponUltralightGun, kWeaponTimeMine};
    weapon.id = Pick(kIds);
    weapon.fireSteps[0] = Random(2) ? 0 : int32_t(Random(3000));
    weapon.refireSteps = int32_t(Random(200)) + 1;
    s.widescreen = Random(2) != 0;
    s.videoMode = int32_t(Random(3));
    if (Random(4) == 0)
        s.cameraMode = fgCameraModeIndices.missile;
    s.hudOn = Random(5) != 0;
    s.dAudio = Random(4) == 0;
    s.simState = Random(4) == 0 ? 2 : 1;
    s.sniperActive = uint8_t(Random(2));
    s.sniperAlpha = int32_t(Random(256));
    s.autoAimFrame = int32_t(Random(32));
    s.audioAiming = uint8_t(Random(2));
    LaunchPage &launch = *reinterpret_cast<LaunchPage *>(s.launch);
    static const char *const kMissions[] = {"paris_mis01", "junglea_mis13a", "snow2a_mis4", "uw_mis11"};
    strcpy(launch.missionName, Pick(kMissions));
    launch.unknown978 = int32_t(Random(5));
    launch.unknown5d8 = int32_t(Random(3)) - 1;
    reinterpret_cast<int32_t *>(s.launch)[0x53c / 4] = int32_t(Random(4)) - 1;
    reinterpret_cast<int32_t *>(s.launch)[0xa3c / 4] = int32_t(Random(5));
    WTargetPicker &picker = *reinterpret_cast<WTargetPicker *>(s.picker);
    picker.sortedCount = int32_t(Random(17));
    for (int i = 0; i < 16; i++) {
        WTargetable &t = s.targets[i];
        t.screenPos.x = Uniform(-50.0f, 690.0f);
        t.screenPos.y = Uniform(-50.0f, 530.0f);
        t.onScreen = Random(3) != 0;
        t.ownerType = Random(2) ? kTargetPhysicsObject : int32_t(Random(4));
        t.physics = Random(4) == 0 ? NULL : reinterpret_cast<PhysicsObject *>(g_targetObjects[i]);
        picker.sorted[i] = &g_targets[i];
    }
    Fakes &f = s.fakes;
    f.selected = int32_t(Random(18)) - 1;
    for (int i = 0; i < 16; i++)
        f.lookupBool[i] = Random(3) == 0;
    f.lookupInt = int32_t(Random(3));
    f.numConfigs = int32_t(Random(4));
    f.inverted = Random(2) != 0;
    for (int i = 0; i < 8; i++) {
        f.objectiveNull[i] = Random(6) == 0;
        f.objectiveStates[i] = int32_t(Random(4));
        f.objectiveIds[i] = int32_t(Random(200));
    }
    f.health = Random(3) == 0 ? 1.0 : Uniform(0.0f, 1.0f);
    f.zoom = Random(3) == 0 ? Pick(kTimes) : Uniform(0.0f, 1.0f);
    f.aim = Uniform(0.0f, 1.0f);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++)
            f.streamAnswers[i][j] = Random(3) == 0;
    f.speechInMix = Random(2) != 0;
    for (int i = 0; i < 8; i++) {
        static const int kActions[] = {0, kActionToggleSecondary, kActionToggleSecondaryUp,
                                       kActionToggleSecondaryDown, kFrontEndStart, kFrontEndSkipMovie,
                                       kFrontEndUp, kFrontEndAccept, kCheatButLeft, 32, 36, 1};
        f.queue[i] = Random(5) == 0 ? -1 : Pick(kActions);
    }
    f.currObjective = Random(4) != 0;
    f.objectiveVisible = Random(4) != 0;
    f.objective = {Uniform(-100.0f, 100.0f), Uniform(-5.0f, 5.0f), Uniform(-100.0f, 100.0f), 1.0f};
    f.arrowHeight = Uniform(0.0f, 4.0f);
    f.comment = Random();
    f.player = {Uniform(-20.0f, 20.0f), Random(4) == 0 ? f.objective.y : Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f)};
    f.trackedCount = int32_t(Random(5));
    for (int i = 0; i < 4; i++) {
        f.trackedKinds[i] = Random(4) == 0;
        f.trackedVisible[i] = Random(4) != 0;
        f.trackedPositions[i] = {f.player.x + Uniform(-130.0f, 130.0f), Uniform(-5.0f, 5.0f),
                                 f.player.z + Uniform(-130.0f, 130.0f)};
        f.trackedDimensions[i] = {Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), 0.0f};
        f.trackedData[i] = Random(3) != 0;
        f.trackedDescriptions[i] = float(Random(100));
    }
    for (int i = 0; i < 8; i++) {
        f.onScreen[i] = Random(3) != 0;
        f.screens[i] = {Uniform(0.0f, 640.0f), Uniform(0.0f, 480.0f)};
    }
    f.zoomingColour = Random();
    f.scoreSeed = Random();
}

// ---- the cases

void TestPauseMenu() {
    for (int i = 0; i < 700; i++) {
        g_case = g_base;
        PerturbState(g_case);
        GHud &h = *reinterpret_cast<GHud *>(g_case.hud);
        h.paused = true;
        if (i % 3 == 0)
            h.pausePage = int32_t(i / 3) % 6;
        RunCase("DrawPauseMenu", 0x000dd340, [&](bool o) {
            o ? O_DrawPauseMenu(g_hud, 0) : g_hud->DrawPauseMenu();
        });
    }
}

void TestResult() {
    for (int i = 0; i < 600; i++) {
        g_case = g_base;
        PerturbState(g_case);
        if (i % 2 == 0)
            reinterpret_cast<GHud *>(g_case.hud)->paused = false;
        bool flag = Random(2) != 0;
        int which = i % 6;
        static const char *kNames[] = {"DrawMissionResult", "DrawMissionResult", "DrawMissionResult",
                                       "SetMissionPassed", "SetMissionFailed", "SetControllerUnplugged"};
        static const unsigned kAt[] = {0x000df2e0, 0x000df2e0, 0x000df2e0, 0x000dee80, 0x000deee0, 0x000e00f0};
        RunCase(kNames[which], kAt[which], [&](bool o) {
            GHud *h = g_hud;
            switch (which) {
            case 0:
            case 1:
            case 2: o ? O_DrawMissionResult(h, 0) : h->DrawMissionResult(); break;
            case 3: o ? O_SetMissionPassed(h, 0, flag) : h->SetMissionPassed(flag); break;
            case 4: o ? O_SetMissionFailed(h, 0, flag) : h->SetMissionFailed(flag); break;
            case 5: o ? O_SetControllerUnplugged(h, 0, flag) : h->SetControllerUnplugged(flag); break;
            }
        });
    }
}

void TestMessages() {
    for (int i = 0; i < 600; i++) {
        g_case = g_base;
        PerturbState(g_case);
        reinterpret_cast<GHud *>(g_case.hud)->paused = Random(5) == 0;
        bool button = i % 2 == 0;
        RunCase(button ? "DrawButtonMessage" : "DrawObjectiveMessage", button ? 0x000def90 : 0x000dfc30, [&](bool o) {
            GHud *h = g_hud;
            if (button)
                o ? O_DrawButtonMessage(h, 0) : h->DrawButtonMessage();
            else
                o ? O_DrawObjectiveMessage(h, 0) : h->DrawObjectiveMessage();
        });
    }
}

void TestCrossHairs() {
    for (int i = 0; i < 900; i++) {
        g_case = g_base;
        PerturbState(g_case);
        RunCase("DrawCrossHairs", 0x000e0440, [&](bool o) {
            o ? O_DrawCrossHairs(g_hud, 0) : g_hud->DrawCrossHairs();
        });
    }
}

void TestFrame() {
    for (int i = 0; i < 600; i++) {
        g_case = g_base;
        PerturbState(g_case);
        int which = i % 2;
        RunCase(which == 0 ? "Render" : "UpdateTargets", which == 0 ? 0x000e0140 : 0x000e1a90, [&](bool o) {
            GHud *h = g_hud;
            if (which == 0)
                LOG("%d ", o ? O_Render(h, 0) : h->Render());
            else
                o ? O_UpdateTargets(h, 0) : h->UpdateTargets();
        });
    }
    int mark = g_hookCount;
    InstallDrawFakes();
    for (int i = 0; i < 300; i++) {
        g_case = g_base;
        PerturbState(g_case);
        RunCase("Draw", 0x000e1970, [&](bool o) {
            o ? O_Draw(g_hud, 0) : g_hud->Draw();
        });
    }
    HooksRemove(mark);
}

} // namespace

void GameFrontEndShadowB_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEFRONTENDSHADOWB");
    if (env == NULL || atoi(env) == 0)
        return;
    g_live = GHud::TheApp();
    if (g_live == NULL || ShMission == NULL || WeaponManager == NULL || fgRenderer == NULL || fgRenderHigh == NULL ||
        fgRenderHigh->views[0].camera == NULL || SniperZoom == NULL || ShPlayerList == NULL) {
        printf("[gamefrontendshadowb] no HUD, mission manager, weapon manager, renderer, camera, sniper zoom or "
               "player: skipped\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    // The fake car and its sound behind the player's pointer
    *reinterpret_cast<void ***>(g_fakeCar) = g_carVtable;
    g_carVtable[PVehicle::kGetAudio] = (void *)&FakeGetAudio;
    *reinterpret_cast<int16_t *>(g_fakeCar + 0x4a) = 3;
    *reinterpret_cast<void ***>(g_fakeAudio) = g_audioVtable;
    g_audioVtable[9] = (void *)&FakeSetRecharge;
    *reinterpret_cast<void **>(g_fakeAudio + 0x190) = g_fakeZoom;
    g_playerList[0] = g_fakeCar;
    g_savedPlayerList = ShPlayerList;
    ShPlayerList = g_playerList;

    Take(g_base);
    InstallFakes();
    TestPauseMenu();
    TestResult();
    TestMessages();
    TestCrossHairs();
    TestFrame();
    HooksRemove(0);
    Apply(g_base);
    ShPlayerList = g_savedPlayerList;
    printf("[gamefrontendshadowb] GHud part 2 (pause menu, result, messages, crosshairs, frame): %ld cases, %ld "
           "checks, %ld differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    fflush(stdout);
}
