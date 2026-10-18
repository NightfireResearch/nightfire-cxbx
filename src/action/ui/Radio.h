#ifndef UI_RADIO_H
#define UI_RADIO_H

#include "Controls.h"

void Radio_Init(M_RADIO *radio, NEW_CONTROL *create, M_CONTROL *parent);
int Radio_SendMessage(M_RADIO *radio, uint message, int arg1, int arg2);
void Radio_Update(M_MANAGER *mgr, M_PAGE *page, M_RADIO *radio);

int _Radio_GetIndex(M_RADIO *radio, LIST_ROW *item);
void Radio_GetIndex(void);  // the original's register arguments, for _Radio_GetIndex
LIST_ROW* _Radio_SetIndex(M_RADIO *radio, int index);
void Radio_SetIndex(void);  // the original's register arguments, for _Radio_SetIndex

#endif // UI_RADIO_H
