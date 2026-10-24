#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "Controls.h"

void Button_Init(M_BUTTON *button, NEW_CONTROL *create, M_CONTROL *parent);
intptr_t Button_SendMessage(M_BUTTON *button, uint message, intptr_t arg1, intptr_t arg2);
void Button_Update(M_MANAGER *mgr, M_PAGE *page, M_BUTTON *button);

#endif // UI_BUTTON_H
