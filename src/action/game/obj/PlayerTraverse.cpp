#include "Player.h"
#include "Wire.h"
#include "CreepWall.h"
#include "../../engine/Anim.h"
#include "../../engine/viewer.h"
#include "../../math/X87Ops.h"
#include "../../input.h"
#include "../../game.h"
#include "../mp/multiplayer.h"
#include "../sp/SwitchChannels.h"
#include "control.h"
#include "../../sound/music.h"
#include <math.h>
#include <string.h>
#include <bit>

#pragma fp_contract(off)

// The player's ways across: ladders, the grapple, wires (hung from hand over hand, or slid down as ziplines) and creep
// walls (edged along with the back to the wall). On wires and walls the camera goes behind the player
// (camera_tracking, camera mode 0xb).

// The originals, called by address
#define AnimSetDeleteAll ((void (__cdecl *)(obj_tag *obj))0x00017820)
#define AnimScriptAdd ((sAnimScript_tag *(__cdecl *)(obj_tag *obj, HASHCODE hashcode))0x00017220)
#define AnimScriptAppendBlend ((sAnimScript_tag *(__cdecl *)(obj_tag *obj, HASHCODE hashcode))0x00015ca0)
#define AnimScriptIsStopped ((int (__cdecl *)(obj_tag *obj))0x00012780)
#define AnimObjectUpdate ((bool (__cdecl *)(obj_tag *obj))0x00018690)
#define vecutil_calculate_closest_point_on_line ((double (__cdecl *)(_VECTOR *out, _VECTOR *a, _VECTOR *b, _VECTOR *p))0x000d7d00)
#define Vec_Dist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e20)
#define Vec_SqDist2D ((double (__cdecl *)(_VECTOR *a, _VECTOR *b))0x000d4e50)

#define HALF_PI 1.5707964f
#define PI 3.1415927f
#define THREE_HALF_PI 4.712389f

// The weapon id the player holds on a ladder (Player_LeaveLadder gives back the one before)
#define WEAPON_LADDER 0x47

// The wire anim playing, which Player_Wire chooses the next one by
// XBE_GLOBAL(0x00278e54, 0x4)
HASHCODE WireAnim;

#define ANIM_ROPE_GRAB        ((HASHCODE)0x060005e5)
#define ANIM_ROPE_HANG_A      ((HASHCODE)0x060005ec)
#define ANIM_ROPE_HANG_B      ((HASHCODE)0x060005ed)
#define ANIM_ROPE_TURN_A      ((HASHCODE)0x060005ee)
#define ANIM_ROPE_TURN_B      ((HASHCODE)0x060005ea)
#define ANIM_ROPE_MOVE_A      ((HASHCODE)0x060000b5)
#define ANIM_ROPE_MOVE_B      ((HASHCODE)0x060000b4)
#define ANIM_ROPE_GRAB_MOTION ((HASHCODE)0x04000536)   // the grab's root motion
#define ANIM_ZIPLINE_GRAB     ((HASHCODE)0x06000095)
#define ANIM_ZIPLINE_SLIDE    ((HASHCODE)0x06000096)
#define ANIM_ZIPLINE_LAND     ((HASHCODE)0x06000094)
#define ANIM_CREEP_START      ((HASHCODE)0x06000476)
#define ANIM_CREEP_STOP_1     ((HASHCODE)0x0600046c)
#define ANIM_CREEP_STOP_2     ((HASHCODE)0x0600046e)
#define ANIM_CREEP_START_1    ((HASHCODE)0x0600046d)
#define ANIM_CREEP_STEP_1     ((HASHCODE)0x06000471)
#define ANIM_CREEP_START_2    ((HASHCODE)0x0600046f)
#define ANIM_CREEP_STEP_2     ((HASHCODE)0x06000475)

// How far below the wire the player hangs, and the grab's root motion's x off the hand
#define ROPE_HANG_DROP 1.0207f
#define ZIPLINE_HANG_DROP 1.17363f
static const float kWire1Rise = std::bit_cast<float>(0x3f8fcfd4u);    // 1.12353, a bit under
static const float kGrabMotionX = std::bit_cast<float>(0x3da7ef9du);  // 0.082, a bit under
#define ROPE_HAND 0.0542f

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

// How far apart two angles are, as the original works it out: it calls Vec_AngleDifference again for the size
static float AngleBetween(float a, float b) {
    if (!(Vec_AngleDifference(a, b) >= 0.0f))
        return -Vec_AngleDifference(a, b);
    return Vec_AngleDifference(a, b);
}

// The ends of a wire or wall: its centre, plus and minus offset
static void Ends(_VECTOR *offset, _VECTOR *endA, _VECTOR *endB) {
    endA->x = endA->x + offset->x;
    endA->y = endA->y + offset->y;
    endA->z = endA->z + offset->z;
    endB->x = endB->x - offset->x;
    endB->y = endB->y - offset->y;
    endB->z = endB->z - offset->z;
}

