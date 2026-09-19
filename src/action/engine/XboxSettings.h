#ifndef XBOXSETTINGS_H_
#define XBOXSETTINGS_H_

#include <stdint.h>

// These five all originally read the Xbox's EEPROM via ExQueryNonVolatileSetting - real hardware's
// dashboard-configured region/language/display/audio/parental-control settings, backed on our end by
// CXBX's own emulation of that EEPROM. All five are now sourced from a plain "settings.ini" in the
// working directory instead - see the block comment above LoadSettingsFile in XboxSettings.cpp.
uint32_t GetParentalControlSettings(void);
uint32_t LanguageNVSetting(void);
unsigned char XboxGetAVRegion(void);
uint32_t GetVideoMode(void);
uint32_t GetAudioMode(void);

// FPS override from settings.ini's [Settings] FPS key. 0 means "not set" - callers should fall back to
// their own region-appropriate default (mainloop does: 60 for NTSC-ish regions, 50 for PAL-I).
int Settings_GetFPSOverride(void);

// settings.ini's [Settings] GraphicsBackend key: 0 = "cxbx" (D3D8 through CXBX's HLE, the default), 1 = "d3d9"
// (the seam's own native Direct3D 9 backend, see Direct3D/d3d9Backend.h).
int Settings_GetGraphicsBackend(void);

// settings.ini's [Settings] AudioBackend key: 0 = "cxbx" (DSOUND through CXBX's HLE, the default), 1 = "xaudio2"
// (the audio seam's own native backend - not written yet, see sound/dsndSeam.h).
int Settings_GetAudioBackend(void);

// settings.ini's [Settings] Reverb key, for the xaudio2 backend only: whether to run the I3DL2 reverb
// send on 3D voices. On by default. Worth being able to turn off, because the room it uses is an
// approximation - the game never sets the room parameters, and CXBX never implemented reverb at all, so
// there is nothing locally to check it against beyond listening with it on and off.
bool Settings_GetReverbEnabled(void);

// Whether to print the periodic frame-time and streaming-I/O summary. See PerfLog in settings.ini.
bool Settings_GetPerfLog(void);

// settings.ini's [Settings] DiscPath key: the host folder the Xbox D: drive resolves to, i.e. the one holding
// eurocom\filesys.d00. Defaults to "../disc", relative to the working directory the executables run in. See
// engine/XboxPaths.h for the drive-letter mapping this feeds.
const char *Settings_GetDiscPath(void);

#endif // XBOXSETTINGS_H_
