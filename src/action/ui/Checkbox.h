#ifndef UI_CHECKBOX_H
#define UI_CHECKBOX_H

#include "Controls.h"

void Checkbox_Init(M_CHECKBOX *checkbox, NEW_CONTROL *create, M_CONTROL *parent);
int Checkbox_SendMessage(M_CHECKBOX *checkbox, uint message, int arg1, int arg2);
void Checkbox_Update(M_MANAGER *mgr, M_PAGE *page, M_CHECKBOX *checkbox);

#endif // UI_CHECKBOX_H
