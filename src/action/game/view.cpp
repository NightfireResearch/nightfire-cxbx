#include "view.h"

// AUTOGEN
void View_SetDrawInViews(obj_tag* o, short views);

// Visible to everyone
// AUTOINJECT
void View_SetDrawInAllViews(obj_tag* o) {
    View_SetDrawInViews(o, 0x1f);
}

// Visible to nobody
// AUTOINJECT
void View_SetDrawInNoViews(obj_tag* o) {
    View_SetDrawInViews(o, 0);
}

// Visible to everyone except the specified player (eg 3rd person view of a gun model)
// AUTOINJECT
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum) {
    char views = 0x1f & (~(1 << (playerNum & 0x1f)));
    View_SetDrawInViews(param_1, views);
}

// Visible to the specified player only (eg 1st person view of a gun model)
// AUTOINJECT
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum) {
    char views = (1 << (playerNum & 0x1f));
    View_SetDrawInViews(param_1, views);
}