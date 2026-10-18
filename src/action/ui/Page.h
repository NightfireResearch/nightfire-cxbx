#ifndef UI_PAGE_H
#define UI_PAGE_H

#include "ui.h"

void Page_Init(M_PAGE *page, HASHCODE hashcode, M_MANAGER *mgr);
int Page_SendMessage(M_PAGE *page, uint message, int arg1, int arg2);
void Page_Update(M_MANAGER *mgr, M_PAGE *page, int takingInput);
void Page_FindHelpControl(M_PAGE *page);
void Page_SetHelpText(M_PAGE *page);

#endif // UI_PAGE_H
