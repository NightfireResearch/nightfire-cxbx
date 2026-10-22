#include "view.h"
#include "../engine/psiSprite.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../engine/viewer.h"
#include "../engine/Direct3D/GraphicsSystem.h"
#include "../engine/Vision.h"
#include "../engine/Camera.h"
#include "../engine/Anim.h"
#include "../engine/Collide.h"
#include "../engine/psiDraw.h"
#include "../engine/psiLight.h"
#include "../engine/psiGraphics.h"
#include "../game.h"
#include "../gfx/Font.h"
#include "../gfx/Sprite.h"
#include "../sound/Sound.h"
#include "../util/Random.h"
#include "../math/X87Ops.h"
#include "obj/Break.h"
#include "obj/Effect.h"
#include "obj/Emitter.h"
#include "obj/Light.h"
#include "obj/RainBox.h"
#include "obj/control.h"
#include "obj/object.h"
#include "../util/Sort.h"
#include <bit>
#include <math.h>

#include <stdio.h>
#include <string.h>

// XBE_GLOBAL(0x0029d7a0, 0x444)
SkyObj SkyObjs[SKY_OBJS];
// XBE_GLOBAL(0x0029e81a, 0x1)
uint8_t LightningFlash;
// XBE_GLOBAL(0x0029e81b, 0x1)
uint8_t ThunderNear;
// XBE_GLOBAL(0x0029e81c, 0x1)
uint8_t LightningOverhead;
// XBE_GLOBAL(0x0029e820, 0x4)
uint32_t SkyTurn;

// The views (a bit per player's) the object is drawn in, and with it an animated object's rigid parts
// AUTOINJECT
void View_SetDrawInViews(obj_tag* o, short views) {
    if (o == NULL)
        return;
    o->displayMask = views;
    if (!(o->transformFlags & TRANSFORM_ANIMATED) || o->animState == NULL)
        return;
    AnimObj *animObj = &o->animState->animObj;
    for (uint i = 0; i < animObj->skin->numDiscrete; i++)
        animObj->discreteObj[i]->displayMask = views;
}

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

// The view's matrix from angles (z, then y, then x) and a position, with FSIN and FCOS. On the x87 the sines and
// cosines stay unrounded, and so does the product of z's sine and y's for the y axis; that product and z's sine times
// y's cosine are stored as floats for the rest.
// AUTOINJECT
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix) {
  Extended cosX = FCos(rotation->x);
  Extended sinX = FSin(rotation->x);
  Extended cosY = FCos(rotation->y);
  Extended sinY = FSin(rotation->y);
  Extended cosZ = FCos(rotation->z);
  Extended sinZ = FSin(rotation->z);

  matrix->m[0] = (float)(cosZ * cosY);
  matrix->m[1] = (float)sinZ;
  matrix->m[2] = (float)-(cosZ * sinY);
  Extended sinZcosY = (float)(sinZ * cosY);
  matrix->m[4] = (float)(sinY * sinX - sinZcosY * cosX);
  matrix->m[5] = (float)(cosZ * cosX);
  Extended sinZsinY = sinZ * sinY;
  Extended sinZsinYStored = (float)sinZsinY;
  matrix->m[6] = (float)(sinZsinY * cosX + cosY * sinX);
  matrix->m[8] = (float)(sinZcosY * sinX + sinY * cosX);
  matrix->m[9] = (float)-(cosZ * sinX);
  matrix->m[10] = (float)(cosY * cosX - sinZsinYStored * sinX);
  matrix->m[0xc] = position->x;
  matrix->m[0xd] = position->y;
  matrix->m[0xe] = position->z;
}

// As View_RotTransMatrix with each axis scaled before it is stored
// AUTOINJECT
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix) {
  Extended cosX = FCos(rotation->x);
  Extended sinX = FSin(rotation->x);
  Extended cosY = FCos(rotation->y);
  Extended sinY = FSin(rotation->y);
  Extended cosZ = FCos(rotation->z);
  Extended sinZ = FSin(rotation->z);
  Extended scaleX = scale->x, scaleY = scale->y, scaleZ = scale->z;

  matrix->m[2] = (float)(-(cosZ * sinY) * scaleX);
  matrix->m[0] = (float)(cosZ * cosY * scaleX);
  matrix->m[1] = (float)(sinZ * scaleX);
  Extended sinZcosY = (float)(sinZ * cosY);
  matrix->m[4] = (float)((sinY * sinX - sinZcosY * cosX) * scaleY);
  Extended sinZsinY = sinZ * sinY;
  Extended sinZsinYStored = (float)sinZsinY;
  matrix->m[6] = (float)((sinZsinY * cosX + cosY * sinX) * scaleY);
  matrix->m[5] = (float)(cosZ * cosX * scaleY);
  matrix->m[8] = (float)((sinZcosY * sinX + sinY * cosX) * scaleZ);
  matrix->m[9] = (float)(-(cosZ * sinX) * scaleZ);
  matrix->m[10] = (float)((cosY * cosX - sinZsinYStored * sinX) * scaleZ);
  matrix->m[0xc] = position->x;
  matrix->m[0xd] = position->y;
  matrix->m[0xe] = position->z;
}

// AUTOINJECT
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale) {
    _MATRIX mtx;
    View_RotTransScaleMatrix(rotation, translation, scale, &mtx);
    psiDrawObjectMatrix(celglist, &mtx);
}

// The views an object is drawn in this pass, a bit each (View_CaptureSceneSub)
// XBE_GLOBAL(0x0029e80c, 0x4)
uint32_t object_display_mask;
// XBE_GLOBAL(0x001dfa18, 0x4)
uint32_t switch_ForceDrawAll;

// XBE_GLOBAL(0x0029dbf0, 0xc)
_VECTOR CamPos;

// Set for each object added to a view (the draw code reads them): its transformFlags' bit 0, and 0
// XBE_GLOBAL(0x0029e814, 0x2)
uint16_t OnlyScaleZ;
// XBE_GLOBAL(0x0029e818, 0x2)
uint16_t FlipScale;
// Set for each object View_DrawObjList draws: its flags' bits 2 and 3
extern uint8_t bCharacterLight;
extern uint8_t bShadowCharacter;
// A codename option (P_CNOPTIONS_Handler): objects that want it flash, with FlashMod, near the view
#define FlashOption U8_AT(0x001fe6dc)
extern float FlashMod;

