#include "GT.h"
#include "Copter.h"
#include "Player.h"
#include "../mp/multiplayer.h"
#include "../drone/Drone.h"
#include "../drone/NDrone2.h"

// AUTOGEN
void GT_LoseControl(obj_tag *gameObj);

// AUTOGEN
void GT_Activate(obj_tag *gameObj);

// AUTOGEN
obj_tag* GT_Create(_VECTOR *pos, _VECTOR *rot, quaternion_tag *param_3, level_tag *lvl, celglist_tag *celgl, obj_tag* param_5);


// AUTOGEN
float __stdcall Vec_SqMagnitude(_VECTOR *v);

// The game's (AUTOGEN in Sprite.cpp)
void __cdecl QuickSort(void *list, uint count, uint size, COMP_FUNC compareType, undefined *customComparisonFunc);

// What GT_Track considers, nearest first once sorted (QuickSort's Compare_TargetObj)
typedef struct {
    obj_tag *obj;
    float dist;     // squared
} GT_TARGET;

#define GT_MAX_TARGETS 64

// XBE_GLOBAL(0x0025fc20, 0x200)
static GT_TARGET TargetList[GT_MAX_TARGETS];   // Ghidra: TargetList_144

// Squared distances
#define GT_TRACK_RANGE 2500.0f
#define GT_TRACK_RANGE_MP (GT_TRACK_RANGE * 0.2f)

// The turret's choice of target: of the two nearest in front of it and in sight from the end of its barrel, the
// one it already has, or else the nearest. In multiplayer every other agent is a candidate; otherwise the player
// (when his someNightVisionThing is set) and, unless the gun hangs from the ceiling, the drones and copters.
// AUTOINJECT
obj_tag* GT_Track(GUNTURRET *gun, obj_tag *obj) {
    if (gun->target != NULL && (gun->target->flags & 1))
        gun->target = NULL;
    if (gun->target != NULL && gun->target->objectType == OBJECTTYPE_DRONE && MPDrone_MaybeIsDyingOrDead(gun->target))
        gun->target = NULL;

    float range = GT_TRACK_RANGE;
    ushort numTargets = 0;
    for (int i = 0; i < GT_MAX_TARGETS; i++)
        TargetList[i].dist = 999999.0f;

    // Zeroed here only: the original leaves it uninitialised until a candidate is measured
    _VECTOR toTarget = {0.0f, 0.0f, 0.0f};

    // Each loop stops once 63 of the 64 entries are taken
    if (MPSettings.isMultiplayer) {
        range = GT_TRACK_RANGE_MP;
        for (ushort i = 0; i < NUM_AGENTS && numTargets < GT_MAX_TARGETS - 1; i++) {
            obj_tag *agent = MPGame.players[i].playerObj;
            if (agent == NULL || gun->playerController == agent)
                continue;
            if (agent->objectType == OBJECTTYPE_DRONE && ((Drone_tag *)agent->extraObjectData)->health <= 0.0f)
                continue;
            if (agent->objectType == OBJECTTYPE_PLAYER && ((BLData *)agent->extraObjectData)->health <= 0.0f)
                continue;
            Vec_Subtract(&agent->position, &obj->position, &toTarget);
            float dist = Vec_SqMagnitude(&toTarget);
            TargetList[numTargets].dist = dist;
            if (dist < range) {
                TargetList[numTargets].obj = MPGame.players[i].playerObj;
                numTargets++;
            }
        }
    } else {
        obj_tag *player = glb_players[0];
        if (gun->playerController != player && player != NULL && glb_blokes[0]->health > 0.0f) {
            Vec_Subtract(&player->position, &obj->position, &toTarget);
            TargetList[0].dist = Vec_SqDist3D(&player->position, &obj->position);
            if (TargetList[0].dist < range && glb_blokes[0]->someNightVisionThing) {
                TargetList[0].obj = player;
                numTargets = 1;
            }
        }

        if (!gun->isCeilingMountedTurret) {
            for (Drone_tag *drone = NPCGlobals.NDrone2List; drone != NULL && numTargets < GT_MAX_TARGETS - 1;
                 drone = drone->next) {
                obj_tag *target = drone->gameObj;
                if (target == NULL || MPDrone_MaybeIsDyingOrDead(target) || (target->effectFlags & FLAG_HIDDEN))
                    continue;
                Vec_Subtract(&target->position, &obj->position, &toTarget);
                float dist = Vec_SqMagnitude(&toTarget);
                TargetList[numTargets].dist = dist;
                if (dist < range) {
                    TargetList[numTargets].obj = target;
                    numTargets++;
                }
            }

            for (COPTER *copter = (COPTER *)CopterList.head; copter != NULL && numTargets < GT_MAX_TARGETS - 1;
                 copter = (COPTER *)copter->node.next) {
                obj_tag *body = Copter_GetBody(copter);
                if (body == NULL)
                    continue;
                // toTarget is not measured to the copter: it is still the last drone's (or the player's)
                float dist = Vec_SqMagnitude(&toTarget) * 0.25f;
                TargetList[numTargets].dist = dist;
                if (dist < range) {
                    TargetList[numTargets].obj = body;
                    numTargets++;
                }
            }
        }
    }

    if (numTargets == 0)
        return NULL;

    QuickSort(TargetList, numTargets, sizeof(GT_TARGET), Compare_TargetObj, NULL);

    obj_tag *nearest = NULL;
    obj_tag *second = NULL;
    _VECTOR muzzle;
    Mat_GetDir(&muzzle, &gun->barrel->transformMatrix);
    auxVec_AddMulR32(Mat_Position(gun->barrel->transformMatrix), &muzzle, gun->barrelLength, &muzzle);

    for (ushort i = 0; i < numTargets; i++) {
        Vec_Subtract(&TargetList[i].obj->centrePoint, Mat_Position(gun->unknownA0->maybeMatrix), &toTarget);
        Vec_Normalise(&toTarget, &toTarget);
        // x87 in the original, summed in this order
        double facing = (double)toTarget.y * gun->unknown78.y + (double)toTarget.z * gun->unknown78.z
                        + (double)toTarget.x * gun->unknown78.x;
        if (facing < 0.01f)
            continue;
        if (!Collide_LineOfSight(&muzzle, &TargetList[i].obj->centrePoint, obj->inCel, obj, TargetList[i].obj, 0x72f))
            continue;
        if (nearest != NULL) {
            second = TargetList[i].obj;
            break;
        }
        nearest = TargetList[i].obj;
    }

    if (gun->target != NULL && (nearest == gun->target || second == gun->target))
        return gun->target;
    return nearest;
}
