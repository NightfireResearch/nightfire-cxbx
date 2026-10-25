# Functions taken from nightfire-port

[nightfire-port](https://github.com/Jeffsteelflex/nightfire-port) (Jeffsteelflex) reimplements the PS2 build's
action engine, and its owner has given us permission to use it (16 October 2026, to be put in writing in its
next release). This file records what is taken from it.

Our authority is still `default.xbe`. Their code is a reading of the PS2's MIPS: PS2 struct layouts (`obj_tag`
0x110 bytes against the Xbox's 0xe4, `Drone_tag` 0xd20 against 0x978), PS2 maths (`PS2Sinf`, VU0, the EE's
division) and comments in Portuguese. So each function is ported from the Xbox's own code, with theirs as a guide
to what it does. A function ported that way:
- is written to docs/style-guide.md against our Xbox structures, with our tags (`AUTOINJECT`, `FUNC_AT`);
- carries the GameCube build's checks (docs/gamecube-checks.md);
- notes in this file what their reading got wrong, if anything, so it can go back to them.

## How a parcel is checked

1. `tools/demo/demo_coverage.py` lists which of the parcel's functions the time demos reach, before porting.
2. The functions are ported.
3. `tools/demo/play_demos.ps1` plays every demo. All must still match frame for frame.
4. A function the demos do not reach gets a shadow test or a replay of its own (`src/action/devtools`), or is
   listed below as unverified.

## Parcels

| # | Their files | Functions | Reached by the demos | Commit |
|---|---|---|---|---|
| 1 | script, script_support, script_player, simple_script, drone_script | 36 + 8 they inline or split differently | 31 of 36. Shadow-tested: Script_Set2Start, SP_Hit, and the three weight helpers (ScriptShadow.cpp, 650 cases). Checked against the disassembly only: Script_FreeFromHashTable. Unverified: Player_Cam2Mode, Drone_CoderCreate | see git log |

| 2 | trigger, door, sensor, player_stats | 45 + 6 (Trigger_Hit, PlrStats_GetDrivingData, Door_SetupSlide, Door_IsOpen, Door_MoveGroup, Door_CloseIfClear) | 40 of 45; 459 of 549 blocks. Every function, and the missed branches, shadow-tested (TriggerShadow, DoorShadow, SecurityShadow, PlrStatShadow: 44,050 cases, 0 differ; each suite shown to catch a planted bug) | see git log |

| 3 | drone_sm, drone_func, drone_behaviour | 32 + 8 (Drone_SM_QueueMsg, DroneAnim_CurrentMoveState, DroneAnim_SetWalkIdleAnim, NDrone2_GetHeadPosOld, NDrone2_PatrolTalk, NDrone2_PowerstationTalk, NDrone2_HandleTalking, NDrone2_FadeOut) | 30 of 32; 391 of 624 blocks. DroneAIShadow: 136,925 cases, 0 differ; 33 planted bugs, 31 caught (the other two need a NaN, or are a branch that cannot be reached) | see git log |

| 4 | ainetwork, aipath | 57 + 18 (AINetwork_AlterDestFor_DROUTE_Nearest, GetRouteDistanceBetweenNodes, OptimiseRoute, CalcRoute, SetupRouteToObject, SetupRouteToPosition, RouteNodeOffset, ResetRoute, VerifyRouteTarget, CalcRouteToObject, CalcRouteToPosition, LinkCreep_ForNodes, LinkCreep_Calc, LinkCreep_CalcToRouteEnd, both NDrone2_MoveTest, NDrone2_NavNodeCache, NDrone_InitDoorNodes) | 55 of 57; 786 of 895 blocks. NavShadow: 57,500 cases, 0 differ (10 planted bugs: 8 caught; one is in a branch that cannot change the result, one only shows in a count); AIHintsShadow: 33,491 cases, 0 differ (4 planted, all caught) | see git log |

| 5 | loadsave, menu_cheats, menu_codename | 47 names = 43 Xbox functions (their five `BIN_PullBits` are four; `P_PS2MEMCARDINIT_Handler`, `PS2_DoCardFlow` have no Xbox counterpart) + 3 (LS_FindBlockByLabel, LS_GetLoadStuff's adaptor, Menu_StartLS) | none: the demos neither save nor use the menus. SaveShadow: 23,640 cases, 0 differ; CodenameShadow: 8,480 cases, 0 differ; 8 planted bugs, all caught. MenuProbe A/B with the 51 functions as the original: codenames, secrets_test, front_end, nightfire and the new saveload (load a codename, save it, the delete box): identical screenshots, message logs and saves | see git log |

| 6 | drone_states, drone_damage, drone_weapon | 60 + 5 of their names under other Xbox names (Drone_BulletHit = 0x31f20, DroneWeap_ManageBurstCount = 0x67590, NDrone2_CanMoveToRelativePos = 0x45490, DroneWeap_PlaceTertiary = 0x67bf0, DroneWeap_CanThrowGrenade = 0x66c50) + 6 helpers (DroneWeap_AimAtOpponent 0x666a0, DroneWeap_FireDelay 0x66e90, DroneWeap_BurstLength 0x66f10, DroneWeap_NextBulletTimeNewBurst 0x67c00, DroneWeap_DropPathClear 0x67640, Drone_MayDealBackshotDamage 0x31da0) | 55 of 60; 800 of 1244 blocks. DroneStateShadow: 21,280 cases; DroneCivilianShadow: 56,000; DroneDamageShadow: 98,000; DroneWeaponShadow: 130,000; all 0 differ. 13 planted bugs, one per suite line, all caught | see git log |

| 7 | drone_vision, drone_move, drone_route, drone_spawner | 62 + 39 Xbox functions renamed from FUN_ (31 under their names, e.g. NDrone2_Steer = 0x43070, DroneVision_OpponentVisibility = 0x65510, NDrone2_FollowRoute = 0x46a80, DroneSpawner_DroneDelete = 0x308b0; 8 under ours, for helpers they inline, e.g. DroneVision_GlobalAlarm 0x650a0) + 15 FUNC_AT helpers (e.g. SolveQuadratic 0x42b20, NDrone2_Fall 0x3ef30, NDrone2_StartRouteCreep 0x46220). Of their 34 names without an Xbox match, NDrone2_DroneInSights is MSVC's shared `return false` (0xde5e0), and four are inline (below) | 59 of 62; 897 of 1119 blocks. DroneVisionShadow: 196,000 cases; DroneAlertShadow: 129,000; DroneOpponentShadow: 155,284; DroneMoveShadow: 152,000; DroneCollisionShadow: 128,000; DroneRouteShadow: 158,300; all 0 differ, with 296 OpponentVisibility and 290 NextRouteAngle results accepted 1-2 ulp apart (FSIN and the XBE's acos against the host's sin and acos). 25 planted bugs, one per suite line, all caught | see git log |

| 8 | drone_init, drone_anim, drone (the creation half, and from Drone_DCVfromOBJ on) | 44 + 39 Xbox functions theirs inline or name otherwise: 32 renamed from FUN_ in Ghidra (the mode initialisers, e.g. NDrone2_init_DMODE_Sniper = 0x408b0; NDrone2_GetDroneTypeAttackTypeFriend = 0x3fe30, NDrone2_DoTypeSettingsOLD = 0x40430, NDrone2_GetDTYPENEW = 0x3fae0; the anim helpers, e.g. DroneAnim_GetDAnimForAnimState = 0x33040, DroneAnim_PreTransCallback = 0x33320, DAnimFunc_Run = 0x35ec0), two already named (DroneAnim_SetDAnimNow 0x35dc0, DroneAnim_SetDAnimVeryInternal 0x32f30) and 5 FUNC_AT (Drone_UpdateCombatChannels 0x314a0, NDrone2_HandleDoor 0x3cbf0, and the three ControlDTYPE_ functions 0x3f8a0, 0x3f910, 0x3f930, created in Ghidra). Of their 49 names without an Xbox match, 12 are MSVC's folded copies or the shared empty function 0xe0ec0, and 7 are inline (below) | 34 of 44; 539 of 667 blocks. DroneModeShadow: 82,000 cases; DroneInitShadow: 66,000; DroneAnimShadow: 216,000; DroneCoreShadow: 108,000; all 0 differ. At least one planted bug per suite line, all caught (two plants that change nothing are not counted: a `<` for `<=` behind a `!= 0` test, and a `/ 9` that truncates as `/ 10` does) | see git log |

| 9 | drone_combat (the drone states and their helpers) | 213, in four packages: combat and aiming (NDrone2_Combat.cpp), cover, doors, snipers, alerts and searching (NDrone2_Cover.cpp, NDrone2_Alert.cpp), civilians, alarms, ninja, astronaut, impacts, stuns and surrender (NDrone2_Special.cpp, NDrone2_Reactions.cpp, NDrone2_Civilian.cpp, NDrone2_Death.cpp), scripted roles: truck drivers, abseiling, scary objects, interrogation, allies, hostages, ambush, testers (NDrone2_Roles.cpp, NDrone2_Hostage.cpp). Their NDrone2_DSTATE_AllyLeadDone is AllyFollowDone (0x4b5c0) in the Xbox state table; NDrone2_HostageSituationActive is the shared empty function 0xe0ec0 | 106 of 213; 1,195 of 3,557 blocks (five sessions now end early: tests/demos/action/README.md). DroneCombatShadow: 230,000 cases; DroneCoverShadow: 317,000; DroneReactShadow: 262,000; DroneRoleShadow: 128,000; all 0 differ. Under the shadow run the originals reach 877 of 878 of the roles' blocks and all but 7 of the 1,710 of cover, alerts and reactions (the rest unreachable with the shipped tables, or rare). One planted bug per package, all caught | see git log |

| 10 | player, player_traverse, player_weapon | 76 + helpers they inline: Player.cpp (the player: init, update, health, pain and death, substates, jumping, zero G, scan mode, aiming, collision, using things), PlayerTraverse.cpp (ladders, the grapple, wires, ziplines, creep walls), PlayerWeapon.cpp (ammo, choosing and changing weapons, the gun in the view, firing, zoom, auto-aim, the laser sight, the muzzle flash, the watch), the grapple, wire, creep-wall, icon and camera object makers, Camera_Shear, camera_tracking, MP_RegisterBulletHit. FUNC_AT helpers: PlayerAnimSetInitDead 0xa96a0, Player_UpdateWalkClass 0xa9160, AccelFunc0Flag 0xb7520, Player_LeaveLadder 0xaeac0, Player_HangByHandA/B 0xafa80/0xafaf0, Player_WeaponUsable 0xb9f70, Player_DroneAwake 0xb7bd0. Their Player_FeetOnPoint is PlayerNotDrone_FeetOnPoint 0xab450, Player_CollLadder is PlayerCollLadder 0xae930, Draw_LaserBeam is Player_SetupLaser 0xb8260 | 67 of 76; 1,202 of 1,895 blocks. PlayerShadow: 153,000 cases; PlayerWeaponShadow: 180,000; all 0 differ; a planted bug caught in each of their eleven groups. Under the shadow run the originals reach about 1,072 of 1,104 of the player's blocks and 818 of 836 of the weapons' | see git log |

| 11 | anim | 54, all in src/action/engine/Anim.cpp but the player's five anim-set functions (Player.cpp): loading (skins, sequences, scripts, the sleeves), script lists and scripts, script ticks and commands, sequence frames and poses, faces and datums, the bone matrices, head tracking, aiming and drawing, the anim object, anim sets. Of their 9 names without an Xbox function of that name, AnimSeqGetInfo is 0x14420, AnimSeqSet 0x17960, AnimSet_WalkUpdater 0x171a0 and AnimDistanceTableGet 0x174a0 (renamed from FUN_ in Ghidra, the first three ported); AnimDistanceTableCreate is 0x16950 (renamed, left to the original); AnimDistanceTableInit, AnimTagGetUnique and AnimSeqDelete are inline; MP_NeedSkin is not called. Static helpers renamed from FUN_: AnimFramesPassed 0x12a20, AnimFrameEmitChannels 0x13c70, AnimData_ReadHalf 0x13880. 21 AUTOLTCG adaptors for the register-argument originals | 52 of 54; 1,240 of 1,552 blocks (AnimSleeveGetEntity and AnimObjectInitialPose not reached). AnimPortShadow: 73,500 cases in eight groups, all 0 differ; one planted bug per group, all caught. Under the shadow run the originals reach 1,601 of the 1,651 blocks of the 54 and their helpers | see git log |

| 12 | collide, rigid_body (package A); break, bullet, effect, explode (package B) | 42 + 16 Xbox functions theirs inline or name otherwise: their `Collide_Filter` is `Intersect_CheckIfFlagsMatch` 0x28a50, `RecurseBoxesSph` 0x291c0 and `ASH_RecurseBoxesRay` 0x292e0 (renamed from FUN_), `Intersect_CylTriGeom` 0x29a30 (Ghidra had `Intersect_RayGeom`), their `Intersect_RayGeom` 0x29fa0; renamed as ours: `Collide_TestHits` 0x2c440 (Ghidra had `Collide_CylinderIntersect`, which is 0x2c6f0), `Collide_CylinderTris` 0x2c760 (was `Collide_unknown`), `Intersect_SphereSphere`, `Intersect_RaySphere`, `Intersect_CylSphere` 0x29370-0x29410, `RB_Drag` 0x0c01d0 and `RB_Move` 0x0c02d0 (were `RB_unknownHelper*`); ported with them: `Collide_Sort`, `Intersect_BoxBox`, `Intersect_AABB_SweptVolume`, `Intersect_UnpackTrianglePlaneEq`, `Intersect_UnpackTriangleVerticesToWorld`. Collide.cpp, the three vecutil_ functions and `vecutil_intersect_line_line_2d` in math.cpp, RigidBody.cpp; 9 AUTOLTCG adaptors. Package B: 55 + 10 Xbox functions theirs inline or name otherwise: their `Debris_Create` is 0xcccf0 (Ghidra had a second `Debris_CreateEx`, renamed), `Occlude_Create` 0x6c880 (was FUN_, renamed), `Sub_ApplyDammage` is `Sub_ApplyDamage` 0xcf6a0 (left to the original), `Sensor_ApplyDamage` and `Monitor_ApplyDamage` are the shared empty function 0xe0ec0, `Bullet_update` is our `Bullet_Update`; ported with them: `Break_ShatterWindow` 0x1fc90, `Break_Create` (the draft replaced), `Bullet_SpawnSmokeDust_SomeOtherProbability` 0x217f0 and `_SomeProbability` 0x21870; renamed as ours from FUN_: `Hurt_IsTriggered` 0x6c640, `Effect_NewMark` 0x6ff70, `Effect_LinkToCel` 0x6faa0, `Effect_LinkToObject` 0x6fad0, and theirs `hashtable_set_sub_object_to_entity_gfx` 0x6b6d0 (left to the original). Break.cpp, bullet.cpp, Destroy.cpp, Hurt.cpp, Effect.cpp (new), Gas.cpp, Env.cpp, Swoosh.cpp (new), Explode.cpp, and one each in Drone.cpp, NDrone2_Damage.cpp, control.cpp, Player.cpp, GT.cpp; 15 AUTOLTCG adaptors | 38 of 42 reached by the demos; under CollidePortShadow the originals reach 984 of the 990 blocks of the 58 (the 6 left: three that cannot be reached, Collide_Update's call of a collision handler, the 166-triangle cap of Collide_CylinderTris, and Collide_Pick's skip of a type between 0x101 and 0x200, reached in an earlier run). CollidePortShadow: 156,000 cases in seven groups, all 0 differ (with about 330 where only the NaN chosen between two NaNs differs, as MathExactShadow allows); one planted bug per group, all caught. Package B: 46 of 55 reached by the demos; under EffectPortShadow the originals reach 1,085 of the 1,108 blocks of the 65 (of the 23 left, 15 cannot be reached: unsigned-conversion fix-ups of values that are never negative, the 0x1000000 test of a byte in `Bullet_handle_object_destruction`, a casing whose anim state has already been read). EffectPortShadow: 173,000 cases in eight groups, all 0 differ; one planted bug per group, all caught. All nine time demos match | see git log |

| 13 | vecmath, view (with camera, vision and build) | 65: package A, 32 from vecmath (math.cpp: the angle, vector, matrix-row, inverse, product, rotation and quaternion helpers, Mat_LookAt), package B, 33 from view (Concat and ApplyMatrixLVI in math.cpp, Rand_Random and Rand_Rand in Random.cpp, the camera in Camera.cpp, build_alloc_viewer and build_point_in_bounding_box in build.cpp, the portal vision in Vision.cpp, the draw lists, render modes, sky and Game_Draw in view.cpp). Their `Mat_Inverse` is two Xbox functions, `Mat_InverseSelf` 0xd5190 (renamed from FUN_) and `Mat_Inverse` 0xd51e0; their `View_SetupRenderModes_Tweak` is 0xd9900 (renamed); their `Camera_Reset` is `Viewer_Init` 0x24290. Helpers ported with them: ClipEdge (FUN_db730), PortalPlanesFrom (FUN_db970), View_AddToList (FUN_d91d0), View_SetupCelRenderModes (FUN_d9bd0), View_DrawAttachments (0xda1d0). 3 AUTOLTCG adaptors (View_AddObjects, View_AddCels, View_DrawCelGlist). With them, every port that used the host's trigonometry or float sums where the original runs the x87 made exact (below) | 62 of 65 (MulMatrix2, RotMatrixY, RotMatrixXY not reached). ViewMathShadow: 84 lines of maths x 20,000 cases and 35 of game functions x 6,000 worlds, all 0 differ (the NaN chosen where two NaNs meet counted apart in the plain double ports); one planted bug in each of its eight groups, all caught. With package A and B alone (Part 2 reverted), all nine time demos match; with Part 2 they no longer do, as expected (the recordings hold the inexact maths) | see git log |

| 14 | turret, scenery, pickup, copter, hazard, emitter, gunimp, weather, lights (package A); control, parsemap, sort, spline, objects, build, resetmap, gameflow, loader, drone_init, ssys (package B) | 71 (package A): GT.cpp (the gun turrets and the deployed minigun), Ripples.cpp, LeafGen.cpp, OneSided.cpp, Env.cpp (breath, trees, wind), Effect.cpp (the particle objects), Pickup.cpp, Player_EquipKey (PlayerWeapon.cpp), Check_Target (Autoaim.cpp), Intersect_ConeSphere (Collide.cpp), Copter.cpp, Mine.cpp, Shooter.cpp, Apocalypse.cpp, Emitter.cpp, GunImp.cpp, RainBox.cpp, Lightning.cpp, Cloud.cpp, Light.cpp (ambient and closest lights), Corona.cpp, Flicker.cpp. Of their 8 names without an Xbox function of that name, `Leaf_Create` is 0xcdef0, `Particles_Create` 0x6feb0 and `ColourGetValue` 0x67ea0 (FUN_, renamed and ported); `Copter_Register` is 0x6b900 (no function in Ghidra; created, named and ported); their `__static_initialization_and_destruction_0` is the two initialisers `Copter_InitConeCos` 0xf5460 and `Copter_InitConeSin` 0xf5470 (renamed, ported); `Emitter_LoadDefs` is `parsemap_block_particles` 0x68020 (left to the original); `Emitter_Kill` is the shared empty function 0xe0ec0; `Light_FindMapLight` is 0xde5f0, a stub returning NULL (Ghidra: `psiLight_Create`). Helpers renamed from FUN_ and ported: `Copter_NextPath` 0x6b9f0, `Copter_SeesPlayer` 0x6b830, `GT_AimAt` 0x9a5b0. 11 AUTOLTCG adaptors (Copter_CreatePath, Copter_NextPath, Copter_Move, Copter_FireGun, Copter_FireMissile, Copter_SeesPlayer, GT_maybeInterpolateAimDir, GT_AimAt, GT_RandMove, Pickup_CanInheritPosition, Pickup_Handler). 65 of 67 (package B): control.cpp (the objects' frame, cel changes and links, matrices and spheres, creation, LOD, references, carried velocity, player index), build.cpp (finding and linking rooms, objects and doors into rooms, the allocations), parsemap.cpp (the map file's blocks), `parsemap_block_particles` (Emitter.cpp), util/Sort.cpp (new: `QuickSort` and the ten comparisons), Spline.cpp, `Plane_PutPntOnPlane` (math.cpp), `View_AddSkyObj` (view.cpp), DynamicObject.cpp, `Env_Create`, Lock.cpp, `Locks_Init` (Locks.cpp, now injected, its FIX_ORIGINAL_GAME_BUGS branch kept), `Player_UnEquipKey` (PlayerWeapon.cpp), game.cpp (`ResetMap_GameInit`, `_GenLoadScreen`, `_Load` and the game flow's stack), `LoadableLoad` and `LoadableReload` (Loader.cpp). Not again: `NDrone2_GetDTYPENEW` (parcel 8's static helper) and `NDrone2_init_DMODE_Normal` (a jump). Their 39 names without an Xbox function of that name: see the section. Helpers ported: `maybeControl_Switches_Update` 0x2d640, `build_MakeRoomList` 0x20bd0 (renamed from FUN_), `init_cel` 0x20660, `set_InhibitGameDrawIfRequired` 0x6ab60; `MenuManager_Enable` 0x92c10 renamed. No register-argument originals | 45 of 71; 666 of 1,249 blocks. ObjectPortShadow: 228,500 cases in seven groups, all 0 differ (2 where only the NaN chosen between two NaNs differs); one planted bug per group, all caught. Under the suite the originals reach 1,219 of the 1,242 blocks of the 71 and their helpers (of the 23 left: unsigned-conversion fix-ups of values never negative, failed-creation returns, `ColourGetValue` with the key past the count, `Particles_Create` past the stub's -1, a few branches of `Pickup_Handler` and `Check_Target`). All nine time demos match. Package B: 55 of 67; 743 of 968 blocks (LoadableLoad, LoadableReload, Locks_Init, Lock_ReLock, Player_UnEquipKey, Control_Plr2Ind, CompTargetObj, CompStrings, CompBoxList and parsemap_block_palette_data_psx not reached; Locks_Init was already ours, called by name). EnginePortShadow: 234,000 cases in twelve groups, all 0 differ (about 200 where only the NaN chosen between two NaNs differs); one planted bug per group, all caught. Under the suite the originals reach 916 of the 934 blocks of the 65 and their helpers (of the 18 left: clamps that cannot fire in `Spline_Interp`, `KeyFrame_Interp` and `Lock_Update`, an unsigned fix-up of a count never negative and the inlined unlock's decrypting test in `Lock_Create`, and `Lock_Update`'s laser tint at 255). All nine time demos match | see git log |

| 15 | font, sprite, view's sprites, cirbuf, text, sound's subtitles, hud, the text box (package A); music, sound, input, llist, hashtable and leftovers of parcels 8, 11 and 14 (package B) | 52 names, 51 functions: `Text_Init` is listed four times for two Xbox functions of that name, the messages' 0xd1c90 (Text.cpp) and the text box's 0x99550 (TextBox.cpp), both `FUNC_AT`. gfx/Font.cpp (new: glyphs, kerning, formats, drawing, measuring, word-wrapping), Sprite_SetFmt, Sprite_SetTextNFmt and Sprite_SetParams (Sprite.cpp), View_DrawSprites (view.cpp), util/CirBuf.cpp (new), the message queue and subtitles (Text.cpp), Sound_DoSubtitle (Sound.cpp), the HUD's panes, crosshair, night sight, multiplayer panes and radar (HUD.cpp), ui/TextBox.cpp (new). Their 3 names without an Xbox function of that name: `HUD_UpdateSightPane` is the shared empty function 0xe0ec0 (our pane table had NULL; now that address, as the other empty panes); `__static_initialization_and_destruction_0` (text.cpp's, building `SubtitlesInfo`) is plain data on the Xbox (0x18cd30); `Sprite_CreateLink` is `Sprite_CreateAndLink2Viewer` 0xcedd0 (already ours). Helpers renamed from FUN_ and ported: `Font_FindGlyph` 0x69a40, `Text_TakeMsg` 0xd1d30, `Text_NextMsg` 0xd2270, `HUD_CreateShift` 0xb5f40, `HUD_PlaceLensFlare` 0xb42b0; renamed and left to the original: `MP_GetTeamScore` 0x9d840, `MP_GetPlayerScore` 0x9d8d0. 9 AUTOLTCG adaptors (Font_FindGlyph, Font_GetKernAdjust, __Font_DrawText, Text_NextMsg, Text_SetupChar, HUD_PlaceLensFlare, HUD_UpdateCrossHair, HUD_MonitorNightSight, HUD_CreateShift); `Sound_DoSubtitle` is `__fastcall` (ECX) Package B: 57 names, 55 ported, and 11 Xbox functions theirs inline or name otherwise: sound/music.cpp (UpdateMusicalEvents, the 21 level functions and `UpdateMFX_MayhewD` 0xc7740, their `UpdateMFX_02MayhewD`; `MusicFadeDown`; their `PickRandomStealth` and `PickRandomCombat`, 0xc6f10 and 0xc6fa0, renamed; FUNC_AT helpers they inline: `Music_PlayingCombat` 0xc4da0, `Music_RequestedCombat` 0xc4de0, `Music_PlayingStealth` 0xc4e20, `Music_RequestedStealth` 0xc4e60, `Music_TrackNis` 0xc4cd0, `Music_SetAIVars` 0xc4d20, `Music_MissionEnded` 0xc70a0, names ours), sound/Sound.cpp (`Sound_Init`, `_Kill`, `_Play`, `_ReqestPlaySfx`, `_UpdateSounds`, `_UpdateListeners`, `_IsFinnished`, `_Alertness`, `_Stop_Ref`, `_Playing_Ref`), game/obj/Trigger.cpp (the sound and music triggers), input.cpp (`Input_Init`, `Input_Update` - the draft replaced: the core's mouse look, mouse steering and debug keys now wrap it in `Input_UpdateWithMouse` -, `StickCompensation2`, `StickCompensation3`, `Input_RumbleStop`), util/LList.cpp and util/DList.cpp (`LList_Init`, `_Remove`, `_InsertAt`, `DList_Init`, `_MoveFromFree2InUse`, `_RemoveFromInUse2Free`), util/hashtable.cpp (`hashtable_initialise`, `_create_sections`, `_deleteitem`, `_set_sub_object_to_entity_gfx`, `_delete_hashtype`, `_get_hashtype_data`), engine/Anim.cpp (`AnimDistanceTableCreate` 0x16950 and `AnimDistanceTableGet` 0x174a0, the thunk replaced), game/obj/Lightning.cpp (`FractalThing` 0xb82e0). Not ported again: `NDrone2_GetDTYPENEW` (parcel 8's static helper in DroneModes.cpp) and `NDrone2_init_DMODE_Normal` (one jump to `NDrone2_init_DMODE_Defaults`). 6 AUTOLTCG adaptors (Sound_Play EAX, Sound_ReqestPlaySfx EDI, StickCompensation2 EDI/ESI, AnimDistanceTableCreate ECX/EDX, AnimDistanceTableGet ESI; `StickCompensation3` is `__fastcall`) | 13 of 57 (the 51 and their helpers); 237 of 1,153 blocks: the fonts, kerning, word-wrapping, `View_DrawSprites`, `Text_AddMsg` and `Sound_DoSubtitle`. The panes are not reached as originals because our `HUD_Update` and pane tables call ours by name. HudPortShadow: 271,000 cases in ten groups, all 0 differ (14 where only the NaN chosen between two NaNs differs); one planted bug per group, all caught. Under the suite the originals reach 1,089 of the 1,095 blocks of the 51 and their helpers (the 6 left: `Font_WordWrapString`'s nothing-fits return for a width of 2^31 or more and its space test before a newline, which tests the newline itself and cannot be reached; `HUD_UpdateMPHealthPane`'s coordinates cheat, whose `sprintf` of floats stops in the XBE's C library before the game has started; `Sound_DoSubtitle` past sound 0x60c, which no table entry is; `Text_AddMsg`'s unsigned fix-up of a value never negative; `Text_Update` with no view showing sprites). All nine time demos match. MenuProbe A/B (front_end, codenames) with the 57 as the original: identical screenshots (5 of 5, 6 of 6) and the same messages in the same order; the codenames run differs only in heap addresses (the heap was placed 0x120000 higher; offsets within it the same) and the one-frame jitter at the movie skip that two runs of ours show too. Run again with package B's probe (our replacements sent to the original) over front_end, codenames, secrets_passport, credits, weapons, mp_start and laser_safe: the same messages and identical screenshots in all seven (mp_start's match and weapons' shots once the movie-skip frame is the same) Package B: 40 of 57 reached by the demos on the original code; 515 of 933 blocks. SoundPortShadow: 1,113,000 cases in eight groups, all 0 differ (about 220 where only the NaN chosen between two NaNs differs); one planted bug per group, all caught. Under the suite the originals reach 912 of the 918 blocks of the 55 ported, the 11 and `LList_Cut` (the 6 left: five that cannot be reached - `Input_Update`'s clamp of a held count below 0 and its signed fix-up of a count never negative, `StickCompensation2`'s two negations of a length never negative, `DList_Init`'s failed `LList_Init` - and one rare branch of `UpdateMFX_HT_Level_EvilBase`). All nine time demos match. | see git log |