// The originals, called by address
extern uint8_t GlobalBlur;

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// Cannot autoinject - custom calling convention
// Only used from within View_CaptureScene, so not a problem
// UNINJECTABLE
void View_CaptureSceneSub(byte mask, viewer_tag* viewer) {
    if(viewer == NULL)
        return;

    object_display_mask = (1 << (mask & 0x1f));
    GfxList = NULL;

    for(cel_tag* cel = viewer->world->firstCel; cel != NULL; cel = cel->nextCel) {
        cel->addedToDraw = 0;
    }

    Vision_Init_Portal_Recurse(viewer);

    if(!switch_ForceDrawAll) {
        Vision_Portal_Recurse(viewer);
        vision_GetCamPos(&CamPos);
        return;
    }

    for(cel_tag* cel = viewer->world->firstCel; cel != NULL; cel = cel->nextCel) {
        Vision_AddCelToDraw(cel, 0);
    }

    vision_generate_display_list(viewer);
    vision_GetCamPos(&CamPos);
}

// The next entry of one of a view's draw lists, for an object or a cel. The lists are rings: past the end, the first
// entries are written again.
static DrawEntry *View_AddToList(viewer_tag *viewer, int list, obj_tag *obj, cel_tag *cel) {
    ViewDrawLists &draw = viewer->draw;
    DrawEntry *entry = &draw.lists[list][draw.counts[list]];
    draw.counts[list] = (draw.counts[list] + 1) & (draw.sizes[list] - 1);
    entry->obj = obj;
    entry->cel = cel;
    if (obj != NULL)
        obj->drawnCountdown = 2;
    return entry;
}

// The list an object or a cel is drawn in, by its flags (bit 0: back to front, by a negative key; 0x2000: last)
static int View_ListFor(uint flags, float *sign) {
    int list = 0;
    *sign = 1.0f;
    if (flags & 1) {
        list = 1;
        *sign = -1.0f;
    }
    if (flags & 0x2000) {
        list = 2;
        *sign = 1.0f;
    }
    return list;
}

// An animated object's detail in this view (0 to 255 for 4 x its distance squared over the zoom), for its animation
static void View_SetDetail(obj_tag *obj, viewer_tag *viewer, double distanceSq) {
    AnimState *anim = obj->animState;
    if (anim == NULL)
        return;
    double detail = distanceSq / viewer->zoom * 0.25f;
    if (detail < 0.0)
        detail = 0.0f;
    else if (detail > 255.0f)
        detail = 255.0f;
    uchar which = (uchar)viewer->idx < 3 ? (uchar)viewer->idx : 3;
    anim->animObj.lod[which] = Ftol(detail);
}

// Adds a cel's objects that this view draws and sees: those with a model, particles (transformFlags 0x10) and
// emitters (0x100); not hidden (effectFlags 0x10) or crossing cels (0x8000, View_AddForcedObjects); with 0x400, only
// in night vision (view mode 2). Particles sort 2000 further back.
void _View_AddObjects(cel_tag *cel, viewer_tag *viewer) {
    for (obj_tag *obj = cel->firstObject; obj != NULL; obj = obj->nextInCel) {
        OnlyScaleZ = 0;
        FlipScale = 0;
        if (!(obj->transformFlags & 0x110) && obj->objGraphics == NULL)
            continue;
        if (!(obj->displayMask & object_display_mask))
            continue;
        if (obj->effectFlags & 0x8010)
            continue;
        if (!Vision_InView(&obj->centrePoint, obj->radius, viewer))
            continue;
        if ((obj->effectFlags & 0x400) && viewer->nightVisionRelated != 2)
            continue;

        OnlyScaleZ = obj->transformFlags & TRANSFORM_SCALE_LENGTH_ONLY;
        float sign;
        int list = View_ListFor(obj->effectFlags, &sign);
        if (obj->transformFlags & TRANSFORM_ANIMATED)
            sign = sign * 50.0f;
        DrawEntry *entry = View_AddToList(viewer, list, obj, NULL);
        double distanceSq = Vec_SqDist3D(&obj->centrePoint, &CamPos);
        double key = sign * distanceSq;
        entry->key = (float)key;
        if (obj->transformFlags & 0x10)
            entry->key = (float)(key + 2000.0f);
        View_SetDetail(obj, viewer, distanceSq);
    }
}

// The cel in EAX, the view on the stack (with a second argument the original does not read)
// AUTOLTCG
void __declspec(naked) View_AddObjects(viewer_tag *viewer, cel_tag *cel) {
    _asm {
        push dword ptr [esp + 4]
        push eax
        call _View_AddObjects
        add esp, 8
        ret
    }
}

// The cels to draw this frame into list 0, in order (keys from -90000 up in tens), with their objects; then the cels
// inside each that the view sees, by their distance squared from the eye, and their objects. Inside cels of kind
// 0x8018 are not drawn from a cel of kind 0xc023; those flagged 0x40000 not in view 4's pass (mask 0x10).
void _View_AddCels(viewer_tag *viewer) {
    float key = -90000.0f;
    for (cel_tag *cel = GfxList; cel != NULL; cel = cel->nextVisible) {
        if (cel->glist != NULL && !(cel->glist->applyFlagsToObject & 0x10)) {
            DrawEntry *entry = View_AddToList(viewer, 0, NULL, cel);
            entry->key = key;
            key = key + 10.0f;
        }
        _View_AddObjects(cel, viewer);

        for (cel_tag *inside = cel->childCel; inside != NULL; inside = inside->childCel) {
            if (inside->glist == NULL || (inside->glist->applyFlagsToObject & 0x10))
                continue;
            if ((inside->bitsFromPlacementTag & 0xffff) == 0x8018 &&
                (viewer->someCel->bitsFromPlacementTag & 0xffff) == 0xc023)
                continue;
            if ((inside->flags84 & 0x40000) && object_display_mask == 0x10)
                continue;
            if (!Vision_InView(&inside->sphere, inside->sphereRadius, viewer))
                continue;

            float sign;
            int list = View_ListFor(inside->flags84, &sign);
            DrawEntry *entry = View_AddToList(viewer, list, NULL, inside);
            entry->key = (float)(Vec_SqDist3D(&inside->sphere, &CamPos) * sign);
            _View_AddObjects(inside, viewer);
        }
    }
}

