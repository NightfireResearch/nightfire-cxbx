#include "Lightning.h"

#include "../view.h"
#include "control.h"
#include "Player.h"

#define LIGHTNING_MODEL ((HASHCODE)0x0200032c)

#include "../../util/Random.h"

#include <math.h>

// The bolt between start and end through segments first to last: the middle one's point is moved off the line by
// up to half the spread either way on each axis, then each half is done again. The spread is half the length, or
// with byRoot a sixth of its square root.
// AUTOINJECT
void FractalThing(obj_tag **segments, short first, short last, _VECTOR *start, _VECTOR *end, char byRoot) {
    if (last - first <= 1)
        return;
    short mid = (first + last) / 2;

    float spread;
    if (byRoot)
        spread = (float)(sqrt(Vec_Dist3D(start, end)) * (1.0f / 6.0f));
    else
        spread = (float)(Vec_Dist3D(start, end) * 0.5f);
    float half = spread * 0.5f;

    _VECTOR point;
    point.x = (float)((Float_FRand(spread) - half) + ((double)start->x + end->x) * 0.5f);
    point.y = (float)((Float_FRand(spread) - half) + ((double)start->y + end->y) * 0.5f);
    point.z = (float)((Float_FRand(spread) - half) + ((double)start->z + end->z) * 0.5f);
    segments[mid]->position = point;

    FractalThing(segments, first, mid, start, &point, byRoot);
    FractalThing(segments, mid, last, &point, end, byRoot);
}

// Pairs with every lightning point already made: the segments between are made unchecked
// AUTOINJECT
obj_tag* Lightning_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl) {
    obj_tag *obj = control_create_object(sizeof(LIGHTNING_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    LightningPlacement *keys = (LightningPlacement *)lvl;
    LIGHTNING_tag *bolt = (LIGHTNING_tag *)obj->extraObjectData;
    bolt->segments[0] = NULL;
    bolt->key0 = keys->key0;
    bolt->pair = (short)keys->pair;
    bolt->key2 = (short)keys->key2;
    obj->curState = 0;
    obj->objectType = OBJECTTYPE_LIGHTNING;
    obj->effectFlags |= 0x8000;

    for (obj_tag *other = control_first_object(); other != NULL; other = other->nextObject) {
        if (other == obj || other->objectType != OBJECTTYPE_LIGHTNING)
            continue;
        if (bolt->pair != ((LIGHTNING_tag *)other->extraObjectData)->pair)
            continue;
        bolt->segments[0] = obj;
        for (int i = 1; i < 7; i++) {
            obj_tag *segment = control_create_object(0, pos, rot, NULL);
            segment->objectType = OBJECTTYPE_GFX;
            bolt->segments[i] = segment;
            segment->effectFlags |= 0x8000;
        }
        bolt->segments[7] = other;
        other->objectType = OBJECTTYPE_GFX;
        Vec_Copy(&obj->position, &bolt->start);
        Vec_Copy(&other->position, &bolt->end);
    }
    return obj;
}

// The bolt shows while the sky flashes, in every view, its segments made again for each new flash
// AUTOINJECT
void Lightning_Update(obj_tag *obj) {
    LIGHTNING_tag *bolt = (LIGHTNING_tag *)obj->extraObjectData;
    if (bolt->segments[0] == NULL)
        return;

    if (!LightningFlash) {
        for (int i = 0; i < 8; i++)
            View_SetDrawInNoViews(bolt->segments[i]);
        return;
    }

    if (LightningOverhead) {
        LightningOverhead = 0;
        FractalThing(bolt->segments, 0, 8, &bolt->start, &bolt->end, 0);
    }
    for (int i = 0; i < 8; i++) {
        obj_tag *segment = bolt->segments[i];
        hashtable_set_object_to_entity_gfx(segment, LIGHTNING_MODEL);
        View_SetDrawInAllViews(segment);
        PositionBeam(segment, &segment->position, i < 7 ? &bolt->segments[i + 1]->position : &bolt->end);
        segment->transformFlags |= TRANSFORM_MOVED;
        segment->parent = glb_players[0];
    }
}