// The same along the ground only, for the lines laid flat
static void EndsXZ(_VECTOR *offset, _VECTOR *endA, _VECTOR *endB) {
    endA->x = endA->x + offset->x;
    endA->z = endA->z + offset->z;
    endB->x = endB->x - offset->x;
    endB->z = endB->z - offset->z;
}

// ---------------------------------------------------------------------------------------------------------------
// Ladders
// ---------------------------------------------------------------------------------------------------------------

// Off the ladder: a second before any ladder takes the player again, walking, his weapon back. The name is ours.
// FUNC_AT(0x000aeac0)
void Player_LeaveLadder(obj_tag *player, BLData *blData) {
    AnimObj *anim = &player->animState->animObj;
    blData->movement.y = 0.0f;
    blData->traverseWait = (char)Ftol(_FRAME_RATE);
    blData->previousSubState = player->subState;
    anim->unknown64 = 0.0f;
    Player_ChangeSubState(player, MovementType_Walk);
    AnimObjectUpdate(player);
    AnimState *animState = player->animState;
    if (animState->currentWeaponId == WEAPON_LADDER)
        animState->switchingToWeaponId = animState->prevHeldWeaponId;
}

// On a ladder (substate 1): facing it, within a right angle (turning back towards it a fifth of the way a frame unless
// aiming), carried with it as it moves; the stick climbs (not into a ceiling, and down off the floor lets go), jump
// lets go backwards and past the top the player climbs off forwards; below the bottom he lets go.
// AUTOINJECT
void Player_Climb(BLData *blData, obj_tag *player) {
    obj_tag *ladder = blData->attachedToSpecialMovementItem;
    float difference = Vec_AngleDifference(player->rotation.y, ladder->rotation.y + PI);
    if (difference > HALF_PI)
        player->rotation.y = (float)((double)blData->attachedToSpecialMovementItem->rotation.y + PI - HALF_PI);
    if (difference < -HALF_PI)
        player->rotation.y = blData->attachedToSpecialMovementItem->rotation.y + THREE_HALF_PI;
    if (!(player->animState->animFlags & 1))
        player->rotation.y = (float)((double)difference * 0.2f + player->rotation.y);

    Player_Move(blData, player, 0.0f);
    Vec_Zero(&blData->field_0x3c);
    Vec_Add2(&player->position, Mat_Position(blData->attachedToSpecialMovementItem->transformMatrix),
             &player->position);
    Vec_Subtract(&player->position, &blData->attachedToSpecialMovementItem->lastPosition, &player->position);

    char playerNum = blData->playerNum;
    if (Input_Action(playerNum, ACTION_JUMP, 1) && Input_Actionf(blData->playerNum, ACTION_WALK_F_B, 1) < 0.05f) {
        blData->movement.z = (float)((double)0.2f / FRAME_RATE_DIV * -0.25f + blData->movement.z);
        Player_LeaveLadder(player, blData);
        return;
    }

    ladder = blData->attachedToSpecialMovementItem;
    blData->ladderTop = (float)((double)ladder->radius + ladder->centrePoint.y + 0.85f);
    double bottom = (double)ladder->centrePoint.y - ladder->radius + 1.0f;
    blData->ladderBottom = (float)bottom;
    if (player->position.y > blData->ladderTop) {
        blData->movement.z = (float)((double)0.2f / FRAME_RATE_DIV * 3.0f + blData->movement.z);
        Player_LeaveLadder(player, blData);
        return;
    }
    if (bottom > player->position.y) {
        Player_LeaveLadder(player, blData);
        return;
    }

    float climb = Input_Actionf(blData->playerNum, ACTION_WALK_F_B, 1);
    char flags = player->animState->animFlags;
    if ((flags & ANIM_CONTACT_ABOVE) && climb > 0.0f)
        climb = 0.0f;
    if ((flags & ANIM_CONTACT_FLOOR) && climb < 0.0f) {
        Player_LeaveLadder(player, blData);
        return;
    }
    blData->movement.y = (float)((double)FRAME_RATE_MUL * climb * 0.02f + blData->movement.y);
}

// ---------------------------------------------------------------------------------------------------------------
// The grapple
// ---------------------------------------------------------------------------------------------------------------

// BLData::grappleState
#define GRAPPLE_NONE    0
#define GRAPPLE_PULLING 1
#define GRAPPLE_HOLDING 2   // arrived, held while fire is
#define GRAPPLE_LET_GO  4

#define ROPE_ENTITY ((HASHCODE)0x02000138)

