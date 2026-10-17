#include "GT.h"

// AUTOGEN
void GT_LoseControl(obj_tag *gameObj);

// AUTOGEN
void GT_Activate(obj_tag *gameObj);

// AUTOGEN
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);

// An explosion's damage
// AUTOINJECT
void GT_ApplyDamage(obj_tag *obj, float damage) {
    ((GUNTURRET *)obj->extraObjectData)->health -= damage;
}
