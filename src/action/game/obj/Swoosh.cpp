#include "Swoosh.h"

#include <math.h>

#include "../../engine/viewer.h"
#include "../mp/multiplayer.h"
#include "control.h"

#pragma fp_contract(off)

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

// A trail that lasts frames 60 Hz frames (half as long in multiplayer, from 61)
// AUTOINJECT
obj_tag* Swoosh_Create(_VECTOR *from, _VECTOR *to, short frames, HASHCODE entity, uchar drift) {

    obj_tag *obj = control_create_object(sizeof(SWOOSH_tag), to, NULL, NULL);
    if (obj == NULL)
        return NULL;

    SWOOSH_tag *swoosh = (SWOOSH_tag *)obj->extraObjectData;
    if (MPSettings.isMultiplayer && frames > 60)
        frames = frames / 2;
    Vec_Copy(from, &swoosh->from);
    Vec_Copy(to, &swoosh->to);
    short timer = Ftol(frames / (double)FRAME_RATE_MUL);
    swoosh->timer = timer;
    swoosh->total = timer;
    swoosh->drift = drift;
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_NO_SCALE;
    obj->objectType = OBJECTTYPE_SWOOSH;
    obj->scale = 1.0f;
    hashtable_set_object_to_entity_gfx(obj, entity);
    return obj;
}

// Turned to camera 0 about its line, as long as the line, narrowing and fading with the time left
// AUTOINJECT
void Swoosh_Update(obj_tag *obj) {

    SWOOSH_tag *swoosh = (SWOOSH_tag *)obj->extraObjectData;
    swoosh->timer--;
    if (swoosh->timer <= 0)
        obj->flags |= 1;
    obj->transformFlags |= TRANSFORM_MOVED;
    obj->tweakA = Ftol((double)swoosh->timer * 128.0f / swoosh->total);

    if (swoosh->drift) {
        swoosh->from.x += 0.001f;
        swoosh->to.x += 0.001f;
        obj->position.x += 0.001f;
        swoosh->from.y += 0.001f;
        swoosh->to.y += 0.001f;
        obj->position.y += 0.001f;
    }

    _VECTOR along, toEye;
    Vec_Subtract(&swoosh->to, &swoosh->from, &along);
    Vec_Subtract(&glb_viewer[0]->pos, &obj->position, &toEye);
    Mat_Align2Dir(&obj->transformMatrix, &along, &toEye, &toEye);
    Vec_Copy(&obj->position, (_VECTOR *)&obj->transformMatrix.m[12]);
    float length = Vec_Dist3D(&swoosh->from, &swoosh->to);
    Mat_Scale3f(&obj->transformMatrix, &obj->transformMatrix, (float)((double)swoosh->timer / swoosh->total), 1.0f,
                length);
    obj->scale = Vec_Dist3D(&swoosh->from, &swoosh->to);
}