// The view in ESI
// AUTOLTCG
void __declspec(naked) View_AddCels(viewer_tag *viewer) {
    _asm {
        push esi
        call _View_AddCels
        add esp, 4
        ret
    }
}

// The objects that cross cels (effectFlags 0x8000), from the world's list, that this view draws and sees. A drone
// larger than 0.5 is drawn only if a cel it is in is drawn this frame.
// AUTOINJECT
void View_AddForcedObjects(viewer_tag *viewer) {
    for (obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {
        if (!(object_display_mask & obj->displayMask))
            continue;
        if (obj->objGraphics == NULL)
            continue;
        if (!(obj->effectFlags & 0x8000) || (obj->effectFlags & FLAG_HIDDEN))
            continue;
        if (!Vision_InView(&obj->centrePoint, obj->radius, viewer))
            continue;
        if ((obj->effectFlags & 0x400) && viewer->nightVisionRelated != 2)
            continue;

        if (obj->objectType == OBJECTTYPE_DRONE && obj->radius > 0.5f) {
            HITTEST_tag test = {};
            test.type = HITTEST_SPHERE;
            Vec_Copy(&obj->centrePoint, &test.pos);
            test.radius = obj->radius;
            test.cel = obj->inCel;
            ushort count;
            cel_tag **cels = Collide_StraddleCels(&test, &count);
            bool drawn = false;
            for (cel_tag *cel = GfxList; cel != NULL && !drawn; cel = cel->nextVisible) {
                for (ushort i = 0; i < count; i++) {
                    if (cels[i] == cel) {
                        drawn = true;
                        break;
                    }
                }
            }
            if (!drawn)
                continue;
        }

        OnlyScaleZ = obj->transformFlags & TRANSFORM_SCALE_LENGTH_ONLY;
        float sign;
        int list = View_ListFor(obj->effectFlags, &sign);
        if (obj->transformFlags & TRANSFORM_ANIMATED)
            sign = sign * 50.0f;
        DrawEntry *entry = View_AddToList(viewer, list, obj, NULL);
        double distanceSq = Vec_SqDist3D(&obj->centrePoint, &CamPos);
        entry->key = (float)(sign * distanceSq);
        View_SetDetail(obj, viewer, distanceSq);
    }
}

// XBE_GLOBAL(0x0029e800, 0xc)
uint32_t Tots[3];

void _View_CaptureScene(viewer_tag *viewer) {
    viewer->draw.counts[0] = 0;
    viewer->draw.counts[1] = 0;
    viewer->draw.counts[2] = 0;
    if (viewer->enabled && viewer->draw.lists[0]) {

        View_CaptureSceneSub(viewer->idx, viewer);
        _View_AddCels(viewer);
        View_AddForcedObjects(viewer);

        // Log statistics
        Tots[0] += viewer->draw.counts[0];
        Tots[1] += viewer->draw.counts[1];
        Tots[2] += viewer->draw.counts[2]; // This handles first-person weapon models intersecting level geometry. Non-zero value causes camera 5 to be drawn in Game_Draw
    }
}

// AUTOLTCG
void __declspec(naked) View_CaptureScene(viewer_tag* viewer) {
    // The original function is provided with its parameter in EAX, but we need to call our reimplementation
    // which doesn't have custom calling convention
    _asm {
        push eax
        call _View_CaptureScene
        add esp, 4
        ret
    }

}

// An object's colour (tweakR, G, B) and alpha (tweakA) as the draw tweak: the alpha a quarter in night vision with
// effectFlags 0x20000, the colour times its light's (light_related) with 0x800, and times FlashMod when it flashes
// (effectFlags 0x80000 or its model's, with FlashOption, within 4 of the eye).
// AUTOINJECT
void View_SetupRenderModes_Tweak(viewer_tag *viewer, obj_tag *obj) {
    short alpha = obj->tweakA;
    if ((obj->effectFlags & 0x20000) && viewer->nightVisionRelated == 1)
        alpha = alpha / 4;

    bool flashes = (obj->effectFlags & 0x80000) ||
                   (obj->objGraphics != NULL && (obj->objGraphics->applyFlagsToObject & 0x80000));
    if (flashes && FlashOption && Vec_SqDist3D(&viewer->pos, &obj->centrePoint) < 16.0f) {
        double flash = FlashMod;
        if (obj->effectFlags & 0x800) {
            psiSetTweakARGB(alpha, Ftol((int)(obj->tweakR * obj->ambientR) * flash * (1.0f / 255.0f)),
                            Ftol((int)(obj->tweakG * obj->ambientG) * flash * (1.0f / 255.0f)),
                            Ftol((int)(obj->tweakB * obj->ambientB) * flash * (1.0f / 255.0f)));
        } else {
            psiSetTweakARGB(alpha, Ftol((int)obj->tweakR * flash), Ftol((int)obj->tweakG * flash),
                            Ftol((int)obj->tweakB * flash));
        }
    } else if (obj->effectFlags & 0x800) {
        psiSetTweakARGB(alpha, (obj->tweakR * obj->ambientR) >> 8, (obj->tweakG * obj->ambientG) >> 8,
                        (obj->tweakB * obj->ambientB) >> 8);
    } else {
        psiSetTweakARGB(alpha, obj->tweakR, obj->tweakG, obj->tweakB);
    }
}

// An object's tweak and the lights nearest it
// AUTOINJECT
void View_SetupRenderModes(viewer_tag *viewer, obj_tag *obj) {
    if (obj == NULL)
        return;

    View_SetupRenderModes_Tweak(viewer, obj);
    ushort count;
    void **lights = (void **)Lights_CalcClosestLights(&obj->centrePoint, (ushort)obj->effectFlags, &count);
    psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
}

// The gun in the view: the view's player's tweak and the lights within 2 of the eye
// AUTOINJECT
void View_SetupRenderModes2(viewer_tag *viewer, obj_tag *obj) {
    if (obj == NULL || viewer->player == NULL)
        return;

    View_SetupRenderModes_Tweak(viewer, viewer->player);
    struct {
        _VECTOR centre;
        float radius;
    } eye = {viewer->pos, 2.0f};
    ushort count;
    void **lights = (void **)Lights_CalcClosestLights(&eye.centre, (ushort)obj->effectFlags, &count);
    psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
}

// A cel's alpha by its blink (1: falling, 2: rising, at its speed times the frame count; else solid) and the lights
// nearest it
static void View_SetupCelRenderModes(cel_tag *cel) {
    uchar alpha = (uchar)(cel->blink >> 4) * (uchar)GameState.NumFramesUnpaused;
    switch (cel->blink & 3) {
    case 1:
        alpha = 0xff - alpha;
        break;
    case 2:
        break;
    default:
        alpha = 0xff;
        break;
    }
    psiSetTweakARGB(alpha, 0xff, 0xff, 0xff);
    ushort count;
    void **lights = (void **)Lights_CalcClosestLights(&cel->sphere, (ushort)cel->flags84, &count);
    psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
}

// The marks on a cel or an object, each in its object's space when it has one; those flagged 0x400 only in night
// vision
static void View_DrawAttachments(ATTACH_tag *attachment, viewer_tag *viewer) {
    for (; attachment != NULL; attachment = attachment->next) {
        if (attachment->glist == NULL)
            continue;
        if ((attachment->glist->applyFlagsToObject & 0x400) && viewer->nightVisionRelated != 2)
            continue;
        if (attachment->parent != NULL) {
            _MATRIX mtx;
            Mat_Copy(&attachment->matrix, &mtx);
            Concat(&attachment->parent->transformMatrix, &mtx);
            psiDrawObjectMatrix(attachment->glist, &mtx);
        } else {
            psiDrawObjectMatrix(attachment->glist, &attachment->matrix);
        }
    }
}

// A cel's geometry: turned by its rotation (or facing the view, flags84 8), shaken in scale (4), at its position
void _View_DrawCelGlist(viewer_tag *viewer, cel_tag *cel) {
    View_SetupCelRenderModes(cel);
    _MATRIX mtx;
    if (cel->flags84 & 8) {
        Mat_CopyRot(&viewer->mtx, &mtx);
    } else {
        RotMatrixZYX(&cel->rotation, &mtx);
    }
    if (cel->flags84 & 4) {
        float z = (float)(Float_FRand(0.05f) + 1.0f);
        float y = (float)(Float_FRand(0.05f) + 1.0f);
        float x = (float)(Float_FRand(0.05f) + 1.0f);
        Mat_Scale3f(&mtx, &mtx, x, y, z);
    }
    Matrix_SetTrans(&cel->position, &mtx);
    psiDrawObjectMatrix(cel->glist, &mtx);
    View_DrawAttachments(cel->attachments, viewer);
}

// The view in EAX, the cel in ESI
// AUTOLTCG
void __declspec(naked) View_DrawCelGlist(cel_tag *cel, viewer_tag *viewer) {
    _asm {
        push esi
        push eax
        call _View_DrawCelGlist
        add esp, 8
        ret
    }
}

// An object's model in its matrix: or facing the view (effectFlags 8; turned about z alone when it has a z rotation,
// which clears its x and y), at its scale; shaken in scale (4)
// AUTOINJECT
void View_DrawObjectGlist(obj_tag *obj, viewer_tag *viewer) {
    _MATRIX mtx;
    if (obj->effectFlags & 8) {
        if (obj->rotation.z != 0.0f) {
            obj->rotation.y = 0.0f;
            obj->rotation.x = 0.0f;
            RotTransMatrix(&obj->rotation, &CONST_ZERO_VECTOR, &mtx);
            Concat(&viewer->mtx, &mtx);
        } else {
            Mat_CopyRot(&viewer->mtx, &mtx);
        }
        Matrix_SetTrans(Mat_Position(obj->transformMatrix), &mtx);
        if (obj->scale != 1.0f)
            Mat_Scale3f(&mtx, &mtx, obj->scale, obj->scale, obj->scale);
    } else {
        Mat_Copy(&obj->transformMatrix, &mtx);
    }
    if (obj->effectFlags & 4) {
        float z = (float)(Float_FRand(0.05f) + obj->scale);
        float y = (float)(Float_FRand(0.05f) + obj->scale);
        float x = (float)(Float_FRand(0.05f) + obj->scale);
        Mat_Scale3f(&mtx, &mtx, x, y, z);
    }
    psiDrawObjectMatrix(obj->objGraphics, &mtx);
    View_DrawAttachments(obj->attachments, viewer);
}

// One of the view's draw lists, sorted: cels, animated objects, particles (whose drawing is gone, an empty call),
// emitters and models
// AUTOINJECT
void View_DrawObjList(viewer_tag *viewer, ushort list) {
    if (list == 1)
        __profiling_or_debugging_hook_point();
    if (viewer->draw.counts[list] != 0)
        QuickSort(viewer->draw.lists[list], viewer->draw.counts[list], sizeof(DrawEntry), Compare_AlphaObj, NULL);

    for (ushort i = 0; i < viewer->draw.counts[list]; i++) {
        DrawEntry *entry = &viewer->draw.lists[list][i];
        cel_tag *cel = entry->cel;
        obj_tag *obj = entry->obj;
        bCharacterLight = 0;
        bShadowCharacter = 0;
        if (cel != NULL) {
            _View_DrawCelGlist(viewer, cel);
            continue;
        }

        bCharacterLight = (obj->flags >> 2) & 1;
        bShadowCharacter = (obj->flags >> 3) & 1;
        if (obj->transformFlags & TRANSFORM_ANIMATED) {
            AnimObjectDraw(obj, viewer);
            continue;
        }
        View_SetupRenderModes_Tweak(viewer, obj);
        ushort count;
        void **lights = (void **)Lights_CalcClosestLights(&obj->centrePoint, (ushort)obj->effectFlags, &count);
        psiLight_SetLights(lights, count, 1.0f, 1.0f, 1.0f);
        if (obj->transformFlags & 0x10)
            __profiling_or_debugging_hook_point();
        else if (obj->transformFlags & 0x100)
            Emitter_Draw(obj, viewer);
        else
            View_DrawObjectGlist(obj, viewer);
    }
    if (list == 1)
        __profiling_or_debugging_hook_point();
    bCharacterLight = 0;
    bShadowCharacter = 0;
}

#define SKY_TURNING_LEVEL 0x0700001b
#define SKY_SUN_YAW 4.9741883f
#define SKY_SUN_PITCH -0.6632251f
static_assert(std::bit_cast<uint32_t>(SKY_SUN_YAW) == 0x409f2c8d, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(SKY_SUN_PITCH) == 0xbf29c91f, "not the original's constant");

// The sky's models of a pass (the near sky 1, the far 0), when the view's cels show it: fog off while they draw, then
// back on
// AUTOINJECT
void View_DrawSky(viewer_tag *viewer, char pass) {
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    psiSetUpColourBlend(2);
    psiLight_SetLights(NULL, 0, 0.0f, 0.0f, 0.0f);
    psiFog(pass, 0.0f, 10.0f, 0xff, 0, 0);
    if (viewer->draw.skyVisible) {
        LightningFlash = 0;
        for (int i = 0; i < SKY_OBJS; i++) {
            SkyObj *sky = &SkyObjs[i];
            if (sky->pass != pass)
                continue;

            celglist_tag *glist = sky->glist;
            obj_tag *obj = sky->obj;
            if (!GameState.SomeAlternatePauseState) {
                if (sky->thunderDelay > 0) {
                    sky->thunderDelay--;
                    if (sky->thunderDelay == 0)
                        Sound_PlayExt((Action_SFX)(ThunderNear ? 0x5e : 0x5d), 100.0f, 0, 0);
                }
                if (sky->flashTimer > 0) {
                    sky->flashTimer--;
                    if (Rand_Rand(100) > 50) {
                        glist = sky->flashGlist;
                        obj = sky->flashObj;
                        if (ThunderNear == 1)
                            LightningFlash = 1;
                    }
                } else if (sky->flashGlist != NULL || sky->flashObj != NULL) {
                    sky->countdown--;
                    LightningOverhead = 0;
                    if (sky->countdown <= 0) {
                        obj = sky->flashObj;
                        glist = sky->flashGlist;
                        sky->flashTimer = sky->flashLength;
                        sky->countdown = Rand_Rand(sky->flashInterval);
                        double delay;
                        if (Rand_Rand(100) > 50) {
                            ThunderNear = 0;
                            delay = (double)Rand_Rand(Ftol((double)_FRAME_RATE + _FRAME_RATE)) +
                                    (double)_FRAME_RATE * 0.5f;
                        } else {
                            delay = (double)_FRAME_RATE * 0.16666667f;
                            LightningOverhead = 1;
                            ThunderNear = 1;
                        }
                        sky->thunderDelay = Ftol(delay);
                    }
                }
            }

            if (glist != NULL) {
                _MATRIX mtx;
                if (glist->applyFlagsToObject & 8) {
                    Mat_CopyRot(&viewer->mtx, &mtx);
                } else {
                    Mat_CopyRot(&MAT_IDENTITY, &mtx);
                    _MATRIX turn;
                    RotMatrix(&sky->rot, &turn);
                    Concat(&turn, &mtx);
                    if (GameState.CurrentLevelHashcode == SKY_TURNING_LEVEL) {
                        Vec_Spherical_2_Cartesian(&SkySunPos.x, 1000.0f, SKY_SUN_YAW, SKY_SUN_PITCH);
                        if (!GameState.SomeAlternatePauseState)
                            SkyTurn++;
                        _VECTOR angles = {(float)((double)SkyTurn * 0.00069444446f), 0.0f, 0.0f};
                        RotMatrix(&angles, &turn);
                        Concat(&turn, &mtx);
                        ApplyMatrixLVI(&mtx, &SkySunPos);
                    }
                }
                if (glist->applyFlagsToObject & 2)
                    Vec_Copy(Mat_Position(viewer->viewMatrix), Mat_Position(mtx));
                else
                    Vec_Copy(&sky->pos, Mat_Position(mtx));
                psiDrawObjectMatrix(glist, &mtx);
            } else if (obj != NULL) {
                _VECTOR scale = {obj->scale, obj->scale, obj->scale};
                _VECTOR pos;
                Vec_Copy(&obj->position, &pos);
                if (obj->objectType == OBJECTTYPE_CLOUD)
                    pos.y = (float)((double)((int *)obj->extraObjectData)[4] + viewer->viewMatrix.m[13]);
                _MATRIX mtx;
                View_RotTransScaleMatrix(&obj->rotation, &pos, &scale, &mtx);
                psiDrawObjectMatrix(obj->objGraphics, &mtx);
            }
        }
    }
    psiFog(1, 0.0f, 20.0f, 0x64, 0x64, 0x96);
}


// One of the sky's models (at most SKY_OBJS): a model or an object, or with 'flash' one that flashes, for 'length'
// frames at random within 'interval' seconds. An object given is hidden from the ordinary drawing.
// AUTOINJECT
void View_AddSkyObj(ushort slot, celglist_tag *glist, _VECTOR *pos, _VECTOR *rot, obj_tag *obj, char flash,
                    ushort interval, ushort length, char pass) {
    if (slot >= SKY_OBJS)
        return;
    SkyObj *sky = &SkyObjs[slot];
    if (flash) {
        sky->flashObj = obj;
        sky->flashGlist = glist;
        sky->flashInterval = (ushort)(int)((double)interval * _FRAME_RATE);
        sky->flashLength = length;
    } else {
        sky->obj = obj;
        sky->glist = glist;
    }
    sky->flashTimer = 0;
    sky->countdown = 0;
    sky->thunderDelay = 0;
    sky->pass = pass;
    if (obj != NULL)
        obj->effectFlags |= FLAG_HIDDEN;
    if (pos != NULL)
        Vec_Copy(pos, &sky->pos);
    else
        Vec_Zero(&sky->pos);
    if (rot != NULL)
        Vec_Copy(rot, &sky->rot);
    else
        Vec_Zero(&sky->rot);
}


// XBE_GLOBAL(0x0029e810, 0x4)
uint32_t SprCnt;

// A pointer to memory address 0x0029dc00 which contains 64 SPRITE_DRAW instances (not pointers)
// We must preserve the original type for correct code generation
// XBE_GLOBAL(0x0029dc00, 0xc00)
SPRITE_DRAW SprBuffList[64];

static_assert(ARRAY_SIZE(SprBuffList) == 64, "Sprite buffer number of entries incorrect");
static_assert(sizeof(SPRITE_DRAW) == 0x30, "Sprite size incorrect");

// A glyph of __Font_DrawText (gfx/Font.cpp) is the font's sprite template, CharInfo, copied into the slot
// View_AddSprite hands out. So by the next call a slot that holds the template is a glyph, whose texture is then
// known - which is when the font's glyph boxes can go out beside the dumped texture (DumpTextures in settings.ini).
// XBE_GLOBAL(0x001f64b4, 0x30)
SPRITE_DRAW CharInfo;
static int s_pendingGlyph = -1;
static const uint8_t *s_pendingFont = NULL;   // the font current at that call - by the next it may not be

// A sprite's textureIndex selects one of the game's texture records; psiDrawSprites binds one of the record's
// animation frames (count at +0x24, slots from +0x54) through d3dSetTextureStage0, which takes a slot in the
// D3D texture table, Gfx.textures - and a slot's Xbox texture header is the slot itself (see D3DTextureSlotRaw in
// engine/Direct3D/GraphicsSystem.h). Gfx is ours, so the table is not at the game's address any more.
#define TextureRecords         ((const uint8_t *const *)0x002abe80)

// The current font (__Font_DrawText's): its first and last character at +8 and +0xa, the number of glyphs at
// +0xc, and at +0x10 a table of 24-byte glyph records sorted by character, whose layout Font_FindGlyph and
// __Font_DrawText read - the box in the sheet at +0 (u, v, width, height), the vertical offset at +8, the
// extra advance at +0xa and the character at +0x14 (gfx/Font.h: FONT_tag, CHARDATA_tag).
#define CurrentFont (*(const uint8_t *const *)0x001f64b0)

// With DumpTextures on, a font's glyph boxes go out beside its texture once, for making a replacement.
static void DescribeFontOnce(const uint8_t *font, const void *xboxTexture) {
    static const void *described[32];
    static int describedCount = 0;
    for (int i = 0; i < describedCount; i++)
        if (described[i] == xboxTexture) return;
    if (font == NULL || describedCount >= 32)
        return;
    uint16_t first = *(const uint16_t *)(font + 0x8), last = *(const uint16_t *)(font + 0xa);
    uint32_t count = *(const uint32_t *)(font + 0xc);
    const uint8_t *records = *(const uint8_t *const *)(font + 0x10);
    if (records == NULL || count == 0 || count > 1024)
        return;
    static D3D9FontGlyph glyphs[1024];
    int kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *g = records + i * 24;
        uint16_t code = *(const uint16_t *)(g + 0x14);
        if (code < first || code > last)
            continue;
        D3D9FontGlyph *out = &glyphs[kept++];
        out->u = *(const uint16_t *)(g + 0x0);
        out->v = *(const uint16_t *)(g + 0x2);
        out->w = *(const uint16_t *)(g + 0x4);
        out->h = *(const uint16_t *)(g + 0x6);
        out->yOffset = *(const int16_t *)(g + 0x8);
        out->advance = *(const int16_t *)(g + 0xa);
        out->code = code;
    }
    if (D3D9_DescribeFont(xboxTexture, glyphs, kept))
        described[describedCount++] = xboxTexture;
}

