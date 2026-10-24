#include "Apocalypse.h"

#include <math.h>
#include <stdint.h>

#include "../../engine/viewer.h"
#include "../../input.h"
#include "control.h"
#include "Player.h"
#include "../../math/X87Ops.h"

#pragma fp_contract(off)

// Not in a cel
// AUTOINJECT
obj_tag* Apocalypse_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(APOCALYPSE_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    APOCALYPSE_tag *quake = (APOCALYPSE_tag *)obj->extraObjectData;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_APOCALYPSE;
    ApocalypsePlacement *keys = (ApocalypsePlacement *)lvl;
    quake->channel = (short)keys->channel;
    float length = (float)((double)keys->seconds * 60.0f);
    quake->timeLeft = length;
    quake->length = length;
    quake->rumble = (short)keys->rumble;
    quake->shake = (uchar)keys->shake;
    quake->flash = (uchar)keys->flash;
    quake->running = 0;
    return obj;
}

// AUTOINJECT
void Apocalypse_Update(obj_tag *obj) {
    APOCALYPSE_tag *quake = (APOCALYPSE_tag *)obj->extraObjectData;
    if (switch_channels[quake->channel])
        quake->running = 1;
    if (!quake->running)
        return;

    float left = quake->timeLeft - FRAME_RATE_MUL;
    quake->timeLeft = left;
    if (left <= 0.0f) {
        obj->flags |= OBJFLAG_DELETE;
        return;
    }

    if (quake->rumble != 0)
        Input_RumbleStart(0, Ftol(left), Ftol((double)quake->rumble / quake->length * left));
    if (quake->shake)
        glb_viewer[0]->apocalypseEffect = (float)((double)quake->timeLeft / quake->length * 5.0f);
    if (quake->flash) {
        double flash = (double)quake->timeLeft / quake->length * 382.0f;
        if (255.0f < flash)
            flash = 255.0f;
        Player_SetFlashBang(glb_blokes[0], quake->timeLeft, (uchar)Ftol(flash));
    }
}
