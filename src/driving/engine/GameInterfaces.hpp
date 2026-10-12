#pragma once

// Calls into parts of the game our code uses but has not reimplemented - the front end: the HUD, the effects
// gallery (GFX), the subtitles, the loading screen and the locale - each reaching the original at its address
// (thiscall through __fastcall with a dummy EDX). The classes are somewhere for the methods to live, under Ghidra's
// names, with only the fields ported code reads mapped. Provisional, until the front end is ported.

#include <stdint.h>

struct Coord3;
struct Coord4;
struct ScreenPos;
class RSceneObj;

// The HUD (Ghidra: GHud).
class GHud {
public:
    unsigned char unknown000[0x2b0];
    unsigned char paused;       // +0x2b0 set: the fire buttons are ignored, the weapon manager holds its fire
    // The one HUD, or null before it exists (0x000d7c10; the global at 0x0023f44c).
    // AUTOGEN
    static GHud *TheApp();

    // Shows or clears the "please reconnect the controller" state; showing it pauses the game (0x000e00f0).
    // AUTOGEN
    void SetControllerUnplugged(bool unplugged);

    // (the names below are Ghidra's; their arguments' names ours)
    void SetLevelLoadText() {
        ((void (__fastcall *)(GHud *, int))0x000d7c20)(this, 0);
    }
    void EnableLavaDamageEffect(bool on) {
        ((void (__fastcall *)(GHud *, int, bool))0x000d7c30)(this, 0, on);
    }
    bool LavaDamageEffectEnabled() {
        return ((bool (__fastcall *)(GHud *, int))0x000d7c60)(this, 0);
    }
    void FadeInFromBlack(int steps) {
        ((void (__fastcall *)(GHud *, int, int))0x000d7c70)(this, 0, steps);
    }
    void FadeOutToBlack(int steps) {
        ((void (__fastcall *)(GHud *, int, int))0x000d7cd0)(this, 0, steps);
    }
    bool GetBlackFadeAnimating() {
        return ((bool (__fastcall *)(GHud *, int))0x000d7d40)(this, 0);
    }
    float GetBlackFadePct() {
        return ((float (__fastcall *)(GHud *, int))0x000d7d50)(this, 0);
    }
    void Stylin() {
        ((void (__fastcall *)(GHud *, int))0x000d7d60)(this, 0);
    }
    float RadarRadiusInMeters() {
        return ((float (__fastcall *)(GHud *, int))0x000d7d90)(this, 0);
    }
    void Reset() {
        ((void (__fastcall *)(GHud *, int))0x000d7da0)(this, 0);
    }
    void TriggerDamageFlash(float damage) {
        ((void (__fastcall *)(GHud *, int, float))0x000d87a0)(this, 0, damage);
    }
    void TriggerBloodCurtain() {
        ((void (__fastcall *)(GHud *, int))0x000d87f0)(this, 0);
    }
    void TriggerTimerOn(int timer) {
        ((void (__fastcall *)(GHud *, int, int))0x000d8800)(this, 0, timer);
    }
    void TriggerTimerOff(int timer) {
        ((void (__fastcall *)(GHud *, int, int))0x000d8810)(this, 0, timer);
    }
    void SetAutoDrive(bool on) {
        ((void (__fastcall *)(GHud *, int, bool))0x000d9040)(this, 0, on);
    }
    void PauseOff() {
        ((void (__fastcall *)(GHud *, int))0x000d9050)(this, 0);
    }
    void LetterBoxOn(bool flag) {
        ((void (__fastcall *)(GHud *, int, bool))0x000d9b30)(this, 0, flag);
    }
    void LetterBoxOff(bool flag) {
        ((void (__fastcall *)(GHud *, int, bool))0x000d9b70)(this, 0, flag);
    }
    int GetLetterBoxMode() {
        return ((int (__fastcall *)(GHud *, int))0x000d9b90)(this, 0);
    }
    void AimOn() {
        ((void (__fastcall *)(GHud *, int))0x000d9ba0)(this, 0);
    }
    void AimOff() {
        ((void (__fastcall *)(GHud *, int))0x000d9bd0)(this, 0);
    }
    void GadgetOn(int gadget, float value) {
        ((void (__fastcall *)(GHud *, int, int, float))0x000d9bf0)(this, 0, gadget, value);
    }
    void GadgetOff() {
        ((void (__fastcall *)(GHud *, int))0x000d9c30)(this, 0);
    }
    void SetTarget(const ScreenPos *cursor) {
        ((void (__fastcall *)(GHud *, int, const ScreenPos *))0x000d9f50)(this, 0, cursor);
    }
    void SetTargetLockState(int state) {
        ((void (__fastcall *)(GHud *, int, int))0x000d9fb0)(this, 0, state);
    }
    void TriggerMissionMsg(int message, float time) {
        ((void (__fastcall *)(GHud *, int, int, float))0x000da030)(this, 0, message, time);
    }
    void TriggerObjectiveMsg(int kind, int id, bool flag) {
        ((void (__fastcall *)(GHud *, int, int, int, bool))0x000da0a0)(this, 0, kind, id, flag);
    }
    void TriggerButtonMsg(int message, int unknown2, int unknown3, int unknown4, float time) {
        ((void (__fastcall *)(GHud *, int, int, int, int, int, float))0x000da180)(this, 0, message, unknown2, unknown3, unknown4, time);
    }
    void KillButtonMessage() {
        ((void (__fastcall *)(GHud *, int))0x000da260)(this, 0);
    }
    void dbDrawTarget(const ScreenPos *position, const char *text, float scale, uint32_t colour, const char *text2) {
        ((void (__fastcall *)(GHud *, int, const ScreenPos *, const char *, float, uint32_t, const char *))0x000da8e0)(this, 0, position, text, scale, colour, text2);
    }
    GHud* Construct() {
        return ((GHud *(__fastcall *)(GHud *, int))0x000dab50)(this, 0);
    }
    void GetScreenCoords(const Coord3 *position, bool *onScreen, ScreenPos *screen) {
        ((void (__fastcall *)(GHud *, int, const Coord3 *, bool *, ScreenPos *))0x000dc4a0)(this, 0, position, onScreen, screen);
    }
    void SetMissionPassed(bool flag) {
        ((void (__fastcall *)(GHud *, int, bool))0x000dee80)(this, 0, flag);
    }
    void SetMissionFailed(bool flag) {
        ((void (__fastcall *)(GHud *, int, bool))0x000deee0)(this, 0, flag);
    }
    void Render() {
        ((void (__fastcall *)(GHud *, int))0x000e0140)(this, 0);
    }
    void UpdateTargets() {
        ((void (__fastcall *)(GHud *, int))0x000e1a90)(this, 0);
    }
};