static void NoteGlyphTexture(const SPRITE_DRAW *glyph, const uint8_t *font) {
    if (!D3D9_DumpingTextures() || glyph->textureIndex >= 0x1000)
        return;
    const uint8_t *record = TextureRecords[glyph->textureIndex];
    if (record == NULL)
        return;
    uint32_t frames = *(const uint32_t *)(record + 0x24);
    if (frames == 0 || frames > 8)
        frames = 1;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t slot = *(const uint32_t *)(record + 0x54 + 4 * i);
        if (slot == 0 || slot >= GFX_TEXTURE_SLOTS)
            continue;
        const void *header = &Gfx.textures[slot];
        DescribeFontOnce(font, header);
    }
}

// AUTOINJECT
SPRITE_DRAW * View_AddSprite(ushort someNum) {

    if (s_pendingGlyph >= 0) {
        if (memcmp(&SprBuffList[s_pendingGlyph], &CharInfo, sizeof(SPRITE_DRAW)) == 0)
            NoteGlyphTexture(&SprBuffList[s_pendingGlyph], s_pendingFont);
        s_pendingGlyph = -1;
    }

    // Only ever appears to be called with someNum == 0.
    // Potentially intended to allocate multiple at the same time, but not correctly implemented (would need to increment SprCnt by a variable amount rather than just 1)?
    NF_ASSERT(someNum == 0, "Assumed View_AddSprite parameter always 0");

    // If the buffer is too full to allow us to release this many sprites, flush it first
    if((SprCnt + someNum) >= (ARRAY_SIZE(SprBuffList)-1)) { // TODO: Check for off by one errors here?
        psiDrawSprites(SprBuffList, SprCnt);
        SprCnt = 0;
    }

    // Return the buffer, starting at the number of sprites already buffered
    if (D3D9_DumpingTextures()) {
        s_pendingGlyph = (int)SprCnt;
        s_pendingFont = CurrentFont;
    }
    return &SprBuffList[SprCnt++];
}

