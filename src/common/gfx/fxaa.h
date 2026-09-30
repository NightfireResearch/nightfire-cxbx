#ifndef COMMON_GFX_FXAA_H_
#define COMMON_GFX_FXAA_H_

// Experimental FXAA on the 3D scene, before the HUD and text are drawn over it (see AntiAliasBeforeOverlay in
// d3d9Backend.cpp). Built into both engines when NF_ANTIALIASING is defined (CMakeLists.txt); it is
// AntiAliasing=1 in settings.ini, the default. See fxaa.cpp.

#include <d3d9.h>

typedef void (*FxaaLogFn)(const char *fmt, ...);

// Anti-aliases the backbuffer in place. Call outside BeginScene/EndScene. Leaves every device state as it
// found it. The first failure is logged and FXAA stays off for the rest of the session.
void Fxaa_Apply(IDirect3DDevice9 *device, IDirect3DSurface9 *backBuffer, FxaaLogFn log);

// The copy of the frame lives in the default pool, so it has to go before a device Reset.
void Fxaa_ReleaseDefaultPool(void);

#endif // COMMON_GFX_FXAA_H_
