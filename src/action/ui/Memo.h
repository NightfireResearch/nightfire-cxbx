#ifndef UI_MEMO_H
#define UI_MEMO_H

#include "Controls.h"

void Memo_Init(M_MEMO *memo, NEW_CONTROL *create, M_CONTROL *parent);
int Memo_SendMessage(M_MEMO *memo, uint message, int arg1, int arg2);
void Memo_Update(M_MANAGER *mgr, M_PAGE *page, M_MEMO *memo);

#endif // UI_MEMO_H
