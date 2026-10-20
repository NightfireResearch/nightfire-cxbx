#ifndef NDRONE2_DAMAGE_H
#define NDRONE2_DAMAGE_H

#include "NDrone2.h"
#include "../obj/bullet.h"

// What hits a drone: its collision's hits (NDrone2_DealWithObjHit), the damage they do (NDrone2_HitDamage,
// NDrone2_DoHitEffects) and how a state takes an impact message (DroneFunc_HandleImpact and the three impacts).
// See docs/drone/architecture/README.md.

// The original takes the drone in EAX: the game reaches the adaptor, C++ calls the core
void _NDrone2_DealWithObjHit(Drone_tag *drone, obj_tag *obj, HITDATA_tag *hit);
void NDrone2_DealWithObjHit(void);
void Drone_BulletHit(obj_tag *obj, BU_tag *bullet, HITDATA_tag *hit);
void Drone_ExplosiveHit(obj_tag *obj, DroneImpact_tag *impact);

void NDrone2_HitDamage(DCVars_tag *dcv, float damage, int location, int weapon, char force);
void NDrone2_DoHitEffects(DCVars_tag *dcv, HITDATA_tag *hit, float scale);
void DroneFunc_SendHurtMessage(DCVars_tag *dcv);
void NDrone2_LogHitByOpponent(DCVars_tag *dcv);
void NDrone2_LogHitByOpponentOrAlive(DCVars_tag *dcv);

bool NDrone2_PunchImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim);
bool NDrone2_BulletImpact(DCVars_tag *dcv, HITDATA_tag *hit, uint nextState, char anim);
bool NDrone2_ExplosiveImpact(DCVars_tag *dcv, DroneImpact_tag *impact, uint nextState, char anim);
bool DroneFunc_HandleImpact(DCVars_tag *dcv, MsgObject *msg, uint nextState, char anim);

#endif // NDRONE2_DAMAGE_H
