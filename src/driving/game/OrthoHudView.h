#ifndef DRIVING_GAME_ORTHOHUDVIEW_H_
#define DRIVING_GAME_ORTHOHUDVIEW_H_

// ---------------------------------------------------------------------------------------------------------------
// The HUD's view (Ghidra: GOrthoHudView, vtable 0x0019ff84): an RViewCamera in device transform mode that GHud's
// constructor makes (fHudView) and GHud::Render renders. DoRender draws the HUD through a view matrix scaled by the
// screen's height over 480; PostRender sends EExit and ERestart when the HUD asks for them. See OrthoHudView.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../camera/Camera.h"             // RViewCamera, EAGL::ViewPort

class GOrthoHudView : public RViewCamera {
public:
    // vtable slot 2
    void PreRender();                                                                           // 0x000d7bf0
    // vtable slot 3
    void PostRender();                                                                          // 0x000daac0
    // vtable slot 4
    void DoRender();                                                                            // 0x000e1ce0
};
static_assert(sizeof(GOrthoHudView) == sizeof(RViewCamera), "GOrthoHudView adds no fields");

extern EAGL::ViewPort *HudViewPort;     // DoRender's view port, kept (read by nothing)

#endif // DRIVING_GAME_ORTHOHUDVIEW_H_
