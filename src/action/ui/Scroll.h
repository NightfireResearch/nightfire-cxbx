#ifndef UI_SCROLL_H
#define UI_SCROLL_H

#include "Controls.h"

void Scroll_Init(M_SCROLL *scroll, NEW_CONTROL *create, M_CONTROL *parent);
intptr_t Scroll_SendMessage(M_SCROLL *scroll, uint message, intptr_t arg1, intptr_t arg2);
void Scroll_Update(M_MANAGER *mgr, M_PAGE *page, M_SCROLL *scroll);

#endif // UI_SCROLL_H