// Clears the grapple and makes its rope. The original takes the player's data in ESI.
void _Player_InitGrapple(BLData *blData, obj_tag *player) {
    memset(&blData->grapplePoint, 0, 0x20);
    blData->rope = Control_CreateObjEx(0, NULL, NULL, NULL, hashtable_hashcode_to_celglist(ROPE_ENTITY), player, 0, 5,
                                       1.0f, 0x30, 0xff, 0xff, 0xff);
}

// AUTOLTCG
void __declspec(naked) Player_InitGrapple(void) {
    _asm {
        push dword ptr [esp + 4]
        push esi
        call _Player_InitGrapple
        add esp, 8
        ret
    }
}

// The grapple has caught at point on grappleObj: the player is pulled towards it (the grapple substate), and the floor
// cannot stop the pull for 15 frames.
// AUTOINJECT
void Player_SetGrapplePoint(BLData *blData, _VECTOR *point, obj_tag *grappleObj) {
    blData->grappleObj = grappleObj;
    Vec_Copy(point, &blData->grapplePoint);
    Player_ChangeSubState(glb_players[blData->playerNum], MovementType_Grapple);
    blData->grappleSpeed = 0.0f;
    blData->grappleState = GRAPPLE_PULLING;
    blData->grappleWait = 15;
}

// On the grapple (substate 2): pulled towards the point, faster by 0.05 a frame up to 1.5 (0.5 in multiplayer, where
// losing sight of it lets go) and held there within 2 of it while fire is held. Landing lets go once the wait is
// over. The point moves with what it caught on, and the player with it. The original takes the player's data in EAX
// and the player in EDI.
void _Player_SSGrapple(BLData *blData, obj_tag *player) {
    _VECTOR *point = &blData->grapplePoint;
    Player_Move(blData, player, 1.0f);
    if (blData->grappleWait != 0)
        blData->grappleWait--;

    switch ((ushort)blData->grappleState) {
        case GRAPPLE_PULLING:
            if (MPSettings.isMultiplayer != 0 &&
                !Collide_LineOfSight(&player->position, point, player->inCel, player, blData->grappleObj, 8)) {
                blData->grappleState = GRAPPLE_LET_GO;
            }
            else if ((player->animState->animFlags & ANIM_CONTACT_FLOOR) && blData->grappleWait == 0) {
                blData->grappleState = GRAPPLE_LET_GO;
            }
            else if (Vec_Dist3D(&player->position, point) < 2.0f) {
                blData->grappleState = GRAPPLE_HOLDING;
            }
            else {
                double speed = (double)blData->grappleSpeed + 0.05f;
                float top = MPSettings.isMultiplayer != 0 ? 0.5f : 1.5f;
                if (top < speed)
                    speed = top;
                blData->grappleSpeed = (float)speed;
                _VECTOR direction;
                Vec_Subtract(point, &player->position, &direction);
                Vec_Normalise(&direction, &direction);
                auxVec_AddMulR32(&player->position, &direction, blData->grappleSpeed, &player->position);
            }
            break;

        case GRAPPLE_HOLDING:
            if (!Input_Action(blData->playerNum, ACTION_FIRE, 1))
                blData->grappleState = GRAPPLE_LET_GO;
            break;

        case GRAPPLE_LET_GO:
            Player_ChangeSubState(player, MovementType_Walk);
            blData->grappleState = GRAPPLE_NONE;
            return;
    }

    Vec_Copy(&blData->movement, &blData->lastMovement);
    blData->movement.x = 0.0f;
    blData->movement.y = 0.0f;
    blData->movement.z = 0.0f;
    obj_tag *grappleObj = blData->grappleObj;
    if (grappleObj != NULL) {
        Vec_Add2(point, Mat_Position(grappleObj->transformMatrix), point);
        Vec_Subtract(point, &blData->grappleObj->lastPosition, point);
        double y = (double)Mat_Position(blData->grappleObj->transformMatrix)->y + player->position.y;
        player->position.y = (float)y;
        player->position.y = (float)(y - blData->grappleObj->lastPosition.y);
        if (blData->grappleObj->flags & 1)
            blData->grappleObj = NULL;
    }
}

// AUTOLTCG
void __declspec(naked) Player_SSGrapple(void) {
    _asm {
        push edi
        push eax
        call _Player_SSGrapple
        add esp, 8
        ret
    }
}

