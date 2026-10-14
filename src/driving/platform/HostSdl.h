#ifndef DRIVING_PLATFORM_HOSTSDL_H_
#define DRIVING_PLATFORM_HOSTSDL_H_

// Between the SDL3 window (HostWindowSdl.cpp) and the SDL3 input (HostInputSdl.cpp), with NIGHTFIRE_SDL.

union SDL_Event;

// An event from the window's loop: pads plugged in or pulled out
void HostInputSdl_Event(const SDL_Event *event);
// Whether the window has the keyboard (true before there is one)
bool HostWindowSdl_HasKeyboard(void);

#endif // DRIVING_PLATFORM_HOSTSDL_H_
