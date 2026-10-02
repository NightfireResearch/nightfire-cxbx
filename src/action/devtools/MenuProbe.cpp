// Menu probes: a log of every message the menu system hands a page or control handler, and a replay of scripted
// pad input with screenshots, for mapping the front end and checking reimplemented handlers against a run of the
// original. Both are off unless settings.ini asks for them; nothing here is game logic.
//
// [Settings]
// MenuLog=on               every Handler_HandleMessage call: "[menu] f=<frame> m<manager> <control> msg=<type>
//                          a=<arg1> b=<arg2> -> <result>" (tools/ui/menu_log.py names the hashcodes and types)
// MenuLogSkip=0x3d,0x3e    message types left out of the log (per-frame ones, say)
// MenuShadowTests=on      run the secret-code, unlock, weapon upgrade, heap allocator and score shadow tests at start, before the game (no display needed)
// MenuCheckLists=on       compare the item lists' contents in our source with the game's, and the generated drone
//                          tables (tools/drone_tables.py), at start
// MenuOriginal=0x40000030:0x8ded0,0x76470
//                          run these as the original code, to compare a reimplementation with it: HASH:ADDR is a page
//                          or control handler (its hashcode and address), ADDR alone any other patched function
// MenuScript=menu_walk.txt a replay script, one command per line (# starts a comment):
//     wait N               let N frames pass (one frame = one pad poll)
//     waitpage HASH [N]    wait until manager 0 is on that page (at most N frames, 3000 if not given), so what follows
//                          is timed from the page, whatever the boot took
//     press BTN [N]        tap BTN (held 4 frames, released 4), N times
//     hold BTN N           hold BTN for N frames
//     shot NAME            write the next frame to menu_shots/<step>_<NAME>.bmp
//     log TEXT             print "[menuscript] TEXT" (a marker in the log)
//     gopage HASH          send GoPage <page hashcode> to manager 0, as a menu script would (reaches pages
//                          normal play cannot, such as P_FMVTEST 0x4000004f)
//     focus HASH [ID]      put the cursor on a control (by id among several of that hashcode, e.g. a keyboard key)
//     secretstest          compare the original secret-code check with ours (SecretsShadow.cpp)
//     poke ADDR VALUE      write a dword into the game (to set up a state a page expects, e.g. a finished match)
//     seed                 put the random number generator back to its state at boot. Its sequence is fixed (it is
//                          never seeded), but how many numbers have been drawn by a given poll frame depends on
//                          how many frames were rendered, i.e. on timing: seed just before a step whose outcome is
//                          random (the Quick Game's level, say) makes it the same on every run.
//                          !!! WARNING: THIS DOES NOT MAKE A REPLAY DETERMINISTIC. !!!
//                          !!! The game logic steps by real elapsed time, not a fixed tick per frame, so the   !!!
//                          !!! number of random draws - and AI, physics, animation - still drift between runs  !!!
//                          !!! from the moment after the seed. Two seeded runs of mp_start.txt load the same   !!!
//                          !!! level, but the match itself differs within seconds (a bot kills you in one).    !!!
//                          !!! Seed right before the random step, and screenshot as soon after it as you can.  !!!
//     unlockstest          compare the original progress/unlock functions with ours (UnlocksShadow.cpp)
//     quit                 end the process
//   BTN: A B X Y BLACK WHITE LT RT START BACK UP DOWN LEFT RIGHT
//
// The script's buttons are laid over port 0 after the game polls its pads (psiInput_PollDevices), so a run is
// the same whatever is plugged in or whichever window is in front.

#include "MenuProbe.h"

#include "../../common/xbeOriginal.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../engine/psiInput.h"
#include "../ui/ui.h"
#include "../ui/Manager.h"
#include "SecretsShadow.h"
#include "UnlocksShadow.h"
#include "UpgradeShadow.h"
#include "MemShadow.h"
#include "ScoreShadow.h"
#include "DroneTablesCheck.h"
#include "DroneShadow.h"
#include "WeaponTableShadow.h"
#include "MatrixShadow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static const unsigned kHandlerHandleMessage = 0x0008e320;
static const unsigned kPollDevices = 0x000e76a0;
static const unsigned kPollDevicesThunk = 0x000de5c0;   // Input_Update calls the poll through this

