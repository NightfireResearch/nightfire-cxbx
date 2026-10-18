#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "Controls.h"

void Button_Init(M_BUTTON *button, NEW_CONTROL *create, M_CONTROL *parent);
int Button_SendMessage(M_BUTTON *button, uint message, int arg1, int arg2);
void Button_Update(M_MANAGER *mgr, M_PAGE *page, M_BUTTON *button);

#endif // UI_BUTTON_H
