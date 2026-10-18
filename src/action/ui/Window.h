#ifndef UI_WINDOW_H
#define UI_WINDOW_H

#include "ui.h"

void Window_Init(M_WINDOW *window, NEW_CONTROL *create, M_MANAGER *mgr);
int Window_SendMessage(M_WINDOW *window, uint message, int arg1, int arg2);
M_CONTROL* Window_Get(M_PAGE *page);

#endif // UI_WINDOW_H