// The rope, every frame: hidden, unless the grapple is in hand (weapons 0x50 and 0x51) and pulling or in flight, when it
// runs from the gun to the point, scaled to the length. The original takes the player in EDI.
void _Player_GrappleSetRope(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    if (blData->rope == NULL)
        return;
    _VECTOR *point = &blData->grapplePoint;
    blData->rope->effectFlags |= FLAG_HIDDEN;
    char weapon = player->animState->currentWeaponId;
    if (weapon != 0x50 && weapon != 0x51)
        return;
    if (blData->grappleState != GRAPPLE_PULLING && !blData->movementDisabled)
        return;

    blData->rope->effectFlags &= ~FLAG_HIDDEN;
    blData->rope->effectFlags |= 0x8000;
    blData->rope->transformFlags |= TRANSFORM_MOVED;
    Vec_Copy(point, Mat_Position(blData->rope->transformMatrix));
    Vec_Copy(point, &blData->rope->position);
    _VECTOR gun, direction;
    AnimGetBoneWorldTrans(((BLData *)player->extraObjectData)->weaponObject, 0, 0, &gun, NULL);
    Vec_Subtract(point, &gun, &direction);
    Vec_Normalise(&direction, &direction);
    Mat_Align2Dir(&blData->rope->transformMatrix, &direction, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);
    blData->rope->scale = Vec_Dist3D(&gun, point);
}

