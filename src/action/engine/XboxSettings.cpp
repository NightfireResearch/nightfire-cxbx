#include "XboxSettings.h"
#include "../actionhelpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_FILE "settings.ini"

// ---------------------------------------------------------------------------------------------------------------
// Plain host settings file, standing in for the Xbox EEPROM (region/language/display/audio/parental-control
// settings normally configured once via the Xbox dashboard, read here by the game through
// ExQueryNonVolatileSetting). GetParentalControlSettings/LanguageNVSetting/XboxGetAVRegion/GetVideoMode/
// GetAudioMode below all originally read that EEPROM and now read this file instead.
//
// Only region/language/widescreen/FPS are actually exposed as settings - everything else (audio mode,
// parental controls) gets a fixed, sensible default, since nothing about them is interesting to a PC player.
//
// Region matters beyond display timing: XboxGetAVRegion() also decides which of two English text variants
// FUN_000e92a0 selects (English + region=NTSC selects one internal language id, English + any other region
// selects a different one) - which is how a real Xbox's "Stunner" gadget item, in the US release, ends up
// rebranded as a Philips-brand electric shaver in every other English-speaking release. It's a genuine
// regional product-tie-in joke baked into the original game, not a translation difference, so Region is
// worth being able to flip even though it has no other gameplay effect.
//
// Default is PAL, not NTSC, for a reason worth being precise about: xboxInitGraphics (now reimplemented in
// Direct3D/d3dSeam.cpp, same logic) feeds
// XboxGetAVRegion()'s result into the D3D9 device's creation flags, including FullScreen_RefreshRateInHz
// (60Hz for NTSC, 50Hz for PAL, via the confusingly-named Gfx.IsPalI - see mainloop's own comment on that).
// The default dates from when the game ran under CXBX: there, Region=NTSC left background-movie (FMV)
// playback permanently black (the video decoder never signalled "frame ready", no crash) while Region=PAL
// played correctly - almost certainly because the setting had to agree with CXBX's own emulated video
// timing. That was never evidence that NTSC/60Hz is broken in itself, and it has not been rechecked under
// the loader; the default simply stayed PAL.
// ---------------------------------------------------------------------------------------------------------------

struct Settings {
    bool widescreen;
    uint32_t avRegion; // raw XC_FACTORY_AV_REGION-style value: 1 = NTSC-M, 3 = PAL-I
    bool perfLog;      // see PerfLog in the file this writes
    bool dumpFiles;    // see DumpFiles in the file this writes
    uint32_t language; // raw XC_LANGUAGE-style value: 1=English,2=Japanese,3=German,4=French,5=Spanish,6=Italian
    int fpsOverride;   // 0 = "unset" - callers fall back to their own region-based default
    bool reverb;         // xaudio2 backend only: run the I3DL2 reverb send. On by default.
    bool mouseLook;        // see engine/mouseLook.h
    float mouseSensitivity;
    bool mouseInvertY;
    bool mouseZoomToggle;
    char discPath[240];  // where the game's disc data lives; see common/xboxPath.cpp
};

static Settings g_settings;
static bool g_settingsLoaded = false;

