#ifndef VIEWFOV_H_
#define VIEWFOV_H_

#include <bit>
#include <stdint.h>

#include "../math/math.h"

// ---------------------------------------------------------------------------------------------------------------
// The player's field of view, from settings.ini's FOV key. The game has it fixed at 60 degrees (vertical:
// widescreen gets its extra width at the sides), passed as a literal wherever a player's view is set up -
// Camera_ScreenCoords, the widescreen option (C_CHCHWS_Handler), Car_Activate, getting off an emplacement and,
// in multiplayer, onto one. Those ask ViewFov_PlayerFov instead.
//
// What does not change: the cutscene viewer and its authored angles, the single-player emplacement's zoomed
// 30 degrees, the debug scan mode. And real scopes - weapon flag 0x40, the gun hidden and the overlay shown -
// whose view stays exactly what it was: a weapon's zoom is applied on top of the field of view (the view is drawn
// at fovRadians / zoom), so Player_PositionCamera multiplies a real scope's zoom by ViewFov_ScopeZoomCorrection.
// A weapon whose zoom only brings up the alternative crosshair keeps the wider view, zoomed as before.
//
// With FOV=60, the default, everything is exactly as the game has it.
// ---------------------------------------------------------------------------------------------------------------

// The game's own field of view for every view.
static constexpr float ViewFov_GameFov = (float)(M_PI / 3.0);
static_assert(std::bit_cast<uint32_t>(ViewFov_GameFov) == 0x3f860a92, "not the original's pi/3");

// The player's view, in radians.
float ViewFov_PlayerFov(void);

// For a viewer by number: the player's for the players' viewers (0 to 3), the game's own for the rest.
float ViewFov_ForViewer(int viewerNum);

// What a real scope's zoom is multiplied by so that it shows what it does at the game's own field of view.
float ViewFov_ScopeZoomCorrection(void);

#endif // VIEWFOV_H_