| 16 | menu, menu_script (package A); menu_manager, menu_controls (package B); menu_frontend, menu_handlers, menu_tweaks, menu_options, menu_dossier, menu_controller_style (package C) | 47 names, 47 functions: `Menu_ResizeControl` is listed twice for two Xbox functions of that name, 0x72c30 (a rectangle) and 0x72c60 (another control's), both `FUNC_AT`. ui/Menu.cpp: the skins (`Component_*`; their layouts in ui/Component.h, new), finding controls and moving the cursor, the dispatch (`__Menu_SendMessage`, `Menu_Update`, `__Menu_Send`, `__Menu_SendEx`, `__Menu_SendEx2`), input, the sprite helpers, `Menu_Malloc`/`Menu_Free`, the pulse, the sounds, `Menu_UpdateTextFmt`; ui/MenuScript.cpp: every menu script function (keyframes, messages, interpolation by line and by spline, default scripts); ui/LinkedList.cpp (new, with `LINKEDLIST`). Their 10 names without an Xbox function of that name: `Component_AddSet` is 0x71c60, `Script_AddScript` 0x969a0 and `Script_CalculateSpline` 0x96d80 (FUN_, renamed and ported); `Component_Destroy`, `Menu_Assert`, `Script_DelMessage` and `Script_CalculateLine` are inline (in `Component_DestroyAll`, `Menu_GetPage`, `Script_DelKeyFrame` and `Script_InterpolateLine`); `Menu_GetNormalTextFmt`, `Menu_StringInit` and `Menu_StringKill` have no Xbox counterpart. Helpers ported with them: `Menu_GetAction` 0x72d70 (AX the player, the one AUTOLTCG adaptor), `Script_Play` 0x96ba0 (Ghidra's `Script_Play_UISTUFF`, `FUNC_AT`), `LinkedList_GetIndex` 0x90bc0 (FUN_, renamed). The text box (parcel 15) now calls these by name Package B: 53 names, 53 functions, and 4 Xbox functions theirs inline or name otherwise. ui/LinkedList.cpp (`LinkedList_Init`, `_ReInit`, `_Add`, `_Insert`, `_Remove`), ui/MenuManager.cpp (`MenuManager_*`, `Menu_PlayerHasQuitOrRestarted`), ui/Manager.cpp (`Manager_SendMessage`: object creation, pages, the history, the cursor, focus), and new files for the control types and what holds them: ui/Label.cpp, Button.cpp, Checkbox.cpp, Scroll.cpp, List.cpp, Radio.cpp, Memo.cpp, Page.cpp, Window.cpp, Process.cpp, with their layouts in ui/Controls.h (new) and the constants they share in ui/MenuCommon.h (new). Their 7 names without an Xbox function of that name: `List_GetIndex_RealItem` is 0x90e60 and `List_SetIndex_RealItem` 0x90ee0 (FUN_, renamed and ported); `Manager_HandleMessage` is inline in `Manager_SendMessage` (its call of `Handler_HandleMessage`); `Window_Update` is the shared empty function 0xe0ec0; both `FixupResolution`s are the PS2's (640 x 480 to its 512 x 448), with no Xbox counterpart; `LinkedList_GetIndex` is package A's 0x90bc0. Ported with them: `Menu_GetNormalTextFmt` 0x73220 (FUN_, renamed; package A thought it absent) and `List_ShowSelected` 0x90f20 (their inline code, `FUNC_AT`, name ours). 5 AUTOLTCG adaptors: `List_GetIndex` (EAX, EDX), `List_SetIndex` (ECX, EDX), `Radio_GetIndex` (EAX, EDX), `Radio_SetIndex` (ECX, ESI), and `MenuManager_GetStatus`, which keeps EDX for `MenuManager_Monitor` Package C: 63 names, 62 Xbox functions (`Menu_CreateOptionBox` is listed twice: the second is `Menu_CreateOptionBoxLabel`, 0x7f3a0), and three FUN_ helpers renamed and ported: `Menu_UpdateControllerLabelString` 0x74910, `Menu_SetSoundLanguage` 0x84080, `Menu_GetLanguageButton` 0x840d0 (names ours). New files ui/ui_frontend.cpp (the boot pages, the main menu, the movies), ui/ui_options.cpp (the codename's options and controller styles), ui/ui_pause.cpp (P_PAUSE, C_GCPAUSE, P_ENDMISSION), ui/ui_tweaks.cpp (the debug page's tools); the rest into ui/Menu.cpp (the iris, option boxes, `Menu_SelectItemInControl`, `Menu_GetItemFromHash`, `Menu_IsBotGood`), ui/ui_codenames.cpp (P_NFSELECT, C_SBNFCN, the codename wheel, `Menu_UpdateDefaultCodename`), ui/ui_nightfire.cpp (P_NFRESULTS, P_NFBONUS, P_NFSTATS and its rows), ui/ui_dossier.cpp (C_RBDSRECORDS, C_RBDSREWARDS), ui/MenuUnlocks.cpp (`Menu_GetLevelBonuses`, `Menu_HasMedal`, `Menu_GetBestMedal`), game/sp/PlayerStats.cpp (`PlrStarts_GetRewardsTableSize`, `PlrStarts_ProcessRewardCounter`), game/Upgrade.cpp (`Set_Upgrade`, `ResetUpgrade`) and engine/Text.cpp (`Timer_Seconds2String`). Their names without an Xbox function of that name: `P_FMV_Handler`, `P_SCREENADJUST_Handler`, `C_SBSCREEN_Handler` (compiled to nothing), `Menu_GetBonus` (inline), `Menu_KillCredits` (the shared empty function 0xe0ec0). No AUTOLTCG adaptor | none: the demos start with `-level`. MenuPortShadow: 405,000 cases in seven groups, all 0 differ; one planted bug per group, all caught. Under the suite the originals reach 603 of the 618 blocks of the 47 and the six helpers (the 15 left cannot be reached without reading through NULL, or are dead: below). Under eleven MenuProbe replays (front_end, codenames, nightfire, multiplayer_bots, mp_setup, secrets_passport, credits, saveload, attract, movie_selector, mp_confirm) they reach 436 of the 618; no replay reaches `Component_DestroyAll`, `Menu_SetAction`, `Script_DelKeyFrame`, `Script_DelScript`, `Script_DelAllScripts` (a manager is deleted only on the way into play), `__Menu_SendEx2` (P_CNCONTROLS), `Script_ExitLoop` (message 0x14 is never sent) or `Script_InterpolateSpline` (no spline script on the pages visited). MenuProbe A/B over 17 scripts with the 53 as the original: identical screenshots (95 of 96; the one apart is mp_start's shot a thousand frames into a match, which play does not repeat) and the same messages in the same order, heap addresses and the movie-skip frame aside. Run again with the probe of package B (our replacements sent to the original, and our `Manager_SendMessage` logged): all 17 the same, with every screenshot identical; mp_start's match shot follows the movie-skip frame (ours and the original agree whenever that frame does). All nine time demos match Package B: MenuControlShadow: 358,000 cases in nine groups, all 0 differ; one planted bug per group, all caught. Under the suite the originals reach 1,391 of the 1,407 blocks of the 57 (two more, in `Radio_SendMessage` and `Scroll_Update`, in an earlier run; the rest below). Under sixteen MenuProbe replays (front_end, codenames, nightfire, multiplayer_bots, mp_setup, secrets_passport, credits, saveload, attract, movie_selector, mp_confirm, mp_start, multiplayer, quickgame, secrets_walk, weapons) they reach 993 of the 1,407; none reaches the Checkbox (the cheat menu) or `List_GetIndex_RealItem` and `List_SetIndex_RealItem`; only mp_start and weapons (into play) reach `MenuManager_Delete`, `Process_Destroy`, `List_GetIndex`, `List_SetIndex` and `Radio_SetIndex`. MenuProbe A/B over 18 scripts (those and secrets_test, shadow_tests) with the 57 as the original (MenuOriginal now also sends our replacement to the original, and `Manager_SendMessage` calls the handlers through 0x8e320, so the log sees ours): the same messages in the same order and identical screenshots, heap addresses and the movie-skip frame aside (quickgame's second shot and weapons' eight differed in one run of ours and matched the original in a second). All nine time demos match Package C: MenuHandlerShadow (new): 214,000 cases in nine groups, all 0 differ, alone and in the full run; one planted bug per group, all caught. Under the suite the originals reach 839 of the 855 blocks of the 65 and their jump-table pieces (left: a codename slot test in P_NFSELECT and 15 blocks of `PlrStarts_ProcessRewardCounter`'s upgrade loop that the shipped rewards table cannot reach). Under 24 MenuProbe replays (package B's 18 and six new scripts: options, pause, dossier_rows, results, debug_page, language) they reach 482 of the 855, every function but `Menu_GetBestMedal` and P_ENDMISSION. MenuProbe A/B over the 24 with the 65 as the original: the same messages in the same order (heap addresses and render-frame stamps aside) and identical screenshots, but for front_end's first (an ident movie's frame, which matched in two later runs of ours), movie_selector's first (17 pixels by 1, identical in a second run), quickgame's second (identical in a second run), and weapons' eight and mp_start's match shot, which follow the movie-skip frame (weapons: ours at frame 7031 three times, identical to package B's run at 7031). All nine time demos match | see git log |

Parcel 1's first playback found two things the coverage alone would not have shown:
- **A crash in our code.** The port of `Script_CreateEntity` called our `Break_Create` by name, which is a known-broken
  draft that was never patched in. `tools/draft_calls.py` now finds such calls.
- **A gap in the demos themselves:** the collision stamp (see docs/action-timedemo.md).

## Where their reading differs from the Xbox

### Parcel 1
- `Script_SoundEnd`, `Script_EntityEnd`, `Script_SubScriptEnd`, `Script_LightEnd`, `Script_TextStart`,
  `Script_AnimEnd`, `Script_CameraEnd`, `Script_SpriteEnd` and `Script_FadeStart` are separate functions there.
  On the Xbox they are inline in `Script_Run`.
- `Script_FreeFromHashTable` skips freeing the -1 a wrong-version script leaves. The Xbox frees it (an original
  bug, kept).
- `SS_Activate` takes two arguments there; the Xbox's reads one.
- `Script_Interp`'s footstep test reads the AnimObj's flags there. The Xbox reads `AnimState`+0x50
  (`animFlags & 4`).
- Platform differences, not errors: `Script_SpriteStart` rescales its coordinates to 512x448 on the PS2, where the
  Xbox stores them raw; the PS2's camera block is 0x50 bytes, the Xbox's 0x48.

Everything else in their readings of parcel 1 matched the Xbox.

### Parcel 2
- `Trigger_Hit`, bullets: the field at the weapon definition's +2 is `weaponBaseNum`, not the ammo type.
- `InverterTrigger_Update` indexes the channels with the sign-extended short. Theirs masks it to 0-255.
- `SSys_Msg`:
  - Their comment says only laser 2 speaks in TowerC. The Xbox, and their own code, silence every item.
  - The four message tables are not in item order in memory: items 1-4 are at 0x18cc50, 0x18cc18, 0x18ccc0 and
    0x18cc88.
  - The Xbox has no guard against a message index of 7 or more.
- `Monitor_Create` takes 4 arguments, not 5.
- The door group state array has 101 entries (a memset of 0xca bytes), not 104.
- Their own additions: guards on a NULL user and viewer, a rows check in `PlrStats_ResetScoring`, and the channel
  clamps.

Everything else in their readings of parcel 2 matched the Xbox.

### Parcel 3
- Inlined on the Xbox: `DroneFunc_DropGrenade`.
- A call on the Xbox where theirs is inline: the "drone active" test in `DroneFunc_DoForcedAttack` and
  `NDrone2_PowerstationTalk` calls `MPDrone_MaybeIsDyingOrDead` (0x1a190); `Drone_MayhewTalk` inlines it.
- `DroneFunc_OnInitDeath`'s death log is a call to `NDrone2_LogDroneDeath` for every enemy whose flags lack
  0x1000000. Theirs tests "killed by the player" around `PlrStat_LogEnemyDispatched`.
- `NDrone2_NearArmedPerson` takes only the DCVars on the Xbox; theirs also takes a radius.
- `NDrone2_ExplosionDir` reads the impact position at +0x8, not +0x10 (layout).
- `DroneFunc_ConsiderExplosive`'s fuse timeout is ((2 x rate + fuse) + frame) on the x87; theirs adds frame + fuse +
  2 x rate in float.
- `DroneFunc_DoBondTalk` and `NDrone2_BondTalk` work their timeouts out in extended precision and truncate with
  `__ftol2`. Theirs uses float rounding and `fptoui`: PS2 maths.
- Their own additions: a drop on a full queue in `Drone_SM_RouteMsg`, the `ch < 256` guard in
  `NDrone2_PowerstationTalk`, and a NULL-player guard in `DroneAnim_LocationDeathAnim`.

Everything else in their readings of parcel 3 matched the Xbox.

### Parcel 4
- Separate functions on the Xbox that theirs inline: the route-to-position check (0x49c50, ours
  `AINetwork_VerifyRouteToPosition`), the creep's segment set-up (0x4a990, `LinkCreep_Setup`), the link lookup
  (0x47b40, `AINetwork_FindLink`), and five helpers ours keep static because only ported code calls them: the
  skip test of `AINetwork_OptimiseRoute` (0x47ff0), the waypoint copy (0x49cd0), the open and closed list helpers
  (0x48310, 0x48360), one path's part of `SetLinksFlagInCircle` (0x4a420) and the creep step's corner check (0x4a3c0).
  The other way round, `AIPath_Link2Cel` is inline in `AIPath_BindNodes` on the Xbox.
- `NDrone2_MoveTest` hands `AINetwork_BoundsTest` a "blocked" flag in both builds, but the Xbox `BoundsTest` takes six
  arguments and never writes it, so the flag never fails a move. Likewise `LinkCreep_Increment`'s blocked flag is
  cleared and never set, and `AINetwork_BoundsNodeTest` reads five arguments; theirs passes the flag on.
- `AINetwork_EmitPath` returns 0 or 1 in the whole of EAX, and `DroneMove_FindSafetyFromScaryPosition` tests all of it: a
  `bool` port (AL only) desynchronised the CastleCourtyard demo in CastleIndoors2. tools/narrow_returns.py did not
  flag it.
- `AINetwork_CalcRoute` passes an argument on to `AINetwork_DoAStarPath`, which on the Xbox reads only four.
- `LinkCreep_AtSpecialNode`: the Xbox counts the mover as arrived within 1 m when *someone* stands within 0.4 m of the
  node (`NDrone2_DroneNearPos` true). Theirs reads the same; our README said the opposite (fixed).
- Layout, not errors: the feet height is `AnimObj.rootHeight` (AnimState+0xac; theirs +0xcc), the creep's bounds
  switch is Drone_tag+0x82c and the blocked mark +0x290 (theirs +0xba8 and +0x31c), the multiplayer test in the
  route checks is `MPSettings.maybeDroneAIEnabled` (+0x1e4; theirs +0x184).
- Their own additions: the guard against link 0xffff in `UpdateLinksUsedCount`, the guard on index -1 in
  `LinkCreep_AtSpecialNode`, and the NULL-DCVars path of `LinkCreep_Handler`.
- From the parcel's AI hints (Drone_AIHints.cpp): `Drone_AIVolume_Create` creates a 0xf4-byte object (theirs 0x120)
  and has no early return when the cover table is missing; its half-size abs keeps -0 and negates a NaN (compare and
  FCHS); the kick object's script is read at SCRIPTINFO+0x90 (theirs +0x98); the drone's target slot is +0x14c
  (theirs +0x174); the cover and AI-point creators have no guard when the table is full; the low-cover anim table
  has five entries on the stack, unchecked (theirs eight, with a guard).

Everything else in their readings of parcel 4 matched the Xbox.

### Parcel 5
- `BIN_PullBits`: their five typed overloads are four functions on the Xbox. The core gathers the bits into a
  64-bit value (each bit sign-extended from 32, so a set bit 31 fills the high word, which it writes too), and
  `BIN_PullBits_U32` / `_U8` / `_U16` call it and store the low 4/1/2 bytes. Their `int32_t` and `uint32_t`
  overloads are `_U32`, their `int16_t` and `uint16_t` ones `_U16`.
- `LS_MakeMission` is `FUN_0006f110` (renamed in Ghidra). `GSET` differs by platform: the Xbox block has no screen
  position and 7 reserved words (theirs 0xdb), and its load drops the language, sound mode and widescreen rather
  than applying them. `LS_LoadBonus` calls `Menu_SetBonus(lo, hi, slot, 0)`.
- Their additions, not in the Xbox: the zero-length and slot-size guards of the block walk, the slot bound in
  `LS_GetSaveStuff` and `LS_LoadFromBuffer` (the Xbox copies `Boot_LoadPTPData`'s 0x12c0 bytes into a 0x1000-byte
  slot), the level-count clamp in `LS_LoadMission`, the player and table guards in the cheat checkboxes and keypad,
  the slot guard in `Menu_MapDefaultCodename`.
- `Menu_IsValidCodename` is the Xbox's 0x76330 (we had named it `Menu_ValidateCodename`; renamed), and
  `Menu_SelectCodenameInControl` is `FUN_0007edf0` (renamed). The set-up of a load or save that the wheels call,
  *Menu_StartLS* (`FUN_0007fef0`, invented name), takes the codename in EAX, the operation in ECX and a stale flag
  in DL; theirs inlines it.
- The memory card has no Xbox counterpart: `P_PS2MEMCARDINIT_Handler` and `PS2_DoCardFlow` are PS2 only;
  `XBox_DoSaveFlow` (not ported) is the Xbox's box flow. `C_LBERROPTIONS_Handler` is that flow's error list on the
  Xbox (the page under the box is told, the box closed, the return page gone to, the states flushed; B can leave for
  the dashboard), not the PS2 card's answers.
- `Menu_UpdateMessageBox`'s delete: `psiDeleteData` is the empty debug hook on the Xbox, and the deletion itself is
  done by `P_CNSELECT`'s answer to the confirmation box (`FUN_000dfec0`). `C_SBCNSELECT`'s X asks with that box
  rather than deleting, and its Select and Y check the free space (`FUN_0008fcc0`) before a new codename;
  `P_CNSELECT`'s update answers the boxes (delete, delete failed, the dashboard for space). None of that is in
  theirs.
- `Menu_EnumSaves` takes `SaveEnum_EntriesPerFrame` (0x194804) names a frame, and `psiGetNextSave`'s -1 means "not
  yet": theirs takes up to 0x100.

Everything else in their readings of parcel 5 matched the Xbox.

### Parcel 6
- The 25 states: their readings match the Xbox, side effects in order. Two things their shared helpers hide.
  AllyLeadInit has no alert or forced-attack cases. Patrol handles message 0x1b (the obstructed path) but not 5 (a
  script's end), and Idle and Alert the other way round. Their JustStand uses `sin(t + pi/2)` where the Xbox uses
  FCOS: the same.
- Not their reading, but ours: every Xbox call to `NDrone2_NearArmedPerson` pushes a radius it never reads.
- `Drone_BulletHit` is FUN_00031f20 on the Xbox, and calls `Drone_Message(obj, 8, hit, 0)`. Theirs builds DCVars and
  sends the message itself.
- `DroneFunc_HandleImpact`'s taser test truncates the health with `__ftol2` (signed, the low dword; NaN gives 0).
  Theirs converts it to unsigned.
- `NDrone2_PainTalk` asks `Drone_maybeDisallowedFromSayingThisSfx` (0x3be21). Theirs compares with the sound
  playing.
- `DroneAnim_LocationImpactAnim`'s abseil variant table has 19 entries, built on the stack. Theirs has 18.
- `DroneFunc_SetMissionFailReason` is inline in the impacts on the Xbox. Their reasons 5, 6 and 7 give the same
  label and channel, so ours calls our port with them.
- Separate functions on the Xbox:
  - helpers called only from the damage code: 0x36350 (the player's touch), 0x363d0 (a hurt volume's damage),
    0x36430 (`NDrone2_BodyHitEffect`), 0x36190 (whether the impact anim is due), 0x3a410 (TowerA/B calming on
    impact), 0x3a3f0 (whether a state may be resumed). The explosion's damage block 0x3db50 is
    `NDrone2_HitDamage` at location 0, handed `hitDamageScale`, which it never reads.
  - `NDrone2_CanMoveToRelativePos` is two functions: 0x45490 checks flag 0x10 and calls 0x453e0. 0x453e0 does the
    feet, matrix and move test, and scales a bot's offset by its speed unless its fourth argument is set (this
    caller sets it).
  - `DroneWeap_PlaceTertiary` (0x67bf0) is a lone jump to `DroneWeap_ThrowGrenade`. Ghidra gives both the one name.
  - The back-shot test is `Drone_MayDealBackshotDamage` (0x31da0, the drone in ECX). It lists the PowerStation
    levels outright, which comes to the same as theirs.
  - `DroneWeap_DropWeapon`'s line-of-sight check is 0x67640 (ray flags 0x125).
  - 0x67c00 is MSVC's copy of `DroneWeap_NextBulletTime` for a new burst. `DroneWeap_DoFiring` calls it.
- The same result by another route:
  - `DroneWeap_HandleFiring`'s bots check is inside 0x666a0.
  - Their `DroneWeap_ManageBurstCount` calls `NextBulletTime(0, 0)`. The Xbox (0x67590, the DCVars in EAX)
    works out max(now, last shot + delay) itself.
- `DroneWeap_ThrowGrenade` tilts the throw with RotMatrix (0xd6560), not `Mat_Rotate`.
- `_VECTOR` is 12 bytes on the Xbox, so their "w is stack garbage" divergences do not arise. `DroneWeap_FireWeapon`'s
  flash rotation writes `.z = 0`.
- Register arguments their C++ does not show:
  - `NDrone2_DealWithObjHit`: the drone in EAX.
  - `DroneWeap_DoFiring` and `DroneWeap_NextBulletTime`: the DCVars in EDI.
  - `DroneWeap_ManageBurstCount`: the DCVars in EAX.
  - `DroneWeap_BurstDelay`: the drone in ESI.
- Layout, not errors: the bot traits are `BOT_vars_t.stats.traitFlags` (+0x84; theirs +0xac).
- Their additions, not in the Xbox: the NULL guards on `NDrone2_ExplosiveImpact`'s source object,
  `NDrone2_DoHitEffects`' sound, `DroneAnim_LocationImpactAnim` and the gas puff.
- Ghidra's labels for the DroneFiring floats read wrong in `DroneWeap_BurstDelay`: 0x164094 is BurstDelay_Min (the
  clamp) and 0x1640a0 is MinDist (compared with the distance), as Player.cpp names them. The drone tuning floats now
  have one definition, in src/action/game/drone/DroneTuning.h.

