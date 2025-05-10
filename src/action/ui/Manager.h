#ifndef MANAGER_H_
#define MANAGER_H_

#include "../actionhelpers.h"

int Manager_SendMessage(M_MANAGER *param_1, MessageType msgType, int param_3, int param_4);


#define manager ((M_MANAGER *)0x0025f1d0)

#endif // MANAGER_H_