#ifndef UI_LIST_H
#define UI_LIST_H

#include "Controls.h"

void List_Init(M_LIST *list, NEW_CONTROL *create, M_CONTROL *parent);
int List_SendMessage(M_LIST *list, uint message, int arg1, int arg2);
void List_Update(M_MANAGER *mgr, M_PAGE *page, M_LIST *list);

int _List_GetIndex(M_LIST *list, LIST_ROW *row);
void List_GetIndex(void);   // the original's register arguments, for _List_GetIndex
LIST_ROW* _List_SetIndex(M_LIST *list, int index);
void List_SetIndex(void);   // the original's register arguments, for _List_SetIndex
int List_GetIndex_RealItem(M_LIST *list, LINKEDLIST *rows, LIST_ROW *row);
LIST_ROW* List_SetIndex_RealItem(M_LIST *list, LINKEDLIST *rows, int index);
void List_ShowSelected(M_LIST *list);

#endif // UI_LIST_H