static unsigned g_frame = 0;

// --- the log -----------------------------------------------------------------------------------------------------

static unsigned g_skip[32];
static int g_skipCount = 0;

static bool Skipped(uint message) {
    for (int i = 0; i < g_skipCount; i++)
        if (g_skip[i] == message)
            return true;
    return false;
}

// Handlers to run as the original: the patch at the handler's address is taken out again, and the dispatcher's
// call - ours goes straight to our function, not through the address - is sent there instead.
typedef bool(__cdecl *HandlerFn)(uchar, M_CONTROL *, uint, uint, int, int);
static struct { unsigned hashcode, address; } g_originals[32];
static int g_originalCount = 0;
static bool g_logging = false;

static bool __cdecl LoggedHandleMessage(uchar managerNum, M_CONTROL *control, uint message, int arg1, int arg2) {
    bool result;
    int i = 0;
    while (i < g_originalCount && (control == NULL || g_originals[i].hashcode != control->hashcode))
        i++;
    if (i < g_originalCount)
        result = ((HandlerFn)g_originals[i].address)(managerNum, control, control->hashcode, message, arg1, arg2);
    else
        result = Handler_HandleMessage(managerNum, control, message, arg1, arg2);
    if (g_logging && !Skipped(message))
        printf("[menu] f=%u m%u 0x%08x msg=0x%02x a=0x%x b=0x%x -> %d\n", g_frame, managerNum,
               control ? control->hashcode : 0, message, (unsigned)arg1, (unsigned)arg2, (int)result);
    return result;
}

// --- the replay --------------------------------------------------------------------------------------------------

enum { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_BLACK, BTN_WHITE, BTN_LT, BTN_RT,       // the analog buttons, by index
       BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START, BTN_BACK, BTN_NONE };  // the digital ones
static const char *const kButtonNames[] = { "A", "B", "X", "Y", "BLACK", "WHITE", "LT", "RT",
                                            "UP", "DOWN", "LEFT", "RIGHT", "START", "BACK" };
static const unsigned short kDigitalBits[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20 };   // UP..BACK

struct Step {
    enum { WAIT, HOLD, SHOT, LOG, QUIT, GOPAGE, SECRETSTEST, FOCUS, WAITPAGE, UNLOCKSTEST, POKE } kind;
    int button;
    unsigned frames;
    char text[96];
};

static Step *g_steps = NULL;
static int g_stepCount = 0, g_stepIndex = 0;
static unsigned g_stepFrames = 0;   // frames spent in the current step
static int g_shotCount = 0;

static int ParseButton(const char *name) {
    for (int i = 0; i < BTN_NONE; i++)
        if (_stricmp(name, kButtonNames[i]) == 0)
            return i;
    return BTN_NONE;
}

static void AddStep(Step s) {
    g_steps = (Step *)realloc(g_steps, sizeof(Step) * (g_stepCount + 1));
    g_steps[g_stepCount++] = s;
}