// AUTOLTCG
void __declspec(naked) Player_GrappleSetRope(void) {
    _asm {
        push edi
        call _Player_GrappleSetRope
        add esp, 4
        ret
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Wires: hanging from them, and ziplines
// ---------------------------------------------------------------------------------------------------------------

// Catches the wire the player touched or jumped for, unless he has just let go of one, is dead, or is already on
// it: at the nearest point of it, hanging under it facing along it (a zipline only from its upper half, facing
// down it), weaponless, with the camera behind. A rope is grabbed with the grab anim, whose root motion moves the
// player.
// AUTOINJECT
void Player_CollWire(obj_tag *player, BLData *blData, obj_tag *wire) {
    if (blData->traverseWait != 0)
        return;
    if (blData->health <= 0.0f)
        return;

    _VECTOR grabStart, grabEnd;
    Vec_Zero(&grabStart);
    Vec_Zero(&grabEnd);
    WIRE *wireData = (WIRE *)wire->extraObjectData;
    if (wireData->type == WIRE_1)
        return;
    ushort subState = wireData->type == WIRE_ZIPLINE ? MovementType_Zipline : MovementType_Wire;
    if (player->subState == subState)
        return;

    _VECTOR endA, endB, at;
    Vec_Copy(&wire->centrePoint, &endA);
    Vec_Copy(&wire->centrePoint, &endB);
    Vec_Spherical_2_Cartesian(&at.x, wire->radius - 2.0f, wire->rotation.y + HALF_PI, wire->rotation.z);
    Ends(&at, &endA, &endB);
    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &player->position);

    if (wireData->type == WIRE_ZIPLINE) {
        _VECTOR low;
        if (endA.y < endB.y)
            Vec_Copy(&endA, &low);
        else
            Vec_Copy(&endB, &low);
        float fromLow = Vec_Dist3D(&low, &at);
        if (Vec_Dist3D(&endA, &endB) * 0.5f > fromLow)
            return;
    }

    Player_ClearInertia(player);
    Player_ChangeSubState(player, subState);
    blData->traverseStep = 6;
    blData->aimAutoLevelState = 2;
    blData->ropeFatigue = 0.0f;
    switch (wireData->type) {
        case WIRE_ROPE:
            blData->traverseStep = 0xc;
            WireAnim = ANIM_ROPE_GRAB;
            AnimScriptAdd(player, ANIM_ROPE_GRAB);
            AnimGetFrameTransRaw(ANIM_ROPE_GRAB_MOTION, 0, &grabStart);
            AnimGetFrameTransRaw(ANIM_ROPE_GRAB_MOTION, 0x170, &grabEnd);
            break;
        case WIRE_ZIPLINE:
            WireAnim = ANIM_ZIPLINE_GRAB;
            AnimScriptAdd(player, ANIM_ZIPLINE_GRAB);
            break;
    }

    float facing = player->rotation.y + HALF_PI;
    double yaw = wire->rotation.y;
    switch (wireData->type) {
        case WIRE_ROPE:
        case WIRE_1: {
            float wireYaw = wire->rotation.y - HALF_PI;
            facing = facing - HALF_PI;
            yaw = wireYaw;
            if (AngleBetween(wireYaw, facing) > HALF_PI)
                yaw = (double)wireYaw + PI;
            at.y = wireData->type == WIRE_ROPE ? at.y - ROPE_HANG_DROP : at.y + kWire1Rise;
            break;
        }
        case WIRE_ZIPLINE:
            if (endA.y < endB.y) {
                yaw = (double)wire->rotation.y + HALF_PI;
                facing = facing + HALF_PI;
            }
            else {
                yaw = (double)wire->rotation.y - HALF_PI;
                facing = facing - HALF_PI;
            }
            at.y = at.y - ZIPLINE_HANG_DROP;
            break;
    }
    player->rotation.y = (float)yaw;
    player->rotation.x = 0.0f;
    player->rotation.z = 0.0f;
    player->position = at;

    Player_WeaponNone(player);
    Player_SetCamMode(blData, CamMode_CreepWall);
    glb_viewer[blData->playerNum]->trackDistanceUsed = 0.0f;
    glb_viewer[blData->playerNum]->trackDistance = 4.0f;
    glb_viewer[blData->playerNum]->trackPitch = blData->pitchFromHorizontal * -HALF_PI;
    glb_viewer[blData->playerNum]->trackYaw = facing;
    blData->attachedToSpecialMovementItem = wire;

    // The grab's root motion, turned by the player's facing, is taken off the position
    Vec_Subtract(&grabEnd, &grabStart, &grabEnd);
    double across = (double)grabEnd.x - kGrabMotionX;
    float angle = -player->rotation.y;
    float cosine = (float)FCos(angle);
    float sine = (float)FSin(angle);
    grabEnd.x = (float)(cosine * across - (double)grabEnd.z * sine);
    grabEnd.z = (float)((double)grabEnd.z * cosine + sine * across);
    Vec_Subtract(&player->position, &grabEnd, &player->position);
    Vec_Copy(&player->position, &player->lastPosition);
}

// Hangs the player under at by one hand or the other: the hand's offset (ROPE_HAND across) turned by his facing.
// The names are ours.
// FUNC_AT(0x000afa80)
void Player_HangByHandA(obj_tag *player, _VECTOR *at) {
    double angle = Negate(player->rotation.y);
    float cosine = (float)FCos(angle);
    Extended sine = FSin(angle);
    Extended x = Extended(at->x) - (Extended((double)cosine * -ROPE_HAND) - sine * Extended(0.0f));
    at->x = (float)x;
    Extended z = Extended((double)cosine * 0.0f) - sine * Extended(ROPE_HAND);
    at->z = (float)(Extended(at->z) - z);
    player->position.x = (float)x;
    player->position.z = at->z;
    player->position.y = at->y - ROPE_HANG_DROP;
}

// FUNC_AT(0x000afaf0)
void Player_HangByHandB(obj_tag *player, _VECTOR *at) {
    double angle = Negate(player->rotation.y);
    float cosine = (float)FCos(angle);
    Extended sine = FSin(angle);
    Extended x = Extended(at->x) - (Extended((double)cosine * ROPE_HAND) - sine * Extended(0.0f));
    at->x = (float)x;
    Extended z = sine * Extended(ROPE_HAND) + Extended((double)cosine * 0.0f);
    at->z = (float)(Extended(at->z) - z);
    player->position.x = (float)x;
    player->position.z = at->z;
    player->position.y = at->y - ROPE_HANG_DROP;
}

// Changes the rope anim: one of a pair, by which hand leads now
static sAnimScript_tag *RopeAnim(obj_tag *player, _VECTOR *at, bool handB, HASHCODE anim) {
    if (handB)
        Player_HangByHandB(player, at);
    else
        Player_HangByHandA(player, at);
    WireAnim = anim;
    return AnimScriptAdd(player, anim);
}

// The rope's traverse steps (BLData::traverseStep)
#define ROPE_STILL   6
#define ROPE_LEFT    7
#define ROPE_RIGHT   8
#define ROPE_TURNED  10
#define ROPE_GRAB    0xc

// Hanging from a rope (substate 6), hand over hand along it: the stick moves the player sideways (not off its ends),
// turning to face along it first; jump, or tiring (100 over 30 seconds of hanging), lets go. Sets the rope's switch
// channel while on it.
// AUTOINJECT
void Player_Wire(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    sAnimScript_tag *script = NULL;
    Vec_Zero(&blData->field_0x3c);
    WIRE *wireData = (WIRE *)blData->attachedToSpecialMovementItem->extraObjectData;
    Music_Event(0xd, 1);
    if (wireData->channel != 0)
        switch_channels[wireData->channel] = 1;

    obj_tag *wire = blData->attachedToSpecialMovementItem;
    _VECTOR endA, endB, at, offset;
    Vec_Copy(&wire->centrePoint, &endA);
    Vec_Copy(&wire->centrePoint, &endB);
    Vec_Spherical_2_Cartesian(&offset.x, wire->radius - 0.5f, wire->rotation.y + HALF_PI, 0.0f);
    EndsXZ(&offset, &endA, &endB);

    // Off either end, the player is put back on it
    float length = (float)Vec_SqDist2D(&endA, &endB);
    float fromA = (float)Vec_SqDist2D(&endA, &player->position);
    double fromB = Vec_SqDist2D(&endB, &player->position);
    char atEndA = 0;
    char atEndB = 0;
    if (fromA > length) {
        atEndB = 1;
        player->position.x = endB.x;
        player->position.z = endB.z;
    }
    if (fromB > length) {
        atEndA = 1;
        player->position.x = endA.x;
        player->position.z = endA.z;
    }
    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &player->position);
    if (Vec_SqDist2D(&endA, &player->position) <= 0.5f)
        atEndA = 1;
    if (Vec_SqDist2D(&endB, &player->position) <= 0.5f)
        atEndB = 1;

    if (blData->traverseStep != ROPE_GRAB)
        blData->ropeFatigue = (float)(100.0f / ((double)_FRAME_RATE * 30.0f) + blData->ropeFatigue);

    if (Input_Action(blData->playerNum, ACTION_JUMP, 4) || blData->ropeFatigue > 100.0f) {
        blData->previousSubState = player->subState;
        Player_ChangeSubState(player, MovementType_Walk);
        Player_SetCamMode(blData, CamMode_Default);
        glb_viewer[blData->playerNum]->trackDistance = 0.0f;
        blData->traverseWait = 30;
        blData->ropeFatigue = 0.0f;
        AnimState *anim = player->animState;
        anim->switchingToWeaponId = anim->prevHeldWeaponId;
        if (wireData->channel != 0)
            switch_channels[wireData->channel] = 0;
        return;
    }

    float stick = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, 1);
    short step;
    if (stick < 0.0f && atEndA)
        step = ROPE_STILL;
    else if (stick > 0.0f)
        step = atEndB ? ROPE_STILL : ROPE_LEFT;
    else if (stick < 0.0f)
        step = ROPE_RIGHT;
    else
        step = ROPE_STILL;

    float facing = player->rotation.y;
    if (step == ROPE_LEFT)
        facing = blData->attachedToSpecialMovementItem->rotation.y + THREE_HALF_PI;
    else if (step == ROPE_RIGHT)
        facing = blData->attachedToSpecialMovementItem->rotation.y + HALF_PI;

    if (blData->traverseStep == ROPE_STILL && step == ROPE_STILL) {
        if (!AnimScriptIsStopped(player))
            return;
        if (WireAnim == ANIM_ROPE_GRAB || WireAnim == ANIM_ROPE_HANG_A || WireAnim == ANIM_ROPE_MOVE_B ||
            WireAnim == ANIM_ROPE_TURN_A)
            RopeAnim(player, &at, false, ANIM_ROPE_HANG_A);
        else
            RopeAnim(player, &at, true, ANIM_ROPE_HANG_B);
        return;
    }
    if (blData->traverseStep != ROPE_STILL && !AnimScriptIsStopped(player))
        return;

    if (step == ROPE_LEFT || step == ROPE_RIGHT) {
        bool handB = WireAnim == ANIM_ROPE_MOVE_A || WireAnim == ANIM_ROPE_HANG_B;
        if (AngleBetween(facing, player->rotation.y) > 0.0002f && WireAnim != ANIM_ROPE_TURN_A &&
            WireAnim != ANIM_ROPE_TURN_B) {
            if (handB)
                script = RopeAnim(player, &at, true, ANIM_ROPE_TURN_B);
            else
                script = RopeAnim(player, &at, false, ANIM_ROPE_TURN_A);
        }
        else {
            player->rotation.y = facing;
            if (handB)
                script = RopeAnim(player, &at, true, ANIM_ROPE_MOVE_B);
            else
                script = RopeAnim(player, &at, false, ANIM_ROPE_MOVE_A);
        }
    }
    else if (blData->traverseStep >= ROPE_LEFT && blData->traverseStep <= ROPE_RIGHT) {
        // Stopped after turning to move: face the wire again
        if (WireAnim == ANIM_ROPE_TURN_B || WireAnim == ANIM_ROPE_TURN_A) {
            obj_tag *attached = blData->attachedToSpecialMovementItem;
            if (AngleBetween(player->rotation.y, attached->rotation.y + HALF_PI) < 0.0002f)
                player->rotation.y = blData->attachedToSpecialMovementItem->rotation.y + THREE_HALF_PI;
            else
                player->rotation.y = blData->attachedToSpecialMovementItem->rotation.y + HALF_PI;
        }
        step = ROPE_TURNED;
    }

    blData->traverseStep = (char)step;
    if (script != NULL)
        script->speed = script->speed >= 0.0f ? 1.2f : -1.2f;
}

