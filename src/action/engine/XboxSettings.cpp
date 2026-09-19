#include "XboxSettings.h"
#include "../actionhelpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_FILE "settings.ini"

// ---------------------------------------------------------------------------------------------------------------
// Plain host settings file, replacing CXBX's emulation of the Xbox EEPROM (region/language/display/audio/
// parental-control settings normally configured once via the Xbox dashboard, read here by the game through
// ExQueryNonVolatileSetting). GetParentalControlSettings/LanguageNVSetting/XboxGetAVRegion/GetVideoMode/
// GetAudioMode below all originally read that EEPROM and now read this file instead, matching how
// psiLaunch.bin/psiSaveData/direct XInput replace their own slices of CXBX elsewhere in this codebase.
//
// Only region/language/widescreen/FPS are actually exposed as settings - everything else (audio mode,
// parental controls) gets a fixed, sensible default, since nothing about them is CXBX-specific/interesting
// to a PC player.
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
// On the machine this was diagnosed on, Region=NTSC left background-movie (FMV) playback permanently black
// (the video decoder never signalled "frame ready", no crash) while Region=PAL played correctly. That is
// NOT evidence that NTSC/60Hz is broken in general - the actual requirement is almost certainly that this
// setting has to agree with whatever CXBX itself is configured/emulating for video timing, and PAL simply
// happened to match that machine's CXBX setup. If FMV breaks after switching a fresh setup to NTSC, try
// PAL, or vice versa, and check CXBX's own video/region configuration rather than assuming one value is
// universally correct. We haven't traced the exact mechanism inside CXBX's own FMV/timing code.
// ---------------------------------------------------------------------------------------------------------------

struct Settings {
    bool widescreen;
    uint32_t avRegion; // raw XC_FACTORY_AV_REGION-style value: 1 = NTSC-M, 3 = PAL-I
    uint32_t language; // raw XC_LANGUAGE-style value: 1=English,2=Japanese,3=German,4=French,5=Spanish,6=Italian
    int fpsOverride;   // 0 = "unset" - callers fall back to their own region-based default
    int graphicsBackend; // 0 = CXBX's D3D8 HLE (default), 1 = the seam's own D3D9 backend (see Direct3D/d3d9Backend.h)
    int audioBackend;    // 0 = CXBX's DSOUND HLE (default), 1 = the audio seam's own native backend (see sound/dsndSeam.h)
    bool reverb;         // xaudio2 backend only: run the I3DL2 reverb send. On by default.
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
        "; NTSC or PAL - also picks which of two English text variants the game uses (a real regional\n"
        "; product tie-in: the \"Stunner\" gadget is rebranded as a Philips-brand shaver outside NTSC/US).\n"
        "; This needs to agree with whatever CXBX itself is configured/emulating for video timing, or\n"
        "; background-movie (FMV) playback can end up permanently black. Defaults to PAL because that's\n"
        "; what matched this project's own CXBX setup during testing - if movies are black for you, try\n"
        "; switching this (and check CXBX's own region/video settings too).\n"
        "Region=PAL\n"
        "\n"
        "; English, Japanese, German, French, Spanish, or Italian\n"
        "Language=English\n"
        "\n"
        "; Target frame rate. 0 = use the region default (60 for NTSC, 50 for PAL)\n"
        "FPS=0\n"
        "\n"
        "; cxbx = render through CXBX's Direct3D 8 emulation (the default), d3d9 = the project's own native\n"
        "; Direct3D 9 backend (work in progress - expect missing rendering while it's being brought up)\n"
        "GraphicsBackend=cxbx\n"
        "\n"
        "; cxbx = play audio through CXBX's DirectSound emulation (the default), xaudio2 = the project's own\n"
        "; native audio backend (work in progress - FMV audio is still played by CXBX either way)\n"
        "AudioBackend=cxbx\n"
        "\n"
        "; xaudio2 backend only: reverb on 3D sounds. The Xbox ran this on its audio DSP; the room here is an\n"
        "; approximation, since the game never sets the room parameters and nothing local can reproduce them.\n"
        "Reverb=on\n"
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
    g_settings.avRegion = 3; // PAL-I - matched this project's own CXBX setup in testing; see block comment above
    g_settings.language = 1; // English
    g_settings.fpsOverride = 0;
    g_settings.graphicsBackend = 0;
    g_settings.audioBackend = 0;
    g_settings.reverb = true;

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
        } else if (_stricmp(key, "GraphicsBackend") == 0) {
            g_settings.graphicsBackend = (_stricmp(value, "d3d9") == 0) ? 1 : 0;
        } else if (_stricmp(key, "AudioBackend") == 0) {
            g_settings.audioBackend = (_stricmp(value, "xaudio2") == 0) ? 1 : 0;
        } else if (_stricmp(key, "Reverb") == 0) {
            g_settings.reverb = !(_stricmp(value, "off") == 0 || _stricmp(value, "0") == 0);
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
    return 0; // No restrictions - CXBX's EEPROM emulation had no meaningful equivalent worth preserving here
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

int Settings_GetGraphicsBackend(void) {
    return GetSettings()->graphicsBackend;
}

int Settings_GetAudioBackend(void) {
    return GetSettings()->audioBackend;
}

bool Settings_GetReverbEnabled(void) {
    return GetSettings()->reverb;
}
