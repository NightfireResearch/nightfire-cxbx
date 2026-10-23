#ifndef VIEW_H_
#define VIEW_H_

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

#pragma pack(push, 1)
// The sky's models (View_AddSkyObj), each a model or an object, and those that flash: lightning, in the sky's
// level of the given pass, at random within its interval for its length, with thunder after
struct SkyObj {
    celglist_tag *glist;
    celglist_tag *flashGlist;   // 0x04
    obj_tag *obj;               // 0x08
    obj_tag *flashObj;          // 0x0c
    _VECTOR pos;                // 0x10
    _VECTOR rot;                // 0x1c
    ushort flashInterval;       // 0x28 - frames
    ushort flashLength;         // 0x2a
    short flashTimer;           // 0x2c
    short countdown;            // 0x2e
    short thunderDelay;         // 0x30
    char pass;                  // 0x32
    char pad33;
};
#pragma pack(pop)
LAYOUT_32(sizeof(SkyObj) == 0x34, "SkyObj is 0x34 bytes");

#define SKY_OBJS 21
extern SkyObj SkyObjs[SKY_OBJS];
// A flash shows this frame; this flash is overhead (Lightning_Update reads both); its thunder is near
extern uint8_t LightningFlash;
extern uint8_t ThunderNear;
extern uint8_t LightningOverhead;
// The frames the sky has turned, on its level 0x0700001b, and where its sun goes
extern uint32_t SkyTurn;
extern _VECTOR SkySunPos;

void View_SetDrawInAllViews(obj_tag* o);
void View_SetDrawInNoViews(obj_tag* o);
void View_SetDrawInViews(obj_tag* o, short views);
void View_SetDrawInOtherViewsOnly(obj_tag* obj, char playerNum);
void View_SetDrawInThisViewOnly(obj_tag *obj, char playerNum);
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix);
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix);
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale);
void View_CaptureScene(viewer_tag* viewer);
void View_AddSkyObj(ushort slot, celglist_tag *glist, _VECTOR *pos, _VECTOR *rot, obj_tag *obj, char flash,
                    ushort interval, ushort length, char pass);
SPRITE_DRAW * View_AddSprite(ushort someNum);
void View_DrawSprites(viewer_tag *viewer, sprite **list, uint first, uint last);
void View_AddObjects(cel_tag *cel, viewer_tag *viewer);
void View_AddCels(viewer_tag *viewer);
void View_AddForcedObjects(viewer_tag *viewer);
void View_SetupRenderModes_Tweak(viewer_tag *viewer, obj_tag *obj);
void View_SetupRenderModes(viewer_tag *viewer, obj_tag *obj);
void View_SetupRenderModes2(viewer_tag *viewer, obj_tag *obj);
void View_DrawCelGlist(viewer_tag *viewer, cel_tag *cel);
void View_DrawObjectGlist(obj_tag *obj, viewer_tag *viewer);
void View_DrawObjList(viewer_tag *viewer, ushort list);
void View_DrawSky(viewer_tag *viewer, char pass);
void __stdcall Game_Draw(void);
void LoadScreen_Draw(void);
short View_3DPoint2Screen(_VECTOR *vecIn, _VECTOR *vecOut, ushort viewerNum);

#endif // VIEW_H_
