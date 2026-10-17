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
SPRITE_DRAW * View_AddSprite(ushort someNum);
void View_AddObjects(viewer_tag *viewer, cel_tag *cel);
void View_AddCels(viewer_tag *viewer);
void View_AddForcedObjects(viewer_tag *viewer);
void View_SetupRenderModes_Tweak(viewer_tag *viewer, obj_tag *obj);
void View_SetupRenderModes(viewer_tag *viewer, obj_tag *obj);
void View_SetupRenderModes2(viewer_tag *viewer, obj_tag *obj);
void View_DrawCelGlist(cel_tag *cel, viewer_tag *viewer);
void View_DrawObjectGlist(obj_tag *obj, viewer_tag *viewer);
void View_DrawObjList(viewer_tag *viewer, ushort list);
void View_DrawSky(viewer_tag *viewer, char pass);
void __stdcall Game_Draw(void);
short View_3DPoint2Screen(_VECTOR *vecIn, _VECTOR *vecOut, ushort viewerNum);

#endif // VIEW_H_