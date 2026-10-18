#include "Sort.h"

#include "../engine/Collide.h"
#include "../engine/Text.h"
#include "../engine/Vision.h"
#include "../engine/viewer.h"
#include "../game/obj/GT.h"
#include "../game/obj/Light.h"
#include "../gfx/Sprite.h"

#include <string.h>

// The order QuickSort sorts in: -1 to put a before b, 1 after, 0 for either. A NaN compares as neither less nor more
// unless said.

// The elements of a Compare_MenuCtrl sort (Menu_FindControl's neighbours, ui/Menu.cpp): a short at 4, the cost
struct MenuCtrlKey {
    char _unknown0[4];
    short order;
};

// Sprites by maybeEnabled (as a signed byte), the highest first
// AUTOINJECT
int CompSpr(const void *a, const void *b) {
    char x = (char)(*(sprite *const *)a)->maybeEnabled;
    char y = (char)(*(sprite *const *)b)->maybeEnabled;
    if (x > y)
        return -1;
    return x < y;
}

// Lights by distanceShare, the nearest first
// AUTOINJECT
int CompLight(const void *a, const void *b) {
    float x = (*(light_tag *const *)a)->distanceShare;
    float y = (*(light_tag *const *)b)->distanceShare;
    if (x > y)
        return 1;
    if (x < y)
        return -1;
    return 0;
}

// Hits by distance, the nearest first
// AUTOINJECT
int CompHitData(const void *a, const void *b) {
    float x = (*(HITDATA_tag *const *)a)->dist;
    float y = (*(HITDATA_tag *const *)b)->dist;
    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

// Boxes by where the ray enters them, the nearest first; a NaN sorts after
// AUTOINJECT
int CompBoxList(const void *a, const void *b) {
    if ((*(BOXENTRY_tag *const *)a)->t < (*(BOXENTRY_tag *const *)b)->t)
        return -1;
    return 1;
}

// Cels to draw by their depth in portals, the shallowest first
// AUTOINJECT
int CompDrawCels(const void *a, const void *b) {
    ushort x = ((const VisCel *)a)->depth;
    ushort y = ((const VisCel *)b)->depth;
    if (x < y)
        return -1;
    return x > y;
}

// Draw entries by key
// AUTOINJECT
int CompAlphaObj(const void *a, const void *b) {
    float x = ((const DrawEntry *)a)->key;
    float y = ((const DrawEntry *)b)->key;
    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

// Message lines by priority, the lowest first
// AUTOINJECT
int CompMsgTxt(const void *a, const void *b) {
    char x = (*(TXT_MSG *const *)a)->priority;
    char y = (*(TXT_MSG *const *)b)->priority;
    if (x < y)
        return -1;
    return x > y;
}

// Targets by the square of their distance, the nearest first
// AUTOINJECT
int CompTargetObj(const void *a, const void *b) {
    float x = ((const TARGETOBJ *)a)->distSq;
    float y = ((const TARGETOBJ *)b)->distSq;
    if (x < y)
        return -1;
    if (x > y)
        return 1;
    return 0;
}

// AUTOINJECT
int CompMenuCtrl(const void *a, const void *b) {
    short x = ((const MenuCtrlKey *)a)->order;
    short y = ((const MenuCtrlKey *)b)->order;
    if (x < y)
        return -1;
    return x > y;
}

// Strings in place, by strcmp
// AUTOINJECT
int CompStrings(const void *a, const void *b) {
    int order = strcmp((const char *)a, (const char *)b);
    if (order < 0)
        return -1;
    return order > 0;
}

// XBE_GLOBAL(0x00279160, 0x4)
static CompareFunc Comp;

static void Swap(char *a, char *b, uint size) {
    if (a == b)
        return;
    for (; size != 0; size--, a++, b++) {
        char t = *a;
        *a = *b;
        *b = t;
    }
}

// Sorts count elements of size bytes in place, compared as compareType says (or by custom): the C runtime's
// quicksort, the middle element as the pivot, the smaller part sorted first and the larger kept on a stack
// AUTOINJECT
void QuickSort(void *list, uint count, uint size, COMP_FUNC compareType, CompareFunc custom) {
    switch (compareType) {
    case Compare_Sprites: Comp = CompSpr; break;
    case Compare_HitData: Comp = CompHitData; break;
    case Compare_DrawCels: Comp = CompDrawCels; break;
    case Compare_AlphaObj: Comp = CompAlphaObj; break;
    case Compare_MsgTxt: Comp = CompMsgTxt; break;
    case Compare_TargetObj: Comp = CompTargetObj; break;
    case Compare_MenuCtrl: Comp = CompMenuCtrl; break;
    case Compare_Strings: Comp = CompStrings; break;
    case Compare_BoxList: Comp = CompBoxList; break;
    case Compare_CustomFunction: Comp = custom; break;
    default: Comp = CompLight; break;
    }
    if (count < 2 || size == 0)
        return;

    char *loStack[30], *hiStack[30];
    int stack = 0;
    char *lo = (char *)list;
    char *hi = lo + (count - 1) * size;
    for (;;) {
        Swap(lo + ((uint)(hi - lo) / size + 1) / 2 * size, lo, size);
        char *left = lo;
        char *right = hi + size;
        for (;;) {
            do
                left += size;
            while (left <= hi && Comp(left, lo) <= 0);
            do
                right -= size;
            while (right > lo && Comp(right, lo) >= 0);
            if (right < left)
                break;
            Swap(left, right, size);
        }
        Swap(lo, right, size);

        if (right - lo - 1 >= hi - left) {
            if (lo + size < right) {
                loStack[stack] = lo;
                hiStack[stack] = right - size;
                stack++;
            }
            if (left < hi) {
                lo = left;
                continue;
            }
        } else {
            if (left < hi) {
                loStack[stack] = left;
                hiStack[stack] = hi;
                stack++;
            }
            if (lo + size < right) {
                hi = right - size;
                continue;
            }
        }
        if (--stack < 0)
            return;
        lo = loStack[stack];
        hi = hiStack[stack];
    }
}
