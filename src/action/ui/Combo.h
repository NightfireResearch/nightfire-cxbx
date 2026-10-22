#ifndef UI_COMBO_H
#define UI_COMBO_H

#include "Controls.h"

void Combo_Init(M_COMBO *combo, NEW_CONTROL *create, M_CONTROL *parent);
intptr_t Combo_SendMessage(M_COMBO *combo, uint message, intptr_t arg1, intptr_t arg2);
void Combo_Update(M_MANAGER *mgr, M_PAGE *page, M_COMBO *combo);

#endif // UI_COMBO_H
