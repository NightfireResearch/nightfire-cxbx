#ifndef UI_RADIO_H
#define UI_RADIO_H

#include "Controls.h"

void Radio_Init(M_RADIO *radio, NEW_CONTROL *create, M_CONTROL *parent);
intptr_t Radio_SendMessage(M_RADIO *radio, uint message, intptr_t arg1, intptr_t arg2);
void Radio_Update(M_MANAGER *mgr, M_PAGE *page, M_RADIO *radio);

int Radio_GetIndex(M_RADIO *radio, LIST_ROW *item);
LIST_ROW* Radio_SetIndex(M_RADIO *radio, int index);

#endif // UI_RADIO_H
