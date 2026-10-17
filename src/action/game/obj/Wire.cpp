#include "Wire.h"
#include "control.h"
#include "build.h"

// The placement's parameters as Wire_Create reads them
#pragma pack(push, 1)
typedef struct {
    uchar _unknown00[0x2c];
    uint param[4];      // 0x2c
} Create_Wire_Params;
#pragma pack(pop)
static_assert(offsetof(Create_Wire_Params, param) == 0x2c, "Bad offset of param");

// A wire (or zipline) to catch: its model, which is what the player touches. The placement's rotation is turned
// over in z first - the caller's copy too.
// AUTOINJECT
obj_tag * Wire_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Wire_Params *params = (Create_Wire_Params *)lvl;
    rot->z = -rot->z;
    obj_tag *obj = control_create_object(sizeof(WIRE), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    WIRE *wire = (WIRE *)obj->extraObjectData;
    wire->thirdCam = (ushort)params->param[0];
    wire->type = (short)params->param[1];
    wire->field_4 = (short)params->param[2];
    wire->channel = (short)params->param[3];
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_THIRDCAM_OR_WIRE;
    build_LinkToRoom(obj, 0, glb_world);
    Controls_StraddleTest(obj);
    return obj;
}
