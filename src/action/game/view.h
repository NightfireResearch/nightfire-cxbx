#ifndef VIEW_H_
#define VIEW_H_

#include "../actionhelpers.h"

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
static_assert(sizeof(SkyObj) == 0x34, "SkyObj is 0x34 bytes");

#define SKY_OBJS 21
#define SkyObjs (*(SkyObj (*)[SKY_OBJS])0x0029d7a0)
// A flash shows this frame; this flash is overhead (Lightning_Update reads both); its thunder is near
#define LightningFlash U8_AT(0x0029e81a)
#define ThunderNear U8_AT(0x0029e81b)
#define LightningOverhead U8_AT(0x0029e81c)
// The frames the sky has turned, on its level 0x0700001b, and where its sun goes
#define SkyTurn U32_AT(0x0029e820)
#define SkySunPos (*(_VECTOR *)0x001f6668)

void View_SetDrawInAllViews(obj_tag* o);
void View_SetDrawInNoViews(obj_tag* o);
void View_SetDrawInViews(obj_tag* o, short views);
void View_SetDrawInOtherViewsOnly(obj_tag* param_1, char playerNum);
void View_SetDrawInThisViewOnly(obj_tag *param_1, char playerNum);
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix);
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix);
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale);
void View_CaptureScene(viewer_tag* viewer);
void View_AddSkyObj(ushort slot, celglist_tag *glist, _VECTOR *pos, _VECTOR *rot, obj_tag *obj, char flash,
                    ushort interval, ushort length, char pass);
SPRITE_DRAW * View_AddSprite(ushort someNum);
void View_DrawSprites(viewer_tag *viewer, sprite **list, uint first, uint last);
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