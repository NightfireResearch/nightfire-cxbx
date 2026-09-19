#ifndef COMMON_RENDERWINDOW_H_
#define COMMON_RENDERWINDOW_H_

// The window class the standalone loader creates its render window with.
//
// Shared because two sides have to agree on it and neither owns it: nfloader creates the window and pumps its
// messages (src/loader/loadermain.cpp), while the injected DLL finds it - the D3D9 backend to present into,
// and the input layer to decide whether the game has focus. Under CXBX the window is CXBX's own "CxbxRender"
// and this is not used at all.
#define NIGHTFIRE_RENDER_WINDOW_CLASS "NightfireRender"

#endif // COMMON_RENDERWINDOW_H_