// The sprite flags View_DrawSprites reads (sprite::maybeFlags)
#define SPRITE_SHADOW       0x0020  // a black copy where it was, the sprite a 64th of its size (1 to 5 pixels) on
#define SPRITE_FLIP_X       0x0040
#define SPRITE_FLIP_Y       0x0080
#define SPRITE_TILED        0x0400  // mirrored tiles, as many as the texture goes into the texels used
#define SPRITE_CENTRED      0x0800
#define SPRITE_TEXT_RING    0x2000  // a text's four-copy shadow (Font_DrawText's FONT_SHADOW_RING)
#define SPRITE_TEXT_OUTLINE 0x4000

// Draws sprites first to last of a view's list: a text by Font_DrawText (the sprite's second colour its shadow),
// a picture as one sprite or as tiles of it, each with a shadow if it asks.
// AUTOINJECT
void View_DrawSprites(viewer_tag *viewer, sprite **list, uint first, uint last) {
    if (!viewer->spritesShown)
        return;
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    psiSetUpColourBlend(2);
    psiLight_SetLights(NULL, 0, 0.0f, 0.0f, 0.0f);
    __profiling_or_debugging_hook_point();

    for (uint i = first; i < last; i++) {
        sprite *spr = list[i];

        if (spr->text != NULL) {
            uint flags = 0;
            if (spr->maybeFlags & SPRITE_TEXT_RING)
                flags = FONT_SHADOW_RING;
            if (spr->maybeFlags & SPRITE_SHADOW)
                flags |= FONT_SHADOW;
            if (spr->maybeFlags & SPRITE_TEXT_OUTLINE)
                flags |= FONT_OUTLINE;
            Font_DrawText(spr->positionX + viewer->xMin, spr->positionY + viewer->yMin, spr->colourTint,
                          spr->unknown1, 0xffffffff, spr->text, spr->maybeClippedString, &spr->scale, flags,
                          (ushort)spr->length, spr->spritesheetWidth);
            continue;
        }

        uchar r = (uchar)(spr->colourTint >> 24);
        uchar g = (uchar)(spr->colourTint >> 16);
        uchar b = (uchar)(spr->colourTint >> 8);
        uchar a = (uchar)spr->colourTint;
        if (a == 0)
            continue;

        uchar across = 1, down = 1;
        if (spr->maybeFlags & SPRITE_TILED) {
            across = (uchar)((spr->spritesheetWidth + 1) / (ushort)spr->backupOnscreenWidth);
            down = (uchar)((spr->spritesheetHeight + 1) / (ushort)spr->backupOnscreenHeight);
        }
        // Toggled a tile at a time by its low bit: a flipped sprite's tiles stay flipped (0x80 and 0x81), a plain
        // one's alternate
        uchar flipY = spr->maybeFlags & SPRITE_FLIP_Y;
        uchar flipX = spr->maybeFlags & SPRITE_FLIP_X;
        // A tile's size and texels: the whole's over the count, by 16-bit shifts
        float x = spr->positionX;
        double y = spr->positionY;
        float tileWidth = (short)(spr->onscreenWidth >> ((across - 1) & 31));
        float tileHeight = (short)(spr->onscreenHeight >> ((down - 1) & 31));
        short u0 = spr->spritesheetX;
        short v0 = spr->spritesheetY;
        short texelsX = (short)(spr->spritesheetWidth >> ((across - 1) & 31));
        short texelsY = (short)(spr->spritesheetHeight >> ((down - 1) & 31));
        if (spr->maybeFlags & SPRITE_CENTRED) {
            x = x - (short)(spr->onscreenWidth >> 1);
            y = y - (short)(spr->onscreenHeight >> 1);
        }
        float top = (short)Ftol(y);

        for (uchar column = across; column != 0; column--) {
            float tileY = top;
            for (uchar row = down; row != 0; row--) {
                SPRITE_DRAW *shadow = NULL;
                if (spr->maybeFlags & SPRITE_SHADOW) {
                    if (SprCnt >= 0x3e) {
                        psiDrawSprites(SprBuffList, SprCnt);
                        SprCnt = 0;
                    }
                    shadow = &SprBuffList[SprCnt++];
                }
                if (SprCnt >= 0x3f) {
                    psiDrawSprites(SprBuffList, SprCnt);
                    SprCnt = 0;
                }
                SPRITE_DRAW *draw = &SprBuffList[SprCnt++];
                memcpy(draw->unknown08, &spr->createdOnFrame, sizeof(draw->unknown08));
                draw->textureIndex = spr->unknownDataMaybeTexPtr;
                draw->flags = spr->maybeFlags;
                draw->invHeight = 1.0f / (ushort)spr->backupOnscreenHeight;
                draw->clrR = r;
                draw->clrG = g;
                draw->clrB = b;
                draw->clrA = a;
                draw->invWidth = 1.0f / (ushort)spr->backupOnscreenWidth;
                draw->x = x + viewer->xMin;
                draw->width = tileWidth;
                draw->height = tileHeight;
                draw->y = tileY + viewer->yMin;
                draw->u0 = flipX ? texelsX : u0;
                draw->v0 = flipY ? texelsY : v0;
                draw->u1 = flipX ? -texelsX : texelsX;
                draw->v1 = flipY ? -texelsY : texelsY;
                if (spr->maybeFlags & SPRITE_SHADOW) {
                    *shadow = *draw;
                    shadow->clrB = 0;
                    shadow->clrG = 0;
                    shadow->clrR = 0;
                    float offset = draw->width * 0.015625f;
                    if (offset < 1.0f)
                        offset = 1.0f;
                    else if (offset > 5.0f)
                        offset = 5.0f;
                    draw->x = offset + draw->x;
                    offset = draw->height * 0.015625f;
                    if (offset < 1.0f)
                        offset = 1.0f;
                    else if (offset > 5.0f)
                        offset = 5.0f;
                    draw->y = offset + draw->y;
                }
                // A column's last flip carries on into the next column
                flipY ^= 1;
                tileY = tileHeight + tileY;
            }
            x = tileWidth + x;
            flipX ^= 1;
        }
    }

    if (SprCnt != 0) {
        psiDrawSprites(SprBuffList, SprCnt);
        SprCnt = 0;
    }
    __profiling_or_debugging_hook_point();
}

