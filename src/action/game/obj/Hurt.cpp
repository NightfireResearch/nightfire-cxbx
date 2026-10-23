#include "Hurt.h"

#include "../../sound/Sound.h"
#include "../Spline.h"
#include "../sp/SwitchChannels.h"
#include "build.h"
#include "control.h"

#pragma fp_contract(off)

// Hidden while its channel is off
// AUTOINJECT
obj_tag * Hurt_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {

    obj_tag *obj = control_create_object(sizeof(Hurt_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    Hurt_tag *hurt = (Hurt_tag *)obj->extraObjectData;
    HurtPlacement *keys = (HurtPlacement *)lvl;
    hurt->pathTime = 0.0f;
    hurt->path = keys->path;
    hurt->pathFrames = keys->pathCount != 0 ? keys->pathCount - 1 : 0;
    hurt->type = (uchar)keys->type;
    hurt->damage = (float)keys->damage;
    hurt->enableSwitchChannel = (ushort)keys->enableSwitchChannel;
    hurt->sound = keys->sound != 0 ? (ushort)keys->sound : 0xffff;
    hurt->armMask = (ushort)keys->armMask;
    hurt->oneShotPath = (uchar)keys->oneShotPath;
    double frames = (double)hurt->pathFrames * REC_FRAME_RATE;
    hurt->pathSpeed = (float)(_FRAME_RATE / (double)keys->pathDuration * frames);

    obj->objectType = OBJECTTYPE_HURT;
    Control_SetGList(obj, celgl);
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    build_LinkToRoom(obj, 0, glb_world);

    if (hurt->enableSwitchChannel != 0 && !switch_channels[hurt->enableSwitchChannel])
        obj->effectFlags |= 0x20;
    return obj;
}

// Whether it goes off: its channel if it has one, else whether something of the kinds in armMask touches it (8 the
// player, 0x10 a drone, 2 a bullet); with no mask, at once
ushort Hurt_IsTriggered(obj_tag *obj, Hurt_tag *hurt) {

    HITDATA_tag *hit = obj->hitList;
    if (hurt->enableSwitchChannel != 0)
        return (uchar)switch_channels[hurt->enableSwitchChannel];
    if (hurt->armMask == 0)
        return 1;

    ushort touching = 0;
    for (; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL)
            continue;
        switch (hit->hitObj->objectType) {
        case OBJECTTYPE_DRONE:
            touching |= 0x10;
            break;
        case OBJECTTYPE_PLAYER:
            touching |= 8;
            break;
        case OBJECTTYPE_BULLET:
            touching |= 2;
            break;
        }
    }
    return hurt->armMask & touching;
}

// State 0 waits to go off; 1 runs the path (the sound at its start), and at its end starts it again or, one shot, goes
// to 2, which deletes it. Shown while the channel is on; switched off part way, it waits again.
// AUTOINJECT
void Hurt_Update(obj_tag *obj) {

    if (obj == NULL)
        return;

    Hurt_tag *hurt = (Hurt_tag *)obj->extraObjectData;
    if (hurt->enableSwitchChannel != 0 && switch_channels[hurt->enableSwitchChannel])
        obj->effectFlags &= ~0x20;
    if (hurt->enableSwitchChannel != 0 && !switch_channels[hurt->enableSwitchChannel])
        obj->effectFlags |= 0x20;

    switch ((short)obj->curState) {
    case 0:
        if (hurt->enableSwitchChannel != 0 && !switch_channels[hurt->enableSwitchChannel])
            return;
        if (Hurt_IsTriggered(obj, hurt))
            obj->curState = 1;
        return;

    case 1: {
        if (hurt->pathFrames == 0)
            return;
        if (hurt->enableSwitchChannel != 0 && !switch_channels[hurt->enableSwitchChannel]) {
            obj->curState = 0;
            obj->effectFlags |= 0x20;
        }
        if (hurt->pathTime == 0.0f)
            Sound_Play3D((Action_SFX)hurt->sound, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Spline_Interp(hurt->path, 0, hurt->pathFrames, hurt->pathTime, &obj->transformMatrix, NULL, 2);
        obj->transformFlags |= TRANSFORM_MOVED;

        double time = (double)hurt->pathSpeed + hurt->pathTime;
        hurt->pathTime = (float)time;
        if (time > (double)hurt->pathFrames) {
            hurt->pathTime = 0.0f;
            if (hurt->oneShotPath == 1)
                obj->curState = 2;
        }
        return;
    }

    case 2:
        obj->objectType = OBJECTTYPE_DELETED;
        return;
    }
}

// A model that hides what is behind it for a time (Bullet_DoTrails)
// AUTOINJECT
obj_tag* Occlude_Create(_VECTOR *pos, ushort life, ushort kind) {

    obj_tag *obj = control_create_object(0, pos, NULL, NULL);
    if (obj == NULL)
        return NULL;

    obj->objectType = OBJECTTYPE_OCCLUDE;
    obj->curState = life;
    HASHCODE model;
    switch (kind) {
    // The models of Occlude_Create's kinds 1 to 3, and of any other
    case 1: model = HT_Entity_OccludeModel1; break;
    case 2: model = HT_Entity_OccludeModel2; break;
    case 3: model = HT_Entity_OccludeModel3; break;
    default: model = HT_Entity_OccludeModelOther; break;
    }
    hashtable_set_object_to_entity_gfx(obj, model);
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Counts its life down a frame, and is deleted at 0
// AUTOINJECT
void Occlude_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    short life = obj->curState;
    if (life == 0)
        obj->flags |= 1;
    if (life > 0)
        obj->curState = life - 1;
}

// The damage a hurt volume does; 0 for anything else.
// AUTOINJECT
float Hurt_GetDamage(obj_tag *obj) {
    if (obj == NULL || obj->objectType != OBJECTTYPE_HURT)
        return 0.0f;
    return ((Hurt_tag *)obj->extraObjectData)->damage;
}

// The kind of hurt a hurt volume does; 0 for anything else.
// AUTOINJECT
ushort Hurt_GetType(obj_tag *obj) {
    if (obj == NULL || obj->objectType != OBJECTTYPE_HURT)
        return 0;
    return ((Hurt_tag *)obj->extraObjectData)->type;
}

