#ifndef DRONEMODES_H_
#define DRONEMODES_H_

#include "NDrone2.h"

// DroneModes.cpp: a drone's mode and type (docs/drone/architecture/A2-mode-and-type-tables.md)
void NDrone2_DoModeSettingsOLD(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars);
void NDrone2_DoModeSettingsNEW(Drone_tag *drone, uint mode, uint *behaviour, DIVars_tag *diVars);
void NDrone2_GetDroneTypeAttackTypeFriend(Drone_tag *drone);
void NDrone2_DoTypeSettingsOLD(Drone_tag *drone, uchar dtype);
short NDrone2_DefaultInitialState(Drone_tag *drone);
short NDrone2_ChangeToAttackMode(Drone_tag *drone);

// The mode initialisers (DroneModeSettings.init). DeleteMe, Ninja and Bot have the game's shared empty function.
void NDrone2_init_DMODE_Defaults(Drone_tag *drone);
void NDrone2_init_DMODE_Sniper(Drone_tag *drone);
void NDrone2_init_DMODE_Stealth(Drone_tag *drone);
void NDrone2_init_DMODE_RunToPoint(Drone_tag *drone);
void NDrone2_init_DMODE_HostageKiller(Drone_tag *drone);
void NDrone2_init_DMODE_HostageTied(Drone_tag *drone);
void NDrone2_init_DMODE_JustStand4Demo(Drone_tag *drone);
void NDrone2_init_DMODE_Civilian(Drone_tag *drone);
void NDrone2_init_DMODE_CivilianScared(Drone_tag *drone);
void NDrone2_init_DMODE_MissionFailer(Drone_tag *drone);
void NDrone2_init_DMODE_Mayhew(Drone_tag *drone);
void NDrone2_init_DMODE_AlarmRaiser(Drone_tag *drone);
void NDrone2_init_DMODE_SearchLight(Drone_tag *drone);
void NDrone2_init_DMODE_Ambush(Drone_tag *drone);
void NDrone2_init_DMODE_Zoe(Drone_tag *drone);
void NDrone2_init_DMODE_PartyGirl(Drone_tag *drone);
void NDrone2_init_DMODE_CivilianGuard(Drone_tag *drone);
void NDrone2_init_DMODE_CivDoorGuard(Drone_tag *drone);
void NDrone2_init_DMODE_CastleChatGuard1(Drone_tag *drone);
void NDrone2_init_DMODE_CastleChatGuard2(Drone_tag *drone);
void NDrone2_init_DMODE_SniperAlert(Drone_tag *drone);
void NDrone2_init_DMODE_PartyGirlLooker(Drone_tag *drone);

// The type initialisers (DroneTypeSettings.init)
void NDrone2_initDTYPE_Sniper(Drone_tag *drone);
void NDrone2_initDTYPE_SniperAlert(Drone_tag *drone);

#endif // DRONEMODES_H_
