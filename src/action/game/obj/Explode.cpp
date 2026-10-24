#include "Explode.h"
#include "Sub.h"

#include <math.h>
#include <stdint.h>

#include "../../game.h"
#include "../../math/X87Ops.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2_Damage.h"
#include "../mp/multiplayer.h"
#include "../sp/SwitchChannels.h"
#include "Break.h"
#include "bullet.h"
#include "car.h"
#include "control.h"
#include "Copter.h"
#include "Destroy.h"
#include "Gas.h"
#include "GT.h"
#include "Player.h"

#pragma fp_contract(off)

// Empty on the Xbox, where the linker shares one empty function among them
static void Sensor_ApplyDamage(obj_tag *obj, float damage) {}
static void Monitor_ApplyDamage(obj_tag *obj, float damage) {}

// The debris an explosion throws: metal, and in CastleIndoors2 with SWITCH_CASTLEINDOORS2_STONE_DEBRIS on, stone
// XBE_GLOBAL(0x001780b8, 0x10) readonly relocate
HASHCODE MetalDebris[4] = {
    HT_Entity_Metal_Debris_1,
    HT_Entity_Metal_Debris_2,
    HT_Entity_Metal_Debris_3,
    HT_Entity_Metal_Debris_4,
};
// XBE_GLOBAL(0x001780a8, 0x10) readonly relocate
HASHCODE StoneDebris[4] = {
    HT_Entity_STONE_Debris_1,
    HT_Entity_STONE_Debris2,
    HT_Entity_STONE_Debris3,
    HT_Entity_STONE_Debris_1,
};

// The hit a player killed in multiplayer, or a mine, takes from who set the explosion off
#define EXPLODE_KILL_DIST 9999.0f

// An explosion at pos: its effect script turned to face the nearest player, the camera shaken, and an object that
// spreads radius and damage once (Explode_CollisionHandler). A radius under 0.5 does no damage.
// AUTOINJECT
void Explode_Create(obj_tag *obj, _VECTOR *pos, _VECTOR *unusedDir, float scriptScale, float radius, HASHCODE script,
                    float damage, uchar unused8, uchar unused9, uchar unused10, obj_tag *from, ushort material) {

    if (radius < 0.5f)
        damage = 0.0f;
    else
        Camera_Shake(pos, radius);

    // The original leaves it as stack when there is no player
    _VECTOR toPlayer = {};
    control_ClosestPlayerPnt(pos, &toPlayer);
    Vec_Subtract(&toPlayer, pos, &toPlayer);
    Vec_Normalise(&toPlayer, &toPlayer);

    obj_tag *explosion = control_create_object(sizeof(EXPLODE_tag), pos, NULL, NULL);
    if (explosion == NULL)
        return;

    EXPLODE_tag *data = (EXPLODE_tag *)explosion->extraObjectData;
    data->damage = damage;
    data->cel = obj->inCel;
    data->from = from;
    data->script = Script_Load(script, pos, &CONST_ZERO_VECTOR, NULL, NULL, NULL, NULL);
    data->radius = radius;

    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &toPlayer);
    Vec_Add2(&u, &v, &data->spread);
    data->propagated = 0;
    data->material = material;

    if (data->script != NULL) {
        data->script->scale = scriptScale;
        data->frames = data->script->endFrame;
        Mat_Align2Dir(&data->script->matrix, &toPlayer, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
        Mat_GetUp(&data->up, &data->script->matrix);
    } else {
        data->frames = 15;
    }

    explosion->objectType = OBJECTTYPE_EXPLODE;
    explosion->effectFlags = EFFECT_HIDDEN | EFFECT_NO_COLLISION;
    explosion->scale = radius * (1.0f / 15.0f);
    Vec_Copy(pos, &explosion->position);
    if (from != NULL)
        Control_ObjectReferenced(from, 1);
    Script_Play(data->script, 0);
}

// Runs the script and is deleted when it ends. In single player, a debris script throws three pieces of its
// material's debris now and then (frames a multiple of 10 to 14) in the first quarter of its frames.
// AUTOINJECT
void Explode_Update(obj_tag *obj) {

    EXPLODE_tag *data = (EXPLODE_tag *)obj->extraObjectData;
    Script_Update(data->script);
    if (!Script_IsPlaying(data->script)) {
        Script_Free(data->script);
        data->script = NULL;
        obj->flags |= OBJFLAG_DELETE;
        return;
    }

    if (MPSettings.isMultiplayer == 1 || data->material == MATERIAL_NONE)
        return;
    if (data->script != NULL && data->script->scriptHashcode != HT_Script_ExplosionBlastRadius)
        return;
    if ((uint)(GameState.NumFramesUnpaused - obj->creationTimeFrames) > data->frames / 4)
        return;
    if (GameState.NumFramesUnpaused % (Rand_Rand(5) + 10) != 0)
        return;

    HASHCODE *debris;
    switch (data->material) {
    case MATERIAL_METALTHIN:
    case MATERIAL_METALTHICK:
        debris = MetalDebris;
        break;
    case MATERIAL_EARTH:
    case MATERIAL_STONE:
    case MATERIAL_GRAVEL:
        if (GameState.CurrentLevelHashcode != HT_Level_CastleIndoors2 ||
            !switch_channels[SWITCH_CASTLEINDOORS2_STONE_DEBRIS])
            return;
        debris = StoneDebris;
        break;
    default:
        return;
    }
    Debris_CreateEx((_VECTOR *)&obj->transformMatrix.m[12], &data->up, &data->spread, data->radius + data->radius,
                    (uint)Ftol((double)_FRAME_RATE * 3.0f), 3, obj->inCel, debris, 4);
}