static bool LoadScript(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        printf("[menuscript] cannot open %s\n", path);
        return false;
    }
    char line[256];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        lineNo++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char cmd[32] = "", a[96] = "", b[32] = "";
        int n = sscanf(line, "%31s %95s %31s", cmd, a, b);
        if (n <= 0)
            continue;
        Step s = {};
        s.button = BTN_NONE;
        if (_stricmp(cmd, "waitpage") == 0 && n >= 2) {
            s.kind = Step::WAITPAGE; s.button = (int)strtoul(a, NULL, 0); s.frames = n >= 3 ? (unsigned)atoi(b) : 3000;
            AddStep(s);
        } else if (_stricmp(cmd, "wait") == 0 && n >= 2) {
            s.kind = Step::WAIT; s.frames = (unsigned)atoi(a); AddStep(s);
        } else if ((_stricmp(cmd, "press") == 0 || _stricmp(cmd, "hold") == 0) && n >= 2 && ParseButton(a) != BTN_NONE) {
            bool press = _stricmp(cmd, "press") == 0;
            int count = n >= 3 ? atoi(b) : 1;
            for (int i = 0; i < (press ? count : 1); i++) {
                s.kind = Step::HOLD; s.button = ParseButton(a); s.frames = press ? 4 : (unsigned)count; AddStep(s);
                if (press) { s.kind = Step::WAIT; s.button = BTN_NONE; s.frames = 4; AddStep(s); }
            }
        } else if (_stricmp(cmd, "shot") == 0 && n >= 2) {
            s.kind = Step::SHOT; snprintf(s.text, sizeof(s.text), "%s", a); AddStep(s);
        } else if (_stricmp(cmd, "log") == 0) {
            s.kind = Step::LOG;
            const char *rest = line + strlen(cmd);
            while (*rest == ' ' || *rest == '\t') rest++;
            snprintf(s.text, sizeof(s.text), "%s", rest);
            s.text[strcspn(s.text, "\r\n")] = 0;
            AddStep(s);
        } else if (_stricmp(cmd, "focus") == 0 && n >= 2) {
            s.kind = Step::FOCUS; s.frames = (unsigned)strtoul(a, NULL, 0);
            s.button = n >= 3 ? atoi(b) : -1;   // the control's id, for one of several (a keyboard key)
            AddStep(s);
        } else if (_stricmp(cmd, "gopage") == 0 && n >= 2) {
            s.kind = Step::GOPAGE; s.frames = (unsigned)strtoul(a, NULL, 0); AddStep(s);
        } else if (_stricmp(cmd, "poke") == 0 && n >= 3) {
            s.kind = Step::POKE; s.frames = (unsigned)strtoul(a, NULL, 0); s.button = (int)strtoul(b, NULL, 0); AddStep(s);
        } else if (_stricmp(cmd, "seed") == 0) {
            // Rand_Random's two multiply-with-carry words (value in the low half, carry in the high), as the XBE has them
            s.kind = Step::POKE; s.frames = 0x0018cdf8; s.button = (int)0x1f123bb5; AddStep(s);
            s.kind = Step::POKE; s.frames = 0x0018cdfc; s.button = (int)0x159a55e5; AddStep(s);
        } else if (_stricmp(cmd, "unlockstest") == 0) {
            s.kind = Step::UNLOCKSTEST; AddStep(s);
        } else if (_stricmp(cmd, "secretstest") == 0) {
            s.kind = Step::SECRETSTEST; AddStep(s);
        } else if (_stricmp(cmd, "quit") == 0) {
            s.kind = Step::QUIT; AddStep(s);
        } else {
            printf("[menuscript] %s:%d: not understood: %s", path, lineNo, line);
        }
    }
    fclose(f);
    printf("[menuscript] %s: %d steps\n", path, g_stepCount);
    return g_stepCount > 0;
}

// Runs the script for one frame; returns the button to hold this frame (BTN_NONE for none).
static int ScriptFrame(void) {
    while (g_stepIndex < g_stepCount) {
        Step &s = g_steps[g_stepIndex];
        switch (s.kind) {
        case Step::WAITPAGE: {
            M_PAGE *page = manager[0].currentPage;
            if ((page == NULL || page->control.hashcode != (uint)s.button) && g_stepFrames < s.frames) {
                g_stepFrames++;
                return BTN_NONE;
            }
            printf("[menuscript] f=%u waitpage 0x%08x: %s after %u frames\n", g_frame, (unsigned)s.button,
                   g_stepFrames < s.frames ? "there" : "TIMED OUT", g_stepFrames);
            break;
        }
        case Step::WAIT:
        case Step::HOLD:
            if (g_stepFrames < s.frames) {
                g_stepFrames++;
                return s.kind == Step::HOLD ? s.button : BTN_NONE;
            }
            break;
        case Step::SHOT: {
            char name[160];
            snprintf(name, sizeof(name), "menu_shots/%03d_%s", ++g_shotCount, s.text);
            unsigned frame = D3D9_RequestScreenshot(name);
            printf("[menuscript] f=%u shot %s.bmp (render frame %u)\n", g_frame, name, frame);
            break;
        }
        case Step::LOG:
            printf("[menuscript] f=%u %s\n", g_frame, s.text);
            break;
        case Step::FOCUS: {
            M_CONTROL *target = s.button >= 0
                ? (M_CONTROL *)__Menu_SendEx(0, (HASHCODE)s.frames, (uint)s.button, MessageType_GetControl, 0, 0)
                : CONTROL_GET(0, (HASHCODE)s.frames);
            printf("[menuscript] f=%u focus 0x%08x %d%s\n", g_frame, s.frames, s.button, target ? "" : " (not found)");
            if (target)
                Manager_SendMessage(&manager[0], MessageType_SetCursor, 0, (int)target);
            break;
        }
        case Step::GOPAGE:
            printf("[menuscript] f=%u gopage 0x%08x\n", g_frame, s.frames);
            Manager_SendMessage(&manager[0], MessageType_GoPage, (int)s.frames, 0);
            break;
        case Step::POKE:
            *(int *)(size_t)s.frames = s.button;
            break;
        case Step::UNLOCKSTEST:
            UnlocksShadow_Run();
            break;
        case Step::SECRETSTEST:
            SecretsShadow_Run();
            break;
        case Step::QUIT:
            if (g_stepFrames < 3) {   // let a screenshot asked for just before reach the disk
                g_stepFrames++;
                return BTN_NONE;
            }
            printf("[menuscript] f=%u quit\n", g_frame);
            fflush(stdout);
            TerminateProcess(GetCurrentProcess(), 0);
            break;
        }
        g_stepIndex++;
        g_stepFrames = 0;
    }
    return BTN_NONE;
}