// AUTOINJECT
short View_3DPoint2Screen(_VECTOR *vecIn, _VECTOR *vecOut, ushort viewerNum) {

    viewer_tag *viewer = glb_viewer[viewerNum];

    if(vecIn == NULL)
        return 0;

    if(vecOut == NULL)
        return 0;

    if(viewer == NULL)
        return 0;

    _VECTOR direction;
    _VECTOR tmp;

    Vec_Subtract(vecIn, &viewer->pos, &direction);

    ApplyMatrixLV(&viewer->worldMatrix, &direction, &tmp);

    // Our return value will be negative if the point is behind the viewer, positive if it is in front (ie z component is negative)
    short returnValue = 1;
    if(tmp.z < 0.0f) {
        returnValue = -1;
        tmp.z = -tmp.z;
    }

    // On the x87, unrounded until each is stored
    Extended width = Extended(viewer->width);
    vecOut->x = (float)(width - (width * Extended(0.5f) +
                                 Extended(1.0f) / Extended(tmp.z) * Extended(viewer->projectionScaleX) * Extended(tmp.x)));
    vecOut->z = tmp.z;
    vecOut->y = (float)(Extended(viewer->height) * Extended(0.5f) +
                        Extended(1.0f) / Extended(tmp.z) * Extended(viewer->projectionScaleY) * Extended(tmp.y));

    return returnValue;
}

