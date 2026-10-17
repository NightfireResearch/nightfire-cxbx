#include "Hurt.h"

// AUTOGEN
obj_tag * Hurt_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

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

