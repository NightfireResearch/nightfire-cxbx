#include "LeafGen.h"

#include <math.h>
#include <stdint.h>

#include "../../math/X87Ops.h"
#include "build.h"
#include "control.h"

#pragma fp_contract(off)

// AUTOINJECT
obj_tag* LeafGen_Create(_VECTOR *pos, _VECTOR *rot, level_tag* lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_LEAFGEN;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// A leaf, one frame in half a second's on average
// AUTOINJECT
void LeafGen_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    if (Rand_Rand(FRAME_RATE_INT / 2) == 0)
        Leaf_Create(obj, 1);
}

// count leaves anywhere in the cube about the generator's sphere, all blown the way it faces, each spinning its own
// way, for 256 frames at 60 Hz. The first draw is not used.
// AUTOINJECT
void Leaf_Create(obj_tag *gen, ushort count) {
    float radius = gen->radius;
    float yaw = gen->rotation.y;

    Float_FRand(0.02f);
    _VECTOR vel;
    double speed = Float_FRand(0.02f) + 0.01f;
    vel.x = (float)(FSin(yaw) * Extended(speed));
    speed = Float_FRand(0.02f) + 0.01f;
    vel.y = 0.0f;
    vel.z = (float)(FCos(yaw) * Extended(speed));

    float diameter = radius + radius;
    for (ushort i = 0; i < count; i++) {
        _VECTOR pos;
        Vec_Copy(&gen->position, &pos);
        pos.x = (float)(Float_FRand(diameter) - radius + pos.x);
        pos.z = (float)(Float_FRand(diameter) - radius + pos.z);
        pos.y = (float)(Float_FRand(diameter) - radius + pos.y);

        obj_tag *leafObj = control_create_object(sizeof(LEAF_tag), &pos, NULL, NULL);
        if (leafObj == NULL)
            continue;
        leafObj->objectType = OBJECTTYPE_LEAF;
        leafObj->tweakA = 0xff;
        LEAF_tag *leaf = (LEAF_tag *)leafObj->extraObjectData;
        leaf->life = (short)Ftol(256.0f / (double)FRAME_RATE_MUL);
        Vec_Copy(&vel, &leaf->vel);
        Vec_Scalef(&leaf->vel, FRAME_RATE_MUL, &leaf->vel);
        Rand_FRandHalf_Vec(&leaf->spin, 0.1f);
        Vec_Scalef(&leaf->spin, FRAME_RATE_MUL, &leaf->spin);
        hashtable_set_object_to_entity_gfx(leafObj, HT_Entity_Leaf);
        leafObj->effectFlags |= 0x20000020;
        if (!build_LinkToRoom(leafObj, 0, glb_world))
            control_link_object_to_cel(leafObj, gen->inCel);
    }
}

// Drifts and flutters down until its life or its alpha runs out
// AUTOINJECT
void Leaf_Update(obj_tag *obj) {
    if (obj == NULL)
        return;
    LEAF_tag *leaf = (LEAF_tag *)obj->extraObjectData;
    if (leaf->life <= 0 || obj->tweakA - leaf->fade <= 0) {
        obj->flags |= 1;
        return;
    }
    obj->tweakA -= leaf->fade;
    if (leaf->life > 0)
        leaf->life--;

    Vec_Add2(&obj->position, &leaf->vel, &obj->position);
    obj->position.y = (float)(obj->position.y - (Float_FRand(0.075f) - 0.025f) * FRAME_RATE_MUL);
    leaf->vel.x = (float)((Float_FRand(0.005f) - 0.0025f) * FRAME_RATE_MUL + leaf->vel.x);
    leaf->vel.z = (float)((Float_FRand(0.005f) - 0.0025f) * FRAME_RATE_MUL + leaf->vel.z);
    Vec_Add2(&obj->rotation, &leaf->spin, &obj->rotation);
    obj->transformFlags |= TRANSFORM_MOVED;
}