static void WriteDefaultSettingsFile() {
    FILE *file = fopen(SETTINGS_FILE, "w");
    if (file == NULL)
        return;

    fprintf(file,
        "; 007: Nightfire settings - replaces the Xbox dashboard's EEPROM-stored options.\n"
        "; Edit and save, then restart the game for changes to take effect.\n"
        "\n"
        "[Settings]\n"
        "\n"
        "; 0 = 4:3, 1 = 16:9\n"
        "Widescreen=0\n"
        "\n"
        "; Render resolution does not affect UI scale, UI is referenced to 640x480 still\n"
        "RenderWidth=640\n"
        "RenderHeight=480\n"
        "\n"
        "; NTSC or PAL - also picks which of two English text variants the game uses (a real regional\n"
        "; product tie-in: the \"Stunner\" gadget is rebranded as a Philips-brand shaver outside NTSC/US).\n"
        "; If background movies stay black for you, try switching this.\n"
        "Region=PAL\n"
        "\n"
        "; English, Japanese, German, French, Spanish, or Italian\n"
        "Language=English\n"
        "\n"
        "; Target frame rate. 0 = use the region default (60 for NTSC, 50 for PAL)\n"
        "FPS=0\n"
        "\n"
        "; Native audio (action.exe) only: reverb on 3D sounds. The Xbox ran this on its audio DSP; the room here is an\n"
        "; approximation, since the game never sets the room parameters and nothing local can reproduce them.\n"
        "Reverb=on\n"
        "\n"
        "; Mouse look, which the game itself has no idea about. Click in the window during play to\n"
        "; capture the pointer, Escape (or anything that opens a menu) to let it go again.\n"
        "MouseLook=on\n"
        "\n"
        "; Multiplier on the default mouse speed, which is about a five inch sweep per 360 degrees\n"
        "; on an 800 DPI mouse. Larger is faster.\n"
        "MouseSensitivity=1.0\n"
        "\n"
        "; Whether moving the mouse away from you looks down instead of up.\n"
        "MouseInvertY=off\n"
        "\n"
        "; The right mouse button zooms: hold = zoomed while it is held down, toggle = each click zooms\n"
        "; in or back out. (With the game's own Aim option set to toggle, the button toggles either way.)\n"
        "MouseZoom=hold\n"
        "\n"
        "; Where the game's disc data lives - the folder containing eurocom\\filesys.d00 and the rest. This is\n"
        "; what the Xbox's D: drive resolves to. Relative paths are relative to this executable's folder.\n"
        "DiscPath=../disc\n"
        "\n"
        "; Developer option: save a copy of every file the game loads, as loaded from the disc's archives,\n"
        "; under dump\\ (the action engine) and dump_driving\\ (the driving engine). Slow, and uses a lot of space.\n"
        "DumpFiles=off\n"
    );

    fclose(file);
}

static void TrimInPlace(char *s) {
    // Trailing whitespace/newline
    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len - 1] <= ' ')
        s[--len] = '\0';

    // Leading whitespace
    char *start = s;
    while (*start != '\0' && (unsigned char)*start <= ' ')
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
}

static void LoadSettingsFile() {
    // Defaults, used for anything the file doesn't mention (or if it can't be read/created at all)
    g_settings.widescreen = false;
    g_settings.avRegion = 3; // PAL-I - see the block comment above for why
    g_settings.language = 1; // English
    g_settings.fpsOverride = 0;
    g_settings.reverb = true;
    g_settings.mouseLook = true;
    g_settings.mouseSensitivity = 1.0f;
    g_settings.mouseInvertY = false;
    g_settings.mouseZoomToggle = false;
    g_settings.dumpFiles = false;
    strncpy(g_settings.discPath, "../disc", sizeof(g_settings.discPath) - 1);
    g_settings.discPath[sizeof(g_settings.discPath) - 1] = '\0';

    FILE *file = fopen(SETTINGS_FILE, "r");
    if (file == NULL) {
        WriteDefaultSettingsFile();
        file = fopen(SETTINGS_FILE, "r");
        if (file == NULL)
            return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *comment = strpbrk(line, ";#");
        if (comment != NULL)
            *comment = '\0';

        char *equals = strchr(line, '=');
        if (equals == NULL)
            continue;

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        TrimInPlace(key);
        TrimInPlace(value);

        if (_stricmp(key, "Widescreen") == 0) {
            g_settings.widescreen = atoi(value) != 0;
        } else if (_stricmp(key, "Region") == 0) {
            // Explicit "NTSC" is honoured; anything else (including a typo) falls back to PAL, matching
            // this file's own default - see the block comment above for why that's PAL rather than NTSC.
            g_settings.avRegion = (_stricmp(value, "NTSC") == 0) ? 1 : 3;
        } else if (_stricmp(key, "Language") == 0) {
            if (_stricmp(value, "Japanese") == 0) g_settings.language = 2;
            else if (_stricmp(value, "German") == 0) g_settings.language = 3;
            else if (_stricmp(value, "French") == 0) g_settings.language = 4;
            else if (_stricmp(value, "Spanish") == 0) g_settings.language = 5;
            else if (_stricmp(value, "Italian") == 0) g_settings.language = 6;
            else g_settings.language = 1; // English, also the fallback for anything unrecognised
        } else if (_stricmp(key, "FPS") == 0) {
            g_settings.fpsOverride = atoi(value);
        } else if (_stricmp(key, "PerfLog") == 0) {
            g_settings.perfLog = (_stricmp(value, "on") == 0 || _stricmp(value, "1") == 0);
        } else if (_stricmp(key, "DumpFiles") == 0) {
            g_settings.dumpFiles = (_stricmp(value, "on") == 0 || _stricmp(value, "1") == 0);
        } else if (_stricmp(key, "Reverb") == 0) {
            g_settings.reverb = !(_stricmp(value, "off") == 0 || _stricmp(value, "0") == 0);
        } else if (_stricmp(key, "MouseLook") == 0) {
            g_settings.mouseLook = !(_stricmp(value, "off") == 0 || _stricmp(value, "0") == 0);
        } else if (_stricmp(key, "MouseSensitivity") == 0) {
            // Clamped rather than trusted: a zero or a typo'd negative would silently look like the mouse
            // not working at all, and a huge one like the view having come loose.
            float sensitivity = (float)atof(value);
            if (sensitivity < 0.05f) sensitivity = 0.05f;
            if (sensitivity > 20.0f) sensitivity = 20.0f;
            g_settings.mouseSensitivity = sensitivity;
        } else if (_stricmp(key, "MouseInvertY") == 0) {
            g_settings.mouseInvertY = (_stricmp(value, "on") == 0 || _stricmp(value, "1") == 0);
        } else if (_stricmp(key, "MouseZoom") == 0) {
            g_settings.mouseZoomToggle = (_stricmp(value, "toggle") == 0);
        } else if (_stricmp(key, "DiscPath") == 0) {
            if (value[0] != '\0') {
                strncpy(g_settings.discPath, value, sizeof(g_settings.discPath) - 1);
                g_settings.discPath[sizeof(g_settings.discPath) - 1] = '\0';
            }
        }
    }

    fclose(file);
}

