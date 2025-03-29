#ifndef VIEW_H_
#define VIEW_H_

#include "../actionhelpers.h"

void View_SetDrawInAllViews(obj_tag* o);
void View_SetDrawInNoViews(obj_tag* o);
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum);
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum);


#endif // VIEW_H_