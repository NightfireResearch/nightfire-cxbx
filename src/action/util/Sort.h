#ifndef SORT_H_
#define SORT_H_

#include "../actionhelpers.h"

typedef int (__cdecl *CompareFunc)(const void *a, const void *b);

void QuickSort(void *list, uint count, uint size, COMP_FUNC compareType, CompareFunc custom);

int CompSpr(const void *a, const void *b);
int CompLight(const void *a, const void *b);
int CompHitData(const void *a, const void *b);
int CompBoxList(const void *a, const void *b);
int CompDrawCels(const void *a, const void *b);
int CompAlphaObj(const void *a, const void *b);
int CompMsgTxt(const void *a, const void *b);
int CompTargetObj(const void *a, const void *b);
int CompMenuCtrl(const void *a, const void *b);
int CompStrings(const void *a, const void *b);

#endif // SORT_H_
