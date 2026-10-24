#ifndef COMMON_RENDERWINDOW_H_
#define COMMON_RENDERWINDOW_H_

// The window class the loader creates its render window with.
//
// Shared because two sides have to agree on it and neither owns it: the loader creates the window and pumps its
// messages (src/loader/loadermain.cpp), while the injected DLL finds it - the D3D9 backend to present into,
// and the input layer to decide whether the game has focus.
#define NIGHTFIRE_RENDER_WINDOW_CLASS "NightfireRender"

#endif // COMMON_RENDERWINDOW_H_
