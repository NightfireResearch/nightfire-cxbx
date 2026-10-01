# Version Tracking names on Driving.xbe, checked against DRIVING.ELF
92 pairs: {'ok': 53, 'size only': 15, 'doubtful': 7, 'mixed': 10, 'no evidence': 7}. 13 call-order proposals (Counter({'size only': 6, 'ok': 4, 'doubtful': 1, 'mixed': 1, 'no evidence': 1})) in results/vt-proposals.json.


| Xbox | was | now | PS2 | strings | callees | size | verdict |
|---|---|---|---|---|---|---|---|
| 00013d30 | FUN_00013d30 | BankInfo::Load | 0010bf90 | 1.00 | 1.00 | 1.14 | ok |
| 000181a0 | FUN_000181a0 | ActModelDatabase::LoadModel | 00111748 | 1.00 | 1.00 | 1.09 | ok |
| 0001ab50 | FUN_0001ab50 | ActWeaponAux::Load | 00116208 | 1.00 | 1.00 | 1.17 | ok |
| 000248b0 | FUN_000248b0 | AICharacterPedestrian::SetActor | 001287f8 | 1.00 | 1.00 | 1.00 | ok |
| 00027ce0 | FUN_00027ce0 | AIElementTracking::GetProperty | 0012b138 | 0.80 | 1.00 | 1.01 | ok |
| 00028190 | FUN_00028190 | AIElementController::AIElementController | 0012b8b0 | 1.00 | 0.86 | 1.01 | ok |
| 00028bb0 | FUN_00028bb0 | AIElementController::WakeUp | 0012cb08 | 0.88 | 0.89 | 0.95 | ok |
| 000318d0 | FUN_000318d0 | AIHelicopter::DisableTargetBeacon | 00138588 | - | 1.00 | 0.79 | ok |
| 000359d0 | FUN_000359d0 | AIVehicle::GetSplinePath | 0013e0b0 | - | - | 1.00 | size only |
| 000362e0 | FUN_000362e0 | AIVehicleController::RegisterTrafficVehicle | 0013f868 | 1.00 | 0.50 | 1.11 | ok |
| 0003d940 | FUN_0003d940 | DTuningFile::ParseData_Colour | 00144740 | 1.00 | 1.00 | 1.11 | ok |
| 000502d0 | FUN_000502d0 | InputConfigManager::ParseMasterConfigFile | 00166548 | 0.50 | 0.50 | 1.17 | ok |
| 000507e0 | FUN_000507e0 | InputTable::GrowArrayForOneElement | 001671a8 | 1.00 | 1.00 | 1.06 | ok |
| 00055ec0 | FUN_00055ec0 | AttributeValue::Alloc | 001739f0 | 1.00 | 1.00 | 1.15 | ok |
| 00055f20 | FUN_00055f20 | String_AttribByteOffsetParserFunc | 0016c2b8 | - | 1.00 | 0.79 | ok |
| 00058020 | FUN_00058020 | AttributeSet::FindAttribute | 001699f8 | - | 0.00 | 1.79 | doubtful |
| 00059550 | AttributeSystem::InitSingleton | AttributeSystem::Init | 001738e8 | - | 1.00 | 2.00 | ok |
| 0005d5c0 | FUN_0005d5c0 | Grenade::Grenade | 0017e560 | 1.00 | 0.89 | 0.87 | ok |
| 00066c90 | FUN_00066c90 | PBondCar::TwoWheelStunt | 0018b830 | - | 0.33 | 0.96 | doubtful |
| 0006f5b0 | FUN_0006f5b0 | PhysicsObject::IsOwnedBy | 00195c70 | - | 0.33 | 1.33 | doubtful |
| 0006f810 | FUN_0006f810 | PVehicle::GetNamedAttribs | 00196740 | 1.00 | 1.00 | 1.00 | ok |
| 000749b0 | FUN_000749b0 | Shell::ApplyWorldEffects | 0019d5a8 | 0.16 | 0.86 | 0.68 | mixed |
| 00076200 | FUN_00076200 | Draw::SetNormalBlendMode | 001a0f80 | - | - | 0.77 | size only |
| 000785f0 | FUN_000785f0 | RCameraIniLoader::ResolveWeaponNames | 001a6e18 | - | 1.00 | 0.80 | ok |
| 000873e0 | FUN_000873e0 | RPlayerCamera::CheckForCameraShaking | 001b9e98 | - | 0.25 | 0.74 | doubtful |
| 0008beb0 | FUN_0008beb0 | RShadowMap::operator_new | 001ecda8 | 1.00 | 1.00 | 3.38 | mixed |
| 00090c80 | FUN_00090c80 | RSkeletalObj::AllocateSkeleton | 001cc8e8 | 1.00 | 0.67 | 0.90 | ok |
| 00092280 | FUN_00092280 | RStateManager::ParseFirstStateTag | 001ce3b0 | 0.00 | 0.00 | 1.37 | doubtful |
| 00096c60 | RViewCamera::scalar_deleting_destructor | RViewCamera::~RViewCamera | 001d4238 | - | 1.00 | 0.71 | ok |
| 000ade40 | FUN_000ade40 | RigidBody::ApplyInitialForcesAndTorques | 001f7c60 | 1.00 | 0.00 | 0.86 | mixed |
| 000afdb0 | FUN_000afdb0 | RigidBody::GenerateImpulse | 001fb318 | - | 0.00 | 1.02 | doubtful |
| 000b28d0 | FUN_000b28d0 | Simulation::GetOrderedBody | 00201590 | - | - | 0.72 | size only |
| 000b5e20 | FUN_000b5e20 | Simulation::SpawnNewtonObject | 002002e0 | 1.00 | 1.00 | 1.05 | ok |
| 000b8460 | FUN_000b8460 | SMissionManager::GetObjectiveByIndex | 00208a48 | - | - | 1.05 | size only |
| 000ba4a0 | FUN_000ba4a0 | SMissionRuleTarget::GetProperty | 00209aa0 | 1.00 | 1.00 | 0.94 | ok |
| 000bb2b0 | FUN_000bb2b0 | SWeaponManager::FireWeapon | 0020de20 | 0.85 | 0.90 | 0.96 | ok |
| 000bd030 | FUN_000bd030 | RSceneObj::Load | 001c7360 (of 2) | - | - | 0.93 | size only |
| 000c3410 | FUN_000c3410 | WCollisionMgr::GetInstanceList | 00217588 (of 2) | - | 1.00 | 0.46 | ok |
| 000c64b0 | FUN_000c64b0 | WGrid::FindNodes | 0021d298 (of 4) | - | 0.50 | 0.95 | ok |
| 000d4160 | FUN_000d4160 | GFXGallery::GLARE_Draw | 00235d50 | - | 1.00 | 1.05 | ok |
| 000d4b90 | FUN_000d4b90 | DEBRIS_Draw | none of this name | | | | no PS2 namesake |
| 000d51e0 | FUN_000d51e0 | CANVAS_Draw | none of this name | | | | no PS2 namesake |
| 000d55f0 | FUN_000d55f0 | GFXGallery::FRAME_Draw | 002345d8 | - | 1.00 | 0.94 | ok |
| 000d9ba0 | FUN_000d9ba0 | GHud::AimOn | 00244ef0 | 1.00 | 1.00 | 0.81 | ok |
| 000dc7d0 | FUN_000dc7d0 | GHud::DrawTimer | 0023e580 | 0.75 | 1.00 | 0.99 | ok |
| 000e2b70 | OSCheck::Init | GLoadingScreen::AddLoadingScreenSyncTask | 00247eb8 | - | 1.00 | 0.60 | ok |
| 000e3e20 | FUN_000e3e20 | GSystem::GetGalleryName | 0024b7d0 | 1.00 | 0.00 | 0.58 | mixed |
| 000e4030 | OpenIniFile | IniFiles::IniFiles | 0024b9c8 | 1.00 | 1.00 | 0.90 | ok |
| 000e4240 | FUN_000e4240 | IniFiles::~IniFiles | 0024bae8 | - | 1.00 | 0.83 | ok |
| 000e52a0 | FUN_000e52a0 | EAGL::DynamicLoader::Initialize | 0029ea00 | 1.00 | 0.67 | 0.87 | ok |
| 000e8970 | FUN_000e8970 | EAGL::DeviceExtension::NewTextureRenderContext | 002875a8 | 1.00 | - | 1.39 | ok |
| 000e9890 | FUN_000e9890 | EAGL::DynamicModel::AddGeoPrim | 002851f8 | 1.00 | - | 0.94 | ok |
| 000ea7b0 | FUN_000ea7b0 | EAGL::Model::Call | 00285ec0 | 1.00 | - | 2.29 | mixed |
| 000ed310 | FUN_000ed310 | RMissileStreak::RMissileStreak | 001e4200 (of 2) | - | - | 1.20 | size only |
| 000f8430 | FUN_000f8430 | EAGLAnim::EventTarget::ResolveEventId | 00263e18 | 1.00 | 1.00 | 0.93 | ok |
| 00104d60 | FUN_00104d60 | EAGLAnim::FnTurnBlender::EvalVel2D | 00279e30 | 1.00 | 1.00 | 0.87 | ok |
| 001069d0 | FUN_001069d0 | EAGLAnim::DeltaCompressedData::DecompressValues | 00276820 | - | - | 1.25 | size only |
| 00107f50 | FUN_00107f50 | SHAPE_createsize | 0025fa50 | - | - | 0.98 | size only |
| 00108000 | FUN_00108000 | SHAPE_createat | 0025fb58 | - | 1.00 | 1.02 | ok |
| 001081e0 | FUN_001081e0 | SHAPE_create | 0025fdf0 | 1.00 | 1.00 | 1.13 | ok |
| 0010c530 | FUN_0010c530 | FILESYS_opstatus | 002525a8 | - | - | 1.97 | no evidence |
| 0010c700 | FUN_0010c700 | FILESYS_completeop | 00252a28 | - | - | 1.41 | size only |
| 00113603 | FUN_00113603 | VU0_quattom4 | 002cc0c8 | - | - | 2.07 | no evidence |
| 0011388d | FUN_0011388d | VU0_m4toquat | 002cc690 | - | - | 0.76 | size only |
| 001140a0 | FUN_001140a0 | MEM_tailsize | 002ca740 | - | - | 0.62 | size only |
| 00114340 | FUN_00114340 | MEM_allocalign | 002cacb0 | - | - | 2.00 | no evidence |
| 00114370 | FUN_00114370 | MEM_alloc | 002cacd0 | - | - | 1.20 | size only |
| 00117010 | FUN_00117010 | UFileLoader::FileExists | 002d74f8 | - | 1.00 | 1.55 | ok |
| 0011a110 | FUN_0011a110 | StringToNumber::ConvertStringToNumber | 002cec98 | - | - | 1.49 | size only |
| 0011b690 | FUN_0011b690 | UCarpNamespace::operator_new | 002cea68 | 1.00 | 0.50 | 6.12 | mixed |
| 0011dad0 | FUN_0011dad0 | ACharacter::Say | 002fbe00 | 0.33 | 1.00 | 0.78 | mixed |
| 0011df50 | FUN_0011df50 | AMenuSound::Trigger | 002f4a20 (of 3) | 1.00 | 1.00 | 1.03 | ok |
| 0011ea80 | FUN_0011ea80 | AVehicle::ActivateZoom | 002df6e8 | 1.00 | 1.00 | 1.58 | ok |
| 00123870 | FUN_00123870 | AVoice::View::Restart | 002de060 | - | 1.00 | 1.67 | ok |
| 00124c40 | FUN_00124c40 | AFader::Priv::Priv | 002f8d58 | 1.00 | 1.00 | 0.94 | ok |
| 00125180 | FUN_00125180 | AFader::AFader | 002f95b0 | 1.00 | 1.00 | 1.29 | ok |
| 00125c20 | FUN_00125c20 | URefCounter<AFader>::Get | 002fa880 | - | 1.00 | 1.48 | ok |
| 00128a40 | FUN_00128a40 | APlayerHeli::APlayerHeli | 002f1428 | 1.00 | 1.00 | 1.27 | ok |
| 00129550 | FUN_00129550 | AGun::AGun | 002f6e60 | 0.67 | 1.00 | 0.84 | ok |
| 00129830 | FUN_00129830 | AFingoDeath::Play | 002f8590 | 1.00 | 0.89 | 0.65 | ok |
| 0012aa10 | FUN_0012aa10 | ASnowMobile::PlayMotor | 002ede60 | 1.00 | 0.88 | 0.74 | ok |
| 0012bb80 | FUN_0012bb80 | ASubmersible::PlayCreaks | 002e73b8 | 0.20 | 1.00 | 0.64 | mixed |
| 0012bf00 | FUN_0012bf00 | ASubmersible::PlayCavitation | 002e76f8 | 1.00 | 1.00 | 0.59 | ok |
| 0012c260 | FUN_0012c260 | ASubmersible::PlayServos | 002e7d60 | 0.83 | 1.00 | 0.39 | mixed |
| 0012e220 | FUN_0012e220 | ATurret::ATurret | 002e4930 | 1.00 | 1.00 | 0.84 | ok |
| 0013282a | FUN_0013282a | strtok | 002aabd8 | - | - | 8.67 | no evidence |
| 00132a7b | _atexit | atexit | 002a71f8 | - | 0.00 | 0.21 | doubtful |
| 0013b9e0 | FUN_0013b9e0 | SNDSTRMI_startstream | 00308760 | - | 1.00 | 0.74 | ok |
| 00144460 | SFILTER_add | SFILTER_addtofilterlist | 003143f8 | - | - | 1.12 | size only |
| 0014a1e0 | FUN_0014a1e0 | decode16x87 | 00314fb0 | - | - | 0.57 | size only |
| 0014a4f0 | REALMUTEX_create | MUTEX_create | 002617c8 | - | - | 0.35 | no evidence |
| 0014a520 | REALMUTEX_lock | MUTEX_lock | 00261830 | - | - | 0.21 | no evidence |
| 0014a530 | REALMUTEX_unlock | MUTEX_unlock | 00261890 | - | - | 0.19 | no evidence |
| 0014c9f0 | FUN_0014c9f0 | RCMP::AUDIO_PLAYER::IsAudioFinished | 00301c30 | - | 1.00 | 4.32 | mixed |