Everything else in their readings of parcel 6 matched the Xbox.

### Parcel 7
- Separate functions on the Xbox that theirs inline:
  - vision: the opponent-in-view test (0x656a0, ours `DroneVision_OpponentInView`), the bone and gun-through-wall half of
    their `SeekOpponentLOS` (0x66030, `DroneVision_OpponentLOS`), the eye's line of sight of `CanSeePosition`
    (0x64660), and the two drone-list walks (0x64580, 0x645c0) of `ProcessDroneSight` and `FindAlertedDrones`.
  - alerts: `EnemyAlerts`' global-alarm case (0x650a0) and its four shout handlers (0x65120-0x651e0); their
    `DroneVision_ShoutFromOtherDrone` is inline in each, and in case 0x16.
  - opponents: `NDrone2_SetOpponentAimPos` is 0x378f0, with its bone lookup 0x37870; `FindOpponent`'s angles to the
    target (0x37bc0); three parts of their `Drone_InitComms` (0x31190, the targets' frame; 0x31110, clearing them;
    0x30950, the dummy player). `Drone_ProcessOpponents` is 0x313e0.
  - movement: `NDrone2_Steer` 0x43070, `NDrone2_EvaluateObstacle` 0x42cc0 (the obstacle drone in EAX, the record in
    ESI), `SphereSphereSweep` 0x42bb0 and its quadratic 0x42b20; `DroneMove_SetBoundryFlags` 0x454e0 (calling 0x453e0
    four times), `DroneMove_NoBunching` 0x437f0, `DroneMove_ObjectMissionPathDistance` 0x44cb0,
    `NDrone2_NearestNodeDCV` 0x44670 (theirs builds the node search itself), the three astronaut moves 0x44f70,
    0x45230, 0x452c0.
  - collision and tracking: `NDrone2_DoCollision` 0x3cce0, `DroneFunc_NearPlayerCollision` 0x3b7f0,
    `NDrone2_DoGravity` 0x3cd50 (a tail jump into DoCollision, where theirs repeats its tests; the same result), the
    fall 0x3ef30 (their gravity lambda), `NDrone2_AimTest` 0x3ab90, the aiming 0x3ab00, `NDrone2_DoHeadTracking`
    0x3a850 (it calls `NDrone2_HeadTrackObj` 0x3a6c0), the nearest-in-room choice 0x3a780,
    `NDrone2_DoHeadTrackingBehaviour` 0x3a940, `NDrone2_DoTracking` 0x3ac10, and the feet's collision 0xab390 (the
    object in EAX, the hit in ECX).
  - routes and spawners: `NDrone2_InitAIPath` 0x44a20, the creep start 0x46220 (its first argument never read),
    `NDrone2_FollowRoute` 0x46a80, `NDrone2_UpdatePatrolRoute` 0x46c20, `NDrone2_UpdateMissionRoute` 0x46d40,
    `NDrone2_FindAlarmPoint` 0x470f0 (in their drone_combat.cpp), the goal set-up 0x470b0, a new wave's reset 0x30860,
    the wave speech 0x30910, `DroneSpawner_DroneDelete` 0x308b0 (called from `Drone_Delete`).
- Inline on the Xbox: `NDrone2_GetAnglesToMoveTarget` (in `NDrone2_maybeCalcDistanceAndDirToTarget`, 0x46080).
  `NDrone2_DroneInSights` is 0xde5e0, `XOR AL,AL; RET`, which MSVC shares with `Particles_Update`: always false, as
  theirs. `AnimScriptPeekSfx` has no counterpart: the Xbox `NDrone2_SpeechSFX` has no deferred facial sound (their
  `talk_pending`, `SFXPreCache`); it calls `NDrone2_PlayFacialAnim` (0x374e0) unless +0x36 or +0x37 is set.
- `NDrone2_UpdateMissionRoute` returns `FollowRoute`'s status (0x46d7e `MOV EAX,EDI`); their note says it returns
  nothing.
- `DroneSpawner_SpawnDrone` only calls `NDrone2_CreateFromDIVars(&entry->vars)` and `NDrone2_PostLoad_Init`, and
  counts (0x324a0-0x324c0). Their creation-time draw, DIVars copy and group set-up are not in it.
