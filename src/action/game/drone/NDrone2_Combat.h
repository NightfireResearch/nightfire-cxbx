#ifndef NDRONE2_COMBAT_H
#define NDRONE2_COMBAT_H

#include "NDrone2.h"

struct CoverNode_tag;

// Combat and aiming (NDrone2_Combat.cpp): the first reaction to an opponent, the talk while attacking, the combat
// moves a drone can make and which it chooses, cover, and the combat and aiming states.

// The first attack
short DroneFunc_FirstAttack(DCVars_tag *dcv);
short DroneFunc_AttackNoWeapon(DCVars_tag *dcv);
// The originals take dcv in ESI (FirstSightState) and EAX (FirstAlertState): the game reaches the adaptors, C++ calls
// the cores
short _DroneFunc_FirstSightState(DCVars_tag *dcv);
void DroneFunc_FirstSightState(void);
short _DroneFunc_FirstAlertState(DCVars_tag *dcv);
void DroneFunc_FirstAlertState(void);
short DroneFunc_CombatState(DCVars_tag *dcv);

// Talk
uint DroneFunc_ChooseSFX(uint first, uint last);
uint DroneFunc_ChooseSFXByID(Drone_tag *drone, uint first, uint last);
bool DroneFunc_InRangeOfTarget(DCVars_tag *dcv, float range);
char DroneFunc_WeaponInHand(DCVars_tag *dcv);
bool NDrone2_CheckSurrender(DCVars_tag *dcv);
void NDrone2_SendAttackMessage(DCVars_tag *dcv);
bool NDrone2_AttackTalk(DCVars_tag *dcv);
bool NDrone2_SeenPlayerTalk(DCVars_tag *dcv);

// What the drone can do
bool NDrone2_StepAllowed(DCVars_tag *dcv);
bool NDrone2_StrafeAllowed(DCVars_tag *dcv);
bool NDrone2_StrafeDodgeAllowed(DCVars_tag *dcv);
bool NDrone2_RollAllowed(DCVars_tag *dcv);
bool NDrone2_CanCrouchWalk(DCVars_tag *dcv);
bool NDrone2_CanMove8(DCVars_tag *dcv);
short NDrone2_ChooseAimStand(void);
short NDrone2_ChooseAimCrouch(DCVars_tag *dcv);
short NDrone2_ChooseRoll(DCVars_tag *dcv);
short NDrone2_ChooseStrafe(DCVars_tag *dcv);
short NDrone2_ChooseStrafeDodge(DCVars_tag *dcv);
short NDrone2_ChooseStep(DCVars_tag *dcv);
bool NDrone2_HasOpponent(DCVars_tag *dcv);
bool NDrone2_CanAltAttack(DCVars_tag *dcv, short variant);
bool NDrone2_CanAimCrouch(DCVars_tag *dcv);
bool NDrone2_CanStepLeft(DCVars_tag *dcv);
bool NDrone2_CanStepRight(DCVars_tag *dcv);
bool NDrone2_CanStrafeLeft(DCVars_tag *dcv);
bool NDrone2_CanStrafeRight(DCVars_tag *dcv);
bool NDrone2_CanStrafeDodgeLeft(DCVars_tag *dcv);
bool NDrone2_CanStrafeDodgeRight(DCVars_tag *dcv);
bool NDrone2_CanRollLeft(DCVars_tag *dcv);
bool NDrone2_CanRollRight(DCVars_tag *dcv);
bool NDrone2_CanBackoff(DCVars_tag *dcv);
short NDrone2_ChooseCombatMove(DCVars_tag *dcv);
short NDrone2_EvasiveMove(DCVars_tag *dcv);

// Going after the opponent
void NDrone2_InvalidateAttackRoute(DCVars_tag *dcv);
uint NDrone2_MoveToObject(DCVars_tag *dcv, obj_tag *obj, float radius, char follow);
bool NDrone2_SetGoalToDroneWithLosToOpponent(DCVars_tag *dcv);
bool NDrone2_SetGoalToCloserPosition(DCVars_tag *dcv);

// Cover
bool DroneAnim_CanStandAtCover(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_ForceCrouchCover(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_CanUseCornerAsLowCover(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_CanCrouchLean(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_CanCrouchStepOut(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_CanStandLean(DCVars_tag *dcv, struct CoverNode_tag *node);
bool DroneAnim_CanStandStepOut(DCVars_tag *dcv, struct CoverNode_tag *node);
short NDrone2_CoverNodeOK(DCVars_tag *dcv, struct CoverNode_tag *node, ushort kind, char force);
bool NDrone2_GetPosForCornerCover(struct CoverNode_tag *node, ushort kind, _VECTOR *pos, _VECTOR *rot, char step);
void NDrone2_DisownCover(DCVars_tag *dcv);
bool NDrone2_CornerCoverAnimOK(DCVars_tag *dcv, struct CoverNode_tag *node);
bool NDrone2_LowCoverOK(DCVars_tag *dcv, struct CoverNode_tag *node);
bool NDrone2_GetCoverPosForTarget(struct CoverNode_tag *node, uint target, _VECTOR *pos, _VECTOR *rot, char step);
void NDrone2_TakeCover(DCVars_tag *dcv, char kind);
bool NDrone2_FindCover(DCVars_tag *dcv, char force);
bool NDrone2_CoverAvailable(DCVars_tag *dcv, char force);

// The combat walk
bool DroneAnim_InTransition(Drone_tag *drone);
int DroneAnim_GetMoveQuad(float angle, ushort prev);
bool DroneAnim_SetCombatMoveAnim(DCVars_tag *dcv, float distance);

// The states. Each returns whether it handled the message; the dispatcher reads only AL.
bool NDrone2_DSTATE_Attack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Alerted1stEncounter(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Combat(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatNoMove(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatOutOfRange(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatNewSighting(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatNoSight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatTooClose(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatWait(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CombatNoRoute(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_NoOpponent(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Obstructed(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DroneStuck(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_DrawWeapon(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimStand(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimStandFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimStandReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimStandDiscard(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_Prone(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_ProneFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimBackoff(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_CrouchCover(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimCrouchFire(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AimCrouchReload(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_AltAttack(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StepAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StepAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StrafeAimLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StrafeAimRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StrafeDodgeLeft(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_StrafeDodgeRight(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_RollLeftCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);
bool NDrone2_DSTATE_RollRightCrouch(DCVars_tag *dcv, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg);

void DroneFunc_SetWeaponInHand(DCVars_tag *dcv, char weapon);

#endif // NDRONE2_COMBAT_H
