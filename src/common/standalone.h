#ifndef COMMON_STANDALONE_H_
#define COMMON_STANDALONE_H_

#include <windows.h>

// ---------------------------------------------------------------------------------------------------------------
// Which host is running the game.
//
// Both engines can be run two ways: under CXBX, which emulates the Xbox kernel and replaces the XBE's libraries
// with its own HLE, or under the standalone loader, where the XBE's own code is all there is. A patch that
// replaces something CXBX has already replaced is at best wasted and usually wrong - the startup replacements
// in particular assume no KPCR, no emulated drive mounting and no kernel image to patch - so every one of them
// is conditional on this.
//
// CXBX's emulation lives in cxbxr-emu.dll. Asking whether it is in the process tests the thing that actually
// matters - whether anything else has already replaced the XBE's libraries - rather than a proxy for it such
// as a setting or a command line.
// ---------------------------------------------------------------------------------------------------------------

static inline bool Xbox_RunningStandalone(void) {
    return GetModuleHandleA("cxbxr-emu.dll") == NULL;
}

#endif // COMMON_STANDALONE_H_