- `NDrone2_PlaySFX` is cdecl with five stack arguments (Ghidra's `__fastcall` with seven is wrong). It logs every
  request in a 16-entry ring at NPCGlobals+0x13a8, played or not, and writes the frame to Drone_tag+0x3d0.
- NaN and precision, from the disassembly:
  - `DroneVision_ConsiderAlerted`'s frame test is on the x87 with both frames loaded by FILD (plus 2^32) and not
    rounded (0x659d4-0x65a15); theirs is in float. `OpponentVisibility`'s `< 0.7` counts a NaN as seen (0x65711
    `TEST AH,5; JP`).
  - `NDrone2_EvaluateObstacle` compares `limit * 7030` unrounded (0x42e65); the quadratic tests the unrounded
    discriminant against 0 (0x42b38) but the float one for "> 0" (0x42b60). Theirs is in float.
  - `NDrone2_Collision`: a NaN root height gives the dead drone's radius as the root height (0x3f39f), not 0.4, and the
    dead capsule's half distance, not the half axis (0x3f442); the fall's water test falls on a NaN (0x3ef80), where
    theirs returns.
  - `DroneFunc_HandleSoundAlerts` stores the alertness step rounded but adds it unrounded.
  - The bots' "lost opponent" patience is `__ftol2((_FRAME_RATE * 20) * BOT_getAggressionMul)` compared unsigned
    (0x37d8d-0x37da9); theirs `fptoui(FRAME_RATE * (mul * 20))`. `SetOpponent` converts unseenFrames unsigned on the
    x87 (0x37ae7).
- `NDrone2_FindOpponent` walks MPGame's 10 Xbox player slots (0x262754-0x262934, stride 0x30), and slots 4-9 for "a
  bot already targets it"; theirs 8, and 4-7. Ours now counts them with `NUM_AGENTS` and `NUM_PLAYERS`
  (game/mp/MPLimits.h; see "Merged with the bot limit" below).
- The obstacle record of `NDrone2_Steer` is 0x30 bytes on the Xbox, the obstacle's ending where the drone's begins
  (ESP+0x18 and ESP+0x48); theirs is one 0xd0-byte record.
- Layout, not errors: the sound alertness is `DYNAMICSOUNDS.alertness` (+0x38; theirs +0x48); a bot's view of a player
  is `botVars->players[i]` (+0x94 + 16i; theirs +0xbc), its weapon +0x75c (theirs +0x768) and its flags +0x728 (theirs
  +0x734); the spawner's template is 0xac bytes (theirs 0xd0), "not drawn recently" `obj_tag.drawnCountdown` (+0xd8).
- Their additions, not in the Xbox: the NULL guards in `Drone_PositionVisible`, `NDrone2_NotNearArmedDrone`,
  `DroneFunc_LostSightTalk`, `DroneMove_NextRouteAngle` and the feet's `Coll_AddHitToList`; the 16-box cap in
  `AnimGetCollData`; the group bounds in the spawners and `DroneSpawner_Init`'s entry bound; the zeroed `w` of their
  4-float vectors in `GetWeaponInfo` and `GunThroughWall`.
- `DroneVision_ShoutFromOtherDrone` was guessed to be 0x650a0 from the call graph; it is the global alarm, which theirs
  inlines in `EnemyAlerts`.

Everything else in their readings of parcel 7 matched the Xbox.

Found along the way:
- **Registers a port must keep.** The original keeps the drone in ECX across its call to `NDrone2_ObjectIsArmed`
  (0x64be0) in `DroneVision_LogSeenOpponent`: the whole-program optimiser knew that callee leaves ECX alone, and our
  port of it does not. The caller is ours now, so the game is unaffected, but the shadow's original run had to keep
  0x64be0 original. `tools/caller_saved.py --action` now lists every live original caller that reads ECX or EDX after
  calling a port whose original never writes them. It finds four, all in math.cpp's ports, which their current
  compiled forms happen not to clobber (maybeAtan2: Vec_Cartesian_2_Spherical; Vec_Normalise: Mat_LookAt and 0xd8590;
  Vec_NormaliseLen: Cel_ObjectLeftCel); Vec_Normalise and Vec_NormaliseLen depend on the CRT's sqrt helper for that.
- **FSIN, FCOS and acos.** The host's sin, cos and acos (SSE2) can differ from the x87's FSIN and FCOS and the XBE's
  `__CIacos` by an ulp or two; the shadows count such results apart rather than as differences (the user accepts
  1-ulp differences of this kind; the time demos are the check that they do not matter). Since then `FSin` and `FCos`
  run the instructions themselves and return an `Extended`, the x87's 80-bit value, so their callers match exactly;
  since parcel 13 `CIAcos` calls the XBE's own `__CIacos`, and DroneMoveShadow compares NextRouteAngle exactly. A
  NaN keeps its sign and payload through the helpers in src/action/math/X87Ops.h (`FSin`, `FCos`, `Negate` for FCHS, `X87NaNs` for an operation on two NaNs, where SSE2 takes the first and the x87 the larger
  or the positive). MSVC compiles `-x + c` as `c - x` and `x <= 0 ? 0 : x` as MAXSD, both of which lose a NaN's or a
  -0's sign.
- `ShadowHook_Install` no longer warns "cannot follow" when a test hooks an address one of the standard fakes already
  hooks (0x1a190).
- `DroneSpawner.cpp` existed but was not compiled; it now is, and actionhelpers.h no longer includes DroneSpawner.h
  (parsemap.cpp does).

### Parcel 8
- Folded on the Xbox (MSVC's identical-code folding), so one Xbox function serves several of their names:
  - `init_DMODE_Normal`, `_Guard`, `_Retreater`, `_Attacker`, `_Assassin` and `_Interogator` are one jump to
    `NDrone2_init_DMODE_Defaults` (0x408a0, now named `NDrone2_init_DMODE_Normal`; a jump leaves nothing to port);
    `_Hostage` and `_HostageTied` are 0x40a80; `_Mayhew` and `_TruckDriver` are 0x40d10.
  - `init_DMODE_DeleteMe`, `_Ninja`, `_Bot`, `NDrone2_initDTYPE_BotInit`, `DroneAnim_SnapRotate2` and
    `DAStateFunc_AimStand` are the shared empty function 0xe0ec0 (Ghidra's `__profiling_or_debugging_hook_point`).
    DefaultInit's call of it at 0x4184b (their `control_init_collision`) does nothing.
  - Ghidra had split `init_DMODE_HostageKiller` (0x409b0) at a bogus FUN_000409fc in its middle; the function runs
    whole from 0x409b0.
- Separate functions on the Xbox that theirs inline:
  - modes and types: `NDrone2_GetDTYPENEW` is 0x3fae0 (the drone in EDI, the mode in ESI; only
    `GetDroneTypeAttackTypeFriend` calls it, so ours is a static helper there). `GetDroneTypeAttackTypeFriend` has its
    mode-0 call (the base DTYPE) inline. The state for a drone with no second type is 0x3fd20 (the drone in EAX; ours
    `NDrone2_SecondStateFor`, static), and `DoTypeSettingsOLD`'s worked-out initial state is 0x403b0 (ours
    `NDrone2_DefaultInitialState`).
  - anim: `DroneAnim_SetDAnimNow` 0x35dc0, `DroneAnim_FreeScript` 0x33120, and register-argument helpers behind
    AUTOLTCG adaptors (now reached only from our code): `CanSetAnimCall` 0x32dd0 (EAX), `CallFullyComplete` 0x33e60
    (ESI), `SetDAnimVeryInternal` 0x32f30 (EAX = &animMode, DX the row), `BaseCallState` 0x32ed0 (AX),
    `SkipAnim` 0x32fe0 (EAX), `TakeCall` 0x344f0 (ESI), `GetDAnimForAnimState` 0x33040 (AX, DX, ESI, EDI),
    `GetDAnimForCall` 0x33de0 (EBX, DX, AX, ECX).
  - core: `Drone_InitComms`' combat music and channels 0x97 and 0x98 (0x314a0, ours `Drone_UpdateCombatChannels`),
    `ControlSTANDARD`'s door step (0x3cbf0, `NDrone2_HandleDoor`), `DroneSpawner_CreateBlank` (0x2fb90, called by
    `Drone_PostLoad_Init`); `PreDroneControl`'s change of mode is a call to `NDrone2_ChangeToAttackMode`.
- Inline on the Xbox: `DroneAnim_ZeroAnimCall` (a memset in TakeCall, CallFullyComplete and CallAnim),
  `CheckForAnimComplete` and `SetAnimCall` (in `CallHandler`, 0x35cd7 and 0x35d72), `NDrone2_AddToGroup` (in
  `SetupGroups`, 0x36ea0), `DroneInit_TakeXHits` (in `DefaultInit`, 0x42543: folded to weapon 0x33's damage, doubled
  by `fadd st0,st0` off difficulty 1; their `weapon < 0x48` test is gone), `Drone_PauseNoDraw` (twice in
  `InitComms`, the only user of 0x1dfa2b; ours is a static helper).
- Their additions, not in the Xbox: the bound on the secondary mode's index and the anim-object guard in
  `DoModeSettingsOLD`; the guard on property 0x20's alertness index in `DoModeSettingsNEW` (a two-bit field, which
  cannot overrun the four levels); the guard on `animState->currentWeaponId` in `DefaultInit` (0x42739); the script
  NULL guards in `SetHTAnim` (after AnimScriptAppendBlend, 0x33fc3), `OnStartAnim` (0x331e5) and `DAnimFunc_Run`
  (0x35edf).
- Values: `Drone_PostLoad_Init`'s default slope is 0x3f5f66f3 (0x32011), an ulp below theirs; `Drone_LevelReset`
  sets NPCGlobals+0x14c to 6 (0x317b4), not 3.
- NaN and precision: `PreTransCallback` compares `rootHeight - 0.02` unrounded with 0.2 (0x33381); theirs in float.
  The mode and type settings test the alertness for "at least 1" (`test ah,1`: a NaN is not), and the sniper's
  initial state for "below 1" (`test ah,5`).
- No deferred facial animation in `ControlSTANDARD` (their `talk_pending`): the facial anim counts as done when the
  script's flags (+0x74) have 0x10000000 set (0x3f6cd). Zoe's control has `DroneFunc_SetMissionFailReason(15)`
  inline (0x3f8f2), the same effect as theirs.
- `NDrone2_CreateObj`'s "owner" is `DIVars_tag.glist` (+0x1c), handed to `Control_CreateObjEx` as the celglist; the
  object type is then 0x4d (MINISUB).
- Layout, not errors: the bot weapon `PostLoad_Init` reads is `BOT_vars_t.weapon` (+0x75c; theirs +0x768); their
  DefaultInit range_e8, range_f0 and range_fc are our viewRange (0xcc), combatRanges[1] (0xd4) and combatRanges[4]
  (0xe0); the slot byte at Drone_tag+0x465 is the anim state's priority. `DoModeSettingsNEW`'s offsets are the
  GameCube's too: its +0x121 (`chatSwitchChannel`) is the switch channel DSTATE_CastleChatGuard1 waits for.

Everything else in their readings of parcel 8 matched the Xbox, the mode initialisers' property lists and every jump
table (read by its bytes) included.

Found along the way:
- **Shadow fakes and inlining.** A fake on an original's address does not catch a call our code makes within one
  file, where MSVC may inline the port: `DroneWeaponShadow`'s fake on 0x35dc0 (our `PlayFiringAnim` inlines our
  `SetDAnimNow`) and `DroneOpponentShadow`'s on 0x31260 (our `Drone_ProcessOpponents` inlines `Drone_GetOpponentInfo`)
  stopped firing, which crashed one suite and failed the other. Both now run the real chain down to deeper fakes.
- `Drone_tag+0x858` holds the death script's hashcode (key 13, now `deathScript`), not an object: NDrone2_Death.cpp
  tests it against 0. `DIVars_tag.glist` is a `celglist_tag *`; `Drone_tag+0x82c` is the door the drone is at
  (`door`), and +0x28c a `Drone_tag *`.
- `DroneTypeSettings`' control functions (0x3f8a0, 0x3f910, 0x3f930) had no function in Ghidra; the json counted them
  in `ControlSTANDARD`, which ends at 0x3f89b.

### Parcel 9
- `NDrone2_DSTATE_RunForCover` slides towards the cover by 0.025 of the gap a frame (`push 0x3ccccccd` at 0x5bb7c);
  theirs uses 0.1. The other slides are as theirs: UnderCoverInit and UnderCoverIdle 0.08, SpecialDeath 0.05.
- Found in our first drafts, not in theirs (their readings were right): `DroneFunc_FirstSightState` returns
  DSTATE_Interogate (0x3c, `mov ax, 0x3c` at 0x3eb43) for CastleIndoors1 and the RefRoom, and DSTATE_CivilianChallenge
  (0x3e) for TowerA and TowerB, by the jump table at 0x3ebd8/0x3ebe4; the eight `NDrone2_Can*` side moves test
  `Drone_tag` 0x290 (`bunchFlags`, which DroneMove_NoBunching sets: 2 ahead, 4 behind, 8 left, 0x10 right), not the
  boundary flags at 0x294; `NDrone2_DSTATE_AllyLeadMissionWait` reads the handler's drone argument (0x5242d).
- `DroneAnim_ValidateCoverAnim` (0x33c10) takes the DCVars in ESI and the anim state in DI (an AUTOLTCG adaptor), and
  reads the anim table for state 7 directly: `DroneAnim_CanDoAnimState(dcv, 7, 0)` folded in.
- GameCube check 92 ("ERROR: Drone has no second weapon", 0x800b032c) is `NDrone2_DSTATE_StunGrenadeRecover`
  (0x562a0); the Xbox dropped the test.

### Parcel 10 (package A: player.cpp and player_traverse.cpp)
- Their `Player_FeetOnPoint` is `PlayerNotDrone_FeetOnPoint` (0xab450), whose fourth argument is never read; their
  `Player_CollLadder` is `PlayerCollLadder` (0xae930).
- `Player_ZeroG` turns at REC x PI x 0.4 a frame (not their 2 pi / 5), and eases its turn by FRM x 0.2 x gap x 0.2 on
  the previous turn, clamped to FRM x 0.05 either way.
- `Player_Climb` leaves the ladder through a function of its own, `Player_LeaveLadder` (0xaeac0), which also tests
  weapon 0x47.
- `Player_CollWire`, `Player_Wire` and `Player_Creep` place the hands through two helpers, `Player_HangByHandA`
  (0xafa80) and `Player_HangByHandB` (0xafaf0): x87 sin and cos of -rotation.y, offsets of +-0.0542. The grab's root
  motion uses 0.082 (0x3da7ef9d).
- `Wire_Create` negates the rotation's z before creating the object.
- `Player_CollCreepWall` casts its ray from the point on the line down by 5.
- `camera_tracking` measures along the line from its position, not its centre.
- `Player_Update` fades the HUD by two steps a frame (it decrements twice).
- `MP_RegisterBulletHit` never reads the health it is given.
- `BLData` is 0x8fc bytes (ours was 0x8f9): `Player_Init`'s create size is 0x8fc.

### Parcel 10 (package B: player_weapon.cpp)
- Their three names without an Xbox function of that name: `Draw_LaserBeam` is `Player_SetupLaser` (0xb8260, already
  ours), `Draw_TaserBeam` is `Player_SetupTaser` (0xb8400) and `FractalThing` is 0xb82e0 (renamed in Ghidra). On the
  Xbox `Player_MuzzleFlash` calls the first two, after its own bone lookup and ray.
- Two Xbox helpers they inline: 0xb9f70, which `Player_WeaponChange` asks whether it can go back to the last weapon or
  gadget (`Player_WeaponUsable`, our name), and 0xb7bd0, the "alive and not in state 1" test of the drones that
  `Player_SetWeaponAnimObj` makes before lowering the gun (`Player_DroneAwake`, our name). Both FUNC_AT.
- `Player_WeaponRecoil` does not set the gun's rotation itself: it calls a register-argument function for that,
  `Player_TiltWeaponTowardsCrosshairs` (0xb7eb0, the player in EAX), before `Player_SetWeaponAnimObj`. Its walk sway
  lifts by |cos(phase)| (FCOS), not |sin(phase + pi/2)|, and keeps that value unrounded on the x87 until it is added.
- `Player_SetFiringAnim` takes the weapon in EDI and `Player_WeaponInitBullet` the player in EAX (AUTOLTCG adaptors).
- `Player_GetAimingPoint` divides the crosshair offset by the zoom before multiplying by the screen size (theirs
  multiplies first); only the rounding differs.
- `BLData::weaponStats` starts at 0x158, not 0x15c: each 12-byte entry is the remembered zoom (float), the clip, the
  "have it" flag, the fire mode and the variant. Ours was laid out 4 bytes late, with the zoom in the previous entry's
  padding. `Player_RamSave` copies 33 ammo counts (0x42 bytes, not the 34 of `BLData::ammo`), the 114 entries and the
  0x33 bytes after them.
