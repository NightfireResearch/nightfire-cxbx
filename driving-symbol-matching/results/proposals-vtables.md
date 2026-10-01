# Proposals from vtables (315 pairs incl. probable)

Outcomes: confirmed 823, stub 10, conflict 9, propose 7

Skipped (two PS2 vtables claiming one Xbox vtable, or a sheet name cut short):

- BlendedAnimationController virtual table: Xbox vtable 0x00189e14 is also paired with CrossFadeAnimationController virtual table
- CrossFadeAnimationController virtual table: Xbox vtable 0x00189e14 is also paired with BlendedAnimationController virtual table
- ESetJumpGravity virtual table: Xbox vtable 0x0018c3ac is also paired with ESetCullDistanceFactor virtual table
- ESetCullDistanceFactor virtual table: Xbox vtable 0x0018c3ac is also paired with ESetJumpGravity virtual table
- ERestart virtual table: Xbox vtable 0x0018c6b0 is also paired with EPlayerLose virtual table
- EPlayerLose virtual table: Xbox vtable 0x0018c6b0 is also paired with ERestart virtual table
- PBondCar virtual table: Xbox vtable 0x0018f580 is also paired with PTank virtual table
- PTank virtual table: Xbox vtable 0x0018f580 is also paired with PBondCar virtual table

