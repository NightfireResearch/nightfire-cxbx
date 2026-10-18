#include "Shooter.h"

#include "../weapon_stats.h"
#include "build.h"
#include "bullet.h"
#include "control.h"

// One shot once its channel (curState) is on, of its weapon (subState), the way it faces (down for weapon 0x3b's
// group), then gone
// AUTOINJECT
obj_tag* Shooter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    ShooterPlacement *keys = (ShooterPlacement *)lvl;
    obj->objectType = OBJECTTYPE_SHOOTER;
    obj->objGraphics = celgl;
    build_LinkToRoom(obj, 0, glb_world);
    obj->curState = (ushort)keys->channel;
    obj->subState = (ushort)keys->weapon;
    return obj;
}

// AUTOINJECT
void Shooter_Update(obj_tag *obj) {
    if (!switch_channels[(short)obj->curState])
        return;
    weapon_definition_tag *weapon = &weapon_data[(short)obj->subState];
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->transformMatrix);
    if (weapon->weaponBaseNum == 0x3b) {
        dir.z = 0.0f;
        dir.x = 0.0f;
        dir.y = -1.0f;
    }
    Bullet_init(0, NULL, obj, weapon, &obj->position, &dir, 0xffff);
    obj->flags |= 1;
}