- `Check_AutoAim`'s angles are FPATAN and the C runtime's x87 `__CIasin` (called as the XBE's own, 0xee8a0);
  `Player_SetWatch` takes its hands' angles modulo 2 pi through the runtime's `fmodf`, which ours does with `fmod`
  (exact for the finite values it is given).
- Fields named (Player.h): `autoAimTarget` 0xd4, `beamObjs` 0x77c, `gadgetTarget` 0x810 (Check_Target's), `zoomSound`
  0x828, `swayPhase`, `recoilX`, `recoilY`, `idleSwayPhase` 0x86c-0x878, `refireTimer` 0x8bc, `lastWeapon` 0x8c0,
  `lastGadget` 0x8c2, `ramLoadedWeaponId` 0x8c4 (a short), `burstShotsLeft` 0x8c6, `muzzleFlashFrames` 0x8c8,
  `idleTicks` 0x8ca, `idleWait` 0x8ce, `twinGunHand` 0x8e8, `idleState` 0x8e9, `shotFired` 0x8eb, `bulletSpawned`
  0x8ec, `taserHit` 0x8ed, `laserSight` 0x8f4; `GUNTURRET::owner` (was `drone`: the player who set the Ronin down
  as well); `GlobalVars_t::DecoderDisplayShown` (0x1f657c).

### Parcel 11 (anim.cpp)
- `AnimLoadFile` (0x14a20) does not call `MP_NeedSkin` for a skin, as theirs does; nothing on the Xbox matches that
  name here.
- `AnimPostLoadInit` (0x11200) gathers the loaded skins in a buffer of 128 hashcodes (theirs: 0xa8).
- Their `AnimObjectNew` swaps skin 0x05000090 on two levels inline: the PS2's own table. The Xbox calls
  `maybeFixupSkinHashcode` (0x11350, the skin in EAX): on HendersonA, B, C and Tower2A 0x05000036 becomes 0xc3 and
  0x05000038 0xc2; on CastleIndoors1, CastleIndoors2, EvilBase and EvilBaseC 0x15 becomes 0xbd, 0x18 0xc5, 0x3b 0xbf
  and 0x5e 0xbe (a jump table on the level, based at 0x07000001).
- Their `AnimSeqGetInfo`, `AnimSeqSet` and `AnimSet_WalkUpdater` are 0x14420, 0x17960 and 0x171a0, the first two
  with register arguments (EAX raw, ECX hash, ESI seq; ECX script, EBX hash and five on the stack).
  `AnimDistanceTableGet` (0x174a0) takes the hash in ESI; `AnimDistanceTableCreate` (0x16950) is called through it
  and left to the original. `AnimDistanceTableInit` is the `nDistanceTables = 0` in `AnimPreLoadInit`;
  `AnimTagGetUnique` is inline in `AnimSetAppend` and `AnimTagAllocAndGetUnique` (0x15970); `AnimSeqDelete` is inline
  in `AnimScriptDelete` and `AnimScriptNew`.
- Corrected in ours, not theirs: the overlay script flag is 0x4000 and sharing the root 0x8000 (ours had one for
  both); `AnimSleeveGetEntity` (0x118b0, EAX) tries all eight sleeves from the index, wrapping (ours stopped at the
  end, and was not patched in); our `maybeFixupSkinHashcode` had its level groups crossed.
- The comparisons follow the original's FCOM tests: `test ah, 1` takes the "less" branch for NaN as well, so many
  are written `!(x >= c)`.
- Fields named (Anim.h): `AnimObj::sleeveGlist` 0x30 (was `maybeSleeveGlist`), `distancePhase` 0x64 (was
  `unknown64`), `rootHeight` 0x54, `heightOffset` 0x58, `distanceThisTick` 0x5c, `rootTravel` 0x60, the head-track and
  aim rates, ranges and angles 0x68-0x84, `poseBufSize` 0x88, `facialTimer` 0x8a, the target bones 0x8c/0x8d,
  `headTrackDelay` 0x8e, `lookMorphX/Y` 0x8f/0x90, `facialState` 0x91, `lod` 0x94; `ANIMOBJ_POSE_VALID` (was
  `ANIMOBJ_FLAG_1`) and the other ANIMOBJ_, ANIMSCRIPT_, ANIMLIST_ and ANIMSEQ_ flags; the whole of
  `sAnimScript_tag`, `sAnimSeq_tag`, `sAnimRawSeq_tag`, `AnimDistanceTable` and `sAnimSet_tag`; `BLData::animSet`
  is an `sAnimSet_tag *`.
- docs/anim, which the brief points to, is on the blender-exports branch (7af6075), not this one, so it is not
  updated here.

### Parcel 12 (package A: collide.cpp and rigid_body.cpp)
- Their names without an Xbox function of that name: `Collide_Filter` is `Intersect_CheckIfFlagsMatch` (0x28a50);
  `RecurseBoxesSph` is 0x291c0 and `ASH_RecurseBoxesRay` 0x292e0 (FUN_ in Ghidra, renamed); `ASH_Intersect_RayBox` is
  `Intersect_RayBox` itself (0x28cf0) and `ASH_vecutil_Dist2Tri` is `vecutil_Dist2Tri` (0xd82d0): the Xbox has no
  second copies; `RecurseBoxesBox` has no Xbox counterpart (`Intersect_CylTriGeom` walks the tree with
  `ASH_RecurseBoxesBox`, 0x29250, and `Intersect_BoxBox`); `Intersect_CylTriGeom` is 0x29a30, which Ghidra had named
  `Intersect_RayGeom`, and their `Intersect_RayGeom` is 0x29fa0; `RB_AddImpulsiveForce` is inline (a `Vec_Add2` into
  the impulse, in `RB_Update` and `RB_ApplyWind`), and so is `RB_SetState` (`Quat_QuaternionToMatrix` in `RB_Create`).
- Ghidra's `Collide_CylinderIntersect` (0x2c440) is the shared narrow phase of the three queries (`Collide_TestHits`,
  our name: the test in EDI, the flags in EAX); their `Collide_CylinderIntersect` is 0x2c6f0. `Collide_unknown`
  (0x2c760) is `Collide_CylinderTris`, our name: the cylinder query that gathers triangles for
  `Collide_PlaceObject` and the character shadows (theirs inlines it in `Collide_PlaceObject`).
- `Collide_PickObj`, for a cylinder, copies only the normal of the test's plane (12 bytes) to the hit's normal and
  direction; theirs copies the plane with its d.
- `Collide_GetDamageNObjects` sums on the x87 and returns the double unrounded (theirs sums floats; ours returned a
  float too, which SP_GetHitDamage and Monitor_Update then rounded where the originals did not).
- `Intersect_RayBox` compares each slab's far side unrounded, and stores both sides as floats; `Intersect_SphereBox`
  and `Intersect_PointOOBox` take their sums in double.
- `Intersect_PointGeom` lets a NaN distance to the plane through (`<= least` skips, theirs `!(least < dist)`), and
  sets the hit's position from the sphere and the old normal before overwriting it (a call theirs leaves out).
- `Intersect_RayGeom`, with the test's flag 0x10, ends the search at the first triangle whose material the filter
  accepts; theirs ends it at a material with 0x40 or 0x80 as well.
- `Intersect_CylGeom` has no test of a triangle's first x against the box (theirs breaks out of the leaf there).
  The Xbox first passes by a triangle both ends of the cylinder are more than its radius from, on the same side of
  its plane (the start's distance rounded to float, the end's not), then tests the triangle's bounds against the box
  (`Intersect_AABB_SweptVolume`, registers EDX, ECX, ESI, EDI, EBX), and takes the axis's dot with the normal as a
  float, in the order y, x, z.
- `Intersect_Portal` keeps its triangle counter in a global (0x1dec34) and picks the plane by the counter halved from
  a pair, the second the portal's plane reversed, which the two triangles it tests never reach.
- `vecutil_intersect_line_line_2d` multiplies by the reciprocal of the denominator; theirs divides by it.
- `vecutil_Dist2Tri` and `vecutil_Dist2Tri_PointGeom` keep the nearest point's x and y unrounded on the x87 into the
  distance, and the point's offsets from an edge's end as floats, but its z unrounded.
- `RB_EulerEvolve`'s drag and integration are two functions on the Xbox, `RB_Drag` (0xc01d0) and `RB_Move` (0xc02d0),
  both renamed. The angular drag's exponent is (I/damp) x (-1/I) x dt, and the exponential is MSVC's inline x87
  sequence (`FExp`, X87Ops.h); the linear drag's factor is (1 - e) x (-1/dt), not their (e - 1)/dt.
- `RB_apply_omega` takes the half angle's cosine with FCOS (theirs: the sine of the angle plus pi/2), and divides the
  sine by the speed before the three products.
- `RB_RestingContact` takes -(d x (restitution + 1)) of the velocity into the push, not -(r d + d).
- `RB_CuboidMomentsOfInertia` adds the x side's square, stored as a float, to the y side's unrounded for the third
  moment.
- `RB_Update`'s light finder is the shared empty function (0xde5f0, `psiLight_Create`): a body that carries a light
  calls it every frame and never finds one.
- Found in ours, not theirs: the cel's sub-cel chain is at 0x18 (theirs reads it right); our `cel_tag::nextCel`
  (0x14) is the world's list of cels, so `childCel` is added; `obj_tag` 0x08 is the next object in the cel and 0x0c the
  previous (ours had the names the other way round; `control_unlink_object` is unchanged in effect).
- Fields named: `HITTEST_tag` whole (`plane`, `dir`, `pos`, `sphere`, `sphereRadius`, `tester`, `ignoreA`, `matrix`,
  `glist`, `radius` (was `rayLength`), `damage`, `type` (was `hitType`, `unknown80/81`), `mask`, `flags`, `found`) and
  the `HITTEST_` types; `HITDATA_tag::dist` (was `unknown1f`), `flags` (was `unknown3`) and `HIT_COPIED`,
  `HIT_NO_CONTACT`, `HIT_NO_PUSH`; `cel_tag::childCel` 0x18, `glist` 0x38, `position`, `rotation`, `sphere`,
  `sphereRadius` 0x58-0x7c, `flags` a dword with `CEL_NO_COLLISION`, `CEL_NO_CONTACT`, `CEL_MASKABLE`, and
  `CEL_WORLD_SPACE` in `bitsFromPlacementTag`; `portal_tag::plane`, `stamp`, `centre`, `boxMin`, `boxMax`;
  `COLLBOX_tag::triEnd`, `vertexBlock`, `quant` (`COLLQUANT_tag`); `collDataB` (a triangle); `obj_tag::maybeCollision`
  is a `HITTEST_tag *` (`CAR_INFO` starts with one); the whole of `RIGIDBODY` and `RigidBodyPlacement` (RigidBody.h);
  `BOXENTRY_tag` (ColBoxs).
- Not changed, but not exact: math.cpp's `Vec_Magnitude`, `Vec_Dist3D` and `Vec_SqDist3D` work in float and return
  floats, where the originals sum the squares on the x87 and return the unrounded double (`Vec_NormaliseLen` returns
  `Vec_Magnitude`'s). The originals and ports above call ours, so the shadow cannot see it; the demos would.

### Parcel 12 (package B: break.cpp, bullet.cpp, effect.cpp and explode.cpp)
- Their names without an Xbox function of that name: `Debris_Create` is 0xcccf0, which Ghidra had named
  `Debris_CreateEx` a second time (renamed); `Occlude_Create` is 0x6c880 (FUN_, renamed and ported: `Bullet_DoTrails`
  calls it); `Sub_ApplyDammage` is `Sub_ApplyDamage` 0xcf6a0; `Sensor_ApplyDamage` and `Monitor_ApplyDamage` are the
  empty function the linker shares (0xe0ec0, Explode_Propagate's types 0x37 and 0x38); `Bullet_update` is
  `Bullet_Update` (ours since an earlier parcel).
- Functions theirs inline that the Xbox keeps apart: `Break_Kill`'s window is `Break_ShatterWindow` 0x1fc90, which
  ends by jumping to the `__WBINVD` thunk 0xdfb20 (their `psiFinishedObjectShatter`); `Hurt_Update`'s trigger test is
  0x6c640 (EAX the object, ECX the volume; `Hurt_IsTriggered`, our name); the marks' allocator and links are 0x6ff70,
  0x6faa0 and 0x6fad0 (`Effect_NewMark`, `Effect_LinkToCel`, `Effect_LinkToObject`, our names; `Effect_Bullet` still
  inlines the allocator and the cel link); the two dust trails of `Bullet_DoTrails` are 0x217f0 and 0x21870 (ESI the
  object); `Bullet_homing`'s target search is `Bullet_acquiretarget` 0x22db0 (left to the original).
- Register arguments their prototypes hide: `Break_Kill` (ESI the object, EDI the hit), `Bullet_DoTrails` (ECX, EAX),
  `Bullet_trail_effect_smoke` (ESI), `Bullet_heatseekable` (EDI), `Bullet_homing` (ESI the bullet), `Effect_Bullet`
  (EAX the object, ECX the hit), `Effect_Player` (EAX the hit, EBX the object), `Effect_Laser` (EAX the hit, EBX the
  cel: tools/abi_action.json lists EAX only, as EBX is first read after a call), `Effect_Body` and `Effect_UnLink`
  (EAX).
- `ShatterPolyRecurse` takes the shard's speed as 0.125 / distance (theirs 1 / distance x 0.125), keeps the third
  side's square unrounded (the first two are stored), and clears the shard it allocates without checking it.
- `Destroy_Create` takes the delay as the key times FRAME_RATE, negated and truncated with no rounding to float
  between; the blast key / 100 is the explosion's radius (Explode_Create's fifth argument), and the explosion's damage
  is its kind times ten. `Destroy_Smash`'s light lookup is the empty `psiLight_Create` (0xde5f0): it never finds one.
- `Hurt_Create` keeps the path's frames x REC_FRAME_RATE unrounded into the speed (FRAME_RATE / duration x that; it
  stores the product first and then overwrites it); `Hurt_Update` compares the new path time unrounded with the frames.
- `Bullet_init` truncates the bloom (shots x spreadPerShot) straight from the x87, with no rounding to float.
- `Bullet_CollisionHandler` takes the bounce's dot product as (c z + b y) + a x, doubled, and the ricochet's as
  (z c + y b) + x a, times -(bounce + 1) (theirs a x + b y + c z, and -(bounce d + d)).
- `Bullet_handle_object_destruction` has a branch for objects that are not bullets (an explosion of script 0x600004f)
  behind a test of 0x1000000 against a byte: it can never run (theirs returns 0 there, the same in effect).
- `Bullet_DoTrails`' flash grenade takes the angle to the player with FPATAN and its size with two calls of
  `Vec_AngleDifference`; with no swoosh interval it divides by zero (theirs skips the trail).
- `Effect_Create` reads the hit before its only check (the object; theirs checks the hit too), and in multiplayer
  leaves glass's normal turned round (the plane is put back only after the multiplayer return).
- `Explode_Create`'s third argument and its last three bytes are never read; it returns nothing. `Explode_Propagate`
  keeps the x87's choice where two NaNs meet (X87NaNs), as the damage it passes on can be one.
- Found in ours, not theirs: `BU_tag` 0x3c to 0xc8 is the bullet's `HITTEST_tag` (obj_tag::maybeCollision points
  at it), not separate sweep fields; `celglist_tag` 0x34 is a pointer to another model (`lod`, was `lodRelated` as a
  uint, and Sensor's copy with it); `CAR_INFO::damageAmt` is a float (explosions add to it); `Effect_tag`'s
  `someThing`/`someOther` are the life and the fade.
- Fields named: `ObjData_Break::mask`, `BreakPlacement`, `SHARD_tag` whole, `TriListTriangle`/`TriListVertex`/
  `TriList` moved to psiDraw.h; `DESTROY_tag` and `DestroyPlacement` whole; `Hurt_tag`'s path fields and
  `HurtPlacement`; `BU_tag::startPos`, `occluder`, `test`, `charge`, and the projectile, impact and weapon flag enums
  the bullet code reads; `CASING_tag` whole; `Effect_tag::vel`, `spin`, `life`, `fade`; `HEINFO_tag`, `HEGAS_tag` and
  `EffectInfo`; `ATTACH_tag` whole, `obj_tag::attachments` and `cel_tag::attachments` (0x10); `GAS_tag` whole;
  `ENV_tag` (the wind) and `cel_tag::env` (0x2c); `SWOOSH_tag` whole; `EXPLODE_tag` whole; `DroneImpact_tag::radius`
  and `source` (was `unknown14`); `GUNTURRET::health`; `COPTER::parts`, `damageA`, `damageB`.

### Parcel 13 (vecmath.cpp and view.cpp)
- Their names without an Xbox function of that name: `Mat_Inverse` is two functions, 0xd5190 (inverts in place;
  renamed `Mat_InverseSelf`) and 0xd51e0 (`Mat_Inverse`); `View_SetupRenderModes_Tweak` is 0xd9900 (FUN_, renamed);
  `Camera_Reset` is `Viewer_Init` 0x24290; `Vec_LineSphereIntersection` is 0xd8590 (FUN_, renamed
  `Vec_LineSphereIntersect`, the name Anim.cpp calls it by; left to the original); `ATAN2_APPROX` is `maybeAtan2`
  0xd6440; `Rand_FRand` is `Float_FRand` 0xd5360; `View_DrawInThisViewOnly` is `View_SetDrawInThisViewOnly` 0xdb380.
  `Mat_GetTrans`, `Mat_SetNorm`, `Mat_SetTrans`, `Vec_Avg2`, `auxVec_Add` and `LimitRadAngle` are inline on the Xbox
  (row copies, sums and the -pi..pi loops in their callers); `Mat_Rotate`, `Vec_MatToEuler`, `PS2Sinf`, `PS2Sinf2`,
  `PS2Sinf3` and `Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM` have no Xbox counterpart (the Xbox runs FSIN).
- Static helpers renamed from FUN_ and ported: `ClipEdge` 0xdb730, `PortalPlanesFrom` 0xdb970, `View_AddToList`
  0xd91d0, `View_SetupCelRenderModes` 0xd9bd0, `View_DrawAttachments` 0xda1d0 (their
  `View_maybeDrawNightVisionExtras`).
- Register arguments their prototypes hide: `View_AddObjects` (EAX the cel), `View_AddCels` (ESI the viewer),
  `View_DrawCelGlist` (EAX the viewer, ESI the cel).
- The x87 throughout: `RotMatrixX/Y/Z/XY` keep FSIN and FCOS unrounded and the terms their source multiplies by zero;
  `Vec_NormToRot` and `Vec_DirToRot` take FPATAN, in the original's write order; `Vision_Calculate_View_Fustrum` takes
  FPTAN; `ClipEdge` tests the dot with the plane's normal unrounded and divides by it as a float.
- `Ang_RotateToBearing` returns a float and takes the step from the unrounded difference (theirs from a rounded end).
- `Vec_MakeOrthonormalBasis` takes the y branch for a NaN component (theirs the x branch).
- `Camera_SetFade` is 60 / frames x alpha x 6.535948e-05 (one constant, theirs split); `Camera_UpdateGlbVars` ends a
  fade whose level is a NaN (theirs keeps it); `Camera_ApplySwing` keeps FSIN unrounded.
- The viewer is 0x208 bytes (theirs 0x240): the draw lists at 0x08, enabled 0x29, the fade fields to 0x205.
- `ClipEdge` sums the clipped point's terms in the original's order (theirs in another).
- The C++ callers of the helpers whose originals return the x87's unrounded value (`Vec_Magnitude`, `Vec_Dist3D`,
  `Vec_SqDist3D`, `Vec_NormaliseLen`, `Vec_AngleDifference`, `maybeAtan2`, `Vec_ScalarTripleProduct`) now keep the
  value in a double where the original compares or computes with it before storing: `LinkCreep_Setup`,
  `AINetwork_LineIntersectsBoundry`, `AnimObjectAimAt`, `Check_AutoAim`, `DroneAnim_LocationDeathAnim`,
  `DroneMove_AstronautAnim`, `DroneMove_NoBunching`, `NDrone2_Steer`, `NDrone2_EvaluateObstacle`,
  `DroneVision_ObjectVisibility`, `Drone_IsCoverNodeUsable`, `HandleMapSoundAllocation`, `MP_GetSpawnPoint`,
  `NDrone2_BondWithinAngle`, `NDrone2_DealWithObjHit`, `NDrone2_ExplosionDir`, `NDrone2_FindAmbushCover`,
  `NDrone2_SetupHostageKiller`, `Player_Activate`, `Player_Climb`, `Player_CollWire`, `Player_Wire`,
  `Player_HandleJump`, `Searchlight_Create`, `Sensor_InCone`, `Switch_GetNearest`, `control_ClosestPlayerPnt`,
  `Drone_GetOpponentInfo` (the bearing plus pi from the unrounded atan), `DroneSpawner_SpawnDrone`,
  `Drone_ModBulletDamage`. `DroneMove_NextRouteAngle` returns the XBE's `__CIacos` unrounded; our callers take it
  through `DroneMove_RouteTurn` (an `Extended`, by way of the injected entry, so a shadow's fake still sees the call).
- The x87 runtime's error path (`__CIacos` outside -1 to 1, or of a NaN, which a NaN position now reaches since
  `Vec_Normalise` normalises a NaN length as the original does) sets errno through a per-thread block that the game's
  heap allocates. The shadow tests run before there is a heap, so MenuProbe sets the runtime's flag at 0x300608,
  which skips that path and gives the same result, while they run.
