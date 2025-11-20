#ifndef VIEW_H_
#define VIEW_H_

#include "../actionhelpers.h"

void View_SetDrawInAllViews(obj_tag* o);
void View_SetDrawInNoViews(obj_tag* o);
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum);
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum);
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix);
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix);
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale);
void View_CaptureScene(viewer_tag* viewer);
void View_AddSkyObj(ushort param_1,celglist_tag *param_2,_VECTOR *param_3,_VECTOR *param_4,obj_tag *param_5,char param_6,ushort param_7,ushort param_8,char param_9);

#endif // VIEW_H_