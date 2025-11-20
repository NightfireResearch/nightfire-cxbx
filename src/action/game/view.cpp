#include "view.h"
#include "../engine/viewer.h"
#include <math.h>

#include <stdio.h>

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

// AUTOINJECT
void View_RotTransMatrix(_VECTOR *rotation, _VECTOR *position, _MATRIX *matrix) {
  
  float cos_rx = cosf(rotation->x);
  float sin_rx = sinf(rotation->x);
  float cos_ry = cosf(rotation->y);
  float sin_ry = sinf(rotation->y);
  float cos_rz = cosf(rotation->z);
  float sin_rz = sinf(rotation->z);

  matrix->m[0] = (cos_rz * cos_ry);
  matrix->m[1] = sin_rz;
  matrix->m[2] = -(cos_rz * sin_ry);
  matrix->m[4] = (sin_ry * sin_rx - sin_rz * cos_ry * cos_rx);
  matrix->m[5] = (cos_rz * cos_rx);
  matrix->m[6] = (cos_ry * sin_rx + sin_rz * sin_ry * cos_rx);
  matrix->m[8] = (sin_ry * cos_rx + sin_rz * cos_ry * sin_rx);
  matrix->m[9] = -(cos_rz * sin_rx);
  matrix->m[10] = (cos_ry * cos_rx - sin_rz * sin_ry * sin_rx);
  matrix->m[0xc] = position->x;
  matrix->m[0xd] = position->y;
  matrix->m[0xe] = position->z;
  return;
}

// AUTOINJECT
void View_RotTransScaleMatrix(_VECTOR *rotation, _VECTOR *position, _VECTOR* scale, _MATRIX *matrix) {
    View_RotTransMatrix(rotation, position, matrix);
    matrix->m[0] *= scale->x;
    matrix->m[1] *= scale->x;
    matrix->m[2] *= scale->x;
    matrix->m[4] *= scale->y;
    matrix->m[5] *= scale->y;
    matrix->m[6] *= scale->y;
    matrix->m[8] *= scale->z;
    matrix->m[9] *= scale->z;
    matrix->m[10] *= scale->z;
}

// AUTOINJECT
void View_DrawGlist(celglist_tag* celglist, _VECTOR *translation, _VECTOR *rotation, _VECTOR* scale) {
    _MATRIX mtx;
    View_RotTransScaleMatrix(rotation, translation, scale, &mtx);
    psiDrawObjectMatrix(celglist, &mtx);
}

#define object_display_mask U32_AT(0x0029e80c)
#define GfxList U32_AT(0x0029d79c)
#define switch_ForceDrawAll U32_AT(0x001dfa18)

// AUTOGEN
void Vision_Init_Portal_Recurse(viewer_tag* viewer);
// AUTOGEN
void Vision_AddCelToDraw(cel_tag* cel, ushort param_2);
// AUTOGEN
void vision_generate_display_list(viewer_tag* viewer);
// AUTOGEN
void vision_GetCamPos(_VECTOR* camPos);
// AUTOGEN
void Vision_Portal_Recurse(viewer_tag* viewer);


#define CamPos (*(_VECTOR*)(0x0029dbf0))

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

// Can't generate automatically - custom calling convention
void __declspec(naked) View_AddCels(viewer_tag* viewer) {
    // Custom wrapper - viewer pointer is expected in ESI by the original function, which is located at 0x000daa00
    _asm {
        mov esi, [esp + 4]
        mov eax, 0x000DAA00         ; load address into EAX
        jmp eax                     ; jump to original function
    }
}

// AUTOGEN
void View_AddForcedObjects(viewer_tag* viewer);

#define Tots U32_AT(0x0029e800)
#define DAT_0029e804 U32_AT(0x0029e804)
#define DAT_0029e808 U32_AT(0x0029e808)

void _View_CaptureScene(viewer_tag *viewer) {
    viewer->field11_0x14 = 0;
    viewer->field12_0x16 = 0;
    viewer->field13_0x18 = 0;
    if (viewer->field25_0x29 && viewer->field2_0x8) {

        View_CaptureSceneSub(viewer->idx, viewer);
        View_AddCels(viewer);
        View_AddForcedObjects(viewer);

        // Log statistics
        Tots += viewer->field11_0x14;
        DAT_0029e804 += viewer->field12_0x16;
        DAT_0029e808 += viewer->field13_0x18; // This handles first-person weapon models intersecting level geometry. Non-zero value causes camera 5 to be drawn in Game_Draw
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

// AUTOGEN
void View_AddSkyObj(ushort param_1,celglist_tag *param_2,_VECTOR *param_3,_VECTOR *param_4,obj_tag *param_5,char param_6,ushort param_7,ushort param_8,char param_9);