- Found in ours, not theirs: `viewer_tag` fields from 0x08 to 0x205 named (the draw lists, the swing, the fade);
  `cel_tag::nextVisible` (0x20) and `blink` (0x93); `portal_tag::door`, `seen`, `open`; `struct VisPortal`,
  `VisCel`, `DrawEntry` and `SkyObj` (21 at 0x29d7a0).

### Parcel 14 (package A: turret, scenery, pickup, copter, hazard, emitter, gunimp, weather and lights)
- Their names without an Xbox function of that name: see the table. `Copter_CreatePath` and `Copter_NextPath` are
  two Xbox functions (0x6b930, EBX the copter; 0x6b9f0, ESI), theirs one.
- Register arguments their prototypes hide: `Copter_CreatePath` (EBX), `Copter_NextPath` and `GT_RandMove` (ESI),
  `Copter_Move` (EAX and the stack), `Copter_FireGun`, `Copter_FireMissile` and `Copter_SeesPlayer` (EDI),
  `GT_maybeInterpolateAimDir` (EAX the matrix it turns and writes, ECX the one it turns from), `Pickup_Handler`
  (EDI the pickup's info).
- `Apocalypse_Update` multiplies in the original's order: (rumble / length) x left and (left / length) x 5 and x 382
  (theirs groups them otherwise). Its channel index is signed and not masked.
- `Mine_Update`'s explosion passes 0xc8, 0xc8 and 0.
- `GT_SpinBarrel`'s matrix has its translation zeroed by `Vec_Zero` (theirs says it is left as garbage).
- `UpdateDrops` works in batches of 1000 drops, the step indexed within the batch (theirs 992).
- `Check_Target` reads the weapon from the player's `animState->currentWeaponId`.
- `Leaf_Update`'s leaf has no w (theirs a 16-byte vector).
- Found in ours, not theirs: `COPTER` fields `damage`, `attackDamage`, `enableChannel`, `scriptPlayer` (were
  `damageA`, `damageB`, `switchChannelToShowCopterBody`, `scriptPlayerObj`), `COPTERPATH`, `COPTERBIT`; the whole
  `GUNTURRET` (0xe4), `TARGETOBJ`; `PICKUPINFO` (0x34); `ENV_tag` to 0x44 (`windSide`, `pos`, `windMax` (was
  `windOn`), `dropType` 0x3f, `windRising` 0x41); `RAINBOX_tag`, `DROP_tag`; `EMITTER_tag` (0x4c), `EMITTER_DEF`,
  `KEYED_CLR`; `light_tag::distanceShare` 0x2c; `cel_tag::ambientR/G/B[2]` 0x8a and `obj_tag::ambientR/G/B` (were
  `light_related1-3`); `BLData::keys` (was `unknown6b0`) and `ambientBoost` 0x8f2 (was `_pad_5e`); the placement
  structs of each object; `GlobalEnv` 0x1f64ac, `ClosestLights` 0x215718, `TargetList` 0x25fc20, `PickupMatrix`
  0x17e5d8, `UseableGuns` 0x17e648, the GUNTYPE table 0x163a30. `psiCopyToSP` takes (pointer, offset, size).

### Parcel 14 (package B: control, parsemap, sort, spline, objects, build, resetmap, gameflow, loader, ssys)
- Their names without an Xbox function of that name: `control_add_object_to_list` is inline in
  `control_create_object`; `control_init_collision` has no Xbox counterpart; `control_AnimUpdate` is not on the Xbox
  (the control table points types 0x15 and 0x1f at `AnimObjectUpdate` itself). `parsemap_block_texture_header`
  (block 0x10), `_texture_data_pc` (0x15, 0x16: the data's address put in the texture header),
  `_texture_data_pcdx` (0x29: the same, then `parsemap_block_palette_data_psx`), `_path_data` (0x19) and
  `_memory_discard` (0x1c) are inline in `parsemap_handle_block_id`; `_texture_data_GameCube` (0x17) is
  `parsemap_block_texture_data_Xbox` and `_palette_data_pc` (0x11) `parsemap_block_palette_data_psx`; the dictionaries
  (`_dict_xyz`, `_uv`, `_rgba`, `_norm`, `_comlist`), `_morph_data`, `_PS2_GFX`, `_coll_data` and `_datums` have no
  Xbox handling (blocks 0-3, 8, 0xa, 0xb, 0x20, 0x22-0x25, 0x2a, 0x2b and 0x2d fall to the default).
  `parseentity_fixup_entity` is `parseentityfixup_entity` (0xa4790, left to the original). `Swap` is inline in
  `QuickSort`. `build_MakeCelOverlapBox` is the shared empty function 0xe0ec0 (`ResetMap_Load` calls it with
  glb_world). `GetLevelToLoadFromMovieCode` is `maybeGetNextMissionInSequence` (0xbdcd0, left to the original).
  `LoaderLoadAnims`, `LoaderShrink`, `MP_NeedFile`, `CheckSize` and `CheckSizeAndShrink` have no Xbox counterpart:
  the Xbox's `LoadableLoad` and `LoadableReload` load nothing, `ResetMap_Load` shrinks with `Mem_Shrink` itself and
  `parsemap_parsenextblock` asks no multiplayer question. The drone mode initialisers were accounted for in parcel 8.
- Not ported again: `NDrone2_GetDTYPENEW` (0x3fae0) is the static helper of our drone mode code (parcel 8, called
  only from there); `NDrone2_init_DMODE_Normal` (0x408a0) is a single jump to `NDrone2_init_DMODE_Defaults`.
- Helpers ported with them: `maybeControl_Switches_Update` (0x2d640, theirs inline in
  `control_movement_object_handler`), `build_MakeRoomList` (0x20bd0, renamed from FUN_; theirs inline in
  `build_FindCel`), `init_cel` (0x20660, theirs inline in `build_alloc_cel`), `set_InhibitGameDrawIfRequired`
  (0x6ab60, our draft finished). `MenuManager_Enable` (0x92c10) renamed from FUN_ (left to the original).
- `control_handle_cel_change` calls `Controls_StraddleTest` and returns only for an object always forced. Its box
  clamp compares each bound (radius plus the box's edge) with the position before rounding it to a float.
- `control_movement_object_handler`'s switched rooms are `maybeControl_Switches_Update`, which reads the cel's
  model's LOD before it knows there is a model (theirs skips cels without one). The pointers cleared for a deleted
  object are the bullet's at 0x24 and 0x34 and the explosion's at 0x1c. Their `MP_DebugDraw` is the shared empty
  function.
- `build_FindCel` takes a hit's distance as the new reach unless the reach is already below it, so a NaN distance is
  taken (theirs: only a smaller one).
- `build_alloc_portal` sums the corners in the original's order (x: 3, 1, 0, 2; y: 2, 3, 1, 0; z: 1, 0, 2, 3).
- `Plane_PutPntOnPlane` sums bÂ·y first, then aÂ·x, cÂ·z and d.
- `Spline_Eval3D` is ((((3t - 5)Â·p1 + 4Â·p2) - 3tÂ·p2 + (t - 1)Â·p3)Â·t + (p2 - (t - 1)Â²Â·p0))Â·t + 2Â·p1, halved,
  unrounded on the x87 (theirs groups the p1 terms otherwise).
- `CompLight` reads the light at 0x2c and gives 0 for a NaN (theirs 0x38 and 1); `CompBoxList` puts a NaN after
  (theirs before).
- `ResetMap_GenLoadScreen` on the Xbox is only label 0x3c1, wrapped 0x21c wide at x 0x138 from 408 down, 15 apart,
  then four `LoadScreen_Draw`s and `LoadingScreenMakeVisible`; theirs (the PS2's) adds the mission and the hint.
- `ResetMap_GameInit`'s `View_Init`, `AnimCacheInit` and `Sound_SetLogSfxFile` are the shared empty function on the
  Xbox; `ResetMap_Load` loads the shared files 0x07000500 first, calls `LoadWoman` (the Xbox layer's thunk 0xdc860),
  `SFXLoadCorrectBank` and `psiLight_Create(1)`, and `LoadingScreenMakeInvisible` once the level is in.
- `GameFlow_PopState` returns the state it takes off (0 for an empty stack), not the new top.
- `LoadableLoad` and `LoadableReload` load nothing on the Xbox: they drop the hashtable's items of type 0x04000000
  and make its sections again under malloc method 1 (`LoadableReload` frees the script first and starts the anims'
  preload again).
- `Lock_Update`'s decryptor reads the bullet's weapon unchecked where the laser checks it.
- Found in ours, not theirs: `obj_tag::lastCentrePoint` 0x54 (was `_pad_2`), `cel_tag::headPrev` 0x0c and `parentCel`
  0x1c (were undefined bytes), `portal_tag::unknown24`, `CEL_ROOM`, the effect flags `FLAG_STRADDLES_CELS`,
  `FLAG_FORCED_LISTED` and `FLAG_ALWAYS_FORCED`; `level_tag` (0xb0: the spline and 33 keys), `TARGET_PLACEMENT`'s
  spline and key count, `block_header_tag` moved to parsemap.h; `GlobalVars_t::keypadPlayer`, `keypadBLData` and
  `DecoderTargetKnown`; `EMITTER_DEF` padded to its 0x40 bytes; `ENV_tag::unknown42` a byte; `LOCK_tag`,
  `LockPlacement`, `EnvPlacement`, `DYNAMICOBJECT_tag`, `DynamicObjectPlacement`, `framelist_tag`; `VisCel` moved to
  Vision.h; `GameState_t::InhibitGameDraw` a byte. util/Sort.cpp is new (QuickSort and the comparisons).

### Parcel 15 (package A: font, sprite, view's sprites, cirbuf, text, sound's subtitles, hud and the text box)
- Their names without an Xbox function of that name: see the table. Their two `Text_Init`s (text.cpp and
  menu_controls.cpp) are two Xbox functions, 0xd1c90 and 0x99550, each listed twice in the parcel.
- Register arguments their prototypes hide: `Font_FindGlyph` (AL the character), `Font_GetKernAdjust` (AL the
  previous character; the adjustment returned in ST0), `__Font_DrawText` (ECX the colour, EAX the scale),
  `Text_NextMsg` (EAX the message, BL the player, EDI the type), `Text_SetupChar` (EAX the box, DL the character),
  `HUD_PlaceLensFlare` (ESI the sprite), `HUD_UpdateCrossHair` (EAX the player, ESI the sprite),
  `HUD_MonitorNightSight` (EAX), `HUD_CreateShift` (EAX the player, EDI the pane, ESI the create entry),
  `Sound_DoSubtitle` (ECX the sound).
- The Xbox font table has the default font at `FontTable[1]` and a font code n selects `FontTable[n]` (theirs
  `FontTable[0]` and n - 1); it has 23 special characters (theirs 17). `Font_ParseFormat` takes no scale and
  `Font_WordWrapString` four arguments (theirs a fifth, the scale). `Font_FindGlyph`'s search does not stop at the
  end of the glyphs (theirs guards it).
- `RADAROBJ` is 0x14 bytes (theirs 0x20); the radar's bot test reads `Drone_tag::flags` at 0x3f4 (theirs the PS2's
  0x4f8).
- Found in ours, not theirs: gfx/Font.h's `KERN_tag`, `CHARDATA_tag`, `FONT_tag`, `FONTTABLE_tag`,
  `SPECIALCHAR_tag`, `PRINTINFO_tag` and the `FONT_*` flags; the `SPRITE_*` draw flags in view.h; `CIRBUF_INFO`;
  `TXTMSG_*`, `TextMsgFormat` (moved to Text.h), `TXT_MSG::priority` (was `param_2`) and its padding to 0x20;
  `SND2LBL_tag`; `HUDPANE_tag::posX`/`posY` (were `unknown2`), `HUDPANECREATE_tag::flags` (was `unknown_thing`) and
  the `HUDPANE_KEEP_IN_*`/`VIEW_*` flags, `SPRITE_CLIP_TO_VIEW`, `RADAROBJ`, `MPRadarMarks` (were
  `MPGameStruct::radar_related`); `M_CONTROL::unknown08` and `skinPiece` (were padding), `NEW_CONTROL`,
  `MENU_SKINPIECE`, `TEXTBOX_KEY`, `M_TEXTBOX` (0x9b0); `BLData::weaponChangeTimer` (was `field_0x8ee`);
  `WATER_HEAD_*` moved to Player.h. `CompMsgTxt` (parcel 14) read its lines as `HUDPANE_tag`s; it now reads
  `TXT_MSG::priority`. Our drafts replaced: the crosshair (float maths, and the crosshair setting tested while
  zoomed), the night sight (fell through for a weapon object not in state 0, without the 1..2 vision test),
  `HUD_CreateShrink` halved with `/ 2` where the original shifts (wrong for negatives); `View_AddSprite` noted font
  glyphs for the texture dump by its caller's return address, now by the glyph's `CharInfo`.

### Parcel 15 (package B: music, sound, input, llist, hashtable and the leftovers)
- Their names without an Xbox function of that name: `PickRandomStealth` and `PickRandomCombat` are 0xc6f10 (was
  FUN_) and 0xc6fa0 (was `maybePickRandomCombat`), renamed; `UpdateMFX_02MayhewD` is `UpdateMFX_MayhewD` (0xc7740);
  `Sound_Ready` (the level's bank) has no Xbox function of that name (`SFXLoadCorrectBank`, called by
  `ResetMap_Load`, is the Xbox's); `MapAnalogStick` is inline in `StickCompensation2`; `DList_AllocnNodes` is not on
  the Xbox. The drone mode initialisers, `NDrone2_initDTYPE_BotInit`, `MP_NeedSkin`, `AnimDistanceTableInit`,
  `AnimTagGetUnique`, `AnimSeqDelete`, `Draw_LaserBeam` and `Draw_TaserBeam` were accounted for in parcels 8, 10 and
  11.
- The Xbox's `Sound_Play` is not theirs (the PS2's 2D play): it is the helper under `Sound_Play3D` and
  `Sound_PlayExt`, with the sound in EAX and the position, volume, radii, pitch, 3D flag and caller's tag on the
  stack. A radius above zero is taken (theirs: the same in `Sound_Play3D`), the table's index is the sound's low 20
  bits, and the tag is what `Sound_Stop_Ref` and `Sound_Playing_Ref` test.
- `Sound_Alertness` compares the sum before it is rounded with the best so far, takes the squared distance rounded to
  a float against the outer radius squared unrounded, and returns 0 only for a sum below 0 (theirs: a sum not above 0,
  which turns -0 and NaN into 0).
- `Sound_UpdateListeners` hands a cut scene's listener the movement of the last player's view, which is uninitialised
  stack with none (theirs zeroes it, as ours does); `GlobalListenerPos` is three floats. The listener count is the
  low 16 bits of `MPSettings.numPlayers`.
- `Input_Init` ends at the empty debug hook (theirs calls `psiInput_Init`). `Input_Update` leaves a NaN channel NaN
  (theirs makes it -1) and multiplies the inverted axes by -1. `StickCompensation2` works the length in double on the
  x87 and maps a NaN length too (theirs skips it). `Input_RumbleStop` calls `psiInput_ResetRumble`.
- `hashtable_initialise` sets only the two end keys, not their data or `ht_insert_ind`; `hashtable_deleteitem`
  does not check the key is there and `hashtable_delete_hashtype` not that the index is still below the count
  (their guards). `Get_Music_Event` does not mask its id.
- `UpdateMFX_02MayhewB`: the start of cut scene 0x600007f falls through to the test for its end, so a scene that
  starts and ends in the same frame asks for section 8 (Ghidra's decompile puts that test in an else; theirs reads it
  right).
- Found in ours, not theirs: `DYNAMICSOUNDS::ref` (was `unknownParam`), `age`, `audible` and `unknown44`; `cel_tag::
  soundEnvironment` (0x88, was two undefined bytes); `PlayerInput::holdFrames` (0xb4), `actionLock` (0x12c) and
  `anyInput` (0x154, was `field141_0x154`); `MUSICDATA` (MapMusic's rows), `MUSICAIVARS` (MapDroneData), the
  `MUSICEV_` events, `SOUNDTRIGGER`, `MUSICTRIGGER`, `AnimDistanceTableEntry`; `CompareFunc` moved to LList.h.
  Wrong in ours: `LList_Cut` set the count to 0 when it emptied the list (the original counts down);
  `hashtable_additem` reset the search windows of all eight hash types (the original: types 0 to 6); our
  `Sound_PlayExt` and `Sound_Play3D` called a draft `Sound_Play` that took a radius of 0 as given and indexed the
  table with the whole sound id. `Input_Update` was a draft calling the original by address.

### Parcel 16 (package A: menu.cpp and menu_script.cpp)
- Their names without an Xbox function of that name: see the table.
- `Menu_GetLastController` returns the controller `Menu_GetAction` last saw act (0x17c540, -1 at boot); theirs
  returns 0.
- `__Menu_Send` reads a control's next link after sending it the message; theirs reads it before (a handler that
  unlinks or moves the control changes what comes next on the Xbox). `Component_DestroyAll` likewise reads the next
  set from the block it has just freed; theirs before freeing it (an original bug, kept).
- `Script_CalculateSpline` sums the cubic term, the square term, the linear term and 2 p1 in that order on the x87
  and returns the sum unrounded (`Script_InterpolateSpline` truncates it); theirs sums in the reverse order in float.
  `Script_RunFrame` divides the frames in double and stores t as a float; theirs works in float.
- `Menu_GetControl` weighs a control by 640 and 480 less its size (theirs 512 and 448: the same order). The sounds go
  through `Sound_PlayExt`; `Menu_Update` calls the shared empty function 0xe0ec0 for a Window.
- Their additions, not the Xbox's: the caps of 32 controls under the cursor and 64 wrap-around neighbours, and a zeroed
  component when `Component_GetHandle` finds no set (the Xbox returns NULL and its callers read through it).
- Found in ours, not theirs: `LINKEDLIST` (ui/LinkedList.h; `M_CONTROL::scripts`, `processes`, `M_PAGE::controls`,
  `M_MANAGER::pages` were `char[16]`); `M_CONTROL::sprites` (0x08, was `unknown08`), `skin` (0x14, was `skinPiece`),
  `script`, `key`, `loopKey`, `loopStep`, `step`, `frame` (0x38-0x54, were `selectedScript`, `scriptPlaying` and
  padding), `numSprites` (0x6c); `M_WIDGET::defaultScripts` typed `M_SCRIPT *`; `M_WINDOW` (its page at 0x94),
  `M_BASECONTROL` (the 0x94 bytes every control type starts with: the text box's frame, cursor and grid, and
  `Menu_GetFirstControl`'s stand-in at 0x224558), `M_SCRIPT`, `M_KEYFRAME` and their `SCRIPT_*`/`KEYFRAME_*` flags;
  `M_MESSAGE::next`, `prev`, `target` (were padding) and `MESSAGE_TO_*`; `MENU_COMPONENT` (the text box's
  `MENU_SKINPIECE`, its `unknown06`-`unknown0c` the insets `left`, `top`, `right`, `bottom`), `MENU_COMPONENTPIECE`
  and its `PIECE_*` flags, `MENU_COMPONENTSET`; `ControlType_72`. Wrong in ours: util/Sort.cpp said nothing asks for
  a `Compare_MenuCtrl` sort (`Menu_FindControl` does); Menu.cpp declared `__Menu_SendEx` a second time with `int **`
  arguments (an overload of the header's); `Menu_GetLastController` was declared `__stdcall`; docs/ui/framework.md
  called `Script_AddScript` an invented `Script_New` and said `Menu_UpdateTextFmt` keeps the text format (it writes a
  scale nothing reads). Not ours to change, noted for its owner: `Menu_UpdateWheel` (ours, earlier work) skips the
  colour of a wheel slot past either end of the list, where the original sends one read from item 999 (`// Previously,
  the game wouldn't check for 999 ... Now fixed.`): a departure outside `FIX_ORIGINAL_GAME_BUGS`.
- The comparisons of a `HASHCODE` with 0xffffffff (every control) and of a message's target with 0xfffffffd/e are
  made on `uint`: clang takes the enum's range as `int`'s and called them always false.

### Parcel 16 (package B: menu_manager.cpp and menu_controls.cpp)
- Their names without an Xbox function of that name: see the table. Their `FixupResolution`s scale the menu data's
  640 x 480 to the PS2's screen; the Xbox uses the data as it is.
- `Process_Destroy` reads the next process from the one it has just freed; theirs reads it before (an original bug,
  kept, as in `Component_DestroyAll`).
- `MenuManager_Exists` and `Menu_PlayerHasQuitOrRestarted` return a byte (AL); theirs a 32-bit value.
  `Manager_SendMessage`'s LockInput stores the argument's low byte as it is, not 0 or 1; the manager's flags (0x1d1-
  0x1d5) are bytes, not `bool`s.
- `MenuManager_Monitor` keeps the controller state in EDX across its call of `MenuManager_GetStatus`, which writes only
  EAX and ECX; ours has an adaptor that keeps EDX. `Manager_SendMessage` hands a page's QueryBack its own argument
  slot for the answer.
- `Menu_GetNormalTextFmt` (0x73220) exists on the Xbox: it sets a sprite's scale to 1 (`Label_Update` calls it). The
  Xbox's `Menu_StringInit` and `Menu_StringKill` are calls of the shared empty function 0xe0ec0.
- `List_Init` counts its visible rows from the row height before it is stored as a float; theirs from the float.
  `List_SetCellText` sizes a cell by its column's share times 0.01f, times 0.9 of the list's width in double.
- Their additions, not the Xbox's: the NULL guards in `List_GetIndex_RealItem`, `List_SetIndex_RealItem` and
  `Radio_SetIndex`. `Radio_SetIndex` is not `List_SetIndex`'s loop: past the end it gives the last item, where
  `List_SetIndex` gives NULL.
- Found in ours, not theirs: the layouts of every control type (ui/Controls.h: `M_LABEL`, `M_BUTTON`, `M_CHECKBOX`,
  `M_SCROLL` and its `SCROLL_*` flags, `LIST_CELL`, `LIST_ROW`, `M_LIST`, `M_RADIO`, `M_COMBO`, `M_SPIN`, `M_MEMO`),
  `M_PROCESS`, `NEW_CONTROL` (moved from TextBox.h to ui.h, its unions at 0x04 and 0x08), the help texts (0x17d640,
  130 of them), the controller-removed flow's globals, `CONTROL_STATE_UNDRAWN` (4); `PlayerInput::controllerIsActive`
  (0x155, was `field142_0x155`). MessageType 0x73 (`MessageType_ListSetSelectionColour`) sets the colour of every
  other row's stripe (docs/ui/messages.md had a guess).
- The 16 blocks the suite leaves: the NULL tests of a cell's text address and the icon cells behind them
  (`List_SendMessage`, `List_Update`, 6), `List_Update`'s hidden bar (hidden lists return first) and its odd-row
  test for a negative row, `Label_Init`'s right alignment (a constant format), `Manager_SendMessage`'s page link
  of 0xa4 (NULL after the offset), the reads through NULL of `List_SetIndex_RealItem` and `Radio_SendMessage` (3),
  `Memo_SendMessage` with all 32 lines drawn (the suite keeps the last empty for `Memo_Update`'s over-read) and
  `List_SendMessage`'s step down past a continued row.
- Wrong in ours: `Menu_ClearStack` (earlier work) freed the page each history record named, not the record. The
  manager's flags were `bool`s.

### Parcel 16 (package C: menu_frontend.cpp, menu_handlers.cpp, menu_tweaks.cpp, menu_options.cpp, menu_dossier.cpp and menu_controller_style.cpp)
- Their names without an Xbox function of that name: `P_FMV_Handler`, `P_SCREENADJUST_Handler` and `C_SBSCREEN_Handler`
  have none (the Xbox's `Handler_HandleMessage` answers true for their hashcodes, compiled to nothing); `Menu_GetBonus`
  is inline (`Menu_UpdateDefaultCodename` reads the bonus words itself); `Menu_KillCredits` is the shared empty
  function 0xe0ec0, which P_CREDITS calls as the page is left. Their second `Menu_CreateOptionBox` is
  `Menu_CreateOptionBoxLabel` (0x7f3a0).
- P_LANGUAGE goes on to P_INTRO (theirs to the PS2's memory card page 0x40000048), and on the Xbox takes the language
  shown after 600 frames without input; it and C_LANGUAGE keep the choice (`Language_SaveChoice`). The help line's
  strings are the game's (the labels keep the pointer); its button for a language is `Menu_GetLanguageButton`
  (0x840d0), and `Menu_SetSoundLanguage` (0x84080) sets the sound's (both FUN_, renamed: names ours).
- P_START's prompt is label 0x2fb (theirs 0x10001eb). P_MAIN sets the line spacing of 0x10000225 to 100 (theirs 87).
- P_PARISENUM checks the Xbox's free space (0x8fcc0, a box when there is too little) before the mini mission and
  answers that box: A runs the mission, B goes to the dashboard (0x8fd90). Its loading countdown (0x25df68) is only
  ever cleared on the Xbox, so the loading panel never shows (theirs starts it).
- `Menu_CreateOptionBox`: the Xbox's box has one answer at most, value 3, worded by the fifth argument (0x10001d6 "Press
  ~A to continue or ~B to cancel." or 0x266), and none without the fourth; it is centred on 480 (theirs 448, and two
  answers Yes/No of the PS2). C_LBMSGOPTIONS's A answers 3 with an answer and 1 without, not the row's value.
- `Menu_SelectItemInControl` tests the list for NULL, not the control (theirs both).
- `Timer_Seconds2String`: format 0 with hours is hh:mm on the Xbox (theirs hh:mm:ss); the sum is made on the x87 from
  the float 0.01, and the divisions are unsigned (theirs in float, signed).
- P_PAUSE centres the two tabs left in multiplayer on 320 (theirs 256), and names the styles 0x28... (theirs the
  PS2's 0x82...); its hint is 0x1000217 (theirs 0x201). C_GCPAUSE's mission statistics ask `PlrStat_GetScore` for
  player 1 (P_NFSTATS for player 0; the argument is unused). X on the style radio sets the inverted line's colour a
  shade apart (0x645a49c0 / 0x644646c0) from the colours the tab sets (0x645a49d2 / 0x694646d2).
- `Menu_DisplayControllerStyleList`: styles 3 and 5 show their ~C line again for ~D (the label at +9 twice); the
  fourth row's buttons are named per language (French ~M, Spanish ~O, Italian ~K, Swedish ~Q ~P, otherwise ~L ~R).
- P_NFRESULTS on a failed mission (Mission_Status 2 or 3) passes medal 4 to `Menu_SetLevelBonus`, as for platinum;
  without a medal it titles the page "Nice work %s" with the codename (or "Agent" for a default one) and shows icon
  0x300019a.
- Their additions, not the Xbox's: the guards against NULL controls in the option box and the pages, the bound on the
  codename index in C_SBNFCN, C_GOMULTIPLAYER's box in place of the multiplayer pages.
- Found in ours, not theirs: `DEFAULT_CODENAME::showNames` (0x30, MPSettings.ShowTeamAndNameOverhead, was
  `unknown30[0]`); `LEVEL_REWARDS` and `LEVEL_REWARD` (the rewards table at 0x17f478: a mission's five rewards by
  medal, their names and pictures) and `LEVEL_BONUSES` (Menu_GetLevelBonuses' answer); the names of the boot and
  main menu's controls (`SUB_P_LANGUAGE_HELP_TEXT`, `SUB_P_START_*`, `SUB_P_MAIN_FADE`, `SUB_P_MESSAGEBOX_BOX`,
  `SUB_P_MESSAGEBOX_TEXT`, `SUB_C_SBNFCN_*`, `SUB_P_NFSELECT_MENU_GROUP`), the codename wheel's descriptions and
  icons; the iris's state, which is ours now (0x25d7ac-0x25d7b9: the tick, the frame, held, running), and the other
  globals the handlers alone use. Wrong in ours: `Menu_IsBotGood` was an untagged draft that said the function is
  inline on the Xbox (it is at 0x7eae0; now injected); `MenuManager.cpp` named the front end's music flag
  `menu_movie`; `XboxSystem.cpp` called 0x84080 "Sets the text language" (it sets the sound's).
- Wrong in the tests: UnlocksShadow opened two `XbeOriginalScope`s on one address when it compared fewer than
  three functions; the second took the original's bytes for the jump, so `Menu_GetNightfireStatus` (0x76240) and
  `Menu_SetLevelBonus` (0x7cbf0) ran as the original for the rest of the full shadow run, and our code calling the
  ports by name went round the later suites' fakes. Each address now gets one scope; MenuHandlerShadow checks that
  the package's 65 keep their jumps.

## Original bugs found while porting (kept; candidates for FIX_ORIGINAL_GAME_BUGS)

- `Script_Load`, a file with 0x3f streams or more: hands its record to `DList_RemoveFromInUse2Free(&ScriptList)`,
  a list it was never in, and leaves it in the hashtable.
- `Script_FreeFromHashTable`: calls `Mem_Free` on the -1 left for a wrong-version script file.
- `Script_EntityStart` writes as many placement parameters into `tptr` as the stream gives, and
  `Script_EventHandler` reads as many arguments into `args[8]`: neither is bounded.
- `Script_CreateEntity` compares the object's type with the placement type for switch, destroy, trigger, pickup
  and emitter entities. They never match, so those objects are always deleted and created again, never reused.
- `FuseBox_Update`: a broken fuse of list 1 clears list 0's second channel.
- `SP_LoadScript` loads script 0's loadable part, whichever script is active.
- `Door_Create` never checks that `control_create_object` succeeded.
- Unchecked dereferences of the user: `Door_Activate` (one-way and swing doors). Unchecked dereferences of the
  user, viewer and sensor data: `Monitor_Update` and `Monitor_Activate`.
- `PlrStats_SetupScoreTable` trusts the count it is given.
- Channel and message indexes in triggers, doors and the security system are never bounded. Trigger inputs above
  255 read and write `switch_channels_hold`.
- The AI message queue's insert (0x4e3c0) writes through NULL when all 1022 items are in use.
- `Drone_SM_RouteMsg` ignores a failed `Drone_DCVfromOBJ` and carries on with uninitialised DCVars.
- `DroneAnim_LocationDeathAnim` uses `glb_players[0]` and its animState without checking them.
- `NDrone2_GetHeadPos` writes `*pos` unchecked for objects that are not drones.
- `AINetwork_UpdateLinksUsedCount` counts the route on link 0xffff (past the table) when two of its nodes are not
  linked.
- `AINetwork_OptimiseRoute` adds, for each node, the length of the link numbered by the node's slot in its parent's
  link list, not of the link itself.
- `AINetwork_NodeSearchCel` compares a node's distance with the search radius squared; `AINetwork_NodeSearch`'s
  portal retries search the origin's own cel again, not the portal's.
- `AINetwork_BoundsTest` (strict) tests node b with the distance rather than its square, and with node a's height;
  `AINetwork_BoundsNodeTest` also uses node a's height for node b.
- `LinkCreep_Calc` never releases link 0's creeping flag (it tests `> 0`).
- `LinkCreep_Handler` uses the DCVars without a check.
- A* and the emitter improve a listed node's cost without moving it in the open list; `Emitter_GetNodeAtDistance`
  sorts its open list by f values left over from the last search.
- `Drone_CoverLowNode` indexes its five-entry anim table with unchecked placement keys.
- `NDrone_InitDoorNodes` never resets its nearest-door distance between nodes (harmless: the first door is always
  taken).

- `Menu_UpdateMessageBox`: a save refused (saving not allowed, or no codename) returns with its re-entry latch
  still set, so no later load, save, listing or message box runs until the game restarts.
- `LS_FindBlockByLabel` never ends on a header with a label and a zero length. `LS_Load` reads as many bytes as a
  full save of the current state would take, so a file longer than that (a different level count) can be cut in a
  header; a corrupt file can do it too. The shadow test has to avoid both.
- `LS_LoadMission` trusts the level count against its 20-entry stack tables; `LS_LoadFromBuffer` copies
  `Boot_LoadPTPData`'s 0x12c0 bytes into a 0x1000-byte slot (into slot 1); `Menu_EnumSaves` adds names to
  `codename_buf` without a bound.
- `C_CHCHWEAP_Handler` uses `glb_blokes[0]` unchecked; `P_CNSELECT_Handler` answers an option box of type 3 that
  nothing in the Xbox build creates.
- `NDrone2_DoHitEffects` reads the bullet's weapon definition for the impact sound, and with bots on for a fired
  shot's weapon id, without the NULL check it makes for the damage. `NDrone2_ExplosiveImpact` reads
  `impact->unknown14` before its own NULL check. `DroneAnim_LocationImpactAnim` uses `glb_players[0]` and
  `lastHit` unchecked, and the body hit's gas puff (0x36430) uses what `Gas_Init` returns unchecked.
- The explosion's damage (0x3db50) is handed `hitDamageScale` and never applies it.
- `DroneFunc_InjuredTalk` runs only with bots on (`maybeDroneAIEnabled != 0`), which looks inverted; its helper draws
  `Rand_Rand(0)`, always 0.
- `DroneWeap_DoBulletAccuracy` applies `DroneFiring_TooClose_Accuracy` twice.
- `DroneWeap_FireWeapon` converts the sound's pitch, `Rand_Rand(500) - 250`, as unsigned, so a negative draw is about
  4.29e9; its test of the sound against -1 can never pass (a ushort).
- `DroneWeap_ThrowGrenade` uses up a grenade even when a Ninja's own grenade is thrown instead, and builds a bone
  matrix that RotMatrix then overwrites.
- 0x67640 (`DroneWeap_DropWeapon`'s line of sight) passes the argument slot that still holds the drone as the
  hit-list pointer. Ours passes NULL: the ray fills it in either way.
- `NDrone2_DSTATE_Civilian` works out a radius of 10 or 20 for `NDrone2_NearArmedPerson`, which does not read it.
  `NDrone2_DSTATE_CivilianPatrol` attacks for every skin class but changes only class 16's side, where the other
  civilian states go to CivilianScared. Impacts in CivilianPatrol, CivilianMission and CivilianMissionWait send the
  drone to Attack.
- `NDrone2_DSTATE_Patrol`'s timeout plays the look-round anim but sets no new timeout, so it looks round once.
- `NDrone2_PlaySFX` and `NDrone2_SpeechSFX` read their table entry through NULL for a sound number of 0x15a or more.
  `DroneFunc_LostSightTalk` uses `NDrone2_Player()` unchecked; `DroneFunc_HandleSoundAlerts` hands `&sound->location`
  to build_FindCel before its NULL check; `NDrone2_SpeechSFXd` carries on after a failed `Drone_DCVfromOBJ`; every shout
  (messages 0x11, 0x12, 0x13, 0x15, 0x16) finds its sender and does nothing with it.
- `DroneVision_CanSeeObjectFrom` drops the caller's mask for objects that are neither drones nor the player;
  `DroneVision_SeekOpponentLOS` tests a line of sight from the second eye and ignores the answer (it still counts as a
  vision test); `DroneVision_GunThroughWall` hands its own argument slot as the sound pointer and an uninitialised
  hit-list pointer; `DroneVision_FindAlertedDrones`, resuming, counts a row for each pair and restarts the rows.
- `NDrone2_FindOpponent`'s bot branch works out a teammate's angle and fills a DCVars, and uses neither; the bone lookup
  0x37870 reads the position at 0x24 for a NULL object; `NDrone2_DroneAlertToPosition` leaves the last alert's rotation
  and velocity in NPCGlobals.alert; 0x31110 frees every target but leaves its position.
- `SphereSphereSweep`, when the first time is the later, sets both to the second; its quadratic returns both roots as 0
  for a discriminant of exactly 0. `NDrone2_Steer`'s obstacle record never has its flags cleared: they start as stack
  garbage, and once a standing obstacle has been met every later one counts as standing (see Departures).
- `NDrone2_DoAiming` (0x3ab00) compares the aim target with the opponent but aims at targetPos, so it sets the aim
  every frame; `Drone_FeetOnPoint` reads the floor hit's object after `Collide_FreeHitList`; `NDrone2_Collision`'s
  bounds push uses inCel unchecked.
- `DroneSpawner_Create`, `_Init` and `_Delete` never bound the group (31 entries); `_Init` writes an entry for every
  drone in the group's chain whatever the count it sized them by, and clears what `Mem_Malloc` returns without a check;
  0x30860 tests the slots but not the entries before clearing them; the wave speech plays with NULL DCVars.
  `NDrone2_FindAlarmPoint` returns true for the mission path's alarm point even when it cannot be reached.

- `NDrone2_DoModeSettingsOLD` indexes `DroneModeSettings` with the secondary mode unchecked, and for modes 9, 10 and
  0x13 writes `gameObj->animState->currentWeaponId` with neither checked. `NDrone2_DefaultInit` reads `animState`
  unchecked when bots are off; `DroneInit_Collision` dereferences `obj->animState` unchecked when the ray hits.
  `NDrone2_initDTYPE_SniperAlert` fills a DCVars it never uses; `GetDroneTypeAttackTypeFriend`'s sniper case writes
  SniperIdle and then SniperAim over it (unreachable: the table gives DTYPE 2 its own state).
- `NDrone2_SetupGroups` (and its inline AddToGroup) never bounds the group against 31.
- `DroneAnim_SetHTAnim` uses the blend script after `AnimScriptAppendBlend` unchecked; `OnStartAnim` (a mode 6 row)
  and `DAnimFunc_Run` use `animScript` unchecked; `DroneAnim_EventFunc` carries on with uninitialised DCVars when
  `Drone_DCVfromOBJ` fails.
- `Drone_Delete` uses other drones', bullets' and casings' extraObjectData unchecked (only the gun turret's is checked).
- `NDrone2_SeenPlayerTalk` returns early for skin class 0x10, so its own speech line for that class further down is never reached.
- `NDrone2_FindAmbushCover` reads whether the node is a corner or low cover from the entry after the table's last node, not from the node it chose; with neither, it returns false having already changed the drone's node.
- `ThirdCam_Create` never checks that the object was created.
- `Player_Collision` dereferences the remote-controlled device unchecked in substate 12.
- `Player_CollWire`'s branch for wire type 1 is unreachable.
- `Player_SSScanMode` leaves the matrix's translation x and z as stack (Concat of `RotMatrixY`'s unwritten
  translation).
- `Player_DealWithObjHit` always returns 0.0.
- `Player_SetWeaponAnimObj`, putting away a weapon used up (state 14), clears `weaponStats[prevHeldWeaponId].enabled`:
  the variant held, where everything else indexes the base weapon. In state 9 it adds the idle anim without the check
  for 0 every other state makes.
- `Player_RamSave` and `Player_InitAmmoWeapons` cover 33 ammo types of `BLData::ammo`'s 34.

- `Mat_Identity` leaves the translation alone, so `AnimProcessScriptCmds`' fire event aims its bullet along a matrix
  whose translation is stack, and `AnimObjectDraw`'s laser does the same.
- `AnimFrameSet` blends the bones the skin does not use from a buffer it never initialises.
- `AnimObjectDraw`, for a weapon id out of range, takes a stale morph count as the weapon's model.
- `AnimObjectAimAt`'s upper yaw limit tests the pitch.
- Unchecked: the script `AnimFacialAppend` makes; `seq->startTime` in `AnimProcessScriptCmds`; the sync master in
  `AnimScriptTick`; `morphWeights` in `AnimObjectNew`; the allocation in `AnimPostLoadInit`. A datum without a bone
  sends `AnimGetBoneWorldTrans2`'s chain walk off its stack (128 matrices, indexed by the byte).
- `AnimSeqSet` keeps the unpack cache of the sequence it replaces.
- `AnimFrameResolve` deletes every script whose fade direction is 0 once any script has faded, and gives a new script
  the bone count of skeleton 0.

- `Coll_AddHitToList` does not check the hit it gets: with the heap used up it writes through NULL.
- `Intersect_PointGeom` and `Intersect_RayGeom`, ending a search early (test flag 0x10), set the box index to BoxCnt,
  and the inner loop's test then reads `ColBoxs[BoxCnt]`, an entry an earlier search left (or NULL), and may run on
  through that box's triangle range with the current box's vertex block. Both test the material of the best triangle
  so far, the byte before the table before the first; `Intersect_CylTriGeom` tests the byte before the table for
  every triangle.
- `Intersect_CylTriGeom` does not check the allocation when it grows TriHeap.
- `Collide_CylinderIntersect` leaves the test's tester pointing at its own stack object.
- `RB_Update`: a rigid body that carries a light looks for it every frame through a function that returns NULL.

- `Break_Update` breaks with the last hit of the frame, NULL if none: a breakable already below 0 health and hit by
  nothing would read through NULL (not seen: health only falls with a hit). `Break_DoBreak`'s hit is stack but for
  its plane, position and direction: `Break_Kill` reads its material and `Drone_BrokenObject` its object (see
  Departures). `ShatterPolyRecurse` clears what `Mem_Malloc` returns without a check.
- `Effect_Create`, `Effect_Bullet`, `Effect_Player`, `Effect_Laser`, `Effect_Body` and `Debris_Create_Loop` index
  `EffectInfo` (23 entries) with the material's 0x3f unbounded (only `Effect_RicochetProb` holds it to 22).
  `Effect_Laser`'s spark and `Bullet_DoTrails`' satchel digits use `control_create_object`'s result unchecked;
  `Effect_Player` leaves a mark taken from EffectList but never linked when the foot is more than 0.2 above the floor.
- `Bullet_DoTrails` divides by the weapon's swoosh interval unchecked; `Bullet_Delete`, letting go of the grapple
  with no hit (or one that is not a grapple point), returns before deleting a trip-bomb's beam (the same weapon never
  has both). `Bullet_InitCasingLists` uses the 32 objects unchecked.
- `Explode_Propagate` writes the mine's damage through `Coll_AddHitToList`'s result unchecked.
- `Mat_Inverse` (0xd51e0) negates the translation without turning it by the transposed rotation. `View_AddCels` reads `viewer->cel` unchecked. `ClipPortalWithPlane` writes as many clipped points as it makes, past the portal's 12. `vision_generate_display_list` links the visible cels onto whatever `GfxList` already holds.

- `Env_Breath`'s second `Gas_Init` and `Cloud_Create`'s object are not checked; `Env_Tree_Create` checks neither its
  object nor its script; `Lightning_Create`'s segments, `GT_Update`'s barrel, `GunImp_Create`'s gun,
  `Emitter_Create`'s object, `DrawDrops`' hashtable item and `Copter_CreatePath`'s allocation are used unchecked.
  `Copter_FireGun` uses the flash and the player unchecked.
- `GT_Track` compares copters with the difference left from the previous candidate, and its second candidate is a
  stack slot reused from the first. `GT_TakeControl` takes the turret's subState as the player's index.
- `Pickup_CreateFromSet` returns NULL for the ammo once the weapon is made. `PlrStat_LogBondBonus` is called with no
  argument (`Pickup_Handler`).
- `ColourGetValue` reads the key past the last. `Flicker_Update`'s blue has no minimum step (red and green have).
- `Copter_GetBody` (ported earlier) tests the channel with the bounded `SwitchChannel_IsActive`, as the original.

- `maybeControl_Switches_Update` reads a cel's model's LOD before it knows the cel has a model.
  `control_link_object_to_cel` and `build_LinkToRoom` index the cel's two ambient colours by its channel's byte, any
  value. `Control_Plr2Ind` reads a drone's bot data unchecked (NULL for a drone that is not a bot) and uses the index
  unchecked against `MPGame.players`.
- `build_MakeRoomList` fills what `Mem_Malloc` returns without a check; so does `parsemap_block_map_header` (the
  entities and the texture table).
- `parsemap_block_map_data_static` lets an entity number equal to the count through (one past the table, where the
  dynamic placements' test is strict) and uses a model found by hashcode unchecked; both it and
  `parsemap_block_map_data_dynamic` write the placement's keys by key number unbounded (33 in a level_tag).
  `parsemap_block_portal_data` keeps the cels found for the previous portal when the next's are not found (NULL for
  the first). `parsemap_block_LOD` indexes the entities with the file's number unchecked.
- `Lock_Update`'s decryptor reads the bullet's weapon unchecked; locks index `KeyCodes` (51) with the placement's key
  unbounded. `Lock_Unlock`'s silent path does not check the lock.
- `ResetMap_GenLoadScreen` takes two strings from the string heap and uses neither.

- `Font_FindGlyph`'s binary search does not stop at the end of the glyphs: a character above the last glyph but in
  the font's range reads past them. `Font_WordWrapString`'s test for a space before a newline looks at the newline
  itself, so it never backs up. `Font_GetTextExtent` hands `Font_ParseFormat` the text pointer as its flags, so the
  font's texture is set or not by the pointer's low bit. `Font_GetAlignment` does not check its format.
- `Text_TakeMsg` passes over free lines whose type is not 0, and `Text_DelMsg` puts lines back with their type, so a
  line once used is never taken again and the queue allocates 0x20 more lines whenever every free one has been
  used: it grows for as long as messages come.
- `HUD_RadarUpdate` tests a mark's x against the view's width less the mark's with the mark's y instead
  (`// The y again where the x was meant`).
- `LList_InsertAt` from the head into a list with no node that compares 0 or -1 (an empty one included) reads
  through NULL. `hashtable_deleteitem` of a key not in the table copies from address 8. `PickRandomStealth` and
  `PickRandomCombat` divide by the length of a list that may be empty.

- `Component_DestroyAll` reads the next skin set from the set it has just freed. `Menu_FindControl` bounds its list
  of nearest controls (63) but not the wrap-around one (64 entries on the stack); `Menu_CursorOverMe` and
  `Menu_GetControl` keep up to 32 controls under the cursor, unbounded. `Script_Play` at the end of a script with no
  keyframes, the interpolations of a script with none, and `Menu_GetFirstControl` (so `Menu_GetControl`) with no
  current page read through NULL. `Menu_GetAction` indexes `PlayerInputs` with any player below 4, -2 included.
- `Script_ExitLoop` (message 0x14) clears bit 8 of the control's +0x58, which nothing reads: `Script_RunFrame` loops on
  the script's own flag, so the message ends no loop. `Menu_GetPage`'s branch for a Window is dead (its type check
  refuses type 13). `Menu_UpdateTextFmt` computes a throb that nothing reads.

- `MessageType_SetLineSpacing` sets a list's visible rows (and a memo's lines) to the row height over the control's
  height, the inverse of what `List_Update` and `Memo_Update` count, so 0 until the next update. `Label_Init` places
  a centred label by its width before it has set it, and its test for right alignment is dead (the format is a
  constant, centred). `List_Init` takes the bar's height from the heap block before setting it.
- `Memo_Update` hides the lines after the last with sprites, reading the count after `lines[32]` as a sprite when
  all 32 have one, and creates line sprites past the 32; `List_Update` writes cell sprites past its 32 rows, and
  ClearItems clears past them, when more than 32 rows are visible.
- `List_GetIndex_RealItem`, `List_SetIndex_RealItem` and `Radio_SetIndex` with no rows, `Radio_SendMessage`'s
  selection with no items, and the List's Scroll and the Memo's MemoBack with nothing selected read through NULL.
  The text box's `Text_Init` writes through the label's drawn sprite, which `Label_Init` has left NULL.
- The List's tests of a cell's text for NULL test its address, never NULL, so its icon cells are dead. A Scroll's
  proportional bar divides by its count, 0 when empty.

- `P_NFSELECT`, the free-space box answered with A for codename slot 1: passes `(uchar)P_NFDFCTY` (0x23) to
  `Menu_ChangePage` as the manager, where the other slots pass the manager.
- `P_NFSELECT` selects the codename in C_SBCNSELECT's wheel (the codenames page's), not its own C_SBNFCN.
- `P_NFRESULTS` on a failed mission passes medal 4 (platinum) to `Menu_SetLevelBonus`.
- `Menu_UpdateCodenameWheel` indexes `codename_buf` with the wheel's item value unbounded.
- `Menu_SetLevelBonus` (earlier work), for a level not in `sp_level`, marks `sp_level[-1]`: the byte at 0x17c578,
  the space after the time in the build string P_PAUSE shows (0x17c560), which then reads "04:26:28", a 0x01 and
  "<!>". Ours writes the same byte of its own copy of the string (PauseBuildString), so it shows the same.

## Departures from the original

- The AI dispatcher's Exit, Enter and 0x2e messages leave their scope and frames as uninitialised stack in the
  original. Ours writes zero there, as the driving engine's port does for uninitialised stack
  (docs/driving/original-quirks.md, 1.1).
- `NDrone2_Steer` starts the obstacle record's flags at 0, where the original leaves stack (the shadow zeroes the
  stack below the original to match). `DroneVision_GunThroughWall` hands a local for the sound and NULL for the hit
  list where the original passes its argument slot and stack; `NDrone2_SpeechSFXd` zeroes the DCVars a failed lookup
  leaves.
- `Drone_InitComms` hands `DroneFunc_HandleSoundAlerts` a DCVars that is uninitialised stack in the original when the
  first listed drone's object is not a drone; ours is zeroed.
- `Player_CollCreepWall` starts the hit list for its ray at NULL; the original leaves it as stack.
- `Player_WeaponInitBullet` starts its hit list at NULL where the original uses its argument slot (the weapon
  pointer; `Collide_RayIntersect` fills it in either way). `Player_MuzzleFlash`, in multiplayer, starts the flash it
  asks `AnimDatumGetWeaponInfo` for at 0, where the original's slot holds the player pointer.

- `AnimFrameSet` clears the next pose before blending into it, `AnimProcessScriptCmds`' muzzle matrix and
  `AnimObjectDraw`'s gun matrix start zeroed: all uninitialised stack in the original. `AnimObjectDraw` gives an
  out-of-range weapon the morph count 0 where the morph block did not run.

- `Break_DoBreak` zeroes its hit before setting its plane, position and direction; `Explode_Create` starts the
  nearest player's position at zero, which the original leaves as stack when there is no player.
- `View_AddForcedObjects` starts its HITTEST at zero; the original leaves it as stack.

- `DrawDrops`, `GunImp_Create`, `Emitter_Create`, `GT_RandMove`, `GT_AimAt`, `GT_SpinBarrel` and `GT_Update`'s
  controlled state start their local matrices zeroed; `GT_DeployMiniGun`'s disable key and `GT_Track`'s difference
  start at zero; `Pickup_Update` and `GunImp_Update` start their hit lists at NULL. All uninitialised stack in the
  original.

- `Controls_StraddleTest` and `build_LinkDoors2Portals` start their HITTEST at zero, and `parsemap_block_particles`
  its definition (whose two padding bytes the original copies from the stack): uninitialised stack in the original.

- `Font_GetTextCountExtent` hands `Font_ParseFormat` flags of 0, which are uninitialised stack in the original
  (only the measure-only bit matters there: it decides whether the font's texture is set).

- `Sound_UpdateListeners` gives a cut scene's listener zero movement where the original leaves it as stack (no player
  view this frame).

- `MenuManager_Create` zeroes the control description's keepState (0x10) and the bytes at 0x22, which the original
  leaves as stack.
- The handlers' `Process_Create` messages (P_MAIN's label fades and P_NFSELECT's group fade) start
  zeroed; the original fills only the fields it sets and leaves the rest as stack.

## Merged with the bot limit (multiplayer-num-bots)

The multiplayer-num-bots branch (based on dev of 10 October 2026) made the bot count a constant: `NUM_PLAYERS`,
`NUM_BOTS` and `NUM_AGENTS` in game/mp/MPLimits.h, with every original that hard-coded 4, 6 or 10 ported
(docs/architecture/player-weapons-mp.md, "The bot limit"). Six of its ports had been made again in these parcels:
`NDrone2_FindOpponent` 0x37c10 (parcels 3 and 7), `Drone_SM_RouteMsg` 0x4e630 (parcel 3), `Pickup_Update` 0xa84d0
and `GT_Track` 0x99da0 (14a), `HUD_RadarUpdate` 0xb54d0 (15a) and `C_GCPAUSE_Handler` 0x80590 (16c). The merge
(18 October 2026) kept ours, in their files and our layouts, with the constants put in where the original has the
count:

- `Pickup_Update` and `GT_Track`: the original tests a drone's flags, health, type and deletion bit inline (0xa87e4,
  0x99fab), as ours does; the branch's called `MPDrone_MaybeIsDyingOrDead`, a call the original does not make.
  `GT_Track` keeps the x87 distance unrounded against the range (FST then FCOMP, 0x99ec4), as ours does.
- `NDrone2_FindOpponent`, `Drone_SM_RouteMsg`, `HUD_RadarUpdate`, `C_GCPAUSE_Handler`: the two agree in behaviour;
  ours kept, the branch's comments taken where they say more (a bot reads another bot's view of it at
  `players[botIndex]`, not at its own player index: kept, as the original).

Structures both sides had invented were made one: `BOT_playerInfo_t` (our names; the branch's `BOT_weapon_t` taken),
`MPGame.radarMarks` (the branch's `radar_related`), `RADAROBJ` (moved to multiplayer.h, the branch's
`MP_RADAR_OBJECT`), `PICKUPINFO`, `GUNTURRET`, `Drone_tag`, `NPCGlobals_t` and BLData (our names). From the branch:
`MPGamePlayer.flags` (`MPGamePlayerFlags`, our `unknown26`), `BOT_vars_t.targeted` (our `beingGuarded`: the branch
has its writer, `BOT_setOtherPlayerInfo`), the bot personality and trait enums, and `PlrMissionStats` sized by
`NUM_AGENTS`.

The branch's live-play shadows (settings keys `<Name>Shadow=on`: BotCore, BotState, DroneMP, MPMatch, MPObjects,
MPFlag, MPGoldenEye, MPObjectives) were written when the callers of `NDrone2_FindOpponent`, `GT_Track` and
`HUD_Update` were originals; ours now call them by name, so DroneMPShadow and MPObjectsShadow also put a jump on our
own entries (taken off while the shadow runs ours), and MPObjectsShadow catches our `_Pickup_Handler` as it does the
original's. Checked with the merge: the full MenuShadowTests run, MPMenusShadow included, 0 differ; the nine time
demos match; MenuProbe A/B over mp_setup, mp_start, mp_confirm, multiplayer and multiplayer_bots with the 37 merged
functions as the originals, identical screenshots and messages; Quick Game matches in each of the 12 modes (mp_start
with `poke 0x0026003c <GameMode>`) with every MP shadow on, 0 differ (planted bugs in `NDrone2_FindOpponent`,
`Pickup_Update` and `HUD_RadarUpdate` caught). `GT_Track` is not reached: the Quick Game map has no turrets.

## What the demos do not cover

The recorded set (tests/demos/action) is single player, started with `-level`. It does not reach multiplayer
(`MP_*`, the bots `BOT_*`/`BOTSTATE_*`/`BOTWEAP_*`/`DSTATE_Bot*`, split screen and its rendering), the front end
and its menus, or saving and loading. Parcels in those areas need their own recordings first. The recorder takes
all four controller ports, so a split-screen or bot match can be recorded. A launch option to boot straight into
a multiplayer match, like `-level`, would make that practical. Until then, those parcels come last, with shadow
tests and MenuProbe replays as their check.