// GHud::TriggerObjectiveMsg's kind, as the objective events pass it (names ours)
enum ObjectiveMsgKind : int32_t {
    kObjectiveMsgCurrent = 1,           // EObjectiveSetCurrent's, and EObjectiveDisplayText's
    kObjectiveMsgPassed = 2,            // EObjectivePass's
    kObjectiveMsgFailed = 3,            // EObjectiveFail's
};

// The effects gallery's entry points (Ghidra: GFX): an effect by its reference or its name, placed at a position and
// rotation, offset by a second pair (in the frame of the owner's instance when there is an owner); Trigger copies
// only x, y and z of the positions. TriggerIncidental is Trigger for an incidental effect. (Argument names ours.)
class GFX {
public:
    static void PurgeAllEffects() {
        ((void (*)())0x000d34b0)();
    }
    static void Trigger(void *effect, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                        const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance, const Coord3 *ownerVelocity) {
        ((void (*)(void *, const Coord3 *, const Coord4 *, const Coord3 *, const Coord4 *, RSceneObj *, uint32_t, const Coord3 *))0x000d34e0)(effect, position, rotation, offset, offsetRotation, owner, instance, ownerVelocity);
    }
    static void TriggerIncidental(void *effect, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                                  const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance,
                                  const Coord3 *ownerVelocity) {
        ((void (*)(void *, const Coord3 *, const Coord4 *, const Coord3 *, const Coord4 *, RSceneObj *, uint32_t, const Coord3 *))0x000d3520)(effect, position, rotation, offset, offsetRotation, owner, instance, ownerVelocity);
    }
    // The effect named, NULL for none
    static void* LookupReference(const char *name) {
        return ((void *(*)(const char *))0x000d35f0)(name);
    }
    static void TriggerByName(const char *name, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                              const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance,
                              const Coord3 *ownerVelocity) {
        ((void (*)(const char *, const Coord3 *, const Coord4 *, const Coord3 *, const Coord4 *, RSceneObj *, uint32_t, const Coord3 *))0x000d3610)(name, position, rotation, offset, offsetRotation, owner, instance, ownerVelocity);
    }
};

// The loading screen (Ghidra: GLoadingScreen). Status is a printf.
#define GLoadingScreen_Status ((void (*)(const char *, ...))0x000e2ff0)

class GLoadingScreen {
public:
    static void InitLoadDots(bool on) {
        ((void (*)(bool))0x000e1f90)(on);
    }
    static void LoadAndDrawUnLoadingScreen() {
        ((void (*)())0x000e2110)();
    }
    static void InitLoadScreen(const char *picture, const char *layout) {
        ((void (*)(const char *, const char *))0x000e2390)(picture, layout);
    }
    static void Shutdown() {
        ((void (*)())0x000e2a70)();
    }
    static void AddLoadingScreenSyncTask() {
        ((void (*)())0x000e2b70)();
    }
    static void DeleteLoadingScreenSyncTask() {
        ((void (*)())0x000e2b90)();
    }
    static void FreeUnloadingScreen() {
        ((void (*)())0x000e2ba0)();
    }
};

class GLocale {
public:
    static int GetLanguage() {
        return ((int (*)())0x000e30d0)();
    }
    static const char* GetSubDirectory() {
        return ((const char *(*)())0x000e3140)();
    }
};

// The subtitles (Ghidra: GSubtitles, 0x94 bytes)
class GSubtitles {
public:
    uint32_t unknown00[0x94 / 4];

    static GSubtitles* TheApp() {
        return ((GSubtitles *(*)())0x000e3180)();
    }
    GSubtitles* Construct() {
        return ((GSubtitles *(__fastcall *)(GSubtitles *, int))0x000e34e0)(this, 0);
    }
    void ClearSubtitles() {
        ((void (__fastcall *)(GSubtitles *, int))0x000e3630)(this, 0);
    }
    void Destruct() {
        ((void (__fastcall *)(GSubtitles *, int))0x000e3730)(this, 0);
    }
    void DisplaySubtitle(uint32_t text, float time) {
        ((void (__fastcall *)(GSubtitles *, int, uint32_t, float))0x000e3790)(this, 0, text, time);
    }
    void LoadSubtitles(const char *file) {
        ((void (__fastcall *)(GSubtitles *, int, const char *))0x000e3840)(this, 0, file);
    }
};
static_assert(sizeof(GSubtitles) == 0x94, "GSubtitles is 0x94 bytes");

// GSubtitles::DrawFrame, the movie player's subtitle callback (by its address)
#define GSubtitles_DrawFrame ((void (*)(int frame))0x000e3ab0)