// Sets up a view for drawing unless it is the one set up last
static void UseCamera(short *current, short num) {
    if (*current != num)
        psiUseCamera(glb_viewer[num]);
    *current = num;
}

// The frame: every view's scene gathered, then the players' views (0-3) drawn pass by pass - near sky, opaque lists,
// far sky, sorted lists with the shards and drops, the last lists (on a cleared depth when view 5 has any), sprites
// and fades - then the other views (4-9) one at a time, with view 4 drawing the shards and drops.
// A frame of the loading screen: its sprites alone, through the last viewer
// AUTOINJECT
void LoadScreen_Draw(void) {
    undefined4 start = 0;
    uint lastForeground = 0, count = 0;
    GameState.maybeLoadingBlobs++;
    psiPreDraw();
    psiUseCamera(glb_viewer[10]);
    sprite **list = Sprite_BuildList(99, &start, &lastForeground, &count);
    View_DrawSprites(glb_viewer[10], list, start, count);
    psiPostDraw();
}

// AUTOINJECT
void __stdcall Game_Draw(void) {
    short current = -1;
    GameState.maybeLoadingBlobs++;
    psiSetScreenBlur(GlobalBlur);
    if (GlobalBlur >= 0x10)
        GlobalBlur -= 0x10;
    else
        GlobalBlur = 0;
    psiPreDraw();

    Tots[0] = 0;
    Tots[1] = 0;
    Tots[2] = 0;
    for (int i = 0; i < 11; i++) {
        if (glb_viewer[i] != NULL)
            View_CaptureScene(glb_viewer[i]);
    }
    psiFog(1, 0.0f, 10.0f, 0xff, 0, 0);

    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL && viewer->enabled && viewer->draw.skyVisible) {
            UseCamera(&current, i);
            View_DrawSky(viewer, 1);
        }
    }
    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL && viewer->enabled && viewer->draw.counts[0] != 0) {
            UseCamera(&current, i);
            psiSetUpColourBlend(2);
            View_DrawObjList(viewer, 0);
        }
    }
    __profiling_or_debugging_hook_point();
    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL && viewer->enabled && viewer->draw.skyVisible) {
            UseCamera(&current, i);
            View_DrawSky(viewer, 0);
        }
    }
    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer == NULL || !viewer->enabled)
            continue;
        if (viewer->draw.counts[1] != 0) {
            UseCamera(&current, i);
            psiSetUpColourBlend(1);
            View_DrawObjList(viewer, 1);
        }
        if (viewer->enabled) {
            UseCamera(&current, i);
            psiSetShardRenderStates();
            DrawAllShards();
            psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
            psiSetUpColourBlend(2);
            psiLight_SetLights(NULL, 0, 0.0f, 0.0f, 0.0f);
            DrawDrops();
        }
    }
    if (Tots[2] != 0) {
        UseCamera(&current, 5);
        psiClearZ();
    }
    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer != NULL && viewer->enabled && viewer->draw.counts[2] != 0) {
            UseCamera(&current, i);
            if (viewer->enabled) {
                psiSetUpColourBlend(2);
                View_DrawObjList(viewer, 2);
            }
        }
    }

    sprite **list = NULL;
    undefined4 start = 0;
    uint lastForeground = 0, count = 0;
    for (short i = 0; i < 4; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer == NULL || !viewer->spritesShown)
            continue;
        list = Sprite_BuildList(i, &start, &lastForeground, &count);
        if (lastForeground != 0 || count != 0) {
            UseCamera(&current, i);
            View_DrawSprites(viewer, list, lastForeground, count);
        }
        if (viewer->fadeState) {
            UseCamera(&current, i);
            psiFadeView(viewer->fadeColour[0], viewer->fadeColour[1], viewer->fadeColour[2], viewer->fadeColour[3]);
        }
        __profiling_or_debugging_hook_point();
    }

    start = 0;
    lastForeground = 0;
    count = 0;
    for (short i = 4; i < 10; i++) {
        viewer_tag *viewer = glb_viewer[i];
        if (viewer == NULL)
            continue;
        if (viewer->enabled || viewer->draw.skyVisible) {
            UseCamera(&current, i);
            View_DrawSky(viewer, 1);
        }
        if (viewer->spritesShown) {
            list = Sprite_BuildList(i, &start, &lastForeground, &count);
            if (start != 0) {
                UseCamera(&current, i);
                View_DrawSprites(viewer, list, start, lastForeground);
            }
        }
        viewer = glb_viewer[i];
        if (viewer->enabled && viewer->draw.counts[0] != 0) {
            UseCamera(&current, i);
            psiSetUpColourBlend(2);
            View_DrawObjList(viewer, 0);
            __profiling_or_debugging_hook_point();
        }
        if (viewer->enabled || viewer->draw.skyVisible) {
            UseCamera(&current, i);
            View_DrawSky(viewer, 0);
        }
        if (viewer->enabled) {
            if (viewer->draw.counts[1] != 0) {
                UseCamera(&current, i);
                psiSetUpColourBlend(1);
                View_DrawObjList(viewer, 1);
            }
            if (viewer->enabled && i == 4) {
                UseCamera(&current, 4);
                psiSetShardRenderStates();
                DrawAllShards();
                psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
                psiSetUpColourBlend(2);
                psiLight_SetLights(NULL, 0, 0.0f, 0.0f, 0.0f);
                DrawDrops();
            }
        }
        if (viewer->enabled && viewer->draw.counts[2] != 0) {
            UseCamera(&current, i);
            psiSetUpColourBlend(2);
            View_DrawObjList(viewer, 2);
        }
        if ((lastForeground != 0 || count != 0) && viewer->spritesShown) {
            UseCamera(&current, i);
            View_DrawSprites(viewer, list, lastForeground, count);
        }
        if (viewer->fadeState && viewer->spritesShown) {
            UseCamera(&current, i);
            psiFadeView(viewer->fadeColour[0], viewer->fadeColour[1], viewer->fadeColour[2], viewer->fadeColour[3]);
        }
        __profiling_or_debugging_hook_point();
    }
    SprCnt = 0;
    psiPostDraw();
}