// The damage of an explosion at pos, to every object in reach (rooms and objects, mask 0xc0): damage times one less
// the distance from the explosion to the object's sphere over the radius, if at least 0.0002, by what it is
// AUTOINJECT
void Explode_Propagate(_VECTOR *pos, float radius, cel_tag *cel, float damage, obj_tag *from) {

    if (cel == NULL || radius < 0.0002f)
        return;

    _VECTOR centre;
    HITDATA_tag *hitList = NULL;
    Vec_Copy(pos, &centre);
    if (!Collide_SphereIntersect(&centre, radius, cel, NULL, NULL, &hitList, 0, 1,
                                 HITMASK_SKIP_ROOMS | HITMASK_SKIP_CHILD_CELS, 4))
        return;

    for (HITDATA_tag *hit = hitList; hit != NULL; hit = hit->next) {

        obj_tag *obj = hit->hitObj;
        if (obj == NULL || (obj->flags & OBJFLAG_DELETE))
            continue;

        // Where two NaNs can meet, the x87's choice between them is kept (X87NaNs)
        double reach = Vec_Dist3D(&obj->centrePoint, pos);
        double dist = X87NaNs(reach - obj->radius, reach, obj->radius);
        if (dist < 0.0)
            dist = 0.0;
        double share = X87NaNs(dist / radius, dist, radius);
        double reached = X87NaNs((1.0f - share) * damage, 1.0f - share, damage);
        float hurt = (float)reached;
        if (reached < 0.0002f)
            continue;

        switch (obj->objectType) {

        case 0:
            // A helicopter's part
            for (COPTER *copter = (COPTER *)CopterList.head; copter != NULL; copter = (COPTER *)copter->node.next) {
                for (int i = 0; i < 9; i++) {
                    if (hit->hitObj == copter->parts[i]) {
                        copter->attackDamage += hurt;
                        copter->damage += hurt;
                    }
                }
            }
            break;

        case OBJECTTYPE_DRONE: {
            DroneImpact_tag impact;
            impact.damage = hurt;
            impact.radius = radius;
            Vec_Copy(pos, &impact.position);
            impact.source = from;
            Drone_ExplosiveHit(hit->hitObj, &impact);
            break;
        }

        case OBJECTTYPE_PLAYER:
            if (Player_Hurt(obj, hurt, pos) <= 0.0f && from != NULL && MPSettings.isMultiplayer) {
                Coll_AddHitToList(&hit->hitObj->hitList, EXPLODE_KILL_DIST, NULL, from);
                if (MPSettings.isMultiplayer) {
                    BLData *blData = (BLData *)hit->hitObj->extraObjectData;
                    MP_RegisterBulletHit(blData->playerNum, from, blData->health);
                }
            }
            break;

        case OBJECTTYPE_BULLET:
            // The explosives that go off with it
            switch (((BU_tag *)obj->extraObjectData)->wpnDef->weaponVariantNum) {
            case 0x2b:
            case 0x34: case 0x35: case 0x36: case 0x37:
            case 0x3a:
                Bullet_handle_object_destruction(obj, from);
                break;
            }
            break;

        case OBJECTTYPE_BREAK:
            hit->dmgAmt = hurt;
            Break_ApplyDamage(obj, hit);
            break;

        case OBJECTTYPE_DESTROY:
            if (!(hurt < 1.0f) && !(((DESTROY_tag *)obj->extraObjectData)->mask & 0x20))
                Destroy_Smash(obj, from, 0x20);
            break;

        case OBJECTTYPE_CAR:
            ((CAR_INFO *)obj->extraObjectData)->damageAmt += hurt;
            break;

        case OBJECTTYPE_MPOBJECT:
            MP_ApplyDamage(obj, hurt, from);
            break;

        case OBJECTTYPE_GUNTURRET:
            GT_ApplyDamage(obj, hurt);
            GT_Disable(hit->hitObj);
            break;

        case OBJECTTYPE_SENSOR:
            Sensor_ApplyDamage(obj, hurt);
            break;

        case OBJECTTYPE_MONITOR:
            Monitor_ApplyDamage(obj, hurt);
            break;

        case OBJECTTYPE_ANIMOBJECT: {
            // A submarine's anim object
            obj_tag *sub = (obj_tag *)obj->extraObjectData;
            if (sub != NULL && sub->objectType == OBJECTTYPE_SUB)
                Sub_ApplyDamage(sub, hurt);
            break;
        }

        case OBJECTTYPE_MINE:
            Coll_AddHitToList(&obj->hitList, EXPLODE_KILL_DIST, NULL, from)->dmgAmt = hurt;
            break;
        }
    }

    Collide_FreeHitList(&hitList);
}

// AUTOINJECT
void Explode_Delete(obj_tag *obj) {
    Script_Free(((EXPLODE_tag *)obj->extraObjectData)->script);
}

// The damage is spread once, at the first collision update
// AUTOINJECT
void Explode_CollisionHandler(obj_tag *obj) {
    EXPLODE_tag *data = (EXPLODE_tag *)obj->extraObjectData;
    if (data->propagated)
        return;
    data->propagated = 1;
    Explode_Propagate(&obj->position, data->radius, data->cel, data->damage, data->from);
}