static void __stdcall ScriptedPollDevices(void) {
    ControllerStateStruct *c = &XboxInputs.Controllers[0];
    unsigned prevHeld = c->prevButtons;
    psiInput_PollDevices();
    g_frame++;
    if (g_steps == NULL || g_stepIndex >= g_stepCount)
        return;
    int button = ScriptFrame();
    // The script owns port 0 while it runs: nothing held unless it says so. The poll has already turned the raw
    // pad into the mask the game reads (analog button i -> bit 16 + i; .buttons is "pressed this frame", the
    // held mask ANDed away), so that is redone here from the script's button, the same way.
    unsigned char *analog = (unsigned char *)c->controllerState.Gamepad.bAnalogButtons;
    memset(analog, 0, 8);
    c->controllerState.Gamepad.wButtons &= ~0x3f;
    unsigned raw = 0;
    if (button <= BTN_RT) {
        analog[button] = 0xff;
        raw = 1u << (16 + button);
    } else if (button != BTN_NONE) {
        c->controllerState.Gamepad.wButtons |= kDigitalBits[button - BTN_UP];
        raw = kDigitalBits[button - BTN_UP];
    }
    c->controllerState.Gamepad.sThumbLX = c->controllerState.Gamepad.sThumbLY = 0;
    c->controllerState.Gamepad.sThumbRX = c->controllerState.Gamepad.sThumbRY = 0;
    c->Joystick_LX = c->Joystick_LY = c->Joystick_RX = c->Joystick_RY = 0.0f;
    c->prevButtons = prevHeld & raw;
    c->buttons = ~prevHeld & raw;
    c->controllerState.dwPacketNumber = g_frame;   // a new packet every frame, as a live pad reports
}

// --- installing --------------------------------------------------------------------------------------------------

static bool SettingOn(const char *key) {
    char v[16] = "";
    GetPrivateProfileStringA("Settings", key, "", v, sizeof(v), ".\\settings.ini");
    return _stricmp(v, "on") == 0 || strcmp(v, "1") == 0;
}