## conflict (9)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00045a70 | ESpawnSmackable::Process | ESpawnSmackable::~ESpawnSmackable |  | ghidra | called by ESpawnSmackable::scalar_deleting_destructor (0x00048ce0) and stores ESpawnSmackable's vtable |
| 000474d0 | EAddObjective::~EAddObjective | EAddObjective::scalar_deleting_destructor |  | ghidra | EAddObjective slot 0 (PS2 00147af8, row 1210, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047990 | EDisplayObjectiveText::~EDisplayObjectiveText | EDisplayObjectiveText::scalar_deleting_destructor |  | ghidra | EDisplayObjectiveText slot 0 (PS2 0014b868, row 1315, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047ed0 | EInsertObjective::~EInsertObjective | EInsertObjective::scalar_deleting_destructor | EStartTimedAction::~EStartTimedAction | ghidra | EStartTimedAction slot 0 (PS2 0015c9d8, row 1777, ghidra); EInsertObjective slot 0 (PS2 0014ffe8, row 1396, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048680 | EBeginDownloadCode::~EBeginDownloadCode | EChangeMaxTraffic::scalar_deleting_destructor | EChangeStage::~EChangeStage, EStopTimer::~EStopTimer | ghidra | EStopTimer slot 0 (PS2 0015cf60, row 1792, ghidra); EChangeStage slot 0 (PS2 00149870, row 1276, ghidra); EChangeMaxTraffic slot 0 (PS2 001497b8, row 1273, ghidra) ... |
| 00048700 | EForceNearPedRespawn::~EForceNearPedRespawn | EForceNearPedRespawn::scalar_deleting_destructor | EStartTimer::~EStartTimer | ghidra | EStartTimer slot 0 (PS2 0015cae0, row 1780, ghidra); EForceNearPedRespawn slot 0 (PS2 0014ef60, row 1366, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048800 | ESapphirePole::~ESapphirePole | ESapphirePole::scalar_deleting_destructor |  | ghidra | ESapphirePole slot 0 (PS2 00156cb0, row 1615, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ce0 | ESpawnSmackable::~ESpawnSmackable | ESpawnSmackable::scalar_deleting_destructor |  | ghidra | ESpawnSmackable slot 0 (PS2 0015bbf8, row 1762, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f8250 | EAGLAnim::FnDeltaQuatChan::scalar_deleting_destructor | EAGLAnim::FnDeltaLerpChan::scalar_deleting_destructor | EAGLAnim::FnDeltaQuatChan::~FnDeltaQuatChan | resolved | EAGLAnim::FnDeltaQuatChan slot 0 (PS2 002758d0, row 6771, ghidra); EAGLAnim::FnDeltaLerpChan slot 0 (PS2 00275670, row 6758, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |

## propose (7)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 0001c110 | FUN_0001c110 | ACharacter::scalar_deleting_destructor |  | sheet | ACharacter slot 0 (PS2 002fc268, row 9094, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a1ed0 | FUN_000a1ed0 | RMovableParticleSystem::scalar_deleting_destructor |  | sheet | RMovableParticleSystem slot 0 (PS2 001e61a0, row 4113, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000b9e90 | FUN_000b9e90 | SRuleRange::scalar_deleting_destructor |  | ghidra | SRuleRange slot 0 (PS2 0020a598, row 4705, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000b9f40 | FUN_000b9f40 | SRuleProgCounter::scalar_deleting_destructor |  | sheet | SRuleProgCounter slot 0 (PS2 0020af20, row 4724, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000b9f60 | FUN_000b9f60 | SRuleProg::scalar_deleting_destructor |  | sheet | SRuleProg slot 0 (PS2 0020a3c8, row 4699, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011dd00 | FUN_0011dd00 | ACharacter::GetName |  | ghidra | ACharacter slot 2 (PS2 002fc318, row 9095, ghidra) |
| 0011dd30 | FUN_0011dd30 | ACharacter::~ACharacter |  | sheet | called by ACharacter::scalar_deleting_destructor (0x0001c110) and stores ACharacter's vtable |

## stub (10)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00015770 | FUN_00015770 | AICharacter::PlayZoneInjuryAnim | RSceneObj::TriggerIlluminate, EAGLAnim::FnAnim::Eval, ASceneObj::PlayLanding | ghidra | AICharacterBond slot 6 (PS2 00119138, row 377, ghidra); AICharacterEnemyDriver slot 6 (PS2 00119138, row 377, ghidra); AICharacterHands slot 6 (PS2 00119138, row 377, ghidra) ... |
| 00017550 | dummyNullFunction | PhysicsObject::SetInShock | RSceneObj::SetViewDrawList, RSceneObj::TransformToWorldSpace, RSceneObj::TransformPointToWorldSpace, RSceneObj::TransformMatrixToWorldSpace +4 | ghidra | Grenade slot 1 (PS2 00195938, row 2710, ghidra); Human slot 1 (PS2 00195938, row 2710, ghidra); Mine slot 1 (PS2 00195938, row 2710, ghidra) ... |
| 0001c080 | Generic_FuncReturnsFalse | AICharacter::IsTooFarAway | AUltraLite::IsTracked, ASceneObj::IsStillActive, ASceneObj::IsTracked, ASceneObj::IsAirborne +3 | resolved | AICharacterBond slot 31 (PS2 00119850, row 444, ghidra); AICharacterEnemyDriver slot 31 (PS2 00119850, row 444, ghidra); AICharacterEnemyGround slot 31 (PS2 00119850, row 444, ghidra) ... |
| 0001d0c0 | AICharacterBond::DoInitial | AICharacterBond::DoInitial | AICharacterBond::DoIdling, AICharacterEnemyDriver::DoIdling, AICharacterEnemyGround::DoIdling, AICharacterEnemyGround::DoUnarming +9 | ghidra | AICharacterBond slot 7 (PS2 0011a2e0, row 456, ghidra); AICharacterBond slot 9 (PS2 0011a360, row 458, ghidra); AICharacterEnemyDriver slot 9 (PS2 0011afc0, row 473, ghidra) ... |
| 00097a90 | PhysicsObject::ComputeImpulse | PhysicsObject::ComputeImpulse | RWorldCamera::CameraInputCallback | resolved | Grenade slot 6 (PS2 00195970, row 2713, ghidra); Human slot 6 (PS2 00195970, row 2713, ghidra); Mine slot 6 (PS2 00195970, row 2713, ghidra) ... |
| 000b98f0 | StubError | AUltraLite::IsAirborne | ASnowMobile::IsTracked, APlayerTank::IsTracked, AMenuSoundPriv::IsOneShot | ghidra | AUltraLite slot 8 (PS2 002e48e8, row 8747, ghidra); ASnowMobile slot 7 (PS2 002ef6a8, row 8874, ghidra); APlayerTank slot 7 (PS2 002f13e0, row 8924, ghidra) ... |
| 000d3580 | dummyNullFunction | AICharacter::DoWalking | AICharacter::DoWandering, AICharacter::DoAvoiding, AICharacter::DoStartled, AICharacter::DoDodging +49 | resolved | AICharacterBond slot 10 (PS2 001197a8, row 423, ghidra); AICharacterBond slot 11 (PS2 001197b0, row 424, ghidra); AICharacterBond slot 12 (PS2 001197b8, row 425, ghidra) ... |
| 000f70e0 | FUN_000f70e0 | EAGLAnim::FnAnim::FindMatchTime | EAGLAnim::FnAnim::EvalPhase, EAGLAnim::FnAnim::EvalVel2D, EAGLAnim::FnAnim::EvalWeights, EAGLAnim::FnAnim::EvalState | ghidra | EAGLAnim::FnPoseMirror slot 5 (PS2 0026dfd8, row 6588, ghidra); EAGLAnim::FnPoseMirror slot 7 (PS2 0026dfe8, row 6590, ghidra); EAGLAnim::FnPoseMirror slot 8 (PS2 0026dff0, row 6591, ghidra) ... |
| 000f7310 | FUN_000f7310 | EAGLAnim::FnAnim::EvalSQT | EAGLAnim::FnAnim::FindTime | ghidra | EAGLAnim::FnPoseMirror slot 12 (PS2 0026e010, row 6595, ghidra); EAGLAnim::FnRawLinearChannel slot 6 (PS2 0026dfe0, row 6589, ghidra); EAGLAnim::FnRawLinearChannel slot 12 (PS2 0026e010, row 6595, ghidra) ... |
| 000f7330 | dummyGetNullValue | AICharacter::GetVehiclePtr | EAGLAnim::FnAnim::GetPhaseChan | ghidra | AICharacterBond slot 3 (PS2 001196c8, row 404, ghidra); AICharacterEnemyDriver slot 3 (PS2 001196c8, row 404, ghidra); AICharacterEnemyGround slot 3 (PS2 001196c8, row 404, ghidra) ... |

## confirmed (823)

| Xbox | now | proposed | also (folded) | weakest PS2 source | evidence |
|---|---|---|---|---|---|
| 00011100 | StandardAnimationController::Update | StandardAnimationController::Update |  | ghidra | StandardAnimationController slot 0 (PS2 00107490, row 20, ghidra) |
| 000125f0 | StandardAnimationController::scalar_deleting_destructor | StandardAnimationController::scalar_deleting_destructor |  | ghidra | StandardAnimationController slot 1 (PS2 0010bba8, row 107, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001c090 | ABaseSound::scalar_deleting_destructor | ABaseSound::scalar_deleting_destructor |  | ghidra | ABaseSound slot 0 (PS2 002fcb28, row 9108, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001c200 | AICharacter::IsAlive | AICharacter::IsAlive |  | ghidra | AICharacterBond slot 1 (PS2 00119638, row 392, ghidra); AICharacterEnemyDriver slot 1 (PS2 00119638, row 392, ghidra); AICharacterEnemySnow slot 1 (PS2 00119638, row 392, ghidra) ... |
| 0001c220 | AICharacter::SetActor | AICharacter::SetActor |  | infill-exact | AICharacterBond slot 2 (PS2 00119698, row 401, infill-exact); AICharacterEnemyDriver slot 2 (PS2 00119698, row 401, infill-exact); AICharacterEnemyGround slot 2 (PS2 00119698, row 401, infill-exact) ... |
| 0001c5b0 | AICharacter::GetZoneHitPointScale | AICharacter::GetZoneHitPointScale |  | ghidra | AICharacterBond slot 5 (PS2 001190f8, row 376, ghidra); AICharacterEnemyDriver slot 5 (PS2 001190f8, row 376, ghidra); AICharacterEnemySnow slot 5 (PS2 001190f8, row 376, ghidra) ... |
| 0001c5e0 | NeedsToReload | AICharacterEnemy::NeedsToReload |  | ghidra | AICharacterEnemyGround slot 34 (PS2 00119270, row 380, ghidra); AICharacterEnemySnow slot 34 (PS2 00119270, row 380, ghidra); AICharacterEnemySSnow slot 34 (PS2 00119270, row 380, ghidra) ... |
| 0001c690 | AICharacter::NotifyZoneDamage | AICharacter::NotifyZoneDamage |  | ghidra | AICharacterBond slot 4 (PS2 00118eb0, row 374, ghidra); AICharacterEnemyDriver slot 4 (PS2 00118eb0, row 374, ghidra); AICharacterEnemyGround slot 4 (PS2 00118eb0, row 374, ghidra) ... |
| 0001ca50 | TargetIsInRange | AICharacterEnemy::TargetIsInRange |  | ghidra | AICharacterEnemyGround slot 33 (PS2 001191e8, row 379, ghidra); AICharacterHands slot 33 (PS2 001191e8, row 379, ghidra) |
| 0001ca90 | TargetIsAcquired | AICharacterEnemy::TargetIsAcquired |  | resolved | AICharacterEnemySSnow slot 35 (PS2 00119290, row 381, resolved); AICharacterHands slot 35 (PS2 00119290, row 381, resolved) |
| 0001cbf0 | AICharacterBond::~AICharacterBond | AICharacterBond::~AICharacterBond |  | ghidra | called by AICharacterBond::scalar_deleting_destructor (0x0001cc00) and stores AICharacterBond's vtable |
| 0001cc00 | AICharacterBond::scalar_deleting_destructor | AICharacterBond::scalar_deleting_destructor |  | ghidra | AICharacterBond slot 0 (PS2 0011a288, row 455, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001d090 | AICharacterBond::DoNeutral | AICharacterBond::DoNeutral |  | ghidra | AICharacterBond slot 8 (PS2 0011a318, row 457, ghidra) |
| 0001d0e0 | AICharacterBond::DoArming | AICharacterBond::DoArming |  | ghidra | AICharacterBond slot 15 (PS2 0011a398, row 459, ghidra) |
| 0001d1c0 | AICharacterBond::DoFiring | AICharacterBond::DoFiring |  | ghidra | AICharacterBond slot 18 (PS2 0011a4a0, row 460, ghidra) |
| 0001d2a0 | AICharacterBond::HandleInterrupts | AICharacterBond::HandleInterrupts |  | ghidra | AICharacterBond slot 30 (PS2 0011a5b8, row 461, ghidra) |
| 0001d640 | AICharacterEnemyDriver::~AICharacterEnemyDriver | AICharacterEnemyDriver::~AICharacterEnemyDriver |  | sheet | called by AICharacterEnemyDriver::scalar_deleting_destructor (0x0001d650) and stores AICharacterEnemyDriver's vtable |
| 0001d650 | AICharacterEnemyDriver::scalar_deleting_destructor | AICharacterEnemyDriver::scalar_deleting_destructor |  | sheet | AICharacterEnemyDriver slot 0 (PS2 0011ad88, row 470, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001d680 | AICharacterEnemyDriver::DoInitial | AICharacterEnemyDriver::DoInitial |  | ghidra | AICharacterEnemyDriver slot 7 (PS2 0011ade0, row 471, ghidra) |
| 0001d7f0 | DoNeutral | AICharacterEnemyDriver::DoNeutral |  | ghidra | AICharacterEnemyDriver slot 8 (PS2 0011af60, row 472, ghidra) |
| 0001d840 | AICharacterEnemyDriver::HandleInterrupts | AICharacterEnemyDriver::HandleInterrupts |  | ghidra | AICharacterEnemyDriver slot 30 (PS2 0011aff8, row 474, ghidra) |
| 0001d990 | UpdateRotPos | AICharacterEnemyDriver::UpdateRotPos | AICharacterEnemySSnow::UpdateRotPos, AICharacterEnemyWindow::UpdateRotPos | resolved | AICharacterEnemyDriver slot 32 (PS2 0011b128, row 475, resolved); AICharacterEnemySSnow slot 32 (PS2 00120428, row 553, ghidra); AICharacterEnemyWindow slot 32 (PS2 00122dd0, row 604, ghidra) |
| 0001da60 | DoDodging | AICharacterEnemyGround::DoDodging |  | ghidra | AICharacterEnemyGround slot 14 (PS2 0011bb78, row 486, ghidra) |
| 0001da70 | DoTilting | AICharacterEnemyGround::DoTilting |  | ghidra | AICharacterEnemyGround slot 23 (PS2 0011c2a0, row 495, ghidra) |
| 0001dc50 | GetZoneHitPointScale | AICharacterEnemyGround::GetZoneHitPointScale |  | ghidra | AICharacterEnemyGround slot 5 (PS2 0011d9b0, row 504, ghidra) |
| 0001de80 | IsAlive | AICharacterEnemyGround::IsAlive |  | sheet | AICharacterEnemyGround slot 1 (PS2 0011e150, row 511, sheet) |
| 0001dea0 | ~AICharacterEnemyGround | AICharacterEnemyGround::~AICharacterEnemyGround |  | sheet | called by AICharacterEnemyGround::scalar_deleting_destructor (0x0001df40) and stores AICharacterEnemyGround's vtable |
| 0001df40 | scalar_deleting_destructor | AICharacterEnemyGround::scalar_deleting_destructor |  | sheet | AICharacterEnemyGround slot 0 (PS2 0011b768, row 480, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001df70 | DoInitial | AICharacterEnemyGround::DoInitial |  | ghidra | AICharacterEnemyGround slot 7 (PS2 0011b800, row 481, ghidra) |
| 0001dfc0 | DoNeutral | AICharacterEnemyGround::DoNeutral |  | ghidra | AICharacterEnemyGround slot 8 (PS2 0011b860, row 482, ghidra) |
| 0001e010 | DoAvoiding | AICharacterEnemyGround::DoAvoiding |  | ghidra | AICharacterEnemyGround slot 12 (PS2 0011bad8, row 485, ghidra) |
| 0001e070 | DoArming | AICharacterEnemyGround::DoArming | AICharacterEnemyGround::DoReloading, AICharacterEnemyGround::DoHurting, AICharacterEnemySnow::DoArming, AICharacterEnemySnow::DoFiring +1 | ghidra | AICharacterEnemyGround slot 15 (PS2 0011bbb8, row 487, ghidra); AICharacterEnemyGround slot 19 (PS2 0011c160, row 491, ghidra); AICharacterEnemyGround slot 21 (PS2 0011c1d0, row 493, ghidra) ... |
| 0001e090 | DoArmed | AICharacterEnemyGround::DoArmed |  | ghidra | AICharacterEnemyGround slot 16 (PS2 0011bbf0, row 488, ghidra) |
| 0001e380 | DoDying | AICharacterEnemyGround::DoDying |  | ghidra | AICharacterEnemyGround slot 22 (PS2 0011c208, row 494, ghidra) |
| 0001e400 | UpdateRotPos | AICharacterEnemyGround::UpdateRotPos |  | ghidra | AICharacterEnemyGround slot 32 (PS2 0011cd10, row 499, ghidra) |
| 0001ea60 | AICharacterEnemyGround::PlayZoneInjuryAnim | AICharacterEnemyGround::PlayZoneInjuryAnim |  | resolved | AICharacterEnemyGround slot 6 (PS2 0011d9f0, row 505, resolved) |
| 0001ee60 | AICharacterEnemyGround::DoWandering | AICharacterEnemyGround::DoWandering |  | ghidra | AICharacterEnemyGround slot 11 (PS2 0011b908, row 484, ghidra) |
| 0001f000 | AICharacterEnemyGround::DoAiming | AICharacterEnemyGround::DoAiming |  | ghidra | AICharacterEnemyGround slot 17 (PS2 0011bf80, row 489, ghidra) |
| 0001f0c0 | AICharacterEnemyGround::DoFiring | AICharacterEnemyGround::DoFiring |  | ghidra | AICharacterEnemyGround slot 18 (PS2 0011c070, row 490, ghidra) |
| 0001f180 | DoRunning | AICharacterEnemyGround::DoRunning |  | ghidra | AICharacterEnemyGround slot 27 (PS2 0011c2c0, row 496, ghidra) |
| 0001f3c0 | AICharacterEnemyGround::HandleInterrupts | AICharacterEnemyGround::HandleInterrupts |  | ghidra | AICharacterEnemyGround slot 30 (PS2 0011c640, row 498, ghidra) |
| 0001f8f0 | TargetIsAcquired | AICharacterEnemyGround::TargetIsAcquired |  | ghidra | AICharacterEnemyGround slot 35 (PS2 0011cd68, row 500, ghidra) |
| 0001fc70 | ~AICharacterEnemySnow | AICharacterEnemySnow::~AICharacterEnemySnow |  | sheet | called by AICharacterEnemySnow::scalar_deleting_destructor (0x0001fdd0) and stores AICharacterEnemySnow's vtable |
| 0001fcd0 | DoTilting | AICharacterEnemySnow::DoTilting |  | ghidra | AICharacterEnemySnow slot 23 (PS2 0011f268, row 529, ghidra) |
| 0001fcf0 | AICharacterEnemySnow::HandleInterrupts | AICharacterEnemySnow::HandleInterrupts |  | ghidra | AICharacterEnemySnow slot 30 (PS2 0011f468, row 533, ghidra) |
| 0001fd30 | GetVehiclePtr | AICharacterEnemySnow::GetVehiclePtr | AICharacterEnemySSnow::GetVehiclePtr, AICharacterEnemyWindow::GetVehiclePtr | ghidra | AICharacterEnemySnow slot 3 (PS2 0011f520, row 535, ghidra); AICharacterEnemySSnow slot 3 (PS2 00120470, row 554, ghidra); AICharacterEnemyWindow slot 3 (PS2 00122e18, row 605, ghidra) |
| 0001fdd0 | scalar_deleting_destructor | AICharacterEnemySnow::scalar_deleting_destructor |  | sheet | AICharacterEnemySnow slot 0 (PS2 0011e5b8, row 516, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0001fe00 | DoNeutral | AICharacterEnemySnow::DoNeutral |  | ghidra | AICharacterEnemySnow slot 8 (PS2 0011e770, row 520, ghidra) |
| 0001ff90 | AICharacterEnemySnow::DoArmed | AICharacterEnemySnow::DoArmed |  | ghidra | AICharacterEnemySnow slot 16 (PS2 0011e9a0, row 523, ghidra) |
| 00020090 | AICharacterEnemySnow::DoAiming | AICharacterEnemySnow::DoAiming |  | ghidra | AICharacterEnemySnow slot 17 (PS2 0011ead0, row 524, ghidra) |
| 000203c0 | DoDying | AICharacterEnemySnow::DoDying |  | ghidra | AICharacterEnemySnow slot 22 (PS2 0011efd8, row 528, ghidra) |
| 000205b0 | DoRunning | AICharacterEnemySnow::DoRunning |  | ghidra | AICharacterEnemySnow slot 27 (PS2 0011f2c8, row 530, ghidra) |
| 000205e0 | DoLeaning | AICharacterEnemySnow::DoLeaning |  | ghidra | AICharacterEnemySnow slot 28 (PS2 0011f308, row 531, ghidra) |
| 000206d0 | UpdateRotPos | AICharacterEnemySnow::UpdateRotPos |  | ghidra | AICharacterEnemySnow slot 32 (PS2 0011f4d8, row 534, ghidra) |
| 00020710 | TargetIsInRange | AICharacterEnemySnow::TargetIsInRange |  | resolved | AICharacterEnemySnow slot 33 (PS2 0011f528, row 536, resolved) |
| 00020870 | AICharacterEnemySnow::PlayZoneInjuryAnim | AICharacterEnemySnow::PlayZoneInjuryAnim |  | resolved | AICharacterEnemySnow slot 6 (PS2 0011f9a8, row 538, resolved) |
| 00020970 | ~AICharacterEnemySSnow | AICharacterEnemySSnow::~AICharacterEnemySSnow |  | sheet | called by AICharacterEnemySSnow::scalar_deleting_destructor (0x000209d0) and stores AICharacterEnemySSnow's vtable |
| 000209d0 | scalar_deleting_destructor | AICharacterEnemySSnow::scalar_deleting_destructor |  | sheet | AICharacterEnemySSnow slot 0 (PS2 00120010, row 544, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00020a00 | AICharacterEnemySSnow::DoNeutral | AICharacterEnemySSnow::DoNeutral |  | ghidra | AICharacterEnemySSnow slot 8 (PS2 001200a0, row 546, ghidra) |
| 00020a60 | DoAiming | AICharacterEnemySSnow::DoAiming |  | ghidra | AICharacterEnemySSnow slot 17 (PS2 00120160, row 548, ghidra) |
| 00020b40 | DoDying | AICharacterEnemySSnow::DoDying |  | ghidra | AICharacterEnemySSnow slot 22 (PS2 001202e0, row 550, ghidra) |
| 00020bb0 | HandleInterrupts | AICharacterEnemySSnow::HandleInterrupts |  | ghidra | AICharacterEnemySSnow slot 30 (PS2 00120370, row 552, ghidra) |
| 00020c30 | TargetIsInRange | AICharacterEnemySSnow::TargetIsInRange |  | resolved | AICharacterEnemySSnow slot 33 (PS2 00120478, row 555, resolved) |
| 00020c70 | AICharacterEnemySSnow::PlayZoneInjuryAnim | AICharacterEnemySSnow::PlayZoneInjuryAnim |  | resolved | AICharacterEnemySSnow slot 6 (PS2 00120500, row 556, resolved) |
| 00021600 | DoFiring | AICharacterEnemyWindow::DoFiring |  | ghidra | AICharacterEnemyWindow slot 18 (PS2 00122be0, row 598, ghidra) |
| 00021d40 | ~AICharacterEnemyWindow | AICharacterEnemyWindow::~AICharacterEnemyWindow |  | sheet | called by AICharacterEnemyWindow::scalar_deleting_destructor (0x00021dc0) and stores AICharacterEnemyWindow's vtable |
| 00021dc0 | scalar_deleting_destructor | AICharacterEnemyWindow::scalar_deleting_destructor |  | sheet | AICharacterEnemyWindow slot 0 (PS2 001225a8, row 590, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00021df0 | SetActor | AICharacterEnemySnow::SetActor | AICharacterEnemyWindow::SetActor | ghidra | AICharacterEnemySnow slot 2 (PS2 0011e610, row 517, ghidra); AICharacterEnemyWindow slot 2 (PS2 00122600, row 591, ghidra) |
| 00021e70 | AICharacterEnemyWindow::DoInitial | AICharacterEnemyWindow::DoInitial |  | ghidra | AICharacterEnemyWindow slot 7 (PS2 00122698, row 592, ghidra) |
| 00021f10 | AICharacterEnemyWindow::DoNeutral | AICharacterEnemyWindow::DoNeutral |  | ghidra | AICharacterEnemyWindow slot 8 (PS2 00122780, row 593, ghidra) |
| 00021f70 | AICharacterEnemyWindow::DoIdling | AICharacterEnemyWindow::DoIdling |  | ghidra | AICharacterEnemyWindow slot 9 (PS2 00122818, row 594, ghidra) |
| 00022010 | DoArming | AICharacterEnemyWindow::DoArming |  | ghidra | AICharacterEnemyWindow slot 15 (PS2 001228d0, row 595, ghidra) |
| 00022050 | AICharacterEnemyWindow::DoArmed | AICharacterEnemyWindow::DoArmed |  | ghidra | AICharacterEnemyWindow slot 16 (PS2 00122920, row 596, ghidra) |
| 00022150 | DoAiming | AICharacterEnemyWindow::DoAiming |  | ghidra | AICharacterEnemyWindow slot 17 (PS2 00122a68, row 597, ghidra) |
| 00022280 | DoUnarming | AICharacterEnemyWindow::DoUnarming |  | ghidra | AICharacterEnemyWindow slot 20 (PS2 00122c30, row 599, ghidra) |
| 000222c0 | DoDying | AICharacterEnemyWindow::DoDying |  | ghidra | AICharacterEnemyWindow slot 22 (PS2 00122cc0, row 601, ghidra) |
| 00022310 | HandleInterrupts | AICharacterEnemyWindow::HandleInterrupts |  | ghidra | AICharacterEnemyWindow slot 30 (PS2 00122d28, row 603, ghidra) |
| 000223a0 | TargetIsInRange | AICharacterEnemyWindow::TargetIsInRange |  | resolved | AICharacterEnemyWindow slot 33 (PS2 00122e20, row 606, resolved) |
| 000224f0 | AICharacterEnemySnow::TargetIsAcquired | AICharacterEnemySnow::TargetIsAcquired | AICharacterEnemyWindow::TargetIsAcquired | resolved | AICharacterEnemySnow slot 35 (PS2 0011f768, row 537, resolved); AICharacterEnemyWindow slot 35 (PS2 00123058, row 607, resolved) |
| 00022680 | AICharacterEnemyWindow::PlayZoneInjuryAnim | AICharacterEnemyWindow::PlayZoneInjuryAnim |  | resolved | AICharacterEnemyWindow slot 6 (PS2 00123298, row 608, resolved) |
| 00022820 | ~AICharacterHands | AICharacterHands::~AICharacterHands |  | sheet | called by AICharacterHands::scalar_deleting_destructor (0x000228b0) and stores AICharacterHands's vtable |
| 000228b0 | scalar_deleting_destructor | AICharacterHands::scalar_deleting_destructor |  | sheet | AICharacterHands slot 0 (PS2 00123ea0, row 618, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00022e70 | DoNeutral | AICharacterHands::DoNeutral |  | ghidra | AICharacterHands slot 8 (PS2 00124240, row 627, ghidra) |
| 00022f10 | AICharacterHands::DoIdling | AICharacterHands::DoIdling |  | ghidra | AICharacterHands slot 9 (PS2 00124348, row 628, ghidra) |
| 00022f70 | AICharacterHands::DoArming | AICharacterHands::DoArming |  | ghidra | AICharacterHands slot 15 (PS2 001243f0, row 629, ghidra) |
| 00022fb0 | DoArmed | AICharacterHands::DoArmed |  | ghidra | AICharacterHands slot 16 (PS2 00124440, row 630, ghidra) |
| 000230a0 | DoAiming | AICharacterHands::DoAiming |  | ghidra | AICharacterHands slot 17 (PS2 00124570, row 631, ghidra) |
| 00023170 | DoFiring | AICharacterHands::DoFiring |  | ghidra | AICharacterHands slot 18 (PS2 00124650, row 632, ghidra) |
| 00023230 | DoReloading | AICharacterHands::DoReloading |  | ghidra | AICharacterHands slot 19 (PS2 00124770, row 633, ghidra) |
| 00023280 | AICharacterHands::DoUnarming | AICharacterHands::DoUnarming |  | ghidra | AICharacterHands slot 20 (PS2 00124800, row 634, ghidra) |
| 000232b0 | UpdateRotPos | AICharacterHands::UpdateRotPos |  | resolved | AICharacterHands slot 32 (PS2 00124848, row 635, resolved) |
| 000236c0 | AICharacterPassenger::~AICharacterPassenger | AICharacterPassenger::~AICharacterPassenger |  | sheet | called by AICharacterPassenger::scalar_deleting_destructor (0x000236d0) and stores AICharacterPassenger's vtable |
| 000236d0 | AICharacterPassenger::scalar_deleting_destructor | AICharacterPassenger::scalar_deleting_destructor |  | sheet | AICharacterPassenger slot 0 (PS2 00125738, row 647, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00023890 | DoNeutral | AICharacterPassenger::DoNeutral |  | ghidra | AICharacterPassenger slot 8 (PS2 001257c8, row 649, ghidra) |
| 000238c0 | HandleInterrupts | AICharacterPassenger::HandleInterrupts |  | ghidra | AICharacterPassenger slot 30 (PS2 00125860, row 654, ghidra) |
| 00023950 | AICharacterBond::UpdateRotPos | AICharacterBond::UpdateRotPos | AICharacterPassenger::UpdateRotPos | resolved | AICharacterBond slot 32 (PS2 0011a858, row 462, resolved); AICharacterPassenger slot 32 (PS2 00125920, row 655, resolved) |
| 00023b70 | AICharacterPedestrian::DoDead | AICharacterPedestrian::DoDead |  | ghidra | AICharacterPedestrian slot 29 (PS2 00128488, row 687, ghidra) |
| 00023f50 | AICharacterPedestrian::~AICharacterPedestrian | AICharacterPedestrian::~AICharacterPedestrian |  | ghidra | called by AICharacterPedestrian::scalar_deleting_destructor (0x00024700) and stores AICharacterPedestrian's vtable |
| 00024700 | AICharacterPedestrian::scalar_deleting_destructor | AICharacterPedestrian::scalar_deleting_destructor |  | ghidra | AICharacterPedestrian slot 0 (PS2 00125b48, row 659, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00024870 | UpdateRotPos | AICharacterPedestrian::UpdateRotPos |  | ghidra | AICharacterPedestrian slot 32 (PS2 00128788, row 689, ghidra) |
| 000248b0 | AICharacterPedestrian::SetActor | AICharacterPedestrian::SetActor |  | ghidra | AICharacterPedestrian slot 2 (PS2 001287f8, row 690, ghidra) |
| 00024910 | AICharacterPedestrian::DoAiming | AICharacterPedestrian::DoAiming |  | ghidra | AICharacterPedestrian slot 17 (PS2 00128860, row 691, ghidra) |
| 00024b30 | TargetIsInRange | AICharacterPedestrian::TargetIsInRange |  | ghidra | AICharacterPedestrian slot 33 (PS2 00128b28, row 692, ghidra) |
| 00025ef0 | AICharacterPedestrian::DoNeutral | AICharacterPedestrian::DoNeutral |  | ghidra | AICharacterPedestrian slot 8 (PS2 00127708, row 677, ghidra) |
| 00025f40 | AICharacterPedestrian::DoIdling | AICharacterPedestrian::DoIdling |  | ghidra | AICharacterPedestrian slot 9 (PS2 00127798, row 678, ghidra) |
| 00025fc0 | DoDying | AICharacterPedestrian::DoDying |  | ghidra | AICharacterPedestrian slot 22 (PS2 001280c8, row 685, ghidra) |
| 00026290 | AICharacterPedestrian::DoTurning | AICharacterPedestrian::DoTurning |  | ghidra | AICharacterPedestrian slot 24 (PS2 001282e0, row 686, ghidra) |
| 000263d0 | AICharacterPedestrian::DoLeaning | AICharacterPedestrian::DoLeaning |  | ghidra | AICharacterPedestrian slot 28 (PS2 001284e0, row 688, ghidra) |
| 000270b0 | AICharacterPedestrian::DoWalking | AICharacterPedestrian::DoWalking |  | ghidra | AICharacterPedestrian slot 10 (PS2 00127840, row 679, ghidra) |
| 00027150 | DoStartled | AICharacterPedestrian::DoStartled |  | ghidra | AICharacterPedestrian slot 13 (PS2 00127908, row 682, ghidra) |
| 00027240 | AICharacterPedestrian::DoAvoiding | AICharacterPedestrian::DoAvoiding |  | ghidra | AICharacterPedestrian slot 12 (PS2 00127a30, row 683, ghidra) |
| 000274c0 | AICharacterPedestrian::DoDodging | AICharacterPedestrian::DoDodging |  | ghidra | AICharacterPedestrian slot 14 (PS2 00127d58, row 684, ghidra) |
| 00027800 | AICharacterPedestrian::HandleInterrupts | AICharacterPedestrian::HandleInterrupts |  | ghidra | AICharacterPedestrian slot 30 (PS2 00128cd8, row 693, ghidra) |
| 00031c80 | AIHelicopter::EngageSplinePath | AIHelicopter::EngageSplinePath |  | ghidra | AIHelicopter slot 3 (PS2 0013b468, row 889, ghidra) |
| 00031ca0 | AIHelicopter::DisengageSplinePath | AIHelicopter::DisengageSplinePath |  | ghidra | AIHelicopter slot 4 (PS2 0013b498, row 890, ghidra) |
| 000321d0 | AIHelicopter::~AIHelicopter | AIHelicopter::~AIHelicopter |  | sheet | called by AIHelicopter::scalar_deleting_destructor (0x00035df0) and stores AIHelicopter's vtable |
| 00032280 | AIHelicopter::Reset | AIHelicopter::Reset |  | ghidra | AIHelicopter slot 1 (PS2 001382f0, row 877, ghidra) |
| 00034760 | AIHelicopter::Update | AIHelicopter::Update |  | resolved | AIHelicopter slot 2 (PS2 00138ef0, row 884, resolved) |
| 00035b90 | scalar_deleting_destructor | AIVehicle::scalar_deleting_destructor |  | ghidra | AIVehicle slot 0 (PS2 0013df50, row 939, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00035df0 | scalar_deleting_destructor | AIHelicopter::scalar_deleting_destructor |  | sheet | AIHelicopter slot 0 (PS2 00138280, row 876, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00037a10 | ASceneObj::PlayCollision | ASceneObj::PlayCollision |  | ghidra | ASentry slot 5 (PS2 002f05b0, row 8894, ghidra); ASceneObj slot 5 (PS2 002f05b0, row 8894, ghidra) |
| 0003db80 | E007Logo::~E007Logo | E007Logo::~E007Logo |  | sheet | called by E007Logo::scalar_deleting_destructor (0x000472c0) and stores E007Logo's vtable |
| 0003dd70 | EAIElementFireOff::~EAIElementFireOff | EAIElementFireOff::~EAIElementFireOff |  | ghidra | called by EAIElementFireOff::scalar_deleting_destructor (0x00047390) and stores EAIElementFireOff's vtable |
| 0003de30 | EAIElementFireOn::~EAIElementFireOn | EAIElementFireOn::~EAIElementFireOn |  | ghidra | called by EAIElementFireOn::scalar_deleting_destructor (0x000473b0) and stores EAIElementFireOn's vtable |
| 0003def0 | EAIElementSetAccuracy::~EAIElementSetAccuracy | EAIElementSetAccuracy::~EAIElementSetAccuracy |  | ghidra | called by EAIElementSetAccuracy::scalar_deleting_destructor (0x000473d0) and stores EAIElementSetAccuracy's vtable |
| 0003dfb0 | EAIElementSetCharParams::~EAIElementSetCharParams | EAIElementSetCharParams::~EAIElementSetCharParams |  | ghidra | called by EAIElementSetCharParams::scalar_deleting_destructor (0x000473f0) and stores EAIElementSetCharParams's vtable |
| 0003e0a0 | EAIElementSetNonScoreable::~EAIElementSetNonScoreable | EAIElementSetNonScoreable::~EAIElementSetNonScoreable |  | ghidra | called by EAIElementSetNonScoreable::scalar_deleting_destructor (0x00047410) and stores EAIElementSetNonScoreable's vtable |
| 0003e180 | EAIElementSetVehicleParams::~EAIElementSetVehicleParams | EAIElementSetVehicleParams::~EAIElementSetVehicleParams |  | ghidra | called by EAIElementSetVehicleParams::scalar_deleting_destructor (0x00047430) and stores EAIElementSetVehicleParams's vtable |
| 0003e210 | EAIElementSetWakeRange::~EAIElementSetWakeRange | EAIElementSetWakeRange::~EAIElementSetWakeRange |  | ghidra | called by EAIElementSetWakeRange::scalar_deleting_destructor (0x00047450) and stores EAIElementSetWakeRange's vtable |
| 0003e2b0 | EAIUpdate::~EAIUpdate | EAIUpdate::~EAIUpdate |  | sheet | called by EAIUpdate::scalar_deleting_destructor (0x00047470) and stores EAIUpdate's vtable |
| 0003e340 | EAbortCinematic::~EAbortCinematic | EAbortCinematic::~EAbortCinematic |  | sheet | called by EAbortCinematic::scalar_deleting_destructor (0x00047490) and stores EAbortCinematic's vtable |
| 0003e410 | EActorMuzzleFlash::~EActorMuzzleFlash | EActorMuzzleFlash::~EActorMuzzleFlash |  | ghidra | called by EActorMuzzleFlash::scalar_deleting_destructor (0x000474b0) and stores EActorMuzzleFlash's vtable |
| 0003e520 | EAddScoreAction::~EAddScoreAction | EAddScoreAction::~EAddScoreAction |  | ghidra | called by EAddScoreAction::scalar_deleting_destructor (0x000474f0) and stores EAddScoreAction's vtable |
| 0003e620 | EAnimUpdate::~EAnimUpdate | EAnimUpdate::~EAnimUpdate |  | ghidra | called by EAnimUpdate::scalar_deleting_destructor (0x00047510) and stores EAnimUpdate's vtable |
| 0003e730 | EAutoDrive::~EAutoDrive | EAutoDrive::~EAutoDrive |  | ghidra | called by EAutoDrive::scalar_deleting_destructor (0x00047530) and stores EAutoDrive's vtable |
| 0003e800 | EAwardHitToPlayer::~EAwardHitToPlayer | EAwardHitToPlayer::~EAwardHitToPlayer |  | resolved | called by EAwardHitToPlayer::scalar_deleting_destructor (0x00047550) and stores EAwardHitToPlayer's vtable |
| 0003e870 | EAwardKillToPlayer::~EAwardKillToPlayer | EAwardKillToPlayer::~EAwardKillToPlayer |  | ghidra | called by EAwardKillToPlayer::scalar_deleting_destructor (0x00047570) and stores EAwardKillToPlayer's vtable |
| 0003e940 | EButtonMsgOff::~EButtonMsgOff | EButtonMsgOff::~EButtonMsgOff |  | ghidra | called by EButtonMsgOff::scalar_deleting_destructor (0x00047590) and stores EButtonMsgOff's vtable |
| 0003e9b0 | EButtonMsgOn::~EButtonMsgOn | EButtonMsgOn::~EButtonMsgOn |  | ghidra | called by EButtonMsgOn::scalar_deleting_destructor (0x000475b0) and stores EButtonMsgOn's vtable |
| 0003ea60 | ECall911::~ECall911 | ECall911::~ECall911 |  | ghidra | called by ECall911::scalar_deleting_destructor (0x000475d0) and stores ECall911's vtable |
| 0003eae0 | ECallStage::~ECallStage | ECallStage::~ECallStage |  | ghidra | called by ECallStage::scalar_deleting_destructor (0x000475f0) and stores ECallStage's vtable |
| 0003ecc0 | ECameraShake::~ECameraShake | ECameraShake::~ECameraShake |  | ghidra | called by ECameraShake::scalar_deleting_destructor (0x00047630) and stores ECameraShake's vtable |
| 0003ed70 | ECameraUpdate::~ECameraUpdate | ECameraUpdate::~ECameraUpdate |  | ghidra | called by ECameraUpdate::scalar_deleting_destructor (0x00047650) and stores ECameraUpdate's vtable |
| 0003ee00 | ECameraZoom::~ECameraZoom | ECameraZoom::~ECameraZoom |  | ghidra | called by ECameraZoom::scalar_deleting_destructor (0x00047670) and stores ECameraZoom's vtable |
| 0003ef10 | EChangeCarCameraView::~EChangeCarCameraView | EChangeCarCameraView::~EChangeCarCameraView |  | ghidra | called by EChangeCarCameraView::scalar_deleting_destructor (0x00047690) and stores EChangeCarCameraView's vtable |
| 0003f260 | EClearBitMagicCounter::~EClearBitMagicCounter | EClearBitMagicCounter::~EClearBitMagicCounter |  | ghidra | called by EClearBitMagicCounter::scalar_deleting_destructor (0x000476b0) and stores EClearBitMagicCounter's vtable |
| 0003f300 | EClearProgrammerEvent::~EClearProgrammerEvent | EClearProgrammerEvent::~EClearProgrammerEvent |  | ghidra | called by EClearProgrammerEvent::scalar_deleting_destructor (0x000476d0) and stores EClearProgrammerEvent's vtable |
| 0003f470 | EDeactivateAIElement::~EDeactivateAIElement | EDeactivateAIElement::~EDeactivateAIElement |  | ghidra | called by EDeactivateAIElement::scalar_deleting_destructor (0x00047930) and stores EDeactivateAIElement's vtable |
| 0003f550 | EDisablePowerUp::~EDisablePowerUp | EDisablePowerUp::~EDisablePowerUp |  | ghidra | called by EDisablePowerUp::scalar_deleting_destructor (0x00047950) and stores EDisablePowerUp's vtable |
| 0003f690 | EDisplayText::~EDisplayText | EDisplayText::~EDisplayText |  | ghidra | called by EDisplayText::scalar_deleting_destructor (0x000479b0) and stores EDisplayText's vtable |
| 0003f720 | EDropMine::~EDropMine | EDropMine::~EDropMine |  | ghidra | called by EDropMine::scalar_deleting_destructor (0x000479d0) and stores EDropMine's vtable |
| 0003f7f0 | EEnablePowerUp::~EEnablePowerUp | EEnablePowerUp::~EEnablePowerUp |  | ghidra | called by EEnablePowerUp::scalar_deleting_destructor (0x000479f0) and stores EEnablePowerUp's vtable |
| 0003f8c0 | EEnableTrigger::~EEnableTrigger | EEnableTrigger::~EEnableTrigger |  | ghidra | called by EEnableTrigger::scalar_deleting_destructor (0x00047a10) and stores EEnableTrigger's vtable |
| 0003f900 | EEndCameraAnim::~EEndCameraAnim | EEndCameraAnim::~EEndCameraAnim |  | ghidra | called by EEndCameraAnim::scalar_deleting_destructor (0x00047a30) and stores EEndCameraAnim's vtable |
| 0003fab0 | EEndMission::~EEndMission | EEndMission::~EEndMission |  | sheet | called by EEndMission::scalar_deleting_destructor (0x00047a50) and stores EEndMission's vtable |
| 0003fb80 | EExit::~EExit | EExit::~EExit |  | ghidra | called by EExit::scalar_deleting_destructor (0x00047a70) and stores EExit's vtable |
| 0003fc10 | EFESimFrameUpdate::~EFESimFrameUpdate | EFESimFrameUpdate::~EFESimFrameUpdate |  | ghidra | called by EFESimFrameUpdate::scalar_deleting_destructor (0x00047a90) and stores EFESimFrameUpdate's vtable |
| 0003fca0 | EFireEventList::~EFireEventList | EFireEventList::~EFireEventList |  | sheet | called by EFireEventList::scalar_deleting_destructor (0x00047ab0) and stores EFireEventList's vtable |
| 0003fd40 | EFireRandomTrigger::~EFireRandomTrigger | EFireRandomTrigger::~EFireRandomTrigger |  | ghidra | called by EFireRandomTrigger::scalar_deleting_destructor (0x00047ad0) and stores EFireRandomTrigger's vtable |
| 0003ff60 | EFireTriggerSpeedCondition::~EFireTriggerSpeedCondition | EFireTriggerSpeedCondition::~EFireTriggerSpeedCondition |  | ghidra | called by EFireTriggerSpeedCondition::scalar_deleting_destructor (0x00047af0) and stores EFireTriggerSpeedCondition's vtable |
| 00040200 | EGadgetOff::~EGadgetOff | EGadgetOff::~EGadgetOff |  | ghidra | called by EGadgetOff::scalar_deleting_destructor (0x00047b10) and stores EGadgetOff's vtable |
| 00040270 | EGadgetOn::~EGadgetOn | EGadgetOn::~EGadgetOn |  | ghidra | called by EGadgetOn::scalar_deleting_destructor (0x00047b30) and stores EGadgetOn's vtable |
| 00040300 | EHideInstance::~EHideInstance | EHideInstance::~EHideInstance |  | ghidra | called by EHideInstance::scalar_deleting_destructor (0x00047b50) and stores EHideInstance's vtable |
| 00040340 | EHideInstanceStatic::~EHideInstanceStatic | EHideInstanceStatic::~EHideInstanceStatic |  | ghidra | called by EHideInstanceStatic::scalar_deleting_destructor (0x00047b70) and stores EHideInstanceStatic's vtable |
| 000404c0 | EIncMagicCounter::~EIncMagicCounter | EIncMagicCounter::~EIncMagicCounter |  | ghidra | called by EIncMagicCounter::scalar_deleting_destructor (0x00047e50) and stores EIncMagicCounter's vtable |
| 00040530 | EInflictDamage::~EInflictDamage | EInflictDamage::~EInflictDamage |  | ghidra | called by EInflictDamage::scalar_deleting_destructor (0x00047e70) and stores EInflictDamage's vtable |
| 000405f0 | EInfraRedOff::~EInfraRedOff | EInfraRedOff::~EInfraRedOff |  | ghidra | called by EInfraRedOff::scalar_deleting_destructor (0x00047e90) and stores EInfraRedOff's vtable |
| 00040660 | EInfraRedOn::~EInfraRedOn | EInfraRedOn::~EInfraRedOn |  | ghidra | called by EInfraRedOn::scalar_deleting_destructor (0x00047eb0) and stores EInfraRedOn's vtable |
| 00040710 | EKickAIElement::~EKickAIElement | EKickAIElement::~EKickAIElement |  | ghidra | called by EKickAIElement::scalar_deleting_destructor (0x00047ef0) and stores EKickAIElement's vtable |
| 00040960 | EKillObject::~EKillObject | EKillObject::~EKillObject |  | ghidra | called by EKillObject::scalar_deleting_destructor (0x00047f10) and stores EKillObject's vtable |
| 00040a30 | EKillSentry::~EKillSentry | EKillSentry::~EKillSentry |  | ghidra | called by EKillSentry::scalar_deleting_destructor (0x00047f30) and stores EKillSentry's vtable |
| 00040ac0 | ELavaHaze::~ELavaHaze | ELavaHaze::~ELavaHaze |  | ghidra | called by ELavaHaze::scalar_deleting_destructor (0x00047f50) and stores ELavaHaze's vtable |
| 00040b40 | ELetterBoxOff::~ELetterBoxOff | ELetterBoxOff::~ELetterBoxOff |  | ghidra | called by ELetterBoxOff::scalar_deleting_destructor (0x00047f70) and stores ELetterBoxOff's vtable |
| 00040bd0 | ELetterBoxOn::~ELetterBoxOn | ELetterBoxOn::~ELetterBoxOn |  | ghidra | called by ELetterBoxOn::scalar_deleting_destructor (0x00047f90) and stores ELetterBoxOn's vtable |
| 00040cb0 | EMuzzleFlash::~EMuzzleFlash | EMuzzleFlash::~EMuzzleFlash |  | ghidra | called by EMuzzleFlash::scalar_deleting_destructor (0x00047fb0) and stores EMuzzleFlash's vtable |
| 00040e50 | ENuclearBlast::~ENuclearBlast | ENuclearBlast::~ENuclearBlast |  | ghidra | called by ENuclearBlast::scalar_deleting_destructor (0x00047fd0) and stores ENuclearBlast's vtable |
| 00040f10 | EObjectiveAdd::~EObjectiveAdd | EObjectiveAdd::~EObjectiveAdd |  | ghidra | called by EObjectiveAdd::scalar_deleting_destructor (0x00047ff0) and stores EObjectiveAdd's vtable |
| 00040fb0 | EObjectiveDisplayText::~EObjectiveDisplayText | EObjectiveDisplayText::~EObjectiveDisplayText |  | ghidra | called by EObjectiveDisplayText::scalar_deleting_destructor (0x00048010) and stores EObjectiveDisplayText's vtable |
| 00041040 | EObjectiveFail::~EObjectiveFail | EObjectiveFail::~EObjectiveFail |  | ghidra | called by EObjectiveFail::scalar_deleting_destructor (0x00048030) and stores EObjectiveFail's vtable |
| 000410e0 | EObjectiveInsert::~EObjectiveInsert | EObjectiveInsert::~EObjectiveInsert |  | ghidra | called by EObjectiveInsert::scalar_deleting_destructor (0x00048050) and stores EObjectiveInsert's vtable |
| 00041180 | EObjectivePass::~EObjectivePass | EObjectivePass::~EObjectivePass |  | ghidra | called by EObjectivePass::scalar_deleting_destructor (0x00048070) and stores EObjectivePass's vtable |
| 00041220 | EObjectiveSetCurrent::~EObjectiveSetCurrent | EObjectiveSetCurrent::~EObjectiveSetCurrent |  | ghidra | called by EObjectiveSetCurrent::scalar_deleting_destructor (0x00048090) and stores EObjectiveSetCurrent's vtable |
| 000412c0 | EObjectiveSetIncomplete::~EObjectiveSetIncomplete | EObjectiveSetIncomplete::~EObjectiveSetIncomplete |  | ghidra | called by EObjectiveSetIncomplete::scalar_deleting_destructor (0x000480b0) and stores EObjectiveSetIncomplete's vtable |
| 00041380 | EPathChangeThrottleDynamic::~EPathChangeThrottleDynamic | EPathChangeThrottleDynamic::~EPathChangeThrottleDynamic |  | ghidra | called by EPathChangeThrottleDynamic::scalar_deleting_destructor (0x000480d0) and stores EPathChangeThrottleDynamic's vtable |
| 00041450 | EPathChangeThrottleStatic::~EPathChangeThrottleStatic | EPathChangeThrottleStatic::~EPathChangeThrottleStatic |  | ghidra | called by EPathChangeThrottleStatic::scalar_deleting_destructor (0x000480f0) and stores EPathChangeThrottleStatic's vtable |
| 00041520 | EPathMultiplyThrottleDynamic::~EPathMultiplyThrottleDynamic | EPathMultiplyThrottleDynamic::~EPathMultiplyThrottleDynamic |  | ghidra | called by EPathMultiplyThrottleDynamic::scalar_deleting_destructor (0x00048110) and stores EPathMultiplyThrottleDynamic's vtable |
| 00041600 | EPathMultiplyThrottleStatic::~EPathMultiplyThrottleStatic | EPathMultiplyThrottleStatic::~EPathMultiplyThrottleStatic |  | ghidra | called by EPathMultiplyThrottleStatic::scalar_deleting_destructor (0x00048130) and stores EPathMultiplyThrottleStatic's vtable |
| 000416e0 | EPathSFX::~EPathSFX | EPathSFX::~EPathSFX |  | ghidra | called by EPathSFX::scalar_deleting_destructor (0x00048150) and stores EPathSFX's vtable |
| 00041890 | EPathSetAccelDelayDynamic::~EPathSetAccelDelayDynamic | EPathSetAccelDelayDynamic::~EPathSetAccelDelayDynamic |  | ghidra | called by EPathSetAccelDelayDynamic::scalar_deleting_destructor (0x000482c0) and stores EPathSetAccelDelayDynamic's vtable |
| 00041930 | EPathSetAccelDelayStatic::~EPathSetAccelDelayStatic | EPathSetAccelDelayStatic::~EPathSetAccelDelayStatic |  | ghidra | called by EPathSetAccelDelayStatic::scalar_deleting_destructor (0x000482e0) and stores EPathSetAccelDelayStatic's vtable |
| 000419c0 | EPathSetAccelerationDynamic::~EPathSetAccelerationDynamic | EPathSetAccelerationDynamic::~EPathSetAccelerationDynamic |  | ghidra | called by EPathSetAccelerationDynamic::scalar_deleting_destructor (0x00048300) and stores EPathSetAccelerationDynamic's vtable |
| 00041a60 | EPathSetAccelerationStatic::~EPathSetAccelerationStatic | EPathSetAccelerationStatic::~EPathSetAccelerationStatic |  | ghidra | called by EPathSetAccelerationStatic::scalar_deleting_destructor (0x00048320) and stores EPathSetAccelerationStatic's vtable |
| 00041af0 | EPathSetDesiredThrottleDynamic::~EPathSetDesiredThrottleDynamic | EPathSetDesiredThrottleDynamic::~EPathSetDesiredThrottleDynamic |  | ghidra | called by EPathSetDesiredThrottleDynamic::scalar_deleting_destructor (0x00048340) and stores EPathSetDesiredThrottleDynamic's vtable |
| 00041b90 | EPathSetDesiredThrottleStatic::~EPathSetDesiredThrottleStatic | EPathSetDesiredThrottleStatic::~EPathSetDesiredThrottleStatic |  | ghidra | called by EPathSetDesiredThrottleStatic::scalar_deleting_destructor (0x00048360) and stores EPathSetDesiredThrottleStatic's vtable |
| 00041c20 | EPathSetRunningDynamic::~EPathSetRunningDynamic | EPathSetRunningDynamic::~EPathSetRunningDynamic |  | ghidra | called by EPathSetRunningDynamic::scalar_deleting_destructor (0x00048380) and stores EPathSetRunningDynamic's vtable |
| 00041cc0 | EPathSetRunningStatic::~EPathSetRunningStatic | EPathSetRunningStatic::~EPathSetRunningStatic |  | ghidra | called by EPathSetRunningStatic::scalar_deleting_destructor (0x000483a0) and stores EPathSetRunningStatic's vtable |
| 00041d60 | EPathSetThrottleDynamic::~EPathSetThrottleDynamic | EPathSetThrottleDynamic::~EPathSetThrottleDynamic |  | ghidra | called by EPathSetThrottleDynamic::scalar_deleting_destructor (0x000483c0) and stores EPathSetThrottleDynamic's vtable |
| 00041e00 | EPathSetThrottleStatic::~EPathSetThrottleStatic | EPathSetThrottleStatic::~EPathSetThrottleStatic |  | ghidra | called by EPathSetThrottleStatic::scalar_deleting_destructor (0x000483e0) and stores EPathSetThrottleStatic's vtable |
| 00041e90 | EPathSetTimeDynamic::~EPathSetTimeDynamic | EPathSetTimeDynamic::~EPathSetTimeDynamic |  | ghidra | called by EPathSetTimeDynamic::scalar_deleting_destructor (0x00048400) and stores EPathSetTimeDynamic's vtable |
| 00041f40 | EPathSetTimeStatic::~EPathSetTimeStatic | EPathSetTimeStatic::~EPathSetTimeStatic |  | ghidra | called by EPathSetTimeStatic::scalar_deleting_destructor (0x00048420) and stores EPathSetTimeStatic's vtable |
| 00042060 | EPause::~EPause | EPause::~EPause |  | ghidra | called by EPause::scalar_deleting_destructor (0x00048440) and stores EPause's vtable |
| 000420f0 | EPlayActorEffect::~EPlayActorEffect | EPlayActorEffect::~EPlayActorEffect |  | ghidra | called by EPlayActorEffect::scalar_deleting_destructor (0x00048460) and stores EPlayActorEffect's vtable |
| 000421e0 | EPlayCameraAnim::~EPlayCameraAnim | EPlayCameraAnim::~EPlayCameraAnim |  | ghidra | called by EPlayCameraAnim::scalar_deleting_destructor (0x00048480) and stores EPlayCameraAnim's vtable |
| 00042350 | EPlayCameraSpline::~EPlayCameraSpline | EPlayCameraSpline::~EPlayCameraSpline |  | ghidra | called by EPlayCameraSpline::scalar_deleting_destructor (0x000484a0) and stores EPlayCameraSpline's vtable |
| 00042500 | EPlayEffect::~EPlayEffect | EPlayEffect::~EPlayEffect |  | ghidra | called by EPlayEffect::scalar_deleting_destructor (0x000484c0) and stores EPlayEffect's vtable |
| 00042750 | EPlayGiottoEffect::~EPlayGiottoEffect | EPlayGiottoEffect::~EPlayGiottoEffect |  | ghidra | called by EPlayGiottoEffect::scalar_deleting_destructor (0x000484e0) and stores EPlayGiottoEffect's vtable |
| 00042840 | EPlayPOVDeath::~EPlayPOVDeath | EPlayPOVDeath::~EPlayPOVDeath |  | ghidra | called by EPlayPOVDeath::scalar_deleting_destructor (0x00048500) and stores EPlayPOVDeath's vtable |
| 000428d0 | EPlaySound::~EPlaySound | EPlaySound::~EPlaySound |  | ghidra | called by EPlaySound::scalar_deleting_destructor (0x00048520) and stores EPlaySound's vtable |
| 000429d0 | EPlaySystemAnim::~EPlaySystemAnim | EPlaySystemAnim::~EPlaySystemAnim |  | ghidra | called by EPlaySystemAnim::scalar_deleting_destructor (0x00048620) and stores EPlaySystemAnim's vtable |
| 00042be0 | EPowerUp::~EPowerUp | EPowerUp::~EPowerUp |  | ghidra | called by EPowerUp::scalar_deleting_destructor (0x00048640) and stores EPowerUp's vtable |
| 00042d20 | EPowerUpAmmo::~EPowerUpAmmo | EPowerUpAmmo::~EPowerUpAmmo |  | ghidra | called by EPowerUpAmmo::scalar_deleting_destructor (0x00048660) and stores EPowerUpAmmo's vtable |
| 00042e90 | EPowerUpMultiDamage::~EPowerUpMultiDamage | EPowerUpMultiDamage::~EPowerUpMultiDamage |  | ghidra | called by EPowerUpMultiDamage::scalar_deleting_destructor (0x000486a0) and stores EPowerUpMultiDamage's vtable |
| 00042ee0 | EPowerUpShield::~EPowerUpShield | EPowerUpShield::~EPowerUpShield |  | ghidra | called by EPowerUpShield::scalar_deleting_destructor (0x000486c0) and stores EPowerUpShield's vtable |
| 00042fa0 | EPowerUpTimer::~EPowerUpTimer | EPowerUpTimer::~EPowerUpTimer |  | ghidra | called by EPowerUpTimer::scalar_deleting_destructor (0x000486e0) and stores EPowerUpTimer's vtable |
| 00043070 | EProfileMissionSection::~EProfileMissionSection | EProfileMissionSection::~EProfileMissionSection |  | ghidra | called by EProfileMissionSection::scalar_deleting_destructor (0x00048720) and stores EProfileMissionSection's vtable |
| 000430e0 | ERandomExplosion::~ERandomExplosion | ERandomExplosion::~ERandomExplosion |  | ghidra | called by ERandomExplosion::scalar_deleting_destructor (0x00048740) and stores ERandomExplosion's vtable |
| 00043290 | EReleaseStream::~EReleaseStream | EReleaseStream::~EReleaseStream |  | ghidra | called by EReleaseStream::scalar_deleting_destructor (0x00048760) and stores EReleaseStream's vtable |
| 000432e0 | ERenderFrame::~ERenderFrame | ERenderFrame::~ERenderFrame |  | sheet | called by ERenderFrame::scalar_deleting_destructor (0x00048780) and stores ERenderFrame's vtable |
| 00043390 | EResetAudioMix::~EResetAudioMix | EResetAudioMix::~EResetAudioMix |  | ghidra | called by EResetAudioMix::scalar_deleting_destructor (0x000487a0) and stores EResetAudioMix's vtable |
| 000434c0 | ERevertAudioMix::~ERevertAudioMix | ERevertAudioMix::~ERevertAudioMix |  | ghidra | called by ERevertAudioMix::scalar_deleting_destructor (0x000487c0) and stores ERevertAudioMix's vtable |
| 00043560 | ERollSub::~ERollSub | ERollSub::~ERollSub |  | ghidra | called by ERollSub::scalar_deleting_destructor (0x000487e0) and stores ERollSub's vtable |
| 00043670 | EScaleFieldOfView::~EScaleFieldOfView | EScaleFieldOfView::~EScaleFieldOfView |  | ghidra | called by EScaleFieldOfView::scalar_deleting_destructor (0x00048820) and stores EScaleFieldOfView's vtable |
| 000436b0 | EScaleFog::~EScaleFog | EScaleFog::~EScaleFog |  | ghidra | called by EScaleFog::scalar_deleting_destructor (0x00048840) and stores EScaleFog's vtable |
| 00043740 | EScheduleEvent::~EScheduleEvent | EScheduleEvent::~EScheduleEvent |  | sheet | called by EScheduleEvent::scalar_deleting_destructor (0x00048860) and stores EScheduleEvent's vtable |
| 000437d0 | ESentryMuzzleFlash::~ESentryMuzzleFlash | ESentryMuzzleFlash::~ESentryMuzzleFlash |  | ghidra | called by ESentryMuzzleFlash::scalar_deleting_destructor (0x00048880) and stores ESentryMuzzleFlash's vtable |
| 000438d0 | ESetAFXMode::~ESetAFXMode | ESetAFXMode::~ESetAFXMode |  | ghidra | called by ESetAFXMode::scalar_deleting_destructor (0x000488a0) and stores ESetAFXMode's vtable |
| 00043960 | ESetAreaBrightness::~ESetAreaBrightness | ESetAreaBrightness::~ESetAreaBrightness |  | ghidra | called by ESetAreaBrightness::scalar_deleting_destructor (0x000488c0) and stores ESetAreaBrightness's vtable |
| 00043a00 | ESetAudioMix::~ESetAudioMix | ESetAudioMix::~ESetAudioMix |  | ghidra | called by ESetAudioMix::scalar_deleting_destructor (0x000488e0) and stores ESetAudioMix's vtable |
| 00043af0 | ESetAutoDriveCamOff::~ESetAutoDriveCamOff | ESetAutoDriveCamOff::~ESetAutoDriveCamOff |  | ghidra | called by ESetAutoDriveCamOff::scalar_deleting_destructor (0x00048900) and stores ESetAutoDriveCamOff's vtable |
| 00043ba0 | ESetAutoDriveCamOn::~ESetAutoDriveCamOn | ESetAutoDriveCamOn::~ESetAutoDriveCamOn |  | ghidra | called by ESetAutoDriveCamOn::scalar_deleting_destructor (0x00048920) and stores ESetAutoDriveCamOn's vtable |
| 00043c60 | ESetAutoDriveCamOnMovie::~ESetAutoDriveCamOnMovie | ESetAutoDriveCamOnMovie::~ESetAutoDriveCamOnMovie |  | ghidra | called by ESetAutoDriveCamOnMovie::scalar_deleting_destructor (0x00048940) and stores ESetAutoDriveCamOnMovie's vtable |
| 00043d50 | ESetAvoidZone::~ESetAvoidZone | ESetAvoidZone::~ESetAvoidZone |  | ghidra | called by ESetAvoidZone::scalar_deleting_destructor (0x00048960) and stores ESetAvoidZone's vtable |
| 00043e20 | ESetBitMagicCounter::~ESetBitMagicCounter | ESetBitMagicCounter::~ESetBitMagicCounter |  | ghidra | called by ESetBitMagicCounter::scalar_deleting_destructor (0x00048980) and stores ESetBitMagicCounter's vtable |
| 00043ec0 | ESetCollisionGeometry::~ESetCollisionGeometry | ESetCollisionGeometry::~ESetCollisionGeometry |  | ghidra | called by ESetCollisionGeometry::scalar_deleting_destructor (0x000489a0) and stores ESetCollisionGeometry's vtable |
| 00043fe0 | ESetFrameAnimationTime::~ESetFrameAnimationTime | ESetFrameAnimationTime::~ESetFrameAnimationTime |  | ghidra | called by ESetFrameAnimationTime::scalar_deleting_destructor (0x000489e0) and stores ESetFrameAnimationTime's vtable |
| 00044070 | ESetMagicCounter::~ESetMagicCounter | ESetMagicCounter::~ESetMagicCounter |  | ghidra | called by ESetMagicCounter::scalar_deleting_destructor (0x00048a20) and stores ESetMagicCounter's vtable |
| 00044100 | ESetMagicCounterThreshold::~ESetMagicCounterThreshold | ESetMagicCounterThreshold::~ESetMagicCounterThreshold |  | ghidra | called by ESetMagicCounterThreshold::scalar_deleting_destructor (0x00048a40) and stores ESetMagicCounterThreshold's vtable |
| 00044190 | ESetMissionMessage::~ESetMissionMessage | ESetMissionMessage::~ESetMissionMessage |  | ghidra | called by ESetMissionMessage::scalar_deleting_destructor (0x00048a60) and stores ESetMissionMessage's vtable |
| 00044250 | ESetNextPath::~ESetNextPath | ESetNextPath::~ESetNextPath |  | ghidra | called by ESetNextPath::scalar_deleting_destructor (0x00048a80) and stores ESetNextPath's vtable |
| 000442e0 | ESetObjective::~ESetObjective | ESetObjective::~ESetObjective |  | ghidra | called by ESetObjective::scalar_deleting_destructor (0x00048aa0) and stores ESetObjective's vtable |
| 00044400 | ESetRecordedGameTime::~ESetRecordedGameTime | ESetRecordedGameTime::~ESetRecordedGameTime |  | ghidra | called by ESetRecordedGameTime::scalar_deleting_destructor (0x00048ac0) and stores ESetRecordedGameTime's vtable |
| 00044450 | ESetReflectiveObject::~ESetReflectiveObject | ESetReflectiveObject::~ESetReflectiveObject |  | ghidra | called by ESetReflectiveObject::scalar_deleting_destructor (0x00048ae0) and stores ESetReflectiveObject's vtable |
| 00044510 | ESetRenderVariation::~ESetRenderVariation | ESetRenderVariation::~ESetRenderVariation |  | ghidra | called by ESetRenderVariation::scalar_deleting_destructor (0x00048b00) and stores ESetRenderVariation's vtable |
| 00044610 | ESetSimRate::~ESetSimRate | ESetSimRate::~ESetSimRate |  | ghidra | called by ESetSimRate::scalar_deleting_destructor (0x00048b20) and stores ESetSimRate's vtable |
| 00044660 | ESetSpecialWeapon::~ESetSpecialWeapon | ESetSpecialWeapon::~ESetSpecialWeapon |  | ghidra | called by ESetSpecialWeapon::scalar_deleting_destructor (0x00048b40) and stores ESetSpecialWeapon's vtable |
| 00044760 | ESetSubtitle::~ESetSubtitle | ESetSubtitle::~ESetSubtitle |  | ghidra | called by ESetSubtitle::scalar_deleting_destructor (0x00048b60) and stores ESetSubtitle's vtable |
| 000448b0 | ESetTimer::~ESetTimer | ESetTimer::~ESetTimer |  | ghidra | called by ESetTimer::scalar_deleting_destructor (0x00048b80) and stores ESetTimer's vtable |
| 00044970 | ESetVideo::~ESetVideo | ESetVideo::~ESetVideo |  | ghidra | called by ESetVideo::scalar_deleting_destructor (0x00048ba0) and stores ESetVideo's vtable |
| 00044c20 | ESetWaypoint::~ESetWaypoint | ESetWaypoint::~ESetWaypoint |  | ghidra | called by ESetWaypoint::scalar_deleting_destructor (0x00048bc0) and stores ESetWaypoint's vtable |
| 00044d00 | EShowInstance::~EShowInstance | EShowInstance::~EShowInstance |  | ghidra | called by EShowInstance::scalar_deleting_destructor (0x00048be0) and stores EShowInstance's vtable |
| 00044d40 | EShowInstanceStatic::~EShowInstanceStatic | EShowInstanceStatic::~EShowInstanceStatic |  | ghidra | called by EShowInstanceStatic::scalar_deleting_destructor (0x00048c00) and stores EShowInstanceStatic's vtable |
| 00044df0 | ESimEndFrame::~ESimEndFrame | ESimEndFrame::~ESimEndFrame |  | ghidra | called by ESimEndFrame::scalar_deleting_destructor (0x00048c20) and stores ESimEndFrame's vtable |
| 00044e80 | ESpawnDramaticSmackable::~ESpawnDramaticSmackable | ESpawnDramaticSmackable::~ESpawnDramaticSmackable |  | ghidra | called by ESpawnDramaticSmackable::scalar_deleting_destructor (0x00048c40) and stores ESpawnDramaticSmackable's vtable |
| 00045120 | ESpawnExplosionStatic::~ESpawnExplosionStatic | ESpawnExplosionStatic::~ESpawnExplosionStatic |  | ghidra | called by ESpawnExplosionStatic::scalar_deleting_destructor (0x00048c60) and stores ESpawnExplosionStatic's vtable |
| 000453a0 | ESpawnForceEffect::~ESpawnForceEffect | ESpawnForceEffect::~ESpawnForceEffect |  | ghidra | called by ESpawnForceEffect::scalar_deleting_destructor (0x00048c80) and stores ESpawnForceEffect's vtable |
| 00045880 | ESpawnSentry::~ESpawnSentry | ESpawnSentry::~ESpawnSentry |  | ghidra | called by ESpawnSentry::scalar_deleting_destructor (0x00048ca0) and stores ESpawnSentry's vtable |
| 000459b0 | ESpawnSimplePhysics::~ESpawnSimplePhysics | ESpawnSimplePhysics::~ESpawnSimplePhysics |  | ghidra | called by ESpawnSimplePhysics::scalar_deleting_destructor (0x00048cc0) and stores ESpawnSimplePhysics's vtable |
| 00045d70 | ESpawnTraffic::~ESpawnTraffic | ESpawnTraffic::~ESpawnTraffic |  | ghidra | called by ESpawnTraffic::scalar_deleting_destructor (0x00048d00) and stores ESpawnTraffic's vtable |
| 00046050 | EStartEffect::~EStartEffect | EStartEffect::~EStartEffect |  | ghidra | called by EStartEffect::scalar_deleting_destructor (0x00048d20) and stores EStartEffect's vtable |
| 00046380 | EStopEffect::~EStopEffect | EStopEffect::~EStopEffect |  | ghidra | called by EStopEffect::scalar_deleting_destructor (0x00048d40) and stores EStopEffect's vtable |
| 00046540 | EStopSoundPos::~EStopSoundPos | EStopSoundPos::~EStopSoundPos |  | ghidra | called by EStopSoundPos::scalar_deleting_destructor (0x00048d60) and stores EStopSoundPos's vtable |
| 000465b0 | EStopTimedAction::~EStopTimedAction | EStopTimedAction::~EStopTimedAction |  | ghidra | called by EStopTimedAction::scalar_deleting_destructor (0x00048d80) and stores EStopTimedAction's vtable |
| 00046720 | ESuppressEffect::~ESuppressEffect | ESuppressEffect::~ESuppressEffect |  | ghidra | called by ESuppressEffect::scalar_deleting_destructor (0x00048da0) and stores ESuppressEffect's vtable |
| 00046850 | ESwitchEffectOff::~ESwitchEffectOff | ESwitchEffectOff::~ESwitchEffectOff |  | ghidra | called by ESwitchEffectOff::scalar_deleting_destructor (0x00048dc0) and stores ESwitchEffectOff's vtable |
| 000468f0 | ESwitchEffectOn::~ESwitchEffectOn | ESwitchEffectOn::~ESwitchEffectOn |  | ghidra | called by ESwitchEffectOn::scalar_deleting_destructor (0x00048de0) and stores ESwitchEffectOn's vtable |
| 00046990 | ESwitchStingerChannel::~ESwitchStingerChannel | ESwitchStingerChannel::~ESwitchStingerChannel |  | ghidra | called by ESwitchStingerChannel::scalar_deleting_destructor (0x00048e00) and stores ESwitchStingerChannel's vtable |
| 00046a10 | ETargetBeaconOnOff::~ETargetBeaconOnOff | ETargetBeaconOnOff::~ETargetBeaconOnOff |  | ghidra | called by ETargetBeaconOnOff::scalar_deleting_destructor (0x00048e20) and stores ETargetBeaconOnOff's vtable |
| 00046ae0 | ETimerDrawOff::~ETimerDrawOff | ETimerDrawOff::~ETimerDrawOff |  | ghidra | called by ETimerDrawOff::scalar_deleting_destructor (0x00048e40) and stores ETimerDrawOff's vtable |
| 00046b60 | ETimerDrawOn::~ETimerDrawOn | ETimerDrawOn::~ETimerDrawOn |  | ghidra | called by ETimerDrawOn::scalar_deleting_destructor (0x00048e60) and stores ETimerDrawOn's vtable |
| 00046c20 | EUnloadWeapon::~EUnloadWeapon | EUnloadWeapon::~EUnloadWeapon |  | ghidra | called by EUnloadWeapon::scalar_deleting_destructor (0x00048e80) and stores EUnloadWeapon's vtable |
| 00046d10 | EWakeupSmackable::~EWakeupSmackable | EWakeupSmackable::~EWakeupSmackable |  | ghidra | called by EWakeupSmackable::scalar_deleting_destructor (0x00048ea0) and stores EWakeupSmackable's vtable |
| 000472c0 | E007Logo::scalar_deleting_destructor | E007Logo::scalar_deleting_destructor |  | sheet | E007Logo slot 0 (PS2 00145f00, row 1171, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047390 | EAIElementFireOff::scalar_deleting_destructor | EAIElementFireOff::scalar_deleting_destructor |  | ghidra | EAIElementFireOff slot 0 (PS2 001469f8, row 1177, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000473b0 | EAIElementFireOn::scalar_deleting_destructor | EAIElementFireOn::scalar_deleting_destructor |  | ghidra | EAIElementFireOn slot 0 (PS2 00146b48, row 1180, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000473d0 | EAIElementSetAccuracy::scalar_deleting_destructor | EAIElementSetAccuracy::scalar_deleting_destructor |  | ghidra | EAIElementSetAccuracy slot 0 (PS2 00146ca8, row 1183, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000473f0 | EAIElementSetCharParams::scalar_deleting_destructor | EAIElementSetCharParams::scalar_deleting_destructor |  | ghidra | EAIElementSetCharParams slot 0 (PS2 00146df8, row 1186, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047410 | EAIElementSetNonScoreable::scalar_deleting_destructor | EAIElementSetNonScoreable::scalar_deleting_destructor |  | ghidra | EAIElementSetNonScoreable slot 0 (PS2 00146f68, row 1189, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047430 | EAIElementSetVehicleParams::scalar_deleting_destructor | EAIElementSetVehicleParams::scalar_deleting_destructor |  | ghidra | EAIElementSetVehicleParams slot 0 (PS2 001470e8, row 1192, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047450 | EAIElementSetWakeRange::scalar_deleting_destructor | EAIElementSetWakeRange::scalar_deleting_destructor |  | ghidra | EAIElementSetWakeRange slot 0 (PS2 001471e0, row 1195, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047470 | EAIUpdate::scalar_deleting_destructor | EAIUpdate::scalar_deleting_destructor |  | sheet | EAIUpdate slot 0 (PS2 001472e0, row 1198, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047490 | EAbortCinematic::scalar_deleting_destructor | EAbortCinematic::scalar_deleting_destructor |  | sheet | EAbortCinematic slot 0 (PS2 001475e8, row 1201, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000474b0 | EActorMuzzleFlash::scalar_deleting_destructor | EActorMuzzleFlash::scalar_deleting_destructor |  | ghidra | EActorMuzzleFlash slot 0 (PS2 001479c0, row 1207, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000474f0 | EAddScoreAction::scalar_deleting_destructor | EAddScoreAction::scalar_deleting_destructor |  | ghidra | EAddScoreAction slot 0 (PS2 00147b80, row 1213, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047510 | EAnimUpdate::scalar_deleting_destructor | EAnimUpdate::scalar_deleting_destructor |  | ghidra | EAnimUpdate slot 0 (PS2 00147d60, row 1219, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047530 | EAutoDrive::scalar_deleting_destructor | EAutoDrive::scalar_deleting_destructor |  | ghidra | EAutoDrive slot 0 (PS2 00148220, row 1225, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047550 | EAwardHitToPlayer::scalar_deleting_destructor | EAwardHitToPlayer::scalar_deleting_destructor |  | resolved | EAwardHitToPlayer slot 0 (PS2 00148540, row 1234, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047570 | EAwardKillToPlayer::scalar_deleting_destructor | EAwardKillToPlayer::scalar_deleting_destructor |  | ghidra | EAwardKillToPlayer slot 0 (PS2 00148600, row 1237, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047590 | EButtonMsgOff::scalar_deleting_destructor | EButtonMsgOff::scalar_deleting_destructor |  | ghidra | EButtonMsgOff slot 0 (PS2 00148ad8, row 1246, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000475b0 | EButtonMsgOn::scalar_deleting_destructor | EButtonMsgOn::scalar_deleting_destructor |  | ghidra | EButtonMsgOn slot 0 (PS2 00148b98, row 1249, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000475d0 | ECall911::scalar_deleting_destructor | ECall911::scalar_deleting_destructor |  | ghidra | ECall911 slot 0 (PS2 00148c98, row 1252, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000475f0 | ECallStage::scalar_deleting_destructor | ECallStage::scalar_deleting_destructor |  | ghidra | ECallStage slot 0 (PS2 00148d50, row 1255, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047630 | ECameraShake::scalar_deleting_destructor | ECameraShake::scalar_deleting_destructor |  | ghidra | ECameraShake slot 0 (PS2 00149118, row 1261, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047650 | ECameraUpdate::scalar_deleting_destructor | ECameraUpdate::scalar_deleting_destructor |  | ghidra | ECameraUpdate slot 0 (PS2 00149240, row 1264, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047670 | ECameraZoom::scalar_deleting_destructor | ECameraZoom::scalar_deleting_destructor |  | ghidra | ECameraZoom slot 0 (PS2 00149338, row 1267, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047690 | EChangeCarCameraView::scalar_deleting_destructor | EChangeCarCameraView::scalar_deleting_destructor |  | ghidra | EChangeCarCameraView slot 0 (PS2 00149528, row 1270, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000476b0 | EClearBitMagicCounter::scalar_deleting_destructor | EClearBitMagicCounter::scalar_deleting_destructor |  | ghidra | EClearBitMagicCounter slot 0 (PS2 00149ba8, row 1282, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000476d0 | EClearProgrammerEvent::scalar_deleting_destructor | EClearProgrammerEvent::scalar_deleting_destructor |  | ghidra | EClearProgrammerEvent slot 0 (PS2 00149c88, row 1285, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047930 | EDeactivateAIElement::scalar_deleting_destructor | EDeactivateAIElement::scalar_deleting_destructor |  | ghidra | EDeactivateAIElement slot 0 (PS2 0014b510, row 1306, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047950 | EDisablePowerUp::scalar_deleting_destructor | EDisablePowerUp::scalar_deleting_destructor |  | ghidra | EDisablePowerUp slot 0 (PS2 0014b630, row 1309, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000479b0 | EDisplayText::scalar_deleting_destructor | EDisplayText::scalar_deleting_destructor |  | ghidra | EDisplayText slot 0 (PS2 0014b8f8, row 1318, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000479d0 | EDropMine::scalar_deleting_destructor | EDropMine::scalar_deleting_destructor |  | ghidra | EDropMine slot 0 (PS2 0014b9c8, row 1321, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000479f0 | EEnablePowerUp::scalar_deleting_destructor | EEnablePowerUp::scalar_deleting_destructor |  | ghidra | EEnablePowerUp slot 0 (PS2 0014bb58, row 1324, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047a10 | EEnableTrigger::scalar_deleting_destructor | EEnableTrigger::scalar_deleting_destructor |  | ghidra | EEnableTrigger slot 0 (PS2 0014bcd8, row 1327, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047a30 | EEndCameraAnim::scalar_deleting_destructor | EEndCameraAnim::scalar_deleting_destructor |  | ghidra | EEndCameraAnim slot 0 (PS2 0014bdb8, row 1330, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047a50 | EEndMission::scalar_deleting_destructor | EEndMission::scalar_deleting_destructor |  | sheet | EEndMission slot 0 (PS2 0014c260, row 1339, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047a70 | EExit::scalar_deleting_destructor | EExit::scalar_deleting_destructor |  | ghidra | EExit slot 0 (PS2 0014c370, row 1342, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047a90 | EFESimFrameUpdate::scalar_deleting_destructor | EFESimFrameUpdate::scalar_deleting_destructor |  | ghidra | EFESimFrameUpdate slot 0 (PS2 0014c440, row 1345, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047ab0 | EFireEventList::scalar_deleting_destructor | EFireEventList::scalar_deleting_destructor |  | sheet | EFireEventList slot 0 (PS2 0014c568, row 1351, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047ad0 | EFireRandomTrigger::scalar_deleting_destructor | EFireRandomTrigger::scalar_deleting_destructor |  | ghidra | EFireRandomTrigger slot 0 (PS2 0014c688, row 1354, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047af0 | EFireTriggerSpeedCondition::scalar_deleting_destructor | EFireTriggerSpeedCondition::scalar_deleting_destructor |  | ghidra | EFireTriggerSpeedCondition slot 0 (PS2 0014c8c8, row 1357, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047b10 | EGadgetOff::scalar_deleting_destructor | EGadgetOff::scalar_deleting_destructor |  | ghidra | EGadgetOff slot 0 (PS2 0014efe8, row 1369, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047b30 | EGadgetOn::scalar_deleting_destructor | EGadgetOn::scalar_deleting_destructor |  | ghidra | EGadgetOn slot 0 (PS2 0014f0a0, row 1372, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047b50 | EHideInstance::scalar_deleting_destructor | EHideInstance::scalar_deleting_destructor |  | ghidra | EHideInstance slot 0 (PS2 0014f168, row 1375, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047b70 | EHideInstanceStatic::scalar_deleting_destructor | EHideInstanceStatic::scalar_deleting_destructor |  | ghidra | EHideInstanceStatic slot 0 (PS2 0014f218, row 1378, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047b90 | EHitWindow::~EHitWindow | EHitWindow::~EHitWindow |  | ghidra | called by EHitWindow::scalar_deleting_destructor (0x0004c7d0) and stores EHitWindow's vtable |
| 00047e50 | EIncMagicCounter::scalar_deleting_destructor | EIncMagicCounter::scalar_deleting_destructor |  | ghidra | EIncMagicCounter slot 0 (PS2 0014fc50, row 1384, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047e70 | EInflictDamage::scalar_deleting_destructor | EInflictDamage::scalar_deleting_destructor |  | ghidra | EInflictDamage slot 0 (PS2 0014fd20, row 1387, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047e90 | EInfraRedOff::scalar_deleting_destructor | EInfraRedOff::scalar_deleting_destructor |  | ghidra | EInfraRedOff slot 0 (PS2 0014fe78, row 1390, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047eb0 | EInfraRedOn::scalar_deleting_destructor | EInfraRedOn::scalar_deleting_destructor |  | ghidra | EInfraRedOn slot 0 (PS2 0014ff28, row 1393, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047ef0 | EKickAIElement::scalar_deleting_destructor | EKickAIElement::scalar_deleting_destructor |  | ghidra | EKickAIElement slot 0 (PS2 00150080, row 1399, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047f10 | EKillObject::scalar_deleting_destructor | EKillObject::scalar_deleting_destructor |  | ghidra | EKillObject slot 0 (PS2 001509c8, row 1408, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047f30 | EKillSentry::scalar_deleting_destructor | EKillSentry::scalar_deleting_destructor |  | ghidra | EKillSentry slot 0 (PS2 00150b18, row 1411, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047f50 | ELavaHaze::scalar_deleting_destructor | ELavaHaze::scalar_deleting_destructor |  | ghidra | ELavaHaze slot 0 (PS2 00150c20, row 1414, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047f70 | ELetterBoxOff::scalar_deleting_destructor | ELetterBoxOff::scalar_deleting_destructor |  | ghidra | ELetterBoxOff slot 0 (PS2 00150ce8, row 1417, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047f90 | ELetterBoxOn::scalar_deleting_destructor | ELetterBoxOn::scalar_deleting_destructor |  | ghidra | ELetterBoxOn slot 0 (PS2 00150dc8, row 1420, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047fb0 | EMuzzleFlash::scalar_deleting_destructor | EMuzzleFlash::scalar_deleting_destructor |  | ghidra | EMuzzleFlash slot 0 (PS2 001511e0, row 1426, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047fd0 | ENuclearBlast::scalar_deleting_destructor | ENuclearBlast::scalar_deleting_destructor |  | ghidra | ENuclearBlast slot 0 (PS2 00151408, row 1429, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00047ff0 | EObjectiveAdd::scalar_deleting_destructor | EObjectiveAdd::scalar_deleting_destructor |  | ghidra | EObjectiveAdd slot 0 (PS2 00151538, row 1432, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048010 | EObjectiveDisplayText::scalar_deleting_destructor | EObjectiveDisplayText::scalar_deleting_destructor |  | ghidra | EObjectiveDisplayText slot 0 (PS2 00151618, row 1435, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048030 | EObjectiveFail::scalar_deleting_destructor | EObjectiveFail::scalar_deleting_destructor |  | ghidra | EObjectiveFail slot 0 (PS2 001516f0, row 1438, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048050 | EObjectiveInsert::scalar_deleting_destructor | EObjectiveInsert::scalar_deleting_destructor |  | ghidra | EObjectiveInsert slot 0 (PS2 001517e8, row 1441, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048070 | EObjectivePass::scalar_deleting_destructor | EObjectivePass::scalar_deleting_destructor |  | ghidra | EObjectivePass slot 0 (PS2 001518d0, row 1444, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048090 | EObjectiveSetCurrent::scalar_deleting_destructor | EObjectiveSetCurrent::scalar_deleting_destructor |  | ghidra | EObjectiveSetCurrent slot 0 (PS2 001519c0, row 1447, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000480b0 | EObjectiveSetIncomplete::scalar_deleting_destructor | EObjectiveSetIncomplete::scalar_deleting_destructor |  | ghidra | EObjectiveSetIncomplete slot 0 (PS2 00151aa8, row 1450, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000480d0 | EPathChangeThrottleDynamic::scalar_deleting_destructor | EPathChangeThrottleDynamic::scalar_deleting_destructor |  | ghidra | EPathChangeThrottleDynamic slot 0 (PS2 00151c88, row 1459, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000480f0 | EPathChangeThrottleStatic::scalar_deleting_destructor | EPathChangeThrottleStatic::scalar_deleting_destructor |  | ghidra | EPathChangeThrottleStatic slot 0 (PS2 00151dc0, row 1462, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048110 | EPathMultiplyThrottleDynamic::scalar_deleting_destructor | EPathMultiplyThrottleDynamic::scalar_deleting_destructor |  | ghidra | EPathMultiplyThrottleDynamic slot 0 (PS2 00151f38, row 1465, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048130 | EPathMultiplyThrottleStatic::scalar_deleting_destructor | EPathMultiplyThrottleStatic::scalar_deleting_destructor |  | ghidra | EPathMultiplyThrottleStatic slot 0 (PS2 00152078, row 1468, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048150 | EPathSFX::scalar_deleting_destructor | EPathSFX::scalar_deleting_destructor |  | ghidra | EPathSFX slot 0 (PS2 00152200, row 1471, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048170 | EPathSFXUpdate::~EPathSFXUpdate | EPathSFXUpdate::~EPathSFXUpdate |  | ghidra | called by EPathSFXUpdate::scalar_deleting_destructor (0x0004cda0) and stores EPathSFXUpdate's vtable |
| 000482c0 | EPathSetAccelDelayDynamic::scalar_deleting_destructor | EPathSetAccelDelayDynamic::scalar_deleting_destructor |  | ghidra | EPathSetAccelDelayDynamic slot 0 (PS2 00152770, row 1477, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000482e0 | EPathSetAccelDelayStatic::scalar_deleting_destructor | EPathSetAccelDelayStatic::scalar_deleting_destructor |  | ghidra | EPathSetAccelDelayStatic slot 0 (PS2 00152860, row 1480, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048300 | EPathSetAccelerationDynamic::scalar_deleting_destructor | EPathSetAccelerationDynamic::scalar_deleting_destructor |  | ghidra | EPathSetAccelerationDynamic slot 0 (PS2 00152998, row 1483, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048320 | EPathSetAccelerationStatic::scalar_deleting_destructor | EPathSetAccelerationStatic::scalar_deleting_destructor |  | ghidra | EPathSetAccelerationStatic slot 0 (PS2 00152a88, row 1486, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048340 | EPathSetDesiredThrottleDynamic::scalar_deleting_destructor | EPathSetDesiredThrottleDynamic::scalar_deleting_destructor |  | ghidra | EPathSetDesiredThrottleDynamic slot 0 (PS2 00152bc0, row 1489, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048360 | EPathSetDesiredThrottleStatic::scalar_deleting_destructor | EPathSetDesiredThrottleStatic::scalar_deleting_destructor |  | ghidra | EPathSetDesiredThrottleStatic slot 0 (PS2 00152cb0, row 1492, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048380 | EPathSetRunningDynamic::scalar_deleting_destructor | EPathSetRunningDynamic::scalar_deleting_destructor |  | ghidra | EPathSetRunningDynamic slot 0 (PS2 00152de8, row 1495, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000483a0 | EPathSetRunningStatic::scalar_deleting_destructor | EPathSetRunningStatic::scalar_deleting_destructor |  | ghidra | EPathSetRunningStatic slot 0 (PS2 00152ed8, row 1498, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000483c0 | EPathSetThrottleDynamic::scalar_deleting_destructor | EPathSetThrottleDynamic::scalar_deleting_destructor |  | ghidra | EPathSetThrottleDynamic slot 0 (PS2 00153010, row 1501, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000483e0 | EPathSetThrottleStatic::scalar_deleting_destructor | EPathSetThrottleStatic::scalar_deleting_destructor |  | ghidra | EPathSetThrottleStatic slot 0 (PS2 00153100, row 1504, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048400 | EPathSetTimeDynamic::scalar_deleting_destructor | EPathSetTimeDynamic::scalar_deleting_destructor |  | ghidra | EPathSetTimeDynamic slot 0 (PS2 00153238, row 1507, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048420 | EPathSetTimeStatic::scalar_deleting_destructor | EPathSetTimeStatic::scalar_deleting_destructor |  | ghidra | EPathSetTimeStatic slot 0 (PS2 00153340, row 1510, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048440 | EPause::scalar_deleting_destructor | EPause::scalar_deleting_destructor |  | ghidra | EPause slot 0 (PS2 00153488, row 1513, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048460 | EPlayActorEffect::scalar_deleting_destructor | EPlayActorEffect::scalar_deleting_destructor |  | ghidra | EPlayActorEffect slot 0 (PS2 00153560, row 1516, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048480 | EPlayCameraAnim::scalar_deleting_destructor | EPlayCameraAnim::scalar_deleting_destructor |  | ghidra | EPlayCameraAnim slot 0 (PS2 00153700, row 1519, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000484a0 | EPlayCameraSpline::scalar_deleting_destructor | EPlayCameraSpline::scalar_deleting_destructor |  | ghidra | EPlayCameraSpline slot 0 (PS2 001539a8, row 1522, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000484c0 | EPlayEffect::scalar_deleting_destructor | EPlayEffect::scalar_deleting_destructor |  | ghidra | EPlayEffect slot 0 (PS2 00153f80, row 1528, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000484e0 | EPlayGiottoEffect::scalar_deleting_destructor | EPlayGiottoEffect::scalar_deleting_destructor |  | ghidra | EPlayGiottoEffect slot 0 (PS2 001543e8, row 1531, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048500 | EPlayPOVDeath::scalar_deleting_destructor | EPlayPOVDeath::scalar_deleting_destructor |  | ghidra | EPlayPOVDeath slot 0 (PS2 00154570, row 1534, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048520 | EPlaySound::scalar_deleting_destructor | EPlaySound::scalar_deleting_destructor |  | ghidra | EPlaySound slot 0 (PS2 00154658, row 1537, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048540 | EPlaySoundPos::~EPlaySoundPos | EPlaySoundPos::~EPlaySoundPos |  | ghidra | called by EPlaySoundPos::scalar_deleting_destructor (0x0004cf30) and stores EPlaySoundPos's vtable |
| 00048620 | EPlaySystemAnim::scalar_deleting_destructor | EPlaySystemAnim::scalar_deleting_destructor |  | ghidra | EPlaySystemAnim slot 0 (PS2 00154ad8, row 1543, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048640 | EPowerUp::scalar_deleting_destructor | EPowerUp::scalar_deleting_destructor |  | ghidra | EPowerUp slot 0 (PS2 001554c0, row 1555, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048660 | EPowerUpAmmo::scalar_deleting_destructor | EPowerUpAmmo::scalar_deleting_destructor |  | ghidra | EPowerUpAmmo slot 0 (PS2 001556b8, row 1558, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000486a0 | EPowerUpMultiDamage::scalar_deleting_destructor | EPowerUpMultiDamage::scalar_deleting_destructor |  | ghidra | EPowerUpMultiDamage slot 0 (PS2 00155a70, row 1570, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000486c0 | EPowerUpShield::scalar_deleting_destructor | EPowerUpShield::scalar_deleting_destructor |  | ghidra | EPowerUpShield slot 0 (PS2 00155b20, row 1573, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000486e0 | EPowerUpTimer::scalar_deleting_destructor | EPowerUpTimer::scalar_deleting_destructor |  | ghidra | EPowerUpTimer slot 0 (PS2 00155c40, row 1576, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048720 | EProfileMissionSection::scalar_deleting_destructor | EProfileMissionSection::scalar_deleting_destructor |  | ghidra | EProfileMissionSection slot 0 (PS2 00155dc0, row 1582, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048740 | ERandomExplosion::scalar_deleting_destructor | ERandomExplosion::scalar_deleting_destructor |  | ghidra | ERandomExplosion slot 0 (PS2 00155ea8, row 1585, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048760 | EReleaseStream::scalar_deleting_destructor | EReleaseStream::scalar_deleting_destructor |  | ghidra | EReleaseStream slot 0 (PS2 001560d8, row 1588, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048780 | ERenderFrame::scalar_deleting_destructor | ERenderFrame::scalar_deleting_destructor |  | sheet | ERenderFrame slot 0 (PS2 001561b8, row 1591, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000487a0 | EResetAudioMix::scalar_deleting_destructor | EResetAudioMix::scalar_deleting_destructor |  | ghidra | EResetAudioMix slot 0 (PS2 001562c8, row 1594, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000487c0 | ERevertAudioMix::scalar_deleting_destructor | ERevertAudioMix::scalar_deleting_destructor |  | ghidra | ERevertAudioMix slot 0 (PS2 00156a58, row 1609, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000487e0 | ERollSub::scalar_deleting_destructor | ERollSub::scalar_deleting_destructor |  | ghidra | ERollSub slot 0 (PS2 00156b50, row 1612, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048820 | EScaleFieldOfView::scalar_deleting_destructor | EScaleFieldOfView::scalar_deleting_destructor |  | ghidra | EScaleFieldOfView slot 0 (PS2 00156d20, row 1618, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048840 | EScaleFog::scalar_deleting_destructor | EScaleFog::scalar_deleting_destructor |  | ghidra | EScaleFog slot 0 (PS2 00156dd8, row 1621, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048860 | EScheduleEvent::scalar_deleting_destructor | EScheduleEvent::scalar_deleting_destructor |  | sheet | EScheduleEvent slot 0 (PS2 00156ea8, row 1624, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048880 | ESentryMuzzleFlash::scalar_deleting_destructor | ESentryMuzzleFlash::scalar_deleting_destructor |  | ghidra | ESentryMuzzleFlash slot 0 (PS2 00156fb0, row 1627, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000488a0 | ESetAFXMode::scalar_deleting_destructor | ESetAFXMode::scalar_deleting_destructor |  | ghidra | ESetAFXMode slot 0 (PS2 00157130, row 1630, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000488c0 | ESetAreaBrightness::scalar_deleting_destructor | ESetAreaBrightness::scalar_deleting_destructor |  | ghidra | ESetAreaBrightness slot 0 (PS2 00157208, row 1633, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000488e0 | ESetAudioMix::scalar_deleting_destructor | ESetAudioMix::scalar_deleting_destructor |  | ghidra | ESetAudioMix slot 0 (PS2 00157308, row 1636, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048900 | ESetAutoDriveCamOff::scalar_deleting_destructor | ESetAutoDriveCamOff::scalar_deleting_destructor |  | ghidra | ESetAutoDriveCamOff slot 0 (PS2 00157460, row 1639, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048920 | ESetAutoDriveCamOn::scalar_deleting_destructor | ESetAutoDriveCamOn::scalar_deleting_destructor |  | ghidra | ESetAutoDriveCamOn slot 0 (PS2 001576e8, row 1645, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048940 | ESetAutoDriveCamOnMovie::scalar_deleting_destructor | ESetAutoDriveCamOnMovie::scalar_deleting_destructor |  | ghidra | ESetAutoDriveCamOnMovie slot 0 (PS2 00157838, row 1648, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048960 | ESetAvoidZone::scalar_deleting_destructor | ESetAvoidZone::scalar_deleting_destructor |  | ghidra | ESetAvoidZone slot 0 (PS2 001579b8, row 1651, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048980 | ESetBitMagicCounter::scalar_deleting_destructor | ESetBitMagicCounter::scalar_deleting_destructor |  | ghidra | ESetBitMagicCounter slot 0 (PS2 00157af8, row 1654, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000489a0 | ESetCollisionGeometry::scalar_deleting_destructor | ESetCollisionGeometry::scalar_deleting_destructor |  | ghidra | ESetCollisionGeometry slot 0 (PS2 00157be0, row 1657, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000489e0 | ESetFrameAnimationTime::scalar_deleting_destructor | ESetFrameAnimationTime::scalar_deleting_destructor |  | ghidra | ESetFrameAnimationTime slot 0 (PS2 00157f78, row 1666, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048a20 | ESetMagicCounter::scalar_deleting_destructor | ESetMagicCounter::scalar_deleting_destructor |  | ghidra | ESetMagicCounter slot 0 (PS2 001580f8, row 1672, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048a40 | ESetMagicCounterThreshold::scalar_deleting_destructor | ESetMagicCounterThreshold::scalar_deleting_destructor |  | ghidra | ESetMagicCounterThreshold slot 0 (PS2 001581c8, row 1675, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048a60 | ESetMissionMessage::scalar_deleting_destructor | ESetMissionMessage::scalar_deleting_destructor |  | ghidra | ESetMissionMessage slot 0 (PS2 00158298, row 1678, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048a80 | ESetNextPath::scalar_deleting_destructor | ESetNextPath::scalar_deleting_destructor |  | ghidra | ESetNextPath slot 0 (PS2 00158508, row 1684, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048aa0 | ESetObjective::scalar_deleting_destructor | ESetObjective::scalar_deleting_destructor |  | ghidra | ESetObjective slot 0 (PS2 00158648, row 1687, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ac0 | ESetRecordedGameTime::scalar_deleting_destructor | ESetRecordedGameTime::scalar_deleting_destructor |  | ghidra | ESetRecordedGameTime slot 0 (PS2 00158938, row 1693, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ae0 | ESetReflectiveObject::scalar_deleting_destructor | ESetReflectiveObject::scalar_deleting_destructor |  | ghidra | ESetReflectiveObject slot 0 (PS2 001589f0, row 1696, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b00 | ESetRenderVariation::scalar_deleting_destructor | ESetRenderVariation::scalar_deleting_destructor |  | ghidra | ESetRenderVariation slot 0 (PS2 00158b88, row 1699, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b20 | ESetSimRate::scalar_deleting_destructor | ESetSimRate::scalar_deleting_destructor |  | ghidra | ESetSimRate slot 0 (PS2 00158d28, row 1702, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b40 | ESetSpecialWeapon::scalar_deleting_destructor | ESetSpecialWeapon::scalar_deleting_destructor |  | ghidra | ESetSpecialWeapon slot 0 (PS2 00158df0, row 1705, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b60 | ESetSubtitle::scalar_deleting_destructor | ESetSubtitle::scalar_deleting_destructor |  | ghidra | ESetSubtitle slot 0 (PS2 00158fc0, row 1711, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048b80 | ESetTimer::scalar_deleting_destructor | ESetTimer::scalar_deleting_destructor |  | ghidra | ESetTimer slot 0 (PS2 001591f8, row 1717, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ba0 | ESetVideo::scalar_deleting_destructor | ESetVideo::scalar_deleting_destructor |  | ghidra | ESetVideo slot 0 (PS2 001592d8, row 1720, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048bc0 | ESetWaypoint::scalar_deleting_destructor | ESetWaypoint::scalar_deleting_destructor |  | ghidra | ESetWaypoint slot 0 (PS2 00159840, row 1726, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048be0 | EShowInstance::scalar_deleting_destructor | EShowInstance::scalar_deleting_destructor |  | ghidra | EShowInstance slot 0 (PS2 00159a88, row 1732, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048c00 | EShowInstanceStatic::scalar_deleting_destructor | EShowInstanceStatic::scalar_deleting_destructor |  | ghidra | EShowInstanceStatic slot 0 (PS2 00159b38, row 1735, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048c20 | ESimEndFrame::scalar_deleting_destructor | ESimEndFrame::scalar_deleting_destructor |  | ghidra | ESimEndFrame slot 0 (PS2 00159c68, row 1738, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048c40 | ESpawnDramaticSmackable::scalar_deleting_destructor | ESpawnDramaticSmackable::scalar_deleting_destructor |  | ghidra | ESpawnDramaticSmackable slot 0 (PS2 00159ec8, row 1744, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048c60 | ESpawnExplosionStatic::scalar_deleting_destructor | ESpawnExplosionStatic::scalar_deleting_destructor |  | ghidra | ESpawnExplosionStatic slot 0 (PS2 0015b010, row 1750, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048c80 | ESpawnForceEffect::scalar_deleting_destructor | ESpawnForceEffect::scalar_deleting_destructor |  | ghidra | ESpawnForceEffect slot 0 (PS2 0015b2f8, row 1753, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ca0 | ESpawnSentry::scalar_deleting_destructor | ESpawnSentry::scalar_deleting_destructor |  | ghidra | ESpawnSentry slot 0 (PS2 0015b900, row 1756, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048cc0 | ESpawnSimplePhysics::scalar_deleting_destructor | ESpawnSimplePhysics::scalar_deleting_destructor |  | ghidra | ESpawnSimplePhysics slot 0 (PS2 0015bae0, row 1759, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048d00 | ESpawnTraffic::scalar_deleting_destructor | ESpawnTraffic::scalar_deleting_destructor |  | ghidra | ESpawnTraffic slot 0 (PS2 0015c080, row 1765, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048d20 | EStartEffect::scalar_deleting_destructor | EStartEffect::scalar_deleting_destructor |  | ghidra | EStartEffect slot 0 (PS2 0015c520, row 1771, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048d40 | EStopEffect::scalar_deleting_destructor | EStopEffect::scalar_deleting_destructor |  | ghidra | EStopEffect slot 0 (PS2 0015cb80, row 1783, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048d60 | EStopSoundPos::scalar_deleting_destructor | EStopSoundPos::scalar_deleting_destructor |  | ghidra | EStopSoundPos slot 0 (PS2 0015cd90, row 1786, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048d80 | EStopTimedAction::scalar_deleting_destructor | EStopTimedAction::scalar_deleting_destructor |  | ghidra | EStopTimedAction slot 0 (PS2 0015ce68, row 1789, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048da0 | ESuppressEffect::scalar_deleting_destructor | ESuppressEffect::scalar_deleting_destructor |  | ghidra | ESuppressEffect slot 0 (PS2 0015d320, row 1798, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048dc0 | ESwitchEffectOff::scalar_deleting_destructor | ESwitchEffectOff::scalar_deleting_destructor |  | ghidra | ESwitchEffectOff slot 0 (PS2 0015d538, row 1801, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048de0 | ESwitchEffectOn::scalar_deleting_destructor | ESwitchEffectOn::scalar_deleting_destructor |  | ghidra | ESwitchEffectOn slot 0 (PS2 0015d650, row 1804, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048e00 | ESwitchStingerChannel::scalar_deleting_destructor | ESwitchStingerChannel::scalar_deleting_destructor |  | ghidra | ESwitchStingerChannel slot 0 (PS2 0015d760, row 1807, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048e20 | ETargetBeaconOnOff::scalar_deleting_destructor | ETargetBeaconOnOff::scalar_deleting_destructor |  | ghidra | ETargetBeaconOnOff slot 0 (PS2 0015d888, row 1810, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048e40 | ETimerDrawOff::scalar_deleting_destructor | ETimerDrawOff::scalar_deleting_destructor |  | ghidra | ETimerDrawOff slot 0 (PS2 0015d9f8, row 1813, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048e60 | ETimerDrawOn::scalar_deleting_destructor | ETimerDrawOn::scalar_deleting_destructor |  | ghidra | ETimerDrawOn slot 0 (PS2 0015dab8, row 1816, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048e80 | EUnloadWeapon::scalar_deleting_destructor | EUnloadWeapon::scalar_deleting_destructor |  | ghidra | EUnloadWeapon slot 0 (PS2 0015dcf8, row 1825, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ea0 | EWakeupSmackable::scalar_deleting_destructor | EWakeupSmackable::scalar_deleting_destructor |  | ghidra | EWakeupSmackable slot 0 (PS2 0015de48, row 1828, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00048ec0 | EAICommand::~EAICommand | EAICommand::~EAICommand |  | ghidra | called by EAICommand::scalar_deleting_destructor (0x0004ea60) and stores EAICommand's vtable |
| 000495d0 | EActivateAIElement::~EActivateAIElement | EActivateAIElement::~EActivateAIElement |  | ghidra | called by EActivateAIElement::scalar_deleting_destructor (0x0004ea80) and stores EActivateAIElement's vtable |
| 00049790 | EAudioUpdate::~EAudioUpdate | EAudioUpdate::~EAudioUpdate |  | sheet | called by EAudioUpdate::scalar_deleting_destructor (0x0004eaa0) and stores EAudioUpdate's vtable |
| 00049a80 | EAutoDriveSpeedChange::~EAutoDriveSpeedChange | EAutoDriveSpeedChange::~EAutoDriveSpeedChange |  | ghidra | called by EAutoDriveSpeedChange::scalar_deleting_destructor (0x0004eac0) and stores EAutoDriveSpeedChange's vtable |
| 00049ae0 | EAutoDriveSteering::~EAutoDriveSteering | EAutoDriveSteering::~EAutoDriveSteering |  | ghidra | called by EAutoDriveSteering::scalar_deleting_destructor (0x0004eae0) and stores EAutoDriveSteering's vtable |
| 00049b90 | EBashCarAlongObjectAxis::~EBashCarAlongObjectAxis | EBashCarAlongObjectAxis::~EBashCarAlongObjectAxis |  | ghidra | called by EBashCarAlongObjectAxis::scalar_deleting_destructor (0x0004eb00) and stores EBashCarAlongObjectAxis's vtable |
| 00049d40 | ECinematicCamera::~ECinematicCamera | ECinematicCamera::~ECinematicCamera |  | ghidra | called by ECinematicCamera::scalar_deleting_destructor (0x0004eb20) and stores ECinematicCamera's vtable |
| 00049eb0 | ECollision::~ECollision | ECollision::~ECollision |  | ghidra | called by ECollision::scalar_deleting_destructor (0x0004eb40) and stores ECollision's vtable |
| 0004a820 | EControlToCPU::~EControlToCPU | EControlToCPU::~EControlToCPU |  | ghidra | called by EControlToCPU::scalar_deleting_destructor (0x0004eb60) and stores EControlToCPU's vtable |
| 0004a890 | EControlToPlayer::~EControlToPlayer | EControlToPlayer::~EControlToPlayer |  | ghidra | called by EControlToPlayer::scalar_deleting_destructor (0x0004eb80) and stores EControlToPlayer's vtable |
| 0004a920 | EControlToSpline::~EControlToSpline | EControlToSpline::~EControlToSpline |  | ghidra | called by EControlToSpline::scalar_deleting_destructor (0x0004eba0) and stores EControlToSpline's vtable |
| 0004a9d0 | ECustomTransition::~ECustomTransition | ECustomTransition::~ECustomTransition |  | ghidra | called by ECustomTransition::scalar_deleting_destructor (0x0004ebc0) and stores ECustomTransition's vtable |
| 0004aad0 | EDamagePlayer::~EDamagePlayer | EDamagePlayer::~EDamagePlayer |  | ghidra | called by EDamagePlayer::scalar_deleting_destructor (0x0004ebe0) and stores EDamagePlayer's vtable |
| 0004ab60 | EEndCarStop::~EEndCarStop | EEndCarStop::~EEndCarStop |  | ghidra | called by EEndCarStop::scalar_deleting_destructor (0x0004ec00) and stores EEndCarStop's vtable |
| 0004abc0 | EEndControlToSpline::~EEndControlToSpline | EEndControlToSpline::~EEndControlToSpline |  | ghidra | called by EEndControlToSpline::scalar_deleting_destructor (0x0004ec20) and stores EEndControlToSpline's vtable |
| 0004ac40 | EFireWeapon::~EFireWeapon | EFireWeapon::~EFireWeapon |  | ghidra | called by EFireWeapon::scalar_deleting_destructor (0x0004ec40) and stores EFireWeapon's vtable |
| 0004c770 | EForceCarStop::~EForceCarStop | EForceCarStop::~EForceCarStop |  | ghidra | called by EForceCarStop::scalar_deleting_destructor (0x0004ec60) and stores EForceCarStop's vtable |
| 0004c7d0 | EHitWindow::scalar_deleting_destructor | EHitWindow::scalar_deleting_destructor |  | ghidra | EHitWindow slot 0 (PS2 0014f480, row 1381, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004c7f0 | EKickObject::~EKickObject | EKickObject::~EKickObject |  | ghidra | called by EKickObject::scalar_deleting_destructor (0x0004ec80) and stores EKickObject's vtable |
| 0004ca20 | EKillAIElement::~EKillAIElement | EKillAIElement::~EKillAIElement |  | ghidra | called by EKillAIElement::scalar_deleting_destructor (0x0004eca0) and stores EKillAIElement's vtable |
| 0004cbc0 | EMaybeActivateAIElement::~EMaybeActivateAIElement | EMaybeActivateAIElement::~EMaybeActivateAIElement |  | ghidra | called by EMaybeActivateAIElement::scalar_deleting_destructor (0x0004ecc0) and stores EMaybeActivateAIElement's vtable |
| 0004cda0 | EPathSFXUpdate::scalar_deleting_destructor | EPathSFXUpdate::scalar_deleting_destructor |  | ghidra | EPathSFXUpdate slot 0 (PS2 00152440, row 1474, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004cdc0 | EPlayCarCameraAnim::~EPlayCarCameraAnim | EPlayCarCameraAnim::~EPlayCarCameraAnim |  | ghidra | called by EPlayCarCameraAnim::scalar_deleting_destructor (0x0004ece0) and stores EPlayCarCameraAnim's vtable |
| 0004cf30 | EPlaySoundPos::scalar_deleting_destructor | EPlaySoundPos::scalar_deleting_destructor |  | ghidra | EPlaySoundPos slot 0 (PS2 00154798, row 1540, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004d1e0 | EPlayerSetImmunity::~EPlayerSetImmunity | EPlayerSetImmunity::~EPlayerSetImmunity |  | ghidra | called by EPlayerSetImmunity::scalar_deleting_destructor (0x0004ed20) and stores EPlayerSetImmunity's vtable |
| 0004d240 | EPlayerWin::~EPlayerWin | EPlayerWin::~EPlayerWin |  | ghidra | called by EPlayerWin::scalar_deleting_destructor (0x0004ed40) and stores EPlayerWin's vtable |
| 0004d3d0 | EPowerUpHealth::~EPowerUpHealth | EPowerUpHealth::~EPowerUpHealth |  | ghidra | called by EPowerUpHealth::scalar_deleting_destructor (0x0004ed60) and stores EPowerUpHealth's vtable |
| 0004d460 | EResetPlayerCar::~EResetPlayerCar | EResetPlayerCar::~EResetPlayerCar |  | ghidra | called by EResetPlayerCar::scalar_deleting_destructor (0x0004ed80) and stores EResetPlayerCar's vtable |
| 0004d4c0 | EResetPlayerCarPos::~EResetPlayerCarPos | EResetPlayerCarPos::~EResetPlayerCarPos |  | sheet | called by EResetPlayerCarPos::scalar_deleting_destructor (0x0004eda0) and stores EResetPlayerCarPos's vtable |
| 0004d7c0 | EResume::~EResume | EResume::~EResume |  | ghidra | called by EResume::scalar_deleting_destructor (0x0004ede0) and stores EResume's vtable |
| 0004d850 | ESetAutoDriveCamOffMovie::~ESetAutoDriveCamOffMovie | ESetAutoDriveCamOffMovie::~ESetAutoDriveCamOffMovie |  | ghidra | called by ESetAutoDriveCamOffMovie::scalar_deleting_destructor (0x0004ee00) and stores ESetAutoDriveCamOffMovie's vtable |
| 0004d930 | ESetCreakLevel::~ESetCreakLevel | ESetCreakLevel::~ESetCreakLevel |  | ghidra | called by ESetCreakLevel::scalar_deleting_destructor (0x0004ee20) and stores ESetCreakLevel's vtable |
| 0004d9d0 | ESetNextAISpline::~ESetNextAISpline | ESetNextAISpline::~ESetNextAISpline |  | ghidra | called by ESetNextAISpline::scalar_deleting_destructor (0x0004ee40) and stores ESetNextAISpline's vtable |
| 0004da70 | ESetRecSpeedOffset::~ESetRecSpeedOffset | ESetRecSpeedOffset::~ESetRecSpeedOffset |  | ghidra | called by ESetRecSpeedOffset::scalar_deleting_destructor (0x0004ee60) and stores ESetRecSpeedOffset's vtable |
| 0004db10 | ESetSubRollDirection::~ESetSubRollDirection | ESetSubRollDirection::~ESetSubRollDirection |  | ghidra | called by ESetSubRollDirection::scalar_deleting_destructor (0x0004ee80) and stores ESetSubRollDirection's vtable |
| 0004db70 | ESetTerrain::~ESetTerrain | ESetTerrain::~ESetTerrain |  | ghidra | called by ESetTerrain::scalar_deleting_destructor (0x0004eea0) and stores ESetTerrain's vtable |
| 0004dbd0 | ESetVisualDamage::~ESetVisualDamage | ESetVisualDamage::~ESetVisualDamage |  | ghidra | called by ESetVisualDamage::scalar_deleting_destructor (0x0004eec0) and stores ESetVisualDamage's vtable |
| 0004dcb0 | EShake::~EShake | EShake::~EShake |  | ghidra | called by EShake::scalar_deleting_destructor (0x0004eee0) and stores EShake's vtable |
| 0004dd10 | ESimFrameUpdate::~ESimFrameUpdate | ESimFrameUpdate::~ESimFrameUpdate |  | sheet | called by ESimFrameUpdate::scalar_deleting_destructor (0x0004ef00) and stores ESimFrameUpdate's vtable |
| 0004de70 | ESpawnExplosion::~ESpawnExplosion | ESpawnExplosion::~ESpawnExplosion |  | ghidra | called by ESpawnExplosion::scalar_deleting_destructor (0x0004ef20) and stores ESpawnExplosion's vtable |
| 0004e860 | EStartMission::~EStartMission | EStartMission::~EStartMission |  | ghidra | called by EStartMission::scalar_deleting_destructor (0x0004ef40) and stores EStartMission's vtable |
| 0004e9c0 | ETwoWheelsOff::~ETwoWheelsOff | ETwoWheelsOff::~ETwoWheelsOff |  | ghidra | called by ETwoWheelsOff::scalar_deleting_destructor (0x0004f110) and stores ETwoWheelsOff's vtable |
| 0004ea10 | ETwoWheelsOn::~ETwoWheelsOn | ETwoWheelsOn::~ETwoWheelsOn |  | ghidra | called by ETwoWheelsOn::scalar_deleting_destructor (0x0004f130) and stores ETwoWheelsOn's vtable |
| 0004ea60 | EAICommand::scalar_deleting_destructor | EAICommand::scalar_deleting_destructor |  | ghidra | EAICommand slot 0 (PS2 00145ff8, row 1174, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ea80 | EActivateAIElement::scalar_deleting_destructor | EActivateAIElement::scalar_deleting_destructor |  | ghidra | EActivateAIElement slot 0 (PS2 001476b8, row 1204, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eaa0 | EAudioUpdate::scalar_deleting_destructor | EAudioUpdate::scalar_deleting_destructor |  | sheet | EAudioUpdate slot 0 (PS2 00147e30, row 1222, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eac0 | EAutoDriveSpeedChange::scalar_deleting_destructor | EAutoDriveSpeedChange::scalar_deleting_destructor |  | ghidra | EAutoDriveSpeedChange slot 0 (PS2 001482f8, row 1228, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eae0 | EAutoDriveSteering::scalar_deleting_destructor | EAutoDriveSteering::scalar_deleting_destructor |  | ghidra | EAutoDriveSteering slot 0 (PS2 001483d0, row 1231, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eb00 | EBashCarAlongObjectAxis::scalar_deleting_destructor | EBashCarAlongObjectAxis::scalar_deleting_destructor |  | ghidra | EBashCarAlongObjectAxis slot 0 (PS2 001486d8, row 1240, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eb20 | ECinematicCamera::scalar_deleting_destructor | ECinematicCamera::scalar_deleting_destructor |  | ghidra | ECinematicCamera slot 0 (PS2 00149910, row 1279, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eb40 | ECollision::scalar_deleting_destructor | ECollision::scalar_deleting_destructor |  | ghidra | ECollision slot 0 (PS2 00149ed8, row 1288, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eb60 | EControlToCPU::scalar_deleting_destructor | EControlToCPU::scalar_deleting_destructor |  | ghidra | EControlToCPU slot 0 (PS2 0014ac38, row 1291, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eb80 | EControlToPlayer::scalar_deleting_destructor | EControlToPlayer::scalar_deleting_destructor |  | ghidra | EControlToPlayer slot 0 (PS2 0014ad18, row 1294, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eba0 | EControlToSpline::scalar_deleting_destructor | EControlToSpline::scalar_deleting_destructor |  | ghidra | EControlToSpline slot 0 (PS2 0014ae60, row 1297, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ebc0 | ECustomTransition::scalar_deleting_destructor | ECustomTransition::scalar_deleting_destructor |  | ghidra | ECustomTransition slot 0 (PS2 0014b020, row 1300, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ebe0 | EDamagePlayer::scalar_deleting_destructor | EDamagePlayer::scalar_deleting_destructor |  | ghidra | EDamagePlayer slot 0 (PS2 0014b3e0, row 1303, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ec00 | EEndCarStop::scalar_deleting_destructor | EEndCarStop::scalar_deleting_destructor |  | ghidra | EEndCarStop slot 0 (PS2 0014c048, row 1333, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ec20 | EEndControlToSpline::scalar_deleting_destructor | EEndControlToSpline::scalar_deleting_destructor |  | ghidra | EEndControlToSpline slot 0 (PS2 0014c110, row 1336, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ec40 | EFireWeapon::scalar_deleting_destructor | EFireWeapon::scalar_deleting_destructor |  | ghidra | EFireWeapon slot 0 (PS2 0014ca60, row 1360, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ec60 | EForceCarStop::scalar_deleting_destructor | EForceCarStop::scalar_deleting_destructor |  | ghidra | EForceCarStop slot 0 (PS2 0014ede0, row 1363, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ec80 | EKickObject::scalar_deleting_destructor | EKickObject::scalar_deleting_destructor |  | ghidra | EKickObject slot 0 (PS2 00150390, row 1402, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eca0 | EKillAIElement::scalar_deleting_destructor | EKillAIElement::scalar_deleting_destructor |  | ghidra | EKillAIElement slot 0 (PS2 00150748, row 1405, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ecc0 | EMaybeActivateAIElement::scalar_deleting_destructor | EMaybeActivateAIElement::scalar_deleting_destructor |  | ghidra | EMaybeActivateAIElement slot 0 (PS2 00150ec8, row 1423, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ece0 | EPlayCarCameraAnim::scalar_deleting_destructor | EPlayCarCameraAnim::scalar_deleting_destructor |  | ghidra | EPlayCarCameraAnim slot 0 (PS2 00153c48, row 1525, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ed20 | EPlayerSetImmunity::scalar_deleting_destructor | EPlayerSetImmunity::scalar_deleting_destructor |  | ghidra | EPlayerSetImmunity slot 0 (PS2 001550d8, row 1549, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ed40 | EPlayerWin::scalar_deleting_destructor | EPlayerWin::scalar_deleting_destructor |  | ghidra | EPlayerWin slot 0 (PS2 001551b8, row 1552, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ed60 | EPowerUpHealth::scalar_deleting_destructor | EPowerUpHealth::scalar_deleting_destructor |  | ghidra | EPowerUpHealth slot 0 (PS2 001558d0, row 1564, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ed80 | EResetPlayerCar::scalar_deleting_destructor | EResetPlayerCar::scalar_deleting_destructor |  | ghidra | EResetPlayerCar slot 0 (PS2 00156390, row 1597, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eda0 | EResetPlayerCarPos::scalar_deleting_destructor | EResetPlayerCarPos::scalar_deleting_destructor |  | sheet | EResetPlayerCarPos slot 0 (PS2 00156460, row 1600, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ede0 | EResume::scalar_deleting_destructor | EResume::scalar_deleting_destructor |  | ghidra | EResume slot 0 (PS2 001568f0, row 1606, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ee00 | ESetAutoDriveCamOffMovie::scalar_deleting_destructor | ESetAutoDriveCamOffMovie::scalar_deleting_destructor |  | ghidra | ESetAutoDriveCamOffMovie slot 0 (PS2 00157540, row 1642, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ee20 | ESetCreakLevel::scalar_deleting_destructor | ESetCreakLevel::scalar_deleting_destructor |  | ghidra | ESetCreakLevel slot 0 (PS2 00157cf8, row 1660, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ee40 | ESetNextAISpline::scalar_deleting_destructor | ESetNextAISpline::scalar_deleting_destructor |  | ghidra | ESetNextAISpline slot 0 (PS2 00158370, row 1681, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ee60 | ESetRecSpeedOffset::scalar_deleting_destructor | ESetRecSpeedOffset::scalar_deleting_destructor |  | ghidra | ESetRecSpeedOffset slot 0 (PS2 001587f8, row 1690, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ee80 | ESetSubRollDirection::scalar_deleting_destructor | ESetSubRollDirection::scalar_deleting_destructor |  | ghidra | ESetSubRollDirection slot 0 (PS2 00158ee0, row 1708, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eea0 | ESetTerrain::scalar_deleting_destructor | ESetTerrain::scalar_deleting_destructor |  | ghidra | ESetTerrain slot 0 (PS2 001590b8, row 1714, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eec0 | ESetVisualDamage::scalar_deleting_destructor | ESetVisualDamage::scalar_deleting_destructor |  | ghidra | ESetVisualDamage slot 0 (PS2 00159660, row 1723, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004eee0 | EShake::scalar_deleting_destructor | EShake::scalar_deleting_destructor |  | ghidra | EShake slot 0 (PS2 001599c8, row 1729, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ef00 | ESimFrameUpdate::scalar_deleting_destructor | ESimFrameUpdate::scalar_deleting_destructor |  | sheet | ESimFrameUpdate slot 0 (PS2 00159d10, row 1741, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ef20 | ESpawnExplosion::scalar_deleting_destructor | ESpawnExplosion::scalar_deleting_destructor |  | ghidra | ESpawnExplosion slot 0 (PS2 0015a098, row 1747, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ef40 | EStartMission::scalar_deleting_destructor | EStartMission::scalar_deleting_destructor |  | ghidra | EStartMission slot 0 (PS2 0015c738, row 1774, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004ef60 | EStreamEvent::~EStreamEvent | EStreamEvent::~EStreamEvent |  | ghidra | called by EStreamEvent::scalar_deleting_destructor (0x0004f150) and stores EStreamEvent's vtable |
| 0004f110 | ETwoWheelsOff::scalar_deleting_destructor | ETwoWheelsOff::scalar_deleting_destructor |  | ghidra | ETwoWheelsOff slot 0 (PS2 0015db78, row 1819, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004f130 | ETwoWheelsOn::scalar_deleting_destructor | ETwoWheelsOn::scalar_deleting_destructor |  | ghidra | ETwoWheelsOn slot 0 (PS2 0015dc38, row 1822, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0004f150 | EStreamEvent::scalar_deleting_destructor | EStreamEvent::scalar_deleting_destructor |  | ghidra | EStreamEvent slot 0 (PS2 0015d000, row 1795, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00050e40 | DeviceHasChanged | InputDevice::DeviceHasChanged |  | ghidra | InputDevice slot 4 (PS2 00167bc8, row 2125, ghidra) |
| 00050eb0 | scalar_deleting_destructor | InputDevice::scalar_deleting_destructor |  | sheet | InputDevice slot 0 (PS2 00167b98, row 2124, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000592b0 | AttributeSystem::Kill | AttributeSystem::Kill |  | sheet | AttributeSystem slot 2 (PS2 00173920, row 2289, sheet) |
| 000592d0 | AttributeSystem::~AttributeSystem | AttributeSystem::~AttributeSystem |  | sheet | called by AttributeSystem::scalar_deleting_destructor (0x00059530) and stores AttributeSystem's vtable |
| 00059530 | AttributeSystem::scalar_deleting_destructor | AttributeSystem::scalar_deleting_destructor |  | sheet | AttributeSystem slot 0 (PS2 0016c880, row 2211, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005d4f0 | ~Grenade | Grenade::~Grenade |  | sheet | called by Grenade::scalar_deleting_destructor (0x0005d7a0) and stores Grenade's vtable |
| 0005d7a0 | scalar_deleting_destructor | Grenade::scalar_deleting_destructor |  | sheet | Grenade slot 0 (PS2 0017e7b0, row 2469, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005d7d0 | Grenade::Simulate | Grenade::Simulate |  | sheet | Grenade slot 4 (PS2 0017e8b8, row 2471, sheet) |
| 0005dc60 | Human::~Human | Human::~Human |  | sheet | called by Human::scalar_deleting_destructor (0x0005dea0) and stores Human's vtable |
| 0005dd00 | Human::Simulate | Human::Simulate |  | sheet | Human slot 4 (PS2 0017f980, row 2486, sheet) |
| 0005dea0 | Human::scalar_deleting_destructor | Human::scalar_deleting_destructor |  | sheet | Human slot 0 (PS2 0017f4c0, row 2481, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005dfa0 | Human::ApplyDamage | Human::ApplyDamage |  | sheet | Human slot 2 (PS2 0017f6c0, row 2485, sheet) |
| 0005e180 | Mine::~Mine | Mine::~Mine |  | sheet | called by Mine::scalar_deleting_destructor (0x0005e6e0) and stores Mine's vtable |
| 0005e3e0 | ApplyDamage | Mine::ApplyDamage |  | ghidra | Mine slot 2 (PS2 001805f8, row 2500, ghidra) |
| 0005e6e0 | scalar_deleting_destructor | Mine::scalar_deleting_destructor |  | sheet | Mine slot 0 (PS2 0017ff48, row 2497, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0005e710 | Mine::Simulate | Mine::Simulate |  | ghidra | Mine slot 4 (PS2 0017ffc8, row 2498, ghidra) |
| 0005f350 | Missile::~Missile | Missile::~Missile |  | sheet | called by Missile::scalar_deleting_destructor (0x0005f6a0) and stores Missile's vtable |
| 0005f6a0 | Missile::scalar_deleting_destructor | Missile::scalar_deleting_destructor |  | sheet | Missile slot 0 (PS2 001818c8, row 2510, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00060a30 | Missile::Simulate | Missile::Simulate |  | sheet | Missile slot 4 (PS2 00181c80, row 2512, sheet) |
| 00060b60 | Newton::~Newton | Newton::~Newton |  | ghidra | called by Newton::scalar_deleting_destructor (0x000612f0) and stores Newton's vtable |
| 00060b70 | Newton::Simulate | Newton::Simulate |  | sheet | Newton slot 4 (PS2 00184098, row 2530, sheet) |
| 00061050 | ApplyDamage | Newton::ApplyDamage |  | ghidra | Newton slot 2 (PS2 001847e0, row 2531, ghidra) |
| 000612f0 | Newton::scalar_deleting_destructor | Newton::scalar_deleting_destructor |  | ghidra | Newton slot 0 (PS2 00184040, row 2529, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006dd10 | GetDamageZones | PHelicopter::GetDamageZones |  | ghidra | PHelicopter slot 3 (PS2 001942d8, row 2666, ghidra) |
| 0006dd30 | PHelicopter::Simulate | PHelicopter::Simulate |  | resolved | PHelicopter slot 4 (PS2 001942e8, row 2667, resolved) |
| 0006e290 | PHelicopter::~PHelicopter | PHelicopter::~PHelicopter |  | ghidra | called by PHelicopter::scalar_deleting_destructor (0x0006e300) and stores PHelicopter's vtable |
| 0006e300 | PHelicopter::scalar_deleting_destructor | PHelicopter::scalar_deleting_destructor |  | ghidra | PHelicopter slot 0 (PS2 001937b0, row 2663, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006e330 | PHelicopter::ApplyDamage | PHelicopter::ApplyDamage |  | ghidra | PHelicopter slot 2 (PS2 00193938, row 2665, ghidra) |
| 0006ed40 | PhysicsNamespace::NameLookup | PhysicsNamespace::NameLookup |  | sheet | PhysicsNamespace slot 0 (PS2 001950a0, row 2696, sheet) |
| 0006f050 | PhysicsNamespace::scalar_deleting_destructor | PhysicsNamespace::scalar_deleting_destructor |  | sheet | PhysicsNamespace slot 1 (PS2 00195070, row 2695, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0006f3e0 | PhysicsObject::GetDamageZones | PhysicsObject::GetDamageZones |  | ghidra | Grenade slot 3 (PS2 00195960, row 2712, ghidra); Human slot 3 (PS2 00195960, row 2712, ghidra); Mine slot 3 (PS2 00195960, row 2712, ghidra) ... |
| 0006f660 | PhysicsObject::DebugObject | PhysicsObject::DebugObject |  | sheet | Grenade slot 5 (PS2 00195d28, row 2728, sheet); Human slot 5 (PS2 00195d28, row 2728, sheet); Mine slot 5 (PS2 00195d28, row 2728, sheet) ... |
| 0006f680 | PhysicsObject::~PhysicsObject | PhysicsObject::~PhysicsObject |  | ghidra | called by PhysicsObject::scalar_deleting_destructor (0x0006f7f0) and stores PhysicsObject's vtable |
| 0006f780 | PhysicsObject::ApplyDamage | PhysicsObject::ApplyDamage |  | sheet | Grenade slot 2 (PS2 00195940, row 2711, sheet); Missile slot 2 (PS2 00195940, row 2711, sheet); PhysicsObject slot 2 (PS2 00195940, row 2711, sheet) ... |
| 0006f7f0 | PhysicsObject::scalar_deleting_destructor | PhysicsObject::scalar_deleting_destructor |  | ghidra | PhysicsObject slot 0 (PS2 00195568, row 2703, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00072160 | scalar_deleting_destructor | RayShell::scalar_deleting_destructor |  | sheet | RayShell slot 0 (PS2 00199200, row 2779, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00073150 | Sentry::~Sentry | Sentry::~Sentry |  | sheet | called by Sentry::scalar_deleting_destructor (0x000735d0) and stores Sentry's vtable |
| 000735d0 | Sentry::scalar_deleting_destructor | Sentry::scalar_deleting_destructor |  | sheet | Sentry slot 0 (PS2 0019b520, row 2807, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00073f80 | Sentry::ApplyDamage | Sentry::ApplyDamage |  | ghidra | Sentry slot 2 (PS2 0019b5a8, row 2808, ghidra) |
| 000744d0 | Sentry::Simulate | Sentry::Simulate |  | sheet | Sentry slot 4 (PS2 0019b9d0, row 2809, sheet) |
| 00074660 | ~Shell | Shell::~Shell |  | sheet | called by Shell::scalar_deleting_destructor (0x00074980) and stores Shell's vtable |
| 00074980 | scalar_deleting_destructor | Shell::scalar_deleting_destructor |  | sheet | Shell slot 0 (PS2 0019d028, row 2826, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00074e80 | Shell::Simulate | Shell::Simulate |  | sheet | Shell slot 4 (PS2 0019d080, row 2827, sheet) |
| 00075300 | Smackable::~Smackable | Smackable::~Smackable |  | sheet | called by Smackable::scalar_deleting_destructor (0x00075530) and stores Smackable's vtable |
| 00075420 | Smackable::Simulate | Smackable::Simulate |  | sheet | Smackable slot 4 (PS2 0019e718, row 2840, sheet) |
| 00075530 | Smackable::scalar_deleting_destructor | Smackable::scalar_deleting_destructor |  | sheet | Smackable slot 0 (PS2 0019e590, row 2838, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00075560 | Smackable::ApplyDamage | Smackable::ApplyDamage |  | sheet | Smackable slot 2 (PS2 0019e8c0, row 2841, sheet) |
| 00078470 | RCamera::scalar_deleting_destructor | RCamera::scalar_deleting_destructor |  | sheet | RCamera slot 0 (PS2 001a4638, row 2951, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00078540 | RCamera::SetActive | RCamera::SetActive |  | sheet | RCamera slot 1 (PS2 001a4578, row 2947, sheet); RPlayerCamera slot 1 (PS2 001a4578, row 2947, sheet); RWorldCamera slot 1 (PS2 001a4578, row 2947, sheet) |
| 0007daa0 | RFog::Kill | RFog::Kill |  | sheet | RFog slot 2 (PS2 001acea0, row 3099, sheet) |
| 0007dac0 | RFog::scalar_deleting_destructor | RFog::scalar_deleting_destructor | RColorize::~RColorize, RWater::~RWater, RGlareManager::~RGlareManager | ghidra | RFog slot 0 (PS2 001ac818, row 3087, sheet); RColorize slot 0 (PS2 001d8440, row 3894, sheet); RWater slot 0 (PS2 001f0378, row 4281, ghidra) ... |
| 0007e8c0 | RLightManager::~RLightManager | RLightManager::~RLightManager |  | sheet | called by RLightManager::scalar_deleting_destructor (0x0007f450) and stores RLightManager's vtable |
| 0007e930 | RLightManager::Kill | RLightManager::Kill |  | ghidra | RLightManager slot 2 (PS2 001b04f8, row 3158, ghidra) |
| 0007f450 | RLightManager::scalar_deleting_destructor | RLightManager::scalar_deleting_destructor |  | sheet | RLightManager slot 0 (PS2 001aeba8, row 3128, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00081840 | RPlayerCamera::CameraInputCallback | RPlayerCamera::CameraInputCallback |  | ghidra | RPlayerCamera slot 6 (PS2 001bcc00, row 3298, ghidra) |
| 00081bf0 | RPlayerCamera::~RPlayerCamera | RPlayerCamera::~RPlayerCamera |  | sheet | called by RPlayerCamera::scalar_deleting_destructor (0x00084a30) and stores RPlayerCamera's vtable |
| 00084a30 | RPlayerCamera::scalar_deleting_destructor | RPlayerCamera::scalar_deleting_destructor |  | sheet | RPlayerCamera slot 0 (PS2 001b2728, row 3228, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000877e0 | UpdateCamera | RPlayerCamera::UpdateCamera |  | ghidra | RPlayerCamera slot 3 (PS2 001b29e0, row 3230, ghidra) |
| 00088d70 | RPlayerCamera::RestartCamera | RPlayerCamera::RestartCamera |  | ghidra | RPlayerCamera slot 4 (PS2 001bbc90, row 3283, ghidra) |
| 0008a410 | ~RPlayerViewCamera | RPlayerViewCamera::~RPlayerViewCamera |  | ghidra | called by RPlayerViewCamera::scalar_deleting_destructor (0x0008a4d0) and stores RPlayerViewCamera's vtable |
| 0008a420 | RPlayerViewCamera::ConfigureView | RPlayerViewCamera::ConfigureView |  | resolved | RPlayerViewCamera slot 5 (PS2 001beba0, row 3357, resolved) |
| 0008a4d0 | scalar_deleting_destructor | RPlayerViewCamera::scalar_deleting_destructor |  | ghidra | RPlayerViewCamera slot 0 (PS2 001beb48, row 3356, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b3c0 | RRenderDebugViewScreenSpace::Debug | RRenderDebugViewScreenSpace::Debug |  | sheet | RRenderDebugViewScreenSpace slot 1 (PS2 001c2f78, row 3421, sheet) |
| 0008b4d0 | scalar_deleting_destructor | RRenderDebugViewPerspective::scalar_deleting_destructor |  | ghidra | RRenderDebugViewPerspective slot 0 (PS2 001c3758, row 3424, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b4f0 | RRenderDebugViewPerspective::PreRender | RRenderDebugViewPerspective::PreRender |  | ghidra | RRenderDebugViewPerspective slot 2 (PS2 001c2e08, row 3416, ghidra) |
| 0008b510 | RRenderDebugViewScreenSpace::scalar_deleting_destructor | RRenderDebugViewScreenSpace::scalar_deleting_destructor |  | ghidra | RRenderDebugViewScreenSpace slot 0 (PS2 001c3830, row 3428, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008b530 | RRenderDebugViewScreenSpace::DoRender | RRenderDebugViewScreenSpace::DoRender |  | sheet | RRenderDebugViewScreenSpace slot 4 (PS2 001c2ec0, row 3419, sheet) |
| 0008c950 | RRenderWorldCamera::~RRenderWorldCamera | RRenderWorldCamera::~RRenderWorldCamera |  | ghidra | called by RRenderWorldCamera::scalar_deleting_destructor (0x0008ccf0) and stores RRenderWorldCamera's vtable |
| 0008c990 | RRenderWorldCamera::PreRender | RRenderWorldCamera::PreRender |  | ghidra | RPlayerViewCamera slot 2 (PS2 001c5458, row 3464, ghidra); RRenderWorldCamera slot 2 (PS2 001c5458, row 3464, ghidra) |
| 0008ccf0 | RRenderWorldCamera::scalar_deleting_destructor | RRenderWorldCamera::scalar_deleting_destructor |  | ghidra | RRenderWorldCamera slot 0 (PS2 001c53b8, row 3462, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0008cfa0 | RRenderWorldCamera::DoRender | RRenderWorldCamera::DoRender |  | sheet | RPlayerViewCamera slot 4 (PS2 001c55d8, row 3466, sheet); RRenderWorldCamera slot 4 (PS2 001c55d8, row 3466, sheet) |
| 0008d6d0 | RSceneObj::ResolveObjectData | RSceneObj::ResolveObjectData |  | resolved | RSceneObj slot 18 (PS2 001c6fa8, row 3496, resolved); RSkeletalObj slot 18 (PS2 001c6fa8, row 3496, resolved) |
| 0008dae0 | RSceneObj::SetEventDynamicData | RSceneObj::SetEventDynamicData |  | ghidra | RSceneObj slot 11 (PS2 001c8b08, row 3526, ghidra); RSkeletalObj slot 11 (PS2 001c8b08, row 3526, ghidra); RVehicle slot 11 (PS2 001c8b08, row 3526, ghidra) |
| 0008dce0 | RSceneObj::GetRenderOffset | RSceneObj::GetRenderOffset |  | ghidra | RSceneObj slot 9 (PS2 001c92f8, row 3537, ghidra); RSkeletalObj slot 9 (PS2 001c92f8, row 3537, ghidra) |
| 0008e1a0 | GetVelocity | RSceneObj::GetVelocity |  | ghidra | RSceneObj slot 13 (PS2 001c9c78, row 3556, ghidra); RSkeletalObj slot 13 (PS2 001c9c78, row 3556, ghidra); RVehicle slot 13 (PS2 001c9c78, row 3556, ghidra) |
| 0008ea70 | RSceneObj::TriggerFX | RSceneObj::TriggerFX |  | ghidra | RSceneObj slot 15 (PS2 001caba8, row 3570, ghidra); RSkeletalObj slot 15 (PS2 001caba8, row 3570, ghidra) |
| 0008f270 | RSceneObj::UpdatePosition | RSceneObj::UpdatePosition |  | sheet | RSceneObj slot 14 (PS2 001c8d70, row 3533, sheet) |
| 0008f930 | RSceneObj::RenderSimple | RSceneObj::RenderSimple |  | ghidra | RSceneObj slot 3 (PS2 001caf08, row 3575, ghidra); RSkeletalObj slot 3 (PS2 001caf08, row 3575, ghidra); RVehicle slot 3 (PS2 001caf08, row 3575, ghidra) |
| 0008f9f0 | RSceneObj::Render | RSceneObj::Render |  | sheet | RSceneObj slot 2 (PS2 001c85f8, row 3518, sheet); RSkeletalObj slot 2 (PS2 001c85f8, row 3518, sheet) |
| 000905a0 | RSceneObj::~RSceneObj | RSceneObj::~RSceneObj |  | sheet | called by RSceneObj::scalar_deleting_destructor (0x000908b0) and stores RSceneObj's vtable |
| 000908b0 | RSceneObj::scalar_deleting_destructor | RSceneObj::scalar_deleting_destructor |  | sheet | RSceneObj slot 0 (PS2 001c7270, row 3498, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00090c10 | ~RSkeletalObj | RSkeletalObj::~RSkeletalObj |  | sheet | called by RSkeletalObj::scalar_deleting_destructor (0x000912d0) and stores RSkeletalObj's vtable |
| 000912b0 | SetProcAnimState | RSkeletalObj::SetProcAnimState |  | resolved | RSkeletalObj slot 10 (PS2 001cd178, row 3618, resolved); RVehicle slot 10 (PS2 001cd178, row 3618, resolved) |
| 000912d0 | scalar_deleting_destructor | RSkeletalObj::scalar_deleting_destructor |  | sheet | RSkeletalObj slot 0 (PS2 001cc640, row 3613, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00091300 | RSkeletalObj::PostLoad | RSkeletalObj::PostLoad |  | sheet | RSkeletalObj slot 17 (PS2 001cc6b0, row 3614, sheet) |
| 00091360 | RSkeletalObj::UpdatePosition | RSkeletalObj::UpdatePosition |  | sheet | RSkeletalObj slot 14 (PS2 001cc738, row 3615, sheet) |
| 00095560 | RVehicle::ResolveObjectData | RVehicle::ResolveObjectData |  | sheet | RVehicle slot 18 (PS2 001d2588, row 3697, sheet) |
| 000955b0 | RVehicle::SetViewDrawList | RVehicle::SetViewDrawList |  | sheet | RVehicle slot 1 (PS2 001d2618, row 3698, sheet) |
| 000955d0 | RVehicle::PostLoad | RVehicle::PostLoad |  | sheet | RVehicle slot 17 (PS2 001d2648, row 3699, sheet) |
| 000959b0 | TriggerIlluminate | RVehicle::TriggerIlluminate |  | resolved | RVehicle slot 12 (PS2 001d2aa0, row 3702, resolved) |
| 00095b30 | RVehicle::RenderShadow | RVehicle::RenderShadow |  | sheet | RVehicle slot 4 (PS2 001d3228, row 3712, sheet) |
| 00095cc0 | GetRenderOffset | RVehicle::GetRenderOffset |  | ghidra | RVehicle slot 9 (PS2 001d38a8, row 3719, ghidra) |
| 00095ed0 | TriggerFX | RVehicle::TriggerFX |  | ghidra | RVehicle slot 15 (PS2 001d3c28, row 3722, ghidra) |
| 00096430 | RVehicle::~RVehicle | RVehicle::~RVehicle |  | sheet | called by RVehicle::scalar_deleting_destructor (0x000967f0) and stores RVehicle's vtable |
| 000964b0 | RVehicle::UpdatePosition | RVehicle::UpdatePosition |  | sheet | RVehicle slot 14 (PS2 001d34a8, row 3716, sheet) |
| 000965e0 | RVehicle::Render | RVehicle::Render |  | sheet | RVehicle slot 2 (PS2 001d35d8, row 3717, sheet) |
| 00096770 | RVehicle::RenderDeferredEffects | RVehicle::RenderDeferredEffects |  | ghidra | RVehicle slot 5 (PS2 001d37f0, row 3718, ghidra) |
| 000967f0 | RVehicle::scalar_deleting_destructor | RVehicle::scalar_deleting_destructor |  | sheet | RVehicle slot 0 (PS2 001d2508, row 3696, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00096c60 | RViewCamera::scalar_deleting_destructor | RViewCamera::scalar_deleting_destructor |  | sheet | RViewCamera slot 0 (PS2 001d4238, row 3735, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00097690 | ReceiveCameraInput | RWorldCamera::ReceiveCameraInput |  | ghidra | RPlayerCamera slot 5 (PS2 001d6128, row 3813, ghidra); RWorldCamera slot 5 (PS2 001d6128, row 3813, ghidra) |
| 000979f0 | RWorldCamera::~RWorldCamera | RWorldCamera::~RWorldCamera |  | ghidra | called by RWorldCamera::scalar_deleting_destructor (0x00097eb0) and stores RWorldCamera's vtable |
| 00097eb0 | RWorldCamera::scalar_deleting_destructor | RWorldCamera::scalar_deleting_destructor |  | ghidra | RWorldCamera slot 0 (PS2 001d54a8, row 3792, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00097ee0 | RWorldCamera::RestartCamera | RWorldCamera::RestartCamera |  | sheet | RWorldCamera slot 4 (PS2 001d5538, row 3793, sheet) |
| 00099590 | ~RReflection | RReflection::~RReflection |  | ghidra | called by RReflection::scalar_deleting_destructor (0x00099cc0) and stores RReflection's vtable |
| 00099cc0 | scalar_deleting_destructor | RReflection::scalar_deleting_destructor |  | ghidra | RReflection slot 0 (PS2 001c16c8, row 3378, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009a480 | RColorize::Kill | RColorize::Kill |  | sheet | RColorize slot 2 (PS2 001d9330, row 3904, sheet) |
| 0009ac00 | RColorize::Reset | RColorize::Reset |  | sheet | RColorize slot 1 (PS2 001d9390, row 3908, sheet) |
| 0009af50 | RDecalManager::Kill | RDecalManager::Kill |  | sheet | RDecalManager slot 2 (PS2 001dae20, row 3935, sheet) |
| 0009af70 | Reset | RDecalManager::Reset |  | ghidra | RDecalManager slot 1 (PS2 001da558, row 3929, ghidra) |
| 0009b3c0 | RDecalManager::~RDecalManager | RDecalManager::~RDecalManager |  | ghidra | called by RDecalManager::scalar_deleting_destructor (0x0009b450) and stores RDecalManager's vtable |
| 0009b450 | scalar_deleting_destructor | RDecalManager::scalar_deleting_destructor |  | ghidra | RDecalManager slot 0 (PS2 001da310, row 3924, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009de00 | RGain::~RGain | RGain::~RGain |  | sheet | called by RGain::scalar_deleting_destructor (0x0009e120) and stores RGain's vtable |
| 0009de80 | Kill | RGain::Kill |  | resolved | RGain slot 2 (PS2 001dfbc0, row 4008, resolved) |
| 0009dea0 | RGain::Reset | RGain::Reset |  | sheet | RGain slot 1 (PS2 001df830, row 4000, sheet) |
| 0009e120 | RGain::scalar_deleting_destructor | RGain::scalar_deleting_destructor |  | sheet | RGain slot 0 (PS2 001df800, row 3999, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009e150 | RLensFlareManager::Reset | RLensFlareManager::Reset |  | sheet | RLensFlareManager slot 1 (PS2 001dfcf8, row 4013, sheet) |
| 0009e1b0 | RLensFlareManager::~RLensFlareManager | RLensFlareManager::~RLensFlareManager |  | sheet | called by RLensFlareManager::scalar_deleting_destructor (0x0009e520) and stores RLensFlareManager's vtable |
| 0009e1d0 | Kill | RLensFlareManager::Kill |  | resolved | RLensFlareManager slot 2 (PS2 001e08a8, row 4023, resolved) |
| 0009e520 | RLensFlareManager::scalar_deleting_destructor | RLensFlareManager::scalar_deleting_destructor |  | sheet | RLensFlareManager slot 0 (PS2 001dfd38, row 4014, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0009fe30 | RLightning::Reset | RLightning::Reset |  | ghidra | RLightning slot 1 (PS2 001e0a70, row 4027, ghidra) |
| 0009fec0 | RLightning::~RLightning | RLightning::~RLightning |  | ghidra | called by RLightning::scalar_deleting_destructor (0x000a0000) and stores RLightning's vtable |
| 0009ff60 | RLightning::Kill | RLightning::Kill |  | sheet | RLightning slot 2 (PS2 001e33e0, row 4066, sheet) |
| 000a0000 | scalar_deleting_destructor | RLightning::scalar_deleting_destructor |  | ghidra | RLightning slot 0 (PS2 001e09d0, row 4026, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a0b30 | RMissileCam::Kill | RMissileCam::Kill |  | sheet | RMissileCam slot 2 (PS2 001e3d70, row 4079, sheet) |
| 000a0df0 | RMissileCam::~RMissileCam | RMissileCam::~RMissileCam |  | sheet | called by RMissileCam::scalar_deleting_destructor (0x000a0e70) and stores RMissileCam's vtable |
| 000a0e70 | RMissileCam::scalar_deleting_destructor | RMissileCam::scalar_deleting_destructor |  | sheet | RMissileCam slot 0 (PS2 001e3688, row 4074, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a1f00 | RParticleSystem::Update | RParticleSystem::Update |  | sheet | RMovableParticleSystem slot 1 (PS2 001e65c0, row 4117, sheet) |
| 000a3240 | RParticulate::~RParticulate | RParticulate::~RParticulate |  | sheet | called by RParticulate::scalar_deleting_destructor (0x000a3fa0) and stores RParticulate's vtable |
| 000a3290 | RParticulate::Kill | RParticulate::Kill |  | sheet | RParticulate slot 2 (PS2 001ea078, row 4185, sheet) |
| 000a3fa0 | RParticulate::scalar_deleting_destructor | RParticulate::scalar_deleting_destructor |  | sheet | RParticulate slot 0 (PS2 001e9220, row 4175, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a4550 | RPostProcessing::Reset | RPostProcessing::Reset |  | ghidra | RPostProcessing slot 1 (PS2 001ea3f8, row 4192, ghidra) |
| 000a4eb0 | RPostProcessing::Kill | RPostProcessing::Kill |  | ghidra | RPostProcessing slot 2 (PS2 001ea850, row 4203, ghidra) |
| 000a4f70 | RPostProcessing::~RPostProcessing | RPostProcessing::~RPostProcessing |  | sheet | called by RPostProcessing::scalar_deleting_destructor (0x000a5060) and stores RPostProcessing's vtable |
| 000a5060 | RPostProcessing::scalar_deleting_destructor | RPostProcessing::scalar_deleting_destructor |  | sheet | RPostProcessing slot 0 (PS2 001ea328, row 4191, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a6810 | RSniperZoom::~RSniperZoom | RSniperZoom::~RSniperZoom |  | sheet | called by RSniperZoom::scalar_deleting_destructor (0x000a6a50) and stores RSniperZoom's vtable |
| 000a6870 | RSniperZoom::Kill | RSniperZoom::Kill |  | ghidra | RSniperZoom slot 2 (PS2 001edd80, row 4249, ghidra) |
| 000a6890 | RSniperZoom::Reset | RSniperZoom::Reset |  | sheet | RSniperZoom slot 1 (PS2 001eddf8, row 4252, sheet) |
| 000a6a50 | RSniperZoom::scalar_deleting_destructor | RSniperZoom::scalar_deleting_destructor |  | sheet | RSniperZoom slot 0 (PS2 001ed570, row 4242, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000a8470 | RWater::Kill | RWater::Kill |  | sheet | RWater slot 2 (PS2 001f0ad8, row 4288, sheet) |
| 000a9eb0 | RGlareManager::Kill | RGlareManager::Kill |  | sheet | RGlareManager slot 2 (PS2 001f2948, row 4308, sheet) |
| 000a9ed0 | RGlareManager::Reset | RGlareManager::Reset |  | ghidra | RGlareManager slot 1 (PS2 001f2990, row 4311, ghidra) |
| 000b9770 | SRuleDamage::CheckRule | SRuleDamage::CheckRule |  | ghidra | SRuleDamage slot 1 (PS2 00209ff0, row 4688, ghidra) |
| 000b9950 | SRuleProg::CheckRule | SRuleProg::CheckRule |  | sheet | SRuleProg slot 1 (PS2 0020a420, row 4700, sheet) |
| 000b9be0 | SRuleProgCounter::CheckRule | SRuleProgCounter::CheckRule |  | sheet | SRuleProgCounter slot 1 (PS2 0020af78, row 4725, sheet) |
| 000b9d90 | SRuleAmmo::scalar_deleting_destructor | SRuleAmmo::scalar_deleting_destructor | SRuleDamage::~SRuleDamage, SRuleTimer::~SRuleTimer, SRulePlayerDir::~SRulePlayerDir | ghidra | SRulePlayerDir slot 0 (PS2 0020ab88, row 4717, ghidra); SRuleTimer slot 0 (PS2 0020a8e8, row 4711, ghidra); SRuleDamage slot 0 (PS2 00209f98, row 4687, ghidra) ... |
| 000b9db0 | SRuleAmmo::CheckRule | SRuleAmmo::CheckRule |  | ghidra | SRuleAmmo slot 1 (PS2 00209ec0, row 4685, ghidra) |
| 000ba0f0 | SRuleTimer::CheckRule | SRuleTimer::CheckRule |  | ghidra | SRuleTimer slot 1 (PS2 0020a940, row 4712, ghidra) |
| 000ba570 | SRuleRange::CheckRule | SRuleRange::CheckRule |  | ghidra | SRuleRange slot 1 (PS2 0020a5f0, row 4706, ghidra) |
| 000ba690 | SRulePlayerDir::CheckRule | SRulePlayerDir::CheckRule |  | ghidra | SRulePlayerDir slot 1 (PS2 0020ac70, row 4719, ghidra) |
| 000dc420 | GHud::~GHud | GHud::~GHud |  | sheet | called by GHud::scalar_deleting_destructor (0x000e00d0) and stores GHud's vtable |
| 000e00d0 | GHud::scalar_deleting_destructor | GHud::scalar_deleting_destructor |  | sheet | GHud slot 0 (PS2 0023c560, row 5307, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000e3730 | GSubtitles::~GSubtitles | GSubtitles::~GSubtitles |  | sheet | called by GSubtitles::scalar_deleting_destructor (0x000e3ad0) and stores GSubtitles's vtable |
| 000e3ad0 | GSubtitles::scalar_deleting_destructor | GSubtitles::scalar_deleting_destructor |  | sheet | GSubtitles slot 0 (PS2 0024a168, row 5449, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000e4240 | IniFiles::scalar_deleting_destructor | IniFiles::scalar_deleting_destructor |  | ghidra | IniFiles slot 0 (PS2 0024bae8, row 5497, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f70f0 | GetTargetCheckSum | EAGLAnim::FnCompoundChannel::GetTargetCheckSum |  | ghidra | EAGLAnim::FnCompoundChannel slot 1 (PS2 00266390, row 6427, ghidra) |
| 000f7100 | EAGLAnim::FnCompoundChannel::GetLength | EAGLAnim::FnCompoundChannel::GetLength |  | ghidra | EAGLAnim::FnCompoundChannel slot 4 (PS2 002663b0, row 6430, ghidra) |
| 000f7140 | scalar_deleting_destructor | EAGLAnim::FnCompoundChannel::scalar_deleting_destructor |  | resolved | EAGLAnim::FnCompoundChannel slot 0 (PS2 00266410, row 6407, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f72f0 | GetTargetCheckSum | EAGLAnim::FnAnim::GetTargetCheckSum |  | ghidra | EAGLAnim::FnPoseMirror slot 1 (PS2 0026dfb8, row 6584, ghidra); EAGLAnim::FnRunBlender slot 1 (PS2 0026dfb8, row 6584, ghidra); EAGLAnim::FnTurnBlender slot 1 (PS2 0026dfb8, row 6584, ghidra) |
| 000f7300 | GetLength | EAGLAnim::FnAnim::GetLength |  | ghidra | EAGLAnim::FnPoseMirror slot 4 (PS2 0026dfd0, row 6587, ghidra); EAGLAnim::FnRawEventChannel slot 4 (PS2 0026dfd0, row 6587, ghidra); EAGLAnim::FnAnimMemoryMap slot 4 (PS2 0026dfd0, row 6587, ghidra) ... |
| 000f7320 | EvalEvent | EAGLAnim::FnAnim::EvalEvent |  | ghidra | EAGLAnim::FnPoseMirror slot 9 (PS2 0026dff8, row 6592, ghidra); EAGLAnim::FnRawLinearChannel slot 9 (PS2 0026dff8, row 6592, ghidra); EAGLAnim::FnRawStateChan slot 9 (PS2 0026dff8, row 6592, ghidra) ... |
| 000f7410 | EAGLAnim::FnRawEventChannel::SetAnimMemoryMap | EAGLAnim::FnRawEventChannel::SetAnimMemoryMap |  | ghidra | EAGLAnim::FnRawEventChannel slot 15 (PS2 00264bf8, row 6379, ghidra) |
| 000f7430 | EAGLAnim::FnRawEventChannel::EvalEvent | EAGLAnim::FnRawEventChannel::EvalEvent |  | ghidra | EAGLAnim::FnRawEventChannel slot 9 (PS2 00264c08, row 6380, ghidra) |
| 000f7460 | EAGLAnim::FnRawEventChannel::Eval | EAGLAnim::FnRawEventChannel::Eval |  | ghidra | EAGLAnim::FnRawEventChannel slot 3 (PS2 00264c40, row 6381, ghidra) |
| 000f7480 | EAGLAnim::FnRawEventChannel::scalar_deleting_destructor | EAGLAnim::FnRawEventChannel::scalar_deleting_destructor |  | ghidra | EAGLAnim::FnRawEventChannel slot 0 (PS2 00264ba0, row 6378, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f74b0 | EAGLAnim::FnRawEventChannel::~FnRawEventChannel | EAGLAnim::FnRawEventChannel::~FnRawEventChannel |  | ghidra | called by EAGLAnim::FnRawEventChannel::scalar_deleting_destructor (0x000f7480) and stores EAGLAnim::FnRawEventChannel's vtable |
| 000f74e0 | EAGLAnim::FnRawLinearChannel::Eval | EAGLAnim::FnRawLinearChannel::Eval |  | sheet | EAGLAnim::FnRawLinearChannel slot 3 (PS2 00264d18, row 6384, sheet) |
| 000f7650 | EAGLAnim::FnRawLinearChannel::GetLength | EAGLAnim::FnRawLinearChannel::GetLength |  | sheet | EAGLAnim::FnRawLinearChannel slot 4 (PS2 00264f30, row 6385, sheet) |
| 000f7670 | EAGLAnim::FnRawLinearChannel::scalar_deleting_destructor | EAGLAnim::FnRawLinearChannel::scalar_deleting_destructor |  | sheet | EAGLAnim::FnRawLinearChannel slot 0 (PS2 00264cc0, row 6383, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f76a0 | EAGLAnim::FnRawLinearChannel::~FnRawLinearChannel | EAGLAnim::FnRawLinearChannel::~FnRawLinearChannel |  | sheet | called by EAGLAnim::FnRawLinearChannel::scalar_deleting_destructor (0x000f7670) and stores EAGLAnim::FnRawLinearChannel's vtable |
| 000f78a0 | EAGLAnim::FnPoseMirror::Eval | EAGLAnim::FnPoseMirror::Eval |  | sheet | EAGLAnim::FnPoseMirror slot 3 (PS2 002650f0, row 6391, sheet) |
| 000f78f0 | EAGLAnim::FnPoseMirror::EvalSQT | EAGLAnim::FnPoseMirror::EvalSQT |  | sheet | EAGLAnim::FnPoseMirror slot 6 (PS2 00265180, row 6392, sheet) |
| 000f7940 | EAGLAnim::FnPoseMirror::scalar_deleting_destructor | EAGLAnim::FnPoseMirror::scalar_deleting_destructor |  | sheet | EAGLAnim::FnPoseMirror slot 0 (PS2 002650c0, row 6390, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f7b50 | GetPhaseChan | EAGLAnim::FnPhaseChan::GetPhaseChan |  | ghidra | EAGLAnim::FnPhaseChan slot 13 (PS2 00277730, row 6850, ghidra) |
| 000f7b80 | GetLength | EAGLAnim::FnRawStateChan::GetLength |  | ghidra | EAGLAnim::FnRawStateChan slot 4 (PS2 00267328, row 6463, ghidra) |
| 000f7ba0 | Eval | EAGLAnim::FnRawStateChan::Eval |  | ghidra | EAGLAnim::FnRawStateChan slot 3 (PS2 00267348, row 6464, ghidra) |
| 000f7bc0 | scalar_deleting_destructor | EAGLAnim::FnRawStateChan::scalar_deleting_destructor |  | ghidra | EAGLAnim::FnRawStateChan slot 0 (PS2 002672c8, row 6462, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f7bf0 | ~FnRawStateChan | EAGLAnim::FnRawStateChan::~FnRawStateChan |  | ghidra | called by EAGLAnim::FnRawStateChan::scalar_deleting_destructor (0x000f7bc0) and stores EAGLAnim::FnRawStateChan's vtable |
| 000f8290 | scalar_deleting_destructor | EAGLAnim::FnKeyLerpChan::scalar_deleting_destructor | EAGLAnim::FnKeyQuatChan::~FnKeyQuatChan | resolved | EAGLAnim::FnKeyQuatChan slot 0 (PS2 00275f38, row 6809, resolved); EAGLAnim::FnKeyLerpChan slot 0 (PS2 00275cd0, row 6796, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000f82d0 | scalar_deleting_destructor | EAGLAnim::FnPhaseChan::scalar_deleting_destructor |  | ghidra | EAGLAnim::FnPhaseChan slot 0 (PS2 00277538, row 6837, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000faab0 | EAGLAnim::FnAnimMemoryMap::SetAnimMemoryMap | EAGLAnim::FnAnimMemoryMap::SetAnimMemoryMap |  | ghidra | EAGLAnim::FnRawLinearChannel slot 15 (PS2 0026dd48, row 6554, ghidra); EAGLAnim::FnRawStateChan slot 15 (PS2 0026dd48, row 6554, ghidra); EAGLAnim::FnAnimMemoryMap slot 15 (PS2 0026dd48, row 6554, ghidra) |
| 000faac0 | GetAnimMemoryMap | EAGLAnim::FnAnimMemoryMap::GetAnimMemoryMap |  | sheet | EAGLAnim::FnRawLinearChannel slot 17 (PS2 0026dd58, row 6556, sheet); EAGLAnim::FnRawEventChannel slot 17 (PS2 0026dd58, row 6556, sheet); EAGLAnim::FnCompoundChannel slot 17 (PS2 0026dd58, row 6556, sheet) ... |
| 000faad0 | GetAnimMemoryMap | EAGLAnim::FnAnimMemoryMap::GetAnimMemoryMap |  | infill-exact | EAGLAnim::FnRawLinearChannel slot 16 (PS2 0026dd50, row 6555, infill-exact); EAGLAnim::FnRawEventChannel slot 16 (PS2 0026dd50, row 6555, infill-exact); EAGLAnim::FnCompoundChannel slot 16 (PS2 0026dd50, row 6555, infill-exact) ... |
| 000faae0 | EAGLAnim::FnAnimMemoryMap::GetTargetCheckSum | EAGLAnim::FnAnimMemoryMap::GetTargetCheckSum |  | ghidra | EAGLAnim::FnRawLinearChannel slot 1 (PS2 0026dd60, row 6557, ghidra); EAGLAnim::FnRawEventChannel slot 1 (PS2 0026dd60, row 6557, ghidra); EAGLAnim::FnRawStateChan slot 1 (PS2 0026dd60, row 6557, ghidra) ... |
| 000faaf0 | EAGLAnim::FnAnimMemoryMap::scalar_deleting_destructor | EAGLAnim::FnAnimMemoryMap::scalar_deleting_destructor |  | sheet | EAGLAnim::FnAnimMemoryMap slot 0 (PS2 0026dd18, row 6553, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000fab60 | ~FnCompoundChannel | EAGLAnim::FnCompoundChannel::~FnCompoundChannel |  | resolved | called by EAGLAnim::FnCompoundChannel::scalar_deleting_destructor (0x000f7140) and stores EAGLAnim::FnCompoundChannel's vtable |
| 000fac00 | SetAnimMemoryMap | EAGLAnim::FnCompoundChannel::SetAnimMemoryMap |  | resolved | EAGLAnim::FnCompoundChannel slot 15 (PS2 00266518, row 6408, resolved) |
| 000fac70 | EvalEvent | EAGLAnim::FnCompoundChannel::EvalEvent |  | resolved | EAGLAnim::FnCompoundChannel slot 9 (PS2 002659a8, row 6409, resolved) |
| 000facf0 | EvalSQT | EAGLAnim::FnCompoundChannel::EvalSQT |  | resolved | EAGLAnim::FnCompoundChannel slot 6 (PS2 00265b28, row 6410, resolved) |
| 000fad60 | EvalWeights | EAGLAnim::FnCompoundChannel::EvalWeights |  | resolved | EAGLAnim::FnCompoundChannel slot 10 (PS2 00265c90, row 6411, resolved) |
| 000fadc0 | EvalVel2D | EAGLAnim::FnCompoundChannel::EvalVel2D |  | resolved | EAGLAnim::FnCompoundChannel slot 8 (PS2 00265df8, row 6412, resolved) |
| 000fae20 | EvalState | EAGLAnim::FnCompoundChannel::EvalState |  | resolved | EAGLAnim::FnCompoundChannel slot 11 (PS2 00265f60, row 6413, resolved) |
| 000fae80 | FindTime | EAGLAnim::FnCompoundChannel::FindTime |  | ghidra | EAGLAnim::FnCompoundChannel slot 12 (PS2 00266820, row 6435, ghidra) |
| 000faf00 | EvalPhase | EAGLAnim::FnCompoundChannel::EvalPhase |  | ghidra | EAGLAnim::FnCompoundChannel slot 7 (PS2 00266910, row 6436, ghidra) |
| 000faf90 | GetPhaseChan | EAGLAnim::FnCompoundChannel::GetPhaseChan |  | resolved | EAGLAnim::FnCompoundChannel slot 13 (PS2 002660c8, row 6414, resolved) |
| 000fafd0 | UseFPS | EAGLAnim::FnCompoundChannel::UseFPS |  | ghidra | EAGLAnim::FnCompoundChannel slot 2 (PS2 00266a20, row 6438, ghidra) |
| 000fb040 | GetAttributes | EAGLAnim::FnCompoundChannel::GetAttributes |  | ghidra | EAGLAnim::FnCompoundChannel slot 14 (PS2 002665e8, row 6432, ghidra) |
| 000fb0a0 | Eval | EAGLAnim::FnCompoundChannel::Eval |  | ghidra | EAGLAnim::FnCompoundChannel slot 3 (PS2 002666b8, row 6434, ghidra) |
| 000fb110 | GetAttributes | EAGLAnim::FnAnim::GetAttributes |  | ghidra | EAGLAnim::FnPoseMirror slot 14 (PS2 0026e4d8, row 6608, ghidra); EAGLAnim::FnRawLinearChannel slot 14 (PS2 0026e4d8, row 6608, ghidra); EAGLAnim::FnRawEventChannel slot 14 (PS2 0026e4d8, row 6608, ghidra) ... |
| 000fb630 | EvalWeights | EAGLAnim::FnDeltaLerpChan::EvalWeights |  | ghidra | EAGLAnim::FnDeltaLerpChan slot 10 (PS2 002764d8, row 6825, ghidra) |
| 000fb650 | EvalVel2D | EAGLAnim::FnDeltaLerpChan::EvalVel2D |  | ghidra | EAGLAnim::FnDeltaLerpChan slot 8 (PS2 00276508, row 6826, ghidra) |
| 000fba00 | Eval | EAGLAnim::FnKeyLerpChan::Eval |  | ghidra | EAGLAnim::FnKeyLerpChan slot 3 (PS2 002767c0, row 6830, ghidra) |
| 000fba20 | EAGLAnim::FnKeyLerpChan::EvalSQT | EAGLAnim::FnKeyLerpChan::EvalSQT |  | ghidra | EAGLAnim::FnKeyLerpChan slot 6 (PS2 00274fe0, row 6745, ghidra) |
| 000fbb40 | Eval | EAGLAnim::FnKeyQuatChan::Eval |  | ghidra | EAGLAnim::FnKeyQuatChan slot 3 (PS2 002767f0, row 6831, ghidra) |
| 000fbb60 | EAGLAnim::FnKeyQuatChan::EvalSQT | EAGLAnim::FnKeyQuatChan::EvalSQT |  | ghidra | EAGLAnim::FnKeyQuatChan slot 6 (PS2 00275228, row 6746, ghidra) |
| 000fbe10 | GetLength | EAGLAnim::FnKeyDeltaChan::GetLength |  | resolved | EAGLAnim::FnKeyQuatChan slot 4 (PS2 00276658, row 6827, resolved); EAGLAnim::FnKeyLerpChan slot 4 (PS2 00276658, row 6827, resolved) |
| 000fd4a0 | GetLength | EAGLAnim::FnPhaseChan::GetLength |  | ghidra | EAGLAnim::FnPhaseChan slot 4 (PS2 00277738, row 6851, ghidra) |
| 000fd4c0 | Eval | EAGLAnim::FnPhaseChan::Eval |  | ghidra | EAGLAnim::FnPhaseChan slot 3 (PS2 00277360, row 6836, ghidra) |
| 000fd670 | SetAnimMemoryMap | EAGLAnim::FnPhaseChan::SetAnimMemoryMap |  | resolved | EAGLAnim::FnPhaseChan slot 15 (PS2 00277758, row 6852, resolved) |
| 000fd840 | EvalState | EAGLAnim::FnRawStateChan::EvalState |  | ghidra | EAGLAnim::FnRawStateChan slot 11 (PS2 00266f48, row 6448, ghidra) |
| 000fe2b0 | Eval | EAGLAnim::FnDeltaF1::Eval |  | resolved | EAGLAnim::FnDeltaF1 slot 3 (PS2 00271830, row 6648, resolved) |
| 000feaa0 | EvalWeights | EAGLAnim::FnDeltaF1::EvalWeights |  | resolved | EAGLAnim::FnDeltaF1 slot 10 (PS2 00271860, row 6649, resolved) |
| 000feac0 | EvalVel2D | EAGLAnim::FnDeltaF1::EvalVel2D |  | resolved | EAGLAnim::FnDeltaF1 slot 8 (PS2 00271890, row 6650, resolved) |
| 000febd0 | EvalSQT | EAGLAnim::FnDeltaF1::EvalSQT |  | ghidra | EAGLAnim::FnDeltaF1 slot 6 (PS2 002701b8, row 6630, ghidra) |
| 000ff1e0 | EAGLAnim::FnDeltaF1::scalar_deleting_destructor | EAGLAnim::FnDeltaF1::scalar_deleting_destructor |  | sheet | EAGLAnim::FnDeltaF1 slot 0 (PS2 00271710, row 6645, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 000ff210 | EAGLAnim::FnDeltaF1::~FnDeltaF1 | EAGLAnim::FnDeltaF1::~FnDeltaF1 |  | sheet | called by EAGLAnim::FnDeltaF1::scalar_deleting_destructor (0x000ff1e0) and stores EAGLAnim::FnDeltaF1's vtable |
| 000ff2b0 | Eval | EAGLAnim::FnDeltaF3::Eval |  | ghidra | EAGLAnim::FnDeltaF3 slot 3 (PS2 00270128, row 6627, ghidra) |
| 000ffce0 | EvalWeights | EAGLAnim::FnDeltaF3::EvalWeights |  | ghidra | EAGLAnim::FnDeltaF3 slot 10 (PS2 00270158, row 6628, ghidra) |
| 000ffd00 | EvalVel2D | EAGLAnim::FnDeltaF3::EvalVel2D |  | ghidra | EAGLAnim::FnDeltaF3 slot 8 (PS2 00270188, row 6629, ghidra) |
| 000fff10 | EvalSQT | EAGLAnim::FnDeltaF3::EvalSQT |  | ghidra | EAGLAnim::FnDeltaF3 slot 6 (PS2 0026e4e0, row 6609, ghidra) |
| 00100880 | SetAnimMemoryMap | EAGLAnim::FnDeltaF3::SetAnimMemoryMap | EAGLAnim::FnDeltaF1::SetAnimMemoryMap | resolved | EAGLAnim::FnDeltaF3 slot 15 (PS2 002700d0, row 6625, ghidra); EAGLAnim::FnDeltaF1 slot 15 (PS2 002717d8, row 6646, resolved) |
| 001008a0 | GetLength | EAGLAnim::FnDeltaF3::GetLength | EAGLAnim::FnDeltaF1::GetLength | resolved | EAGLAnim::FnDeltaF3 slot 4 (PS2 002700e8, row 6626, ghidra); EAGLAnim::FnDeltaF1 slot 4 (PS2 002717f0, row 6647, resolved) |
| 001008e0 | EAGLAnim::FnDeltaF3::scalar_deleting_destructor | EAGLAnim::FnDeltaF3::scalar_deleting_destructor |  | sheet | EAGLAnim::FnDeltaF3 slot 0 (PS2 00270008, row 6624, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00100910 | ~FnDeltaF3 | EAGLAnim::FnDeltaF3::~FnDeltaF3 |  | sheet | called by EAGLAnim::FnDeltaF3::scalar_deleting_destructor (0x001008e0) and stores EAGLAnim::FnDeltaF3's vtable |
| 001009c0 | ~FnDeltaSingleQ | EAGLAnim::FnDeltaSingleQ::~FnDeltaSingleQ |  | resolved | called by EAGLAnim::FnDeltaSingleQ::scalar_deleting_destructor (0x00101f00) and stores EAGLAnim::FnDeltaSingleQ's vtable |
| 00100a20 | SetAnimMemoryMap | EAGLAnim::FnDeltaSingleQ::SetAnimMemoryMap |  | resolved | EAGLAnim::FnDeltaSingleQ slot 15 (PS2 0026b950, row 6515, resolved) |
| 00100a30 | GetLength | EAGLAnim::FnDeltaSingleQ::GetLength |  | resolved | EAGLAnim::FnDeltaSingleQ slot 4 (PS2 0026b958, row 6516, resolved) |
| 00100a80 | Eval | EAGLAnim::FnDeltaSingleQ::Eval |  | infill-count | EAGLAnim::FnDeltaSingleQ slot 3 (PS2 0026b998, row 6517, infill-count) |
| 00100aa0 | EvalSQT | EAGLAnim::FnDeltaSingleQ::EvalSQT |  | infill-count | EAGLAnim::FnDeltaSingleQ slot 6 (PS2 0026b9d0, row 6518, infill-count) |
| 00100be0 | EvalSQTMasked | EAGLAnim::FnDeltaSingleQ::EvalSQTMasked |  | ghidra | EAGLAnim::FnDeltaSingleQ slot 18 (PS2 00268e40, row 6501, ghidra) |
| 00101f00 | scalar_deleting_destructor | EAGLAnim::FnDeltaSingleQ::scalar_deleting_destructor |  | resolved | EAGLAnim::FnDeltaSingleQ slot 0 (PS2 0026b368, row 6500, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00101f90 | ~FnDeltaQFast | EAGLAnim::FnDeltaQFast::~FnDeltaQFast |  | resolved | called by EAGLAnim::FnDeltaQFast::scalar_deleting_destructor (0x001032b0) and stores EAGLAnim::FnDeltaQFast's vtable |
| 00102500 | SetAnimMemoryMap | EAGLAnim::FnDeltaQFast::SetAnimMemoryMap |  | resolved | EAGLAnim::FnDeltaQFast slot 15 (PS2 0026bbc8, row 6523, resolved) |
| 001029f0 | EvalSQT | EAGLAnim::FnDeltaQFast::EvalSQT |  | resolved | EAGLAnim::FnDeltaQFast slot 6 (PS2 0026bd08, row 6524, resolved) |
| 001032b0 | scalar_deleting_destructor | EAGLAnim::FnDeltaQFast::scalar_deleting_destructor |  | resolved | EAGLAnim::FnDeltaQFast slot 0 (PS2 0026d468, row 6522, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00103420 | SetAnimMemoryMap | EAGLAnim::FnDeltaQ::SetAnimMemoryMap |  | resolved | EAGLAnim::FnDeltaQ slot 15 (PS2 00268d88, row 6496, resolved) |
| 00103430 | GetLength | EAGLAnim::FnDeltaQ::GetLength |  | resolved | EAGLAnim::FnDeltaQ slot 4 (PS2 00268d90, row 6497, resolved) |
| 00103480 | Eval | EAGLAnim::FnDeltaQ::Eval |  | resolved | EAGLAnim::FnDeltaQ slot 3 (PS2 00268dd0, row 6498, resolved) |
| 001034a0 | EvalSQT | EAGLAnim::FnDeltaQ::EvalSQT |  | resolved | EAGLAnim::FnDeltaQ slot 6 (PS2 00268e08, row 6499, resolved) |
| 001034c0 | EvalSQTMasked | EAGLAnim::FnDeltaQ::EvalSQTMasked |  | ghidra | EAGLAnim::FnDeltaQ slot 18 (PS2 00267938, row 6478, ghidra) |
| 00104bf0 | EvalSQT | EAGLAnim::FnTurnBlender::EvalSQT |  | ghidra | EAGLAnim::FnTurnBlender slot 6 (PS2 00279aa0, row 6892, ghidra) |
| 00104d60 | EAGLAnim::FnTurnBlender::EvalVel2D | EAGLAnim::FnTurnBlender::EvalVel2D |  | ghidra | EAGLAnim::FnTurnBlender slot 8 (PS2 00279e30, row 6894, ghidra) |
| 00105050 | EAGLAnim::FnRunBlender::~FnRunBlender | EAGLAnim::FnRunBlender::~FnRunBlender |  | resolved | called by EAGLAnim::FnRunBlender::scalar_deleting_destructor (0x00106300) and stores EAGLAnim::FnRunBlender's vtable |
| 001051a0 | EAGLAnim::FnRunBlender::Eval | EAGLAnim::FnRunBlender::Eval |  | sheet | EAGLAnim::FnRunBlender slot 3 (PS2 00279398, row 6881, sheet) |
| 00105780 | EvalPhase | EAGLAnim::FnRunBlender::EvalPhase |  | ghidra | EAGLAnim::FnRunBlender slot 7 (PS2 00279498, row 6884, ghidra) |
| 00105790 | FindMatchTime | EAGLAnim::FnRunBlender::FindMatchTime |  | ghidra | EAGLAnim::FnRunBlender slot 5 (PS2 00278698, row 6858, ghidra) |
| 00106050 | EvalSQT | EAGLAnim::FnRunBlender::EvalSQT |  | resolved | EAGLAnim::FnRunBlender slot 6 (PS2 002779e0, row 6854, resolved) |
| 001061f0 | EvalVel2D | EAGLAnim::FnRunBlender::EvalVel2D |  | ghidra | EAGLAnim::FnRunBlender slot 8 (PS2 00278450, row 6857, ghidra) |
| 00106300 | scalar_deleting_destructor | EAGLAnim::FnRunBlender::scalar_deleting_destructor |  | resolved | EAGLAnim::FnRunBlender slot 0 (PS2 002777b8, row 6853, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00117e40 | UGroup::Processor::StartGroup | UGroup::Processor::StartGroup |  | ghidra | CARP::SymbolicResolver slot 0 (PS2 002dc150, row 8560, ghidra) |
| 00118b40 | CARP::SymbolicResolver::ProcessData | CARP::SymbolicResolver::ProcessData |  | sheet | CARP::SymbolicResolver slot 1 (PS2 002dc1b0, row 8562, sheet) |
| 0011aa80 | NameLookup | UCarpNamespace::NameLookup |  | ghidra | UCarpNamespace slot 0 (PS2 002cda80, row 8209, ghidra) |
| 0011b650 | USymbolTable::scalar_deleting_destructor | USymbolTable::scalar_deleting_destructor |  | sheet | USymbolTable slot 0 (PS2 002ccfa8, row 8199, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011b760 | UCarpNamespace::scalar_deleting_destructor | UCarpNamespace::scalar_deleting_destructor |  | sheet | UCarpNamespace slot 1 (PS2 002cd818, row 8207, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011c6f0 | ABaseSound::~ABaseSound | ABaseSound::~ABaseSound |  | ghidra | called by ABaseSound::scalar_deleting_destructor (0x0001c090) and stores ABaseSound's vtable |
| 0011c710 | ABaseSound::GetName | ABaseSound::GetName |  | ghidra | AVehicle slot 2 (PS2 002fcb80, row 9109, ghidra); AUltraLite slot 2 (PS2 002fcb80, row 9109, ghidra); AStream slot 2 (PS2 002fcb80, row 9109, ghidra) ... |
| 0011ddb0 | ACharacter::Play | ACharacter::Play |  | ghidra | ACharacter slot 3 (PS2 002fc130, row 9093, ghidra) |
| 0011dee0 | AMenuSoundPriv::Play | AMenuSoundPriv::Play |  | sheet | AMenuSoundPriv slot 3 (PS2 002f4918, row 8979, sheet) |
| 0011e060 | AMenuSoundPriv::scalar_deleting_destructor | AMenuSoundPriv::scalar_deleting_destructor |  | sheet | AMenuSoundPriv slot 0 (PS2 002f4cb8, row 8984, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0011e090 | AMenuSoundPriv::~AMenuSoundPriv | AMenuSoundPriv::~AMenuSoundPriv |  | sheet | called by AMenuSoundPriv::scalar_deleting_destructor (0x0011e060) and stores AMenuSoundPriv's vtable |
| 0011ed30 | AVehicle::PlayLanding | AVehicle::PlayLanding |  | sheet | AVehicle slot 4 (PS2 002dfa00, row 8688, sheet); AUltraLite slot 4 (PS2 002dfa00, row 8688, sheet); ATrafficVehicle slot 4 (PS2 002dfa00, row 8688, sheet) ... |
| 0011f1a0 | AVehicle::PlayCollision | AVehicle::PlayCollision |  | ghidra | AVehicle slot 5 (PS2 002e0890, row 8689, ghidra); AUltraLite slot 5 (PS2 002e0890, row 8689, ghidra); ATrafficVehicle slot 5 (PS2 002e0890, row 8689, ghidra) ... |
| 0011f9b0 | AVehicle::Play | AVehicle::Play |  | sheet | AVehicle slot 3 (PS2 002e1768, row 8690, sheet) |
| 00120a10 | AVehicle::~AVehicle | AVehicle::~AVehicle |  | sheet | called by AVehicle::scalar_deleting_destructor (0x00120d50) and stores AVehicle's vtable |
| 00120d50 | AVehicle::scalar_deleting_destructor | AVehicle::scalar_deleting_destructor |  | sheet | AVehicle slot 0 (PS2 002e2b60, row 8691, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00122450 | AStream::~AStream | AStream::~AStream |  | sheet | called by AStream::scalar_deleting_destructor (0x00122550) and stores AStream's vtable |
| 00122550 | AStream::scalar_deleting_destructor | AStream::scalar_deleting_destructor |  | sheet | AStream slot 0 (PS2 002e8c00, row 8790, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00122580 | AStream::Play | AStream::Play |  | ghidra | AStream slot 3 (PS2 002e8c70, row 8791, ghidra) |
| 001285f0 | ~APlayerVehicle | APlayerVehicle::~APlayerVehicle |  | ghidra | called by APlayerVehicle::scalar_deleting_destructor (0x00128670) and stores APlayerVehicle's vtable |
| 00128670 | scalar_deleting_destructor | APlayerVehicle::scalar_deleting_destructor |  | ghidra | APlayerVehicle slot 0 (PS2 002f0c18, row 8915, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 001286a0 | Play | APlayerVehicle::Play |  | ghidra | APlayerVehicle slot 3 (PS2 002f0900, row 8914, ghidra) |
| 00128d10 | Play | APlayerHeli::Play |  | ghidra | APlayerHeli slot 3 (PS2 002f16b8, row 8928, ghidra) |
| 001290e0 | ~APlayerHeli | APlayerHeli::~APlayerHeli |  | resolved | called by APlayerHeli::scalar_deleting_destructor (0x00129220) and stores APlayerHeli's vtable |
| 00129220 | scalar_deleting_destructor | APlayerHeli::scalar_deleting_destructor |  | resolved | APlayerHeli slot 0 (PS2 002f1ae8, row 8929, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 001297a0 | AFingoDeath::~AFingoDeath | AFingoDeath::~AFingoDeath |  | ghidra | called by AFingoDeath::scalar_deleting_destructor (0x00129a00) and stores AFingoDeath's vtable |
| 00129820 | SetRechargePercent | AFingoDeath::SetRechargePercent |  | sheet | AFingoDeath slot 9 (PS2 002f8d10, row 9040, sheet) |
| 00129830 | AFingoDeath::Play | AFingoDeath::Play |  | ghidra | AFingoDeath slot 3 (PS2 002f8590, row 9036, ghidra) |
| 00129a00 | AFingoDeath::scalar_deleting_destructor | AFingoDeath::scalar_deleting_destructor |  | ghidra | AFingoDeath slot 0 (PS2 002f84e0, row 9035, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00129b00 | Play | AUltraLite::Play |  | ghidra | AUltraLite slot 3 (PS2 002e3c40, row 8743, ghidra) |
| 00129b20 | AUltraLite::~AUltraLite | AUltraLite::~AUltraLite |  | sheet | called by AUltraLite::scalar_deleting_destructor (0x0012a410) and stores AUltraLite's vtable |
| 00129cd0 | PlayMotor | AUltraLite::PlayMotor |  | resolved | AUltraLite slot 9 (PS2 002e3c90, row 8744, resolved) |
| 0012a410 | AUltraLite::scalar_deleting_destructor | AUltraLite::scalar_deleting_destructor |  | sheet | AUltraLite slot 0 (PS2 002e3948, row 8742, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012a890 | ~ASnowMobile | ASnowMobile::~ASnowMobile |  | ghidra | called by ASnowMobile::scalar_deleting_destructor (0x0012b510) and stores ASnowMobile's vtable |
| 0012b510 | scalar_deleting_destructor | ASnowMobile::scalar_deleting_destructor |  | ghidra | ASnowMobile slot 0 (PS2 002edb08, row 8869, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012b540 | Play | ASnowMobile::Play |  | ghidra | ASnowMobile slot 3 (PS2 002edd88, row 8870, ghidra) |
| 0012bb60 | ASubmersible::SetCreakLevel | ASubmersible::SetCreakLevel |  | ghidra | ASubmersible slot 9 (PS2 002e8650, row 8783, ghidra) |
| 0012be30 | ~ASubmersible | ASubmersible::~ASubmersible |  | ghidra | called by ASubmersible::scalar_deleting_destructor (0x0012c4e0) and stores ASubmersible's vtable |
| 0012c4e0 | scalar_deleting_destructor | ASubmersible::scalar_deleting_destructor |  | ghidra | ASubmersible slot 0 (PS2 002e7210, row 8775, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012c510 | Play | ASubmersible::Play |  | ghidra | ASubmersible slot 3 (PS2 002e7358, row 8776, ghidra) |
| 0012c640 | Play | APlayerTank::Play |  | ghidra | APlayerTank slot 3 (PS2 002f1000, row 8921, ghidra) |
| 0012c730 | APlayerTank::~APlayerTank | APlayerTank::~APlayerTank |  | resolved | called by APlayerTank::scalar_deleting_destructor (0x0012c7c0) and stores APlayerTank's vtable |
| 0012c7c0 | scalar_deleting_destructor | APlayerTank::scalar_deleting_destructor |  | resolved | APlayerTank slot 0 (PS2 002f1128, row 8922, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012c930 | AHelicopter::PlayLanding | AHelicopter::PlayLanding |  | ghidra | AHelicopter slot 4 (PS2 002f6900, row 9010, ghidra) |
| 0012c950 | AHelicopter::Play | AHelicopter::Play |  | ghidra | AHelicopter slot 3 (PS2 002f6938, row 9011, ghidra) |
| 0012cac0 | AHelicopter::~AHelicopter | AHelicopter::~AHelicopter |  | sheet | called by AHelicopter::scalar_deleting_destructor (0x0012cb60) and stores AHelicopter's vtable |
| 0012cb60 | AHelicopter::scalar_deleting_destructor | AHelicopter::scalar_deleting_destructor |  | sheet | AHelicopter slot 0 (PS2 002f6b08, row 9012, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012ce10 | IsTracked | ATrafficVehicle::IsTracked |  | resolved | ATrafficVehicle slot 7 (PS2 002e6a20, row 8761, resolved) |
| 0012ce20 | GetName | ATrafficVehicle::GetName | ASubmersible::GetName, APlayerVehicle::GetName | resolved | ATrafficVehicle slot 2 (PS2 002e67e0, row 8758, resolved); ASubmersible slot 2 (PS2 002e8450, row 8781, resolved); APlayerVehicle slot 2 (PS2 002f0cd0, row 8916, resolved) |
| 0012d300 | ~ATrafficVehicle | ATrafficVehicle::~ATrafficVehicle |  | ghidra | called by ATrafficVehicle::scalar_deleting_destructor (0x0012d400) and stores ATrafficVehicle's vtable |
| 0012d400 | scalar_deleting_destructor | ATrafficVehicle::scalar_deleting_destructor |  | ghidra | ATrafficVehicle slot 0 (PS2 002e6640, row 8757, ghidra); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012d430 | Play | ATrafficVehicle::Play |  | ghidra | ATrafficVehicle slot 3 (PS2 002e5598, row 8754, ghidra) |
| 0012d6f0 | ASentry::Play | ASentry::Play |  | sheet | ASentry slot 3 (PS2 002eff88, row 8888, sheet) |
| 0012d750 | ~ASentry | ASentry::~ASentry |  | resolved | called by ASentry::scalar_deleting_destructor (0x0012d7c0) and stores ASentry's vtable |
| 0012d7c0 | scalar_deleting_destructor | ASentry::scalar_deleting_destructor |  | resolved | ASentry slot 0 (PS2 002effe8, row 8889, resolved); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012e1e0 | ~ASceneObj | ASceneObj::~ASceneObj |  | sheet | called by ASceneObj::scalar_deleting_destructor (0x0012e1f0) and stores ASceneObj's vtable |
| 0012e1f0 | scalar_deleting_destructor | ASceneObj::scalar_deleting_destructor |  | sheet | ASceneObj slot 0 (PS2 002f05c0, row 8896, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012ede0 | AEngine::Play | AEngine::Play |  | sheet | AEngine slot 0 (PS2 002fa8f0, row 9074, sheet) |
| 0012ef30 | scalar_deleting_destructor | AEngine::scalar_deleting_destructor |  | sheet | AEngine slot 1 (PS2 002faa90, row 9075, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012fa90 | ABasic::Play | ABasic::Play |  | sheet | ABasic slot 3 (PS2 002fc700, row 9101, sheet) |
| 0012fb80 | ~ABasic | ABasic::~ABasic |  | sheet | called by ABasic::scalar_deleting_destructor (0x0012fc00) and stores ABasic's vtable |
| 0012fc00 | scalar_deleting_destructor | ABasic::scalar_deleting_destructor |  | sheet | ABasic slot 0 (PS2 002fc848, row 9103, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 0012fc30 | ABasic::Stop | ABasic::Stop |  | ghidra | ABasic slot 4 (PS2 002fc7c8, row 9102, ghidra) |
| 0012ffb0 | ARaceEngine::Play | ARaceEngine::Play |  | sheet | ARaceEngine slot 0 (PS2 002ff320, row 9178, sheet) |
| 001306b0 | ARaceEngine::scalar_deleting_destructor | ARaceEngine::scalar_deleting_destructor |  | sheet | ARaceEngine slot 1 (PS2 002ff998, row 9179, sheet); vtable slot 0 on MSVC is the scalar deleting destructor |
| 00131d35 | __pure_virtual | __pure_virtual |  | ghidra | AIVehicle slot 1 (PS2 0024c5d0, row 5533, ghidra); AIVehicle slot 2 (PS2 0024c5d0, row 5533, ghidra); AIVehicle slot 3 (PS2 0024c5d0, row 5533, ghidra) ... |