static Settings *GetSettings() {
    if (!g_settingsLoaded) {
        LoadSettingsFile();
        g_settingsLoaded = true;
    }
    return &g_settings;
}

// AUTOINJECT
uint32_t GetParentalControlSettings(void) {
    return 0; // No restrictions
}

// Real signature/behaviour: see the block comment above.
//
// AUTOINJECT
uint32_t LanguageNVSetting(void) {
    return GetSettings()->language;
}

// Real signature/behaviour: see the block comment above.
//
// AUTOINJECT
unsigned char XboxGetAVRegion(void) {
    return (unsigned char)GetSettings()->avRegion;
}

// Real signature/behaviour: see the block comment above. Only bit 0 (widescreen) is driven by settings.ini
// here - the original's other bits (720p/1080i/letterbox) are about physical AV-pack capability and TV
// broadcast standard, neither of which has a PC equivalent worth emulating; this game runs at a fixed
// internal resolution regardless (SCREEN_WIDTH/SCREEN_HEIGHT, used directly by the reimplemented
// xboxInitGraphics and by the remaining width/height patches in Inject()).
//
// AUTOINJECT
uint32_t GetVideoMode(void) {
    return GetSettings()->widescreen ? 1 : 0;
}

// Real signature/behaviour: see the block comment above. Always reports plain stereo - nothing in
// settings.ini drives this, and the original's AV-pack-based mono/surround overrides don't apply on PC.
//
// AUTOINJECT
uint32_t GetAudioMode(void) {
    return 0;
}

int Settings_GetFPSOverride(void) {
    return GetSettings()->fpsOverride;
}

// GraphicsBackend and AudioBackend were settings.ini keys once, when the game could also run under CXBX's HLE.
// The native backends are the only ones now; a file that still has those keys is fine, since unknown keys are
// ignored.

bool Settings_GetReverbEnabled(void) {
    return GetSettings()->reverb;
}

bool Settings_GetPerfLog(void) {
    return GetSettings()->perfLog;
}

bool Settings_GetDumpFiles(void) {
    return GetSettings()->dumpFiles;
}

bool Settings_GetMouseLook(void) {
    return GetSettings()->mouseLook;
}

float Settings_GetMouseSensitivity(void) {
    return GetSettings()->mouseSensitivity;
}

bool Settings_GetMouseInvertY(void) {
    return GetSettings()->mouseInvertY;
}

bool Settings_GetMouseZoomToggle(void) {
    return GetSettings()->mouseZoomToggle;
}

const char *Settings_GetDiscPath(void) {
    return GetSettings()->discPath;
}
