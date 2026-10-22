#include "ThirdCam.h"
#include "../../../common/xbeClass.h"
#include "control.h"
#include "../Spline.h"

// XBE_GLOBAL(0x001dc700, 0x40)
obj_tag *ThirdPersonCameras[16];

// The placement's path and parameters as ThirdCam_Create reads them
#pragma pack(push, 1)
typedef struct {
    ushort unknown00;           // 0x00
    ushort pathPointCount;      // 0x02
    uchar _unknown04[4];
    Ptr32<void> path;           // 0x08
    uchar _unknown0c[0x2c - 0x0c];
    uint param[2];              // 0x2c
} Create_ThirdCam_Params;
#pragma pack(pop)
static_assert(offsetof(Create_ThirdCam_Params, path) == 0x08, "Bad offset of path");
static_assert(offsetof(Create_ThirdCam_Params, param) == 0x2c, "Bad offset of param");

// A path for the third-person view to follow, kept by its id. The object is used without a check that it was made.
// AUTOINJECT
obj_tag * ThirdCam_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_ThirdCam_Params *params = (Create_ThirdCam_Params *)lvl;
    obj_tag *obj = control_create_object_xbox(sizeof(THIRDCAM), XBOX_SIZEOF(THIRDCAM), pos, rot, NULL);
    THIRDCAM *camera = (THIRDCAM *)obj->extraObjectData;
    camera->id = (ushort)params->param[0];
    camera->lastKey = params->pathPointCount - 1;
    camera->path = params->path;
    camera->show = (uchar)params->param[1];
    ThirdPersonCameras[camera->id] = obj;
    return obj;
}

// The camera's place along its path, from 0 at the start to 1 at the end
// AUTOINJECT
void ThirdCam_GetPos(obj_tag *camera, float along, _MATRIX *mtx) {
    THIRDCAM *data = (THIRDCAM *)camera->extraObjectData;
    Spline_Interp(data->path, 0, data->lastKey, data->lastKey * along, mtx, NULL, 2);
}
