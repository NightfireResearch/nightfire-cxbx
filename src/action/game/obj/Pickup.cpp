#include "Pickup.h"
#include "../mp/multiplayer.h"
#include "../drone/Drone.h"
#include "../../engine/celglist.h"

#include <string.h>

// AUTOGEN
obj_tag * Pickup_Create(_VECTOR *pos,_VECTOR *rot,_MATRIX *mtx,celglist_tag *param_4,ushort maybePickupType,ushort param_6,uint param_7,uint param_8,uint param_9,char param_10,ushort param_11,ushort param_12,uint param_13);

// AUTOGEN
int MP_RegisterPickup(obj_tag *obj);

// AUTOGEN
void MP_UnregisterPickup(obj_tag *obj);

// The game's (AUTOGEN in Player.cpp)
void Concat(float *param_1, float *param_2);

// Whether a pickup landing on hitObj should ride on it. The hit object arrives in ECX and the pickup in EAX
// (tools/abi_action.json: pops 0, regs_in eax ecx).
static bool __declspec(naked) Pickup_CanInheritPosition(obj_tag *hitObj, obj_tag *pickupObj) {
    _asm {
        mov ecx, [esp + 4]          // hitObj
        mov eax, [esp + 8]          // pickupObj
        mov edx, 0x000a76a0
        call edx
        ret
    }
}

// Gives the pickup to whoever touched it. The PICKUPINFO arrives in EDI, the other two on the stack, removed by
// the caller (pops 0, regs_in edi). EDI is callee-saved for our compiler, so it is put back afterwards.
static void __declspec(naked) Pickup_Handler(obj_tag *obj, PICKUPINFO *pickup, obj_tag *picker) {
    _asm {
        push edi
        mov edi, [esp + 12]         // pickup
        push dword ptr [esp + 16]   // picker
        push dword ptr [esp + 12]   // obj (12 again: the push moved it along)
        mov eax, 0x000a7710
        call eax
        add esp, 8
        pop edi
        ret
    }
}

// The original compares and negates, which keeps the sign of -0 (a compiled fabs clears it).
static float Abs(float x) {
    if (x < 0.0f) {
        uint32_t bits;
        memcpy(&bits, &x, sizeof(bits));
        bits ^= 0x80000000;
        memcpy(&x, &bits, sizeof(bits));
    }
    return x;
}

// AUTOINJECT
void Pickup_Update(obj_tag *obj) {
    if (obj == NULL)
        return;

    PICKUPINFO *pickup = (PICKUPINFO *)obj->extraObjectData;

    if (pickup->state == PICKUP_FALLING) {
        auxVec_AddMulR32(&pickup->velocity, &GRAVITY_VECTOR, REC_FRAME_RATE * REC_FRAME_RATE, &pickup->velocity);
        _VECTOR *pos = Mat_Position(obj->transformMatrix);
        Vec_Add2(pos, &pickup->velocity, pos);
        obj->transformFlags |= TRANSFORM_MOVED;

        _VECTOR below;
        Vec_Copy(pos, &below);
        below.y -= Abs(obj->objGraphics->extentMin.y);

        HITDATA_tag *hits;
        if (Collide_RayIntersect(&obj->lastPosition, &below, obj->inCel, obj, NULL, &hits, 0, 0x50d, 0)) {
            float height = obj->transformMatrix.m[13] - hits->hitPosition.y;
            if (height < 0.0f) {
                bool onObject = false;
                for (HITDATA_tag *hit = hits; hit != NULL; hit = hit->next)
                    onObject |= Pickup_CanInheritPosition(hit->hitObj, obj);

                _VECTOR landed;
                auxVec_AddMulR32(&hits->hitPosition, &hits->maybehitDirection, -Abs(obj->objGraphics->extentMin.y),
                                 &landed);
                Vec_Add2(&landed, &pickup->centreOffset, pos);
                Vec_Copy(pos, &obj->position);
                if (landed.y != 0.0f)
                    Vec_Zero(&pickup->velocity);
                if (!onObject) {
                    pickup->state = PICKUP_READY;
                    MP_RegisterPickup(obj);
                }
            }
            Collide_FreeHitList(&hits);
        }
    } else if (pickup->state == PICKUP_RESPAWNING) {
        if (GameState.NumFramesUnpaused - obj->creationTimeFrames > pickup->respawnTime * FRAME_RATE_INT * 10) {
            obj->effectFlags &= ~FLAG_HIDDEN;
            pickup->state = PICKUP_READY;
        }
        return;
    }

    if (pickup->spins) {
        _VECTOR spin = {0.0f, REC_FRAME_RATE, 0.0f};
        _MATRIX rotation;
        Mat_IdentityT(&rotation);
        RotMatrix(&spin, &rotation);
        Concat(obj->transformMatrix.m, rotation.m);
        Mat_CopyRot(&rotation, &obj->transformMatrix);
    }

    // x87 in the original: the sum is squared unrounded
    double reach = (double)obj->radius + 1.1f;
    float reachSq = (float)(reach * reach);

    for (ushort i = 0; i < NUM_PLAYERS; i++) {
        obj_tag *player = glb_players[i];
        if (player == NULL || player->curState != 1 || player->objectType == OBJECTTYPE_DEAD_PLAYER)
            continue;
        if (Vec_SqDist3D(&obj->centrePoint, &player->position) >= reachSq)
            continue;
        if (!Collide_LineOfSight(&player->centrePoint, &obj->centrePoint, player->inCel, obj, NULL, 0x721))
            continue;
        Pickup_Handler(obj, pickup, player);
        return;
    }

    if (MPSettings.maybeDroneAIEnabled) {
        for (ushort i = 0; i < NUM_BOTS; i++) {
            obj_tag *bot = MPGame.players[NUM_PLAYERS + i].playerObj;
            if (bot == NULL || bot->objectType == OBJECTTYPE_DEAD_DRONE || MPDrone_MaybeIsDyingOrDead(bot))
                continue;
            if (Vec_SqDist3D(&obj->centrePoint, &bot->position) >= reachSq)
                continue;
            // From the bot's position, where a player's test starts from his centre
            if (!Collide_LineOfSight(&bot->position, &obj->centrePoint, bot->inCel, obj, NULL, 0x721))
                continue;
            Pickup_Handler(obj, pickup, bot);
            return;
        }
    }

    if (pickup->lifetime != 0 && --pickup->lifetime == 0) {
        obj->flags |= 1;
        MP_UnregisterPickup(obj);
    }
}
