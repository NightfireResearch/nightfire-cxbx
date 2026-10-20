#ifndef UI_COMBO_H
#define UI_COMBO_H

#include "Controls.h"

void Combo_Init(M_COMBO *combo, NEW_CONTROL *create, M_CONTROL *parent);
int Combo_SendMessage(M_COMBO *combo, uint message, int arg1, int arg2);
void Combo_Update(M_MANAGER *mgr, M_PAGE *page, M_COMBO *combo);

#endif // UI_COMBO_H