// Sliding down a zipline (substate 15): along it, at the hanging height, until within 1 of its lower end, where the
// landing anim plays; when it ends the player is off, walking, his weapon back, a second before he can catch another.
// Sets the line's switch channel while on it.
// AUTOINJECT
void Player_Zipline(obj_tag *player) {
    BLData *blData = (BLData *)player->extraObjectData;
    obj_tag *line = blData->attachedToSpecialMovementItem;
    WIRE *lineData = (WIRE *)line->extraObjectData;

    if (blData->traverseStep == 9) {
        if (!AnimScriptIsStopped(player))
            return;
        blData->previousSubState = player->subState;
        Player_ChangeSubState(player, MovementType_Walk);
        Player_SetCamMode(blData, CamMode_Default);
        glb_viewer[blData->playerNum]->trackDistance = 0.0f;
        blData->traverseWait = (char)Ftol(_FRAME_RATE);
        AnimState *anim = player->animState;
        anim->switchingToWeaponId = anim->prevHeldWeaponId;
        if (lineData->channel != 0)
            switch_channels[lineData->channel] = 0;
        return;
    }

    if (lineData->channel != 0)
        switch_channels[lineData->channel] = 1;
    if (AnimScriptIsStopped(player)) {
        WireAnim = ANIM_ZIPLINE_SLIDE;
        AnimScriptAdd(player, ANIM_ZIPLINE_SLIDE);
    }

    _VECTOR endA, endB, offset, at;
    Vec_Copy(&line->centrePoint, &endA);
    Vec_Copy(&line->centrePoint, &endB);
    Vec_Spherical_2_Cartesian(&offset.x, line->radius, line->rotation.y + HALF_PI, line->rotation.z);
    endA.x = endA.x + offset.x;
    double endAY = (double)endA.y + offset.y;
    endA.y = (float)endAY;
    endA.z = endA.z + offset.z;
    endB.x = endB.x - offset.x;
    endB.y = endB.y - offset.y;
    endB.z = endB.z - offset.z;
    player->position.y = player->position.y + ZIPLINE_HANG_DROP;

    // The lower end (the y compared before it is stored)
    if (endAY < endB.y)
        Vec_Copy(&endA, &at);
    else
        Vec_Copy(&endB, &at);
    if (Vec_Dist3D(&player->position, &at) < 1.0f) {
        blData->traverseStep = 9;
        WireAnim = ANIM_ZIPLINE_LAND;
        AnimScriptAdd(player, ANIM_ZIPLINE_LAND);
    }
    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &player->position);
    player->position.x = at.x;
    player->position.z = at.z;
    player->position.y = at.y - ZIPLINE_HANG_DROP;
    blData->movement.z = (float)((double)FRAME_RATE_MUL * 0.15f + blData->movement.z);
}

