#ifndef THIRDCAM_H_
#define THIRDCAM_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A third-person camera's obj_tag::extraObjectData (0xc bytes): a path the view follows while the player is on the
// wire or wall that names it (camera_tracking).
typedef struct THIRDCAM {
    ushort id;          // 0x0 - its place in ThirdPersonCameras, the placement's first parameter
    uchar _pad02[2];
    void *path;         // 0x4 - the placement's path, for Spline_Interp
    ushort lastKey;     // 0x8 - the path's points less one
    uchar show;         // 0xa - the placement's second parameter: unless set, camera_tracking leaves the view alone
    uchar _pad0b;
} THIRDCAM;
#pragma pack(pop)
static_assert(sizeof(THIRDCAM) == 0xc, "Bad size for THIRDCAM");

// The third-person cameras by id (wires and creep walls name theirs, WIRE::thirdCam, CREEPWALL::thirdCam)
// XBE_GLOBAL(0x001dc700, 0x40)
#define ThirdPersonCameras (*(obj_tag *(*)[16])0x001dc700)

obj_tag * ThirdCam_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void ThirdCam_GetPos(obj_tag *camera, float along, _MATRIX *mtx);

#endif // THIRDCAM_H_