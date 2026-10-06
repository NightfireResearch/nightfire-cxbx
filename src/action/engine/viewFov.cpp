#include "viewFov.h"

#include "XboxSettings.h"

// See viewFov.h.

float ViewFov_PlayerFov(void) {
    // Exactly the game's constant at the default, rather than 60 degrees converted, which could differ in the
    // last bit - so FOV=60 is the original, not a near miss of it.
    float degrees = Settings_GetFovDegrees();
    if (degrees == 60.0f)
        return ViewFov_GameFov;
    return degrees * (3.14159265f / 180.0f);
}

float ViewFov_ForViewer(int viewerNum) {
    return viewerNum < 4 ? ViewFov_PlayerFov() : ViewFov_GameFov;
}

float ViewFov_ScopeZoomCorrection(void) {
    return ViewFov_PlayerFov() / ViewFov_GameFov;
}
