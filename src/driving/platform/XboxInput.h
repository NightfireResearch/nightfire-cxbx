#ifndef DRIVING_PLATFORM_XBOXINPUT_H_
#define DRIVING_PLATFORM_XBOXINPUT_H_

#include "../../common/standalone.h"

// Replaces XAPI's controller API with Win32's XInput, so that IOModule reads real pads. See XboxInput.cpp for
// the translation between the two, and for what is deliberately not implemented. Standalone only: called from
// Inject().
void Inject_XboxInput(void);

#endif // DRIVING_PLATFORM_XBOXINPUT_H_