// ---------------------------------------------------------------------------------------------------------------
// Creep walls
// ---------------------------------------------------------------------------------------------------------------

// The creep's traverse steps (BLData::traverseStep): still, or moving one way or the other
#define CREEP_STILL 0
#define CREEP_1     1
#define CREEP_2     2

#define CREEP_STAND_OFF 0.275f  // how far from the wall the player stands

// Takes the player onto a creep wall (when he uses it, or Player_Enable asks), unless he has just let go of one, is
// dead, or is creeping or on a wire: at the nearest point of it, his back to it, standing on the floor below (found
// within 5), weaponless, with the camera behind (the view it had kept to go back to).
// AUTOINJECT
void Player_CollCreepWall(obj_tag *player, BLData *blData, obj_tag *wall) {
    if (blData->traverseWait != 0)
        return;
    if (blData->health <= 0.0f)
        return;
    if (player->subState == MovementType_Creep || player->subState == MovementType_Wire)
        return;

    viewer_tag *viewer = glb_viewer[blData->playerNum];
    _VECTOR endA, endB, at;
    Vec_Copy(&wall->centrePoint, &endA);
    Vec_Copy(&wall->centrePoint, &endB);
    float wallYaw = wall->rotation.y + HALF_PI;
    Vec_Spherical_2_Cartesian(&at.x, wall->radius, wallYaw, 0.0f);
    EndsXZ(&at, &endA, &endB);
    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &player->position);

    float rotationBefore = player->rotation.y;
    player->rotation.x = 0.0f;
    player->rotation.z = 0.0f;
    player->position.x = at.x;
    player->rotation.y = wallYaw + HALF_PI;
    player->position.z = at.z;

    // The floor, by a ray from the line down 5
    _VECTOR below;
    Vec_Copy(&at, &below);
    below.y = below.y - 5.0f;
    HITDATA_tag *hitList = NULL;
    if (Collide_RayIntersect(&at, &below, player->inCel, player, wall, &hitList, 0, 0x62d, 0))
        player->position.y = hitList->hitPosition.y + 1.0f;
    Collide_FreeHitList(&hitList);

    Player_WeaponNone(player);
    Player_ClearInertia(player);
    Player_ChangeSubState(player, MovementType_Creep);
    blData->traverseStep = CREEP_STILL;
    blData->aimAutoLevelState = 2;
    Player_SetCamMode(blData, CamMode_CreepWall);
    viewer->savedTrackDistance = viewer->trackDistance;
    viewer->savedTrackPitch = viewer->trackPitch;
    viewer->savedTrackYaw = viewer->trackYaw;
    viewer->trackDistance = 4.0f;
    viewer->trackPitch = blData->pitchFromHorizontal * -HALF_PI;
    viewer->trackYaw = rotationBefore + PI;
    AnimScriptAdd(player, ANIM_CREEP_START);
    blData->attachedToSpecialMovementItem = wall;
}

