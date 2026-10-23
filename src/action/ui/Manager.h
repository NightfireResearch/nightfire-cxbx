#ifndef MANAGER_H_
#define MANAGER_H_

#include "../actionhelpers.h"

intptr_t Manager_SendMessage(M_MANAGER *mgr, MessageType msgType, intptr_t arg1, intptr_t arg2);

// MessageType_GoPage's flags
#define GOPAGE_NO_HISTORY   1   // the page left is not put on the history
#define GOPAGE_OVERLAY      2   // the page left stays shown under the new one, which plays no movie
#define GOPAGE_KEEP_HISTORY 4   // going to P_MAIN keeps the history
#define GOPAGE_QUIET        8   // no PageLeave and PageEnter (a page come back to): PageResume
#define GOPAGE_NO_RESUME    0x10


extern M_MANAGER manager[5];

#endif // MANAGER_H_