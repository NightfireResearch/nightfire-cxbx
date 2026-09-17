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

#endif // XBOXSETTINGS_H_