// Creeping along a wall (substate 7): the stick, pushed at least halfway, edges the player along it, off either end
// (within 1 of it) turning him to walk away, a few frames before he can take a wall again. Sets the wall's switch
// channel while on it.
// AUTOINJECT
void Player_Creep(BLData *blData, obj_tag *player) {
    char atEnd = 0;
    sAnimScript_tag *script = NULL;
    CREEPWALL *wallData = (CREEPWALL *)blData->attachedToSpecialMovementItem->extraObjectData;
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    Music_Event(0xd, 1);
    if (wallData->channel != 0)
        switch_channels[wallData->channel] = 1;

    obj_tag *wall = blData->attachedToSpecialMovementItem;
    _VECTOR endA, endB, at;
    Vec_Copy(&wall->centrePoint, &endA);
    Vec_Copy(&blData->attachedToSpecialMovementItem->centrePoint, &endB);
    wall = blData->attachedToSpecialMovementItem;
    Vec_Spherical_2_Cartesian(&at.x, wall->radius, wall->rotation.y + HALF_PI, 0.0f);
    EndsXZ(&at, &endA, &endB);
    vecutil_calculate_closest_point_on_line(&at, &endA, &endB, &player->position);
    float fromA = (float)Vec_Dist2D(&endA, &at);
    double fromB = Vec_Dist2D(&endB, &at);
    if (fromA < 1.0f)
        atEnd = 1;
    if (fromB < 1.0f)
        atEnd = 2;
    char nearer = fromA < fromB ? 1 : 2;

    // His back to the wall, a little off it
    double facing = (double)blData->attachedToSpecialMovementItem->rotation.y + PI;
    player->rotation.y = (float)facing;
    player->position.x = (float)(FSin(facing) * Extended(CREEP_STAND_OFF) + Extended(at.x));
    player->position.z = (float)(FCos(player->rotation.y) * Extended(CREEP_STAND_OFF) + Extended(at.z));

    float stick = Input_Actionf(blData->playerNum, ACTION_WALK_L_R, 1);
    short direction = CREEP_STILL;
    float magnitude = stick < 0.0f ? -stick : stick;
    if (!(magnitude < 0.5f)) {
        if (stick > 0.0f)
            direction = CREEP_2;
        else if (stick < 0.0f)
            direction = CREEP_1;
    }

    if ((atEnd == 1 && direction == CREEP_2) || (atEnd == 2 && direction == CREEP_1)) {
        // Off the end: walking away from the wall
        blData->previousSubState = player->subState;
        Player_ChangeSubState(player, MovementType_Walk);
        Player_SetCamMode(blData, CamMode_Default);
        viewer->trackDistance = 0.0f;
        viewer->trackYaw = viewer->trackYaw - PI;
        if (nearer == 1)
            player->rotation.y = player->rotation.y - HALF_PI;
        else
            player->rotation.y = player->rotation.y + HALF_PI;
        blData->traverseWait = 0x78;
        AnimState *anim = player->animState;
        anim->switchingToWeaponId = anim->prevHeldWeaponId;
        blData->movement.z = blData->movement.z + 0.05f;
        if (wallData->channel != 0)
            switch_channels[wallData->channel] = 0;
        return;
    }

    char step = blData->traverseStep;
    if (step == CREEP_1 && direction == CREEP_2)
        direction = CREEP_STILL;
    if (step == CREEP_2 && direction == CREEP_1)
        direction = CREEP_STILL;
    if (!AnimScriptIsStopped(player) && direction != CREEP_STILL)
        return;

    switch (direction) {
        case CREEP_STILL:
            switch (blData->traverseStep) {
                case CREEP_1:
                    AnimScriptAppendBlend(player, ANIM_CREEP_STOP_1);
                    break;
                case CREEP_2:
                    AnimScriptAppendBlend(player, ANIM_CREEP_STOP_2);
                    break;
            }
            break;
        case CREEP_1:
            if (blData->traverseStep == CREEP_2 || blData->traverseStep == CREEP_STILL)
                script = AnimScriptAdd(player, ANIM_CREEP_START_1);
            else if (AnimScriptIsStopped(player))
                script = AnimScriptAdd(player, ANIM_CREEP_STEP_1);
            break;
        case CREEP_2:
            if (blData->traverseStep == CREEP_1 || blData->traverseStep == CREEP_STILL)
                script = AnimScriptAdd(player, ANIM_CREEP_START_2);
            else if (AnimScriptIsStopped(player))
                script = AnimScriptAdd(player, ANIM_CREEP_STEP_2);
            break;
    }

    blData->traverseStep = (char)direction;
    if (script != NULL) {
        script->flags &= 0x7fffffff;
        script->speed = script->speed * 1.5f;
    }
}