// The item lists in our source against the game's copies (which nothing uses any more), before the game has
// run: any difference is a mistake in the source, or a deliberate fix to be named.
static void CheckLists(void) {
    static const struct { const char *name; const void *ours; unsigned theirs, size; } lists[] = {
        { "sp_level", sp_level, 0x17c580, sizeof(sp_level) },
        { "mp_level", mp_level, 0x17c6a0, sizeof(mp_level) },
        { "difficulty", difficulty, 0x17c760, sizeof(difficulty) },
        { "mp_scenario", mp_scenario, 0x17c7a8, sizeof(mp_scenario) },
        { "mp_characters", mp_characters, 0x17c8e0, sizeof(mp_characters) },
        { "mp_characters_small", mp_characters_small, 0x17cb98, sizeof(mp_characters_small) },
        { "mp_options", mp_options, 0x17ce50, sizeof(mp_options) },
        { "cn_options", cn_options, 0x17cec8, sizeof(cn_options) },
        { "ds_options", ds_options, 0x17cf70, sizeof(ds_options) },
        { "mp_bots", mp_bots, 0x17cfd0, sizeof(mp_bots) },
        { "ds_weapons", ds_weapons, 0x17d168, sizeof(ds_weapons) },
        { "ds_gadgets", ds_gadgets, 0x17d3f0, sizeof(ds_gadgets) },
    };
    int bad = 0;
    for (const auto &l : lists) {
        const M_ITEM *a = (const M_ITEM *)l.ours, *b = (const M_ITEM *)(size_t)l.theirs;
        for (unsigned i = 0; i < l.size / sizeof(M_ITEM); i++)
            if (memcmp(&a[i], &b[i], sizeof(M_ITEM)) != 0) {
                bad++;
                const uint *x = (const uint *)&a[i], *y = (const uint *)&b[i];
                printf("[lists] %s[%u] differs: ours %08x %08x %08x %08x %08x %08x, game %08x %08x %08x %08x %08x %08x\n",
                       l.name, i, x[0], x[1], x[2], x[3], x[4], x[5], y[0], y[1], y[2], y[3], y[4], y[5]);
            }
    }
    printf("[lists] %d item lists checked against the game's: %d items differ\n", (int)ARRAY_SIZE(lists), bad);
}

void MenuProbe_Install(void) {
    if (SettingOn("MenuCheckLists")) {
        CheckLists();
        DroneTablesCheck_Run();
    }
    // The shadow tests need only the game's data, so they can run before it starts (and with no display): on the
    // shipped state, as a fresh codename would have it.
    if (SettingOn("MenuShadowTests")) {
        SecretsShadow_Run();
        UnlocksShadow_Run();
        UpgradeShadow_Run();
        MemShadow_Run();
        ScoreShadow_Run();
        DroneShadow_Run();
        WeaponTableShadow_Run();
        MatrixShadow_Run();
    }
    g_logging = SettingOn("MenuLog");
    if (g_logging) {
        char skip[256] = "";
        GetPrivateProfileStringA("Settings", "MenuLogSkip", "", skip, sizeof(skip), ".\\settings.ini");
        for (char *t = strtok(skip, ", "); t && g_skipCount < 32; t = strtok(NULL, ", "))
            g_skip[g_skipCount++] = (unsigned)strtoul(t, NULL, 0);
    }
    char originals[512] = "";
    GetPrivateProfileStringA("Settings", "MenuOriginal", "", originals, sizeof(originals), ".\\settings.ini");
    for (char *t = strtok(originals, ", "); t; t = strtok(NULL, ", ")) {
        char *colon = strchr(t, ':');
        unsigned address = (unsigned)strtoul(colon ? colon + 1 : t, NULL, 0);
        if (!XbeOriginal_Restore(address, true)) {
            printf("[menu] MenuOriginal: 0x%x is not a patched function\n", address);
            continue;
        }
        if (colon && g_originalCount < 32) {
            g_originals[g_originalCount].hashcode = (unsigned)strtoul(t, NULL, 0);
            g_originals[g_originalCount++].address = address;
        }
        printf("[menu] running the original 0x%x%s\n", address, colon ? " (handler)" : "");
    }
    if ((g_logging || g_originalCount > 0) && XbeOriginal_Redirect(kHandlerHandleMessage, (const void *)&LoggedHandleMessage) && g_logging)
        printf("[menu] logging every handler message (%d types skipped)\n", g_skipCount);
    char script[MAX_PATH] = "";
    GetPrivateProfileStringA("Settings", "MenuScript", "", script, sizeof(script), ".\\settings.ini");
    if (script[0] != 0 && LoadScript(script)) {
        CreateDirectoryA("menu_shots", NULL);
        XbeOriginal_Redirect(kPollDevices, (const void *)&ScriptedPollDevices);
        XbeOriginal_Redirect(kPollDevicesThunk, (const void *)&ScriptedPollDevices);
    }
}
