#ifndef UI_LABEL_H
#define UI_LABEL_H

#include "Controls.h"

void Label_Init(M_LABEL *label, NEW_CONTROL *create, M_CONTROL *parent);
int Label_SendMessage(M_LABEL *label, uint message, int arg1, int arg2);
void Label_Update(M_MANAGER *mgr, M_PAGE *page, M_LABEL *label, bool cursorOver);

#endif // UI_LABEL_H
