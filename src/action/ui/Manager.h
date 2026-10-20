#ifndef MANAGER_H_
#define MANAGER_H_

#include "../actionhelpers.h"

int Manager_SendMessage(M_MANAGER *param_1, MessageType msgType, int param_3, int param_4);

// MessageType_GoPage's flags
#define GOPAGE_NO_HISTORY   1   // the page left is not put on the history
#define GOPAGE_OVERLAY      2   // the page left stays shown under the new one, which plays no movie
#define GOPAGE_KEEP_HISTORY 4   // going to P_MAIN keeps the history
#define GOPAGE_QUIET        8   // no PageLeave and PageEnter (a page come back to): PageResume
#define GOPAGE_NO_RESUME    0x10


// XBE_GLOBAL(0x0025f1d0, 0x938)
#define manager ((M_MANAGER *)0x0025f1d0)

#endif // MANAGER_H_