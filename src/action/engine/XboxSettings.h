#ifndef XBOXSETTINGS_H_
#define XBOXSETTINGS_H_

#include <stdint.h>

// These five all originally read the Xbox's EEPROM via ExQueryNonVolatileSetting - real hardware's
// dashboard-configured region/language/display/audio/parental-control settings. All five are now
// sourced from a plain "settings.ini" in the working directory instead - see the block comment above LoadSettingsFile in XboxSettings.cpp.
uint32_t GetParentalControlSettings(void);
uint32_t LanguageNVSetting(void);
unsigned char XboxGetAVRegion(void);
uint32_t GetVideoMode(void);
uint32_t GetAudioMode(void);

// FPS override from settings.ini's [Settings] FPS key. 0 means "not set" - callers should fall back to
// their own region-appropriate default (mainloop does: 60 for NTSC-ish regions, 50 for PAL-I).
int Settings_GetFPSOverride(void);

// settings.ini's [Settings] Reverb key: whether the XAudio2 backend runs the I3DL2 reverb send on 3D
// voices. On by default. Worth being able to turn off, because the room it uses is an approximation - the
// game never sets the room parameters, and nothing available locally emulates the Xbox's reverb, so there is
// nothing to check it against beyond listening with it on and off.
bool Settings_GetReverbEnabled(void);

// Whether to print the periodic frame-time and streaming-I/O summary. See PerfLog in settings.ini.
bool Settings_GetPerfLog(void);

// settings.ini's [Settings] DumpFiles key: save a copy of every file the game loads, under dump\ (off by
// default). The driving engine reads the same key from the same file (src/driving/devtools/FileDump.cpp).
bool Settings_GetDumpFiles(void);

// settings.ini's [Settings] MouseLook, MouseSensitivity and MouseInvertY keys - see engine/mouseLook.h.
// Sensitivity is a plain multiplier on the default, and is clamped to something usable rather than trusted.
bool Settings_GetMouseLook(void);
float Settings_GetMouseSensitivity(void);
bool Settings_GetMouseInvertY(void);

// settings.ini's [Settings] DiscPath key: the host folder the Xbox D: drive resolves to, i.e. the one holding
// eurocom\filesys.d00. Defaults to "../disc". This is the value as written; a relative one is resolved against
// the executable's folder by Xbox_SetDiscRoot. See common/xboxPath.h for the drive-letter mapping this feeds.
const char *Settings_GetDiscPath(void);

#endif // XBOXSETTINGS_H_
