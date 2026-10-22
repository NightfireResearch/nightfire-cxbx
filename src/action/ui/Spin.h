#ifndef UI_SPIN_H
#define UI_SPIN_H

#include "Controls.h"

void Spin_Init(M_SPIN *spin, NEW_CONTROL *create, M_CONTROL *parent);
intptr_t Spin_SendMessage(M_SPIN *spin, uint message, intptr_t arg1, intptr_t arg2);
void Spin_Update(M_MANAGER *mgr, M_PAGE *page, M_SPIN *spin);

extern char SpinZero[2];

#endif // UI_SPIN_H
