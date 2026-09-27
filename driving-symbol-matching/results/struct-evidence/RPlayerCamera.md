# RPlayerCamera

FastAlloc/constructed sizes under its tag: {'allocated': [880], 'constructed': [76]}
deleting destructor 0x84a30 frees/deletes with size 0x370 (call to UMemory::FastFree)
Xbox vtable 0x001918ac (7 slots) stored by its constructor
PS2 sheet virtual table row: ['RPlayerCamera virtual table']
constructor 0x89060 first calls: ['RWorldCamera::RWorldCamera', 'UMemory::FastAlloc', 'WWorldPos::WWorldPos']

Xbox methods (76):
  0x80a60 undefined __stdcall Shutdown(void)
  0x80b90 undefined UpdateBumperCam(void)
  0x80cf0 undefined UpdateAIPathAnimationCam(void)
  0x80e20 undefined GetZoomPercent(void)
  0x80e80 undefined SetupCameraZoom(void)
  0x81110 undefined GetSafeWRoadNavPosition(undefined4 param_1, undefined4 param_2)
  0x811d0 undefined FindHeliArmInd(undefined4 param_1)
  0x81290 undefined FindAutoDriveArmInd(undefined4 param_1)
  0x81340 undefined DirectorSetAnchor(undefined4 param_1)
  0x81430 undefined ResetCamera(void)
  0x81460 undefined DoSmoothModeChange(void)
  0x814c0 undefined SetCameraLookBack(undefined1 param_1)
  0x81540 undefined SetAutoDriveRotation(undefined4 param_1)
  0x81610 undefined CameraLockOn(undefined4 param_1)
  0x816e0 undefined SetAutoDriveZoom(undefined4 param_1)
  0x81700 undefined ToggleAutoDriveZoom(undefined1 param_1)
  0x81740 undefined TriggerCarAnimationCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 par
  0x817f0 undefined PauseOff(void)
  0x81840 undefined CameraInputCallback(undefined4 param_1, undefined4 param_2)
  0x81890 undefined GetForwardAimVec4(undefined4 param_1)
  0x81930 undefined SetAutoDriveForwardLock(undefined1 param_1)
  0x81940 undefined SetControlToCPU(undefined1 param_1)
  0x81950 undefined WeaponFired(undefined4 param_1)
  0x81980 undefined CameraAiming(void)
  0x819a0 undefined InitSpin(void)
  0x81a20 undefined InitWeaponChange(void)
  0x81a70 undefined InitAimZoom(void)
  0x81ad0 undefined ResetZoomSlope(void)
  0x81af0 undefined GetMaxTumble(void)
  0x81bf0 undefined ~RPlayerCamera(void)
  0x81d10 undefined UpdateDashboardCam(void)
  0x82400 undefined UpdateWorldAnimationCam(void)
  0x82840 undefined UpdateRelativeAnimationCam(void)
  0x82e00 undefined UpdateADTargetAngles(undefined4 param_1, undefined4 param_2)
  0x83190 undefined LimitPitchYaw(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x83940 undefined SetCameraZoom(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x83b40 undefined AdjustCamAroundObjectEllipse(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x83d70 undefined InitTransition(undefined1 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x83e10 undefined UpdateTransition(undefined4 param_1, undefined4 param_2)
  0x83e60 undefined ShakeCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x83f50 undefined SetCameraModeByIndex(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4,
  0x84030 undefined NextCameraMode(undefined4 param_1)
  0x840b0 undefined PrevCameraMode(undefined4 param_1)
  0x84130 undefined DirectorChangeCameraMode(undefined4 param_1)
  0x84680 undefined UpdateCurrentArm(void)
  0x847f0 undefined SetAutoDriveRotationX(undefined4 param_1)
  0x84820 undefined SetAutoDriveRotationY(undefined4 param_1)
  0x84850 undefined TriggerFixedCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x848d0 undefined SetCinematicCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x84a30 undefined scalar_deleting_destructor(undefined1 param_1)
  0x84a60 undefined UpdateSplineCam(void)
  0x85160 undefined UpdateFixedCam(void)
  0x853e0 undefined UpdateADWeaponAnims(void)
  0x857d0 undefined UpdateAutoDriveCam(void)
  0x86bb0 undefined CheckObjectCollisions(undefined4 param_1)
  0x86d00 undefined ResolveAllCollisions(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0x873e0 undefined CheckForCameraShaking(void)
  0x876b0 undefined GetAimMatrix4(undefined4 param_1, undefined4 param_2)
  0x877e0 undefined UpdateCamera(void)
  0x87880 undefined UpdateMomentumHeliCam(void)
  0x88380 undefined UpdateTumbleCam(void)
  0x886f0 undefined UpdateEllipseCam(void)
  0x88b10 undefined SetAutoDriveCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x88be0 undefined SetMissileCamera(undefined4 param_1)
  0x88cc0 undefined SetPauseCamera(void)
  0x88d10 undefined SetTumbleCam(undefined4 param_1)
  0x88d70 undefined RestartCamera(void)
  0x89010 undefined AbortCinematic(void)
  0x89060 undefined RPlayerCamera(void)
  0x89200 undefined SetOldCameraMode(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4)
  0x894d0 undefined SetLastSelectableCameraMode(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 p
  0x895e0 undefined EndMissileCamera(void)
  0x89630 undefined TriggerAnimationCamera(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_
  0x898d0 undefined TriggerAIPathAnimationCamera(undefined param_1, undefined param_2, undefined param_3, undefined4 par
  0x89a40 undefined ForceCameraChange(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x89ac0 undefined EndCameraAnim(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)

PS2 methods (87):
  0x1b2620 RPlayerCamera::RPlayerCamera
  0x1b2728 RPlayerCamera::~RPlayerCamera
  0x1b2808 RPlayerCamera::Shutdown
  0x1b29e0 RPlayerCamera::UpdateCamera
  0x1b2b48 RPlayerCamera::UpdateMomentumHeliCam
  0x1b3788 RPlayerCamera::UpdateTumbleCam
  0x1b3b40 RPlayerCamera::UpdateBumperCam
  0x1b3ce0 RPlayerCamera::UpdateDashboardCam
  0x1b4430 RPlayerCamera::UpdateSplineCam
  0x1b4c30 RPlayerCamera::UpdateEllipseCam
  0x1b5120 RPlayerCamera::UpdateFixedCam
  0x1b53c0 RPlayerCamera::UpdateWorldAnimationCam
  0x1b5998 RPlayerCamera::UpdateRelativeAnimationCam
  0x1b6160 RPlayerCamera::UpdateAIPathAnimationCam
  0x1b62d0 RPlayerCamera::UpdateADWeaponAnims
  0x1b6848 RPlayerCamera::UpdateADTargetAngles
  0x1b6c60 RPlayerCamera::LimitPitchYaw
  0x1b72f0 RPlayerCamera::UpdateAutoDriveCam
  0x1b8740 RPlayerCamera::SetCameraZoom
  0x1b8960 RPlayerCamera::GetZoomPercent
  0x1b89e0 RPlayerCamera::SetupCameraZoom
  0x1b8ae0 RPlayerCamera::CheckObjectCollisions
  0x1b8d08 RPlayerCamera::AdjustCamAroundObjectEllipse
  0x1b9198 RPlayerCamera::ResolveAllCollisions
  0x1b9ab8 RPlayerCamera::GetSafeWRoadNavPosition
  0x1b9cf8 RPlayerCamera::InitTransition
  0x1b9de8 RPlayerCamera::UpdateTransition
  0x1b9e58 RPlayerCamera::ClearTransition
  0x1b9e68 RPlayerCamera::SetTransition
  0x1b9e80 RPlayerCamera::DeincrementTransition
  0x1b9e98 RPlayerCamera::CheckForCameraShaking
  0x1ba2f8 RPlayerCamera::ShakeCamera
  0x1ba3f8 RPlayerCamera::SetCameraModeByQueueData
  0x1ba418 RPlayerCamera::SetCameraModeByIndex
  0x1ba518 RPlayerCamera::SetCameraModeByType
  0x1ba5c0 RPlayerCamera::NextCameraMode
  0x1ba6b0 RPlayerCamera::PrevCameraMode
  0x1ba7a0 RPlayerCamera::DirectorChangeCameraMode
  0x1badd0 RPlayerCamera::FindHeliArmInd
  0x1baef8 RPlayerCamera::FindAutoDriveArmInd
  0x1bb010 RPlayerCamera::UpdateCurrentArm
  0x1bb098 RPlayerCamera::EndCameraAnim
  0x1bb1f8 RPlayerCamera::SetLastSelectableCameraMode
  0x1bb340 RPlayerCamera::SetOldCameraMode
  0x1bb7c0 RPlayerCamera::SetAutoDriveCamera
  0x1bb8a8 RPlayerCamera::SetMissileCamera
  0x1bb970 RPlayerCamera::EndMissileCamera
  0x1bb9e8 RPlayerCamera::SetPauseCamera
  0x1bba38 RPlayerCamera::DirectorSetAnchor
  0x1bbb78 RPlayerCamera::SetCollisionCam
  0x1bbbf8 RPlayerCamera::SetTumbleCam
  0x1bbc58 RPlayerCamera::ResetCamera
  0x1bbc90 RPlayerCamera::RestartCamera
  0x1bbf70 RPlayerCamera::DoSmoothModeChange
  0x1bc048 RPlayerCamera::SetCameraLookBack
  0x1bc0e0 RPlayerCamera::SetAutoDriveRotation
  0x1bc390 RPlayerCamera::SetAutoDriveRotationX
  0x1bc3c8 RPlayerCamera::SetAutoDriveRotationY
  0x1bc400 RPlayerCamera::CameraLockOn
  0x1bc4a0 RPlayerCamera::SetAutoDriveZoom
  0x1bc4c0 RPlayerCamera::ToggleAutoDriveZoom
  0x1bc510 RPlayerCamera::TriggerAnimationCamera
  0x1bc890 RPlayerCamera::TriggerAIPathAnimationCamera
  0x1bc9f0 RPlayerCamera::TriggerCarAnimationCamera
  0x1bcab8 RPlayerCamera::TriggerFixedCamera
  0x1bcb88 RPlayerCamera::PauseOff
  0x1bcc00 RPlayerCamera::CameraInputCallback
  0x1bcc70 RPlayerCamera::LoadCameraIniFile
  0x1bcca0 RPlayerCamera::GetForwardAimVec4
  0x1bcd10 RPlayerCamera::GetAimMatrix4
  0x1bce60 RPlayerCamera::CreateDBVars
  0x1bce68 RPlayerCamera::GetAutoDriveWeapTransform
  0x1bd070 RPlayerCamera::SetAutoDriveForwardLock
  0x1bd078 RPlayerCamera::SetCinematicCamera
  0x1bd198 RPlayerCamera::SetControlToCPU
  0x1bd1b8 RPlayerCamera::ForceCameraChange
  0x1bd288 RPlayerCamera::WeaponFired
  0x1bd2b8 RPlayerCamera::AbortCinematic
  0x1bd2f8 RPlayerCamera::CameraAiming
  0x1bd338 RPlayerCamera::InitSpin
  0x1bd3c8 RPlayerCamera::InitWeaponChange
  0x1bd440 RPlayerCamera::InitAimZoom
  0x1bd4a0 RPlayerCamera::ResetZoomSlope
  0x1bd4b8 RPlayerCamera::GetMaxTumble
  0x1bd878 RPlayerCamera::operator_new
  0x1bd898 RPlayerCamera::operator_delete
  0x1bda78 RPlayerCamera::fBumperCamInfo_global_ctors

Sheet rows:
  RPlayerCamera::RPlayerCamera(void)
  RPlayerCamera::~RPlayerCamera(void)
  RPlayerCamera::Shutdown(void)
  RPlayerCamera::UpdateCamera(void)
  RPlayerCamera::UpdateMomentumHeliCam(void)
  RPlayerCamera::UpdateCollisionCam(void)
  RPlayerCamera::UpdateTumbleCam(void)
  RPlayerCamera::UpdateBumperCam(void)
  RPlayerCamera::UpdateDashboardCam(void)
  RPlayerCamera::UpdateSplineCam(void)
  RPlayerCamera::UpdateEllipseCam(void)
  RPlayerCamera::UpdateFixedCam(void)
  RPlayerCamera::UpdateWorldAnimationCam(void)
  RPlayerCamera::UpdateRelativeAnimationCam(void)
  RPlayerCamera::UpdateAIPathAnimationCam(void)
  RPlayerCamera::UpdateADWeaponAnims(void)
  RPlayerCamera::UpdateADTargetAngles(float &, float &)
  RPlayerCamera::LimitPitchYaw(RPlayerCamera::AutoDriveArmInfo &,
  RPlayerCamera::UpdateAutoDriveCam(void)
  RPlayerCamera::SetCameraZoom(CameraZoomType, float, int)
  RPlayerCamera::GetZoomPercent(void)
  RPlayerCamera::SetupCameraZoom(void)
  RPlayerCamera::CheckObjectCollisions(COORD4 &)
  RPlayerCamera::AdjustCamAroundObjectEllipse(COORD4 &, RigidBody
  RPlayerCamera::CheckCollisionSegment(COORD4 &, COORD4 &)
  RPlayerCamera::ResolveAllCollisions(COORD4 &, COORD4 &, bool)
  RPlayerCamera::GetSafeWRoadNavPosition(COORD4 &, float &)
  RPlayerCamera::InitTransition(char, COORD4 &, COORD4 &, int)
  RPlayerCamera::UpdateTransition(COORD4 &, COORD4 &)
  RPlayerCamera::ClearTransition(void)
  RPlayerCamera::SetTransition(unsigned int, char)
  RPlayerCamera::DeincrementTransition(void)
  RPlayerCamera::CheckForCameraShaking(void)
  RPlayerCamera::ShakeCamera(int, float, COORD4 &, int)
  RPlayerCamera::SetCameraModeByQueueData(RDirectorQueueData &)
  RPlayerCamera::SetCameraModeByIndex(unsigned int, unsigned shor
  RPlayerCamera::SetCameraModeByType(unsigned int, unsigned int,
  RPlayerCamera::NextCameraMode(unsigned short)
  RPlayerCamera::PrevCameraMode(unsigned short)
  RPlayerCamera::DirectorChangeCameraMode(RDirectorQueueData &)
  RPlayerCamera::FindHeliArmInd(unsigned int)
  RPlayerCamera::FindAutoDriveArmInd(unsigned int)
  RPlayerCamera::UpdateCurrentArm(void)
  RPlayerCamera::EndCameraAnim(unsigned short, unsigned int, bool
  RPlayerCamera::SetLastSelectableCameraMode(unsigned short, unsi
  RPlayerCamera::SetOldCameraMode(unsigned int, unsigned short, u
  RPlayerCamera::SetAutoDriveCamera(float, float, int)
  RPlayerCamera::SetMissileCamera(PhysicsObject *)
  RPlayerCamera::EndMissileCamera(void)
  RPlayerCamera::SetPauseCamera(void)
  RPlayerCamera::DirectorSetAnchor(RDirectorQueueData *)
  RPlayerCamera::SetCollisionCam(unsigned int)
  RPlayerCamera::SetTumbleCam(unsigned int)
  RPlayerCamera::ResetCamera(void)
  RPlayerCamera::RestartCamera(void)
  RPlayerCamera::DoSmoothModeChange(void)
  RPlayerCamera::ToggleCameraLookBack(void)
  RPlayerCamera::SetCameraLookBack(bool)
  RPlayerCamera::SetAutoDriveRotation(float)
  RPlayerCamera::SetAutoDriveRotationX(float)
  RPlayerCamera::SetAutoDriveRotationY(float)
  RPlayerCamera::CameraLockOn(COORD4 *, COORD4 &, unsigned int, i
  RPlayerCamera::SetAutoDriveZoom(float)
  RPlayerCamera::ToggleAutoDriveZoom(bool)
  RPlayerCamera::TriggerAnimationCamera(CARP::Instance *, unsigne
  RPlayerCamera::TriggerAIPathAnimationCamera(CARP::AISpline *, u
  RPlayerCamera::TriggerCarAnimationCamera(unsigned int, unsigned
  RPlayerCamera::TriggerFixedCamera(COORD4 &, int, unsigned short
  RPlayerCamera::PauseOff(void)
  RPlayerCamera::CameraInputCallback(int, float)
  RPlayerCamera::LoadCameraIniFile(void)
  RPlayerCamera::SetSpecialDebugCamera(void)
  RPlayerCamera::DrawValidate(void)
  RPlayerCamera::GetForwardAimVec4(float)
  RPlayerCamera::GetAimMatrix4(float, float)
  RPlayerCamera::CreateDBVars(void)
  RPlayerCamera::GetAutoDriveWeapTransform(int, MATRIX4 &, float,
  RPlayerCamera::SetAutoDriveForwardLock(bool)
  RPlayerCamera::SetCinematicCamera(COORD4 &, int, int)
  RPlayerCamera::SetControlToCPU(bool)

Xbox methods treated as members (70 of 76; untyped ones count when ECX is read before it is written): AbortCinematic, CameraAiming, CameraInputCallback, CameraLockOn, CheckForCameraShaking, CheckObjectCollisions, DirectorChangeCameraMode, DirectorSetAnchor, DoSmoothModeChange, EndCameraAnim, EndMissileCamera, GetAimMatrix4, GetForwardAimVec4, GetMaxTumble, GetSafeWRoadNavPosition, GetZoomPercent, InitAimZoom, InitSpin, InitTransition, InitWeaponChange, NextCameraMode, PauseOff, PrevCameraMode, RPlayerCamera, ResetCamera, ResetZoomSlope, ResolveAllCollisions, RestartCamera, SetAutoDriveCamera, SetAutoDriveForwardLock, SetAutoDriveRotation, SetAutoDriveRotationX, SetAutoDriveRotationY, SetAutoDriveZoom, SetCameraLookBack, SetCameraModeByIndex, SetCameraZoom, SetCinematicCamera, SetControlToCPU, SetLastSelectableCameraMode, SetMissileCamera, SetOldCameraMode, SetPauseCamera, SetTumbleCam, SetupCameraZoom, ShakeCamera, ToggleAutoDriveZoom, TriggerAIPathAnimationCamera, TriggerAnimationCamera, TriggerCarAnimationCamera, TriggerFixedCamera, UpdateADTargetAngles, UpdateADWeaponAnims, UpdateAIPathAnimationCam, UpdateAutoDriveCam, UpdateBumperCam, UpdateCamera, UpdateCurrentArm, UpdateDashboardCam, UpdateEllipseCam, UpdateFixedCam, UpdateMomentumHeliCam, UpdateRelativeAnimationCam, UpdateSplineCam, UpdateTransition, UpdateTumbleCam, UpdateWorldAnimationCam, WeaponFired, scalar_deleting_destructor, ~RPlayerCamera

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: RPlayerCamera@89060, SetAutoDriveCamera@88b10, ~RPlayerCamera@81bf0]
  +0x010  w- LEA addr-taken [6: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateADWeaponAnims@853e0, UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x01c  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x02c  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x030  w[4] LEA/R addr-taken [6: DirectorChangeCameraMode@84130, GetForwardAimVec4@81890, RestartCamera@88d70, UpdateADWeaponAnims@853e0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x034  w[4] R [2: DirectorChangeCameraMode@84130, UpdateADWeaponAnims@853e0]
  +0x038  w[4] R [2: DirectorChangeCameraMode@84130, UpdateADWeaponAnims@853e0]
  +0x03c  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x040  w[4] LEA/W addr-taken [4: UpdateADWeaponAnims@853e0, UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x044  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x048  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x04c  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x050  w[4] LEA/W addr-taken -> FUN_00115c80 [3: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x054  w[4] W [1: UpdateAIPathAnimationCam@80cf0]
  +0x058  w[4] W [1: UpdateAIPathAnimationCam@80cf0]
  +0x060  w[1] W [6: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateADWeaponAnims@853e0, UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x0b0  w[1] R [1: UpdateCamera@877e0]
  +0x0b4  w[4] W float [4: GetZoomPercent@80e20, TriggerAnimationCamera@89630, UpdateBumperCam@80b90, UpdateWorldAnimationCam@82400]
  +0x0c0  w- LEA addr-taken [5: UpdateADWeaponAnims@853e0, UpdateAIPathAnimationCam@80cf0, UpdateBumperCam@80b90, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x0c4  w[4] R/W float [3: UpdateAIPathAnimationCam@80cf0, UpdateBumperCam@80b90, UpdateRelativeAnimationCam@82840]
  +0x0c8  w[4] R/W [3: UpdateAIPathAnimationCam@80cf0, UpdateBumperCam@80b90, UpdateRelativeAnimationCam@82840]
  +0x0d0  w- LEA addr-taken [5: UpdateAIPathAnimationCam@80cf0, UpdateBumperCam@80b90, UpdateFixedCam@85160, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x0e0  w- LEA addr-taken [3: SetOldCameraMode@89200, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x0f0  w[1, 4] R/RW/W [13: DirectorChangeCameraMode@84130, DoSmoothModeChange@81460, EndMissileCamera@895e0, InitWeaponChange@81a20, ResetCamera@81430, RestartCamera@88d70…]
  +0x0f4  w[4] W float [1: UpdateWorldAnimationCam@82400]
  +0x0f8  w[4] R/W float [1: UpdateWorldAnimationCam@82400]
  +0x114  w[4] R [2: UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x120  w[4] R [1: UpdateAIPathAnimationCam@80cf0]
  +0x128  w[4] R [1: SetOldCameraMode@89200]
  +0x130  w[4] R/W [23: DirectorChangeCameraMode@84130, DoSmoothModeChange@81460, GetZoomPercent@80e20, InitAimZoom@81a70, InitWeaponChange@81a20, NextCameraMode@84030…]
  +0x134  w[4] R/W [4: DirectorChangeCameraMode@84130, DoSmoothModeChange@81460, SetLastSelectableCameraMode@894d0, UpdateTumbleCam@88380]
  +0x138  w[4] R/W [2: DirectorChangeCameraMode@84130, SetLastSelectableCameraMode@894d0]
  +0x13c  w[4] R/RW/W [8: AbortCinematic@89010, DirectorChangeCameraMode@84130, InitTransition@83d70, ResetCamera@81430, RestartCamera@88d70, UpdateRelativeAnimationCam@82840…]
  +0x140  w[4] W [3: ResetCamera@81430, RestartCamera@88d70, SetTumbleCam@88d10]
  +0x144  w[1] W [1: RPlayerCamera@89060]
  +0x145  w[1] W [1: RPlayerCamera@89060]
  +0x148  w[4] R/RW/W [3: RestartCamera@88d70, SetAutoDriveCamera@88b10, UpdateCamera@877e0]
  +0x150  w[4] W [1: RestartCamera@88d70]
  +0x160  w- LEA addr-taken [1: RestartCamera@88d70]
  +0x170  w[4] W [1: RestartCamera@88d70]
  +0x174  w[4] W [1: RestartCamera@88d70]
  +0x178  w[1] W [2: RestartCamera@88d70, SetAutoDriveForwardLock@81930]
  +0x180  w- LEA addr-taken [3: DirectorChangeCameraMode@84130, GetForwardAimVec4@81890, RestartCamera@88d70]
  +0x190  w- LEA addr-taken [2: GetAimMatrix4@876b0, RestartCamera@88d70]
  +0x1d0  w[4] W [1: RestartCamera@88d70]
  +0x1d4  w[4] W [1: RestartCamera@88d70]
  +0x1d8  w[4] W [1: RestartCamera@88d70]
  +0x1dc  w[4] W [1: RestartCamera@88d70]
  +0x1e0  w[4] W [2: InitAimZoom@81a70, RestartCamera@88d70]
  +0x1e4  w[4] W [2: InitAimZoom@81a70, RestartCamera@88d70]
  +0x1e8  w[4] W float [3: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateADWeaponAnims@853e0]
  +0x1ec  w[4] W float [3: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateADWeaponAnims@853e0]
  +0x1f0  w[4] W [1: RestartCamera@88d70]
  +0x1f4  w[4] W [1: RestartCamera@88d70]
  +0x1f8  w[4] R/W [2: RestartCamera@88d70, UpdateADWeaponAnims@853e0]
  +0x1fc  w[1] R/W [3: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateADWeaponAnims@853e0]
  +0x200  w[4] W [1: RestartCamera@88d70]
  +0x204  w[4] W [2: RestartCamera@88d70, WeaponFired@81950]
  +0x208  w[4] W [2: InitSpin@819a0, RestartCamera@88d70]
  +0x210  w[4] LEA/W float addr-taken [2: InitSpin@819a0, RestartCamera@88d70]
  +0x214  w[4] W float [1: InitSpin@819a0]
  +0x218  w[4] W float [1: InitSpin@819a0]
  +0x21c  w[4] W [1: InitSpin@819a0]
  +0x220  w[1] W [1: RestartCamera@88d70]
  +0x221  w[1] W [1: RestartCamera@88d70]
  +0x224  w[4] W [2: ResolveAllCollisions@86d00, RestartCamera@88d70]
  +0x228  w[4] R/W float [4: RestartCamera@88d70, UpdateMomentumHeliCam@87880, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x22c  w[4] W float [5: InitTransition@83d70, RestartCamera@88d70, UpdateRelativeAnimationCam@82840, UpdateTransition@83e10, UpdateWorldAnimationCam@82400]
  +0x230  w- LEA addr-taken [1: RestartCamera@88d70]
  +0x23c  w[4] W [1: RestartCamera@88d70]
  +0x240  w- LEA addr-taken -> VU0_v4Init [1: RestartCamera@88d70]
  +0x24c  w[4] W [1: RestartCamera@88d70]
  +0x250  w[4] W [1: RestartCamera@88d70]
  +0x254  w[4] W [1: RestartCamera@88d70]
  +0x258  w[4] W [2: DirectorChangeCameraMode@84130, RestartCamera@88d70]
  +0x25c  w[4] W [1: RestartCamera@88d70]
  +0x260  w[4] R/W float [3: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateRelativeAnimationCam@82840]
  +0x264  w[4] W [1: RestartCamera@88d70]
  +0x268  w[4] W [1: RestartCamera@88d70]
  +0x26c  w[4] R/W [4: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x270  w[4] R/W [3: CheckForCameraShaking@873e0, DirectorChangeCameraMode@84130, RestartCamera@88d70]
  +0x274  w[4] W [2: CheckForCameraShaking@873e0, RestartCamera@88d70]
  +0x278  w[1] R/W [2: RestartCamera@88d70, UpdateCamera@877e0]
  +0x279  w[1] W [4: DirectorChangeCameraMode@84130, InitTransition@83d70, ResetCamera@81430, RestartCamera@88d70]
  +0x27a  w[1] W [5: DirectorChangeCameraMode@84130, InitTransition@83d70, ResetCamera@81430, RestartCamera@88d70, UpdateTransition@83e10]
  +0x27b  w[1] R/W [5: DirectorChangeCameraMode@84130, RestartCamera@88d70, UpdateCurrentArm@84680, UpdateMomentumHeliCam@87880, UpdateTumbleCam@88380]
  +0x27c  w[1] R/W [6: GetZoomPercent@80e20, InitAimZoom@81a70, InitWeaponChange@81a20, RestartCamera@88d70, ToggleAutoDriveZoom@81700, UpdateADWeaponAnims@853e0]
  +0x27d  w[1] W [1: RestartCamera@88d70]
  +0x290  w[4] W [1: RestartCamera@88d70]
  +0x294  w[4] W [1: RestartCamera@88d70]
  +0x298  w[4] W float [3: InitAimZoom@81a70, SetupCameraZoom@80e80, ToggleAutoDriveZoom@81700]
  +0x29c  w[4] W float [1: SetupCameraZoom@80e80]
  +0x2a0  w[4] LEA/W addr-taken -> FUN_000846d0 [3: RestartCamera@88d70, SetAutoDriveRotationX@847f0, SetAutoDriveRotationY@84820]
  +0x2a4  w[4] LEA/W addr-taken [3: RestartCamera@88d70, SetAutoDriveRotationX@847f0, SetAutoDriveRotationY@84820]
  +0x2a8  w[1] R/W [3: RestartCamera@88d70, SetAutoDriveRotationX@847f0, SetAutoDriveRotationY@84820]
  +0x2ac  w[4] W float [5: ResetZoomSlope@81ad0, RestartCamera@88d70, SetAutoDriveZoom@816e0, SetCameraZoom@83940, SetupCameraZoom@80e80]
  +0x2b0  w[4] W float [2: SetCameraZoom@83940, SetupCameraZoom@80e80]
  +0x2b4  w[4] W [4: ResetZoomSlope@81ad0, RestartCamera@88d70, SetAutoDriveZoom@816e0, SetupCameraZoom@80e80]
  +0x2c0  w- LEA addr-taken [1: RestartCamera@88d70]
  +0x2c4  w[4] W [1: RestartCamera@88d70]
  +0x2d0  w- LEA addr-taken [1: RestartCamera@88d70]
  +0x2e0  w- LEA addr-taken [2: InitTransition@83d70, RestartCamera@88d70]
  +0x2ec  w[4] W float [4: InitTransition@83d70, UpdateRelativeAnimationCam@82840, UpdateTransition@83e10, UpdateWorldAnimationCam@82400]
  +0x2f0  w[4] LEA/W addr-taken -> VU0_v3add [4: RestartCamera@88d70, UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400]
  +0x2f4  w[4] W [1: UpdateAIPathAnimationCam@80cf0]
  +0x2f8  w[4] W [1: UpdateAIPathAnimationCam@80cf0]
  +0x2fc  w[4] W [2: UpdateAIPathAnimationCam@80cf0, UpdateRelativeAnimationCam@82840]
  +0x300  w- LEA addr-taken -> VU0_v4Init [2: RestartCamera@88d70, TriggerFixedCamera@84850]
  +0x310  w- LEA addr-taken [2: RestartCamera@88d70, UpdateRelativeAnimationCam@82840]
  +0x340  w- LEA addr-taken -> VU0_MATRIX4_vect3rotate [1: UpdateRelativeAnimationCam@82840]
  +0x350  w[4] R/W -> RCameraSpline::BuildSpline, RCameraSpline::ClearSplinePtList, RCameraSpline::EvaluateSpline, RCameraSpline::GetPointList [5: DirectorChangeCameraMode@84130, RPlayerCamera@89060, UpdateRelativeAnimationCam@82840, UpdateWorldAnimationCam@82400, ~RPlayerCamera@81bf0]
  +0x354  w[4] R/W -> RPlayerCamState::AutoDriveCamInputHandler, RPlayerCamState::ResetState, RPlayerCamState::ResetStateForAnimation, dummyNu [18: AbortCinematic@89010, CameraAiming@81980, CameraInputCallback@81840, DirectorChangeCameraMode@84130, EndCameraAnim@89ac0, EndMissileCamera@895e0…]
  +0x358  w[4] R/W -> dummyNullFunction [2: RPlayerCamera@89060, ~RPlayerCamera@81bf0]
  +0x35c  w[4] R/W -> WCollider::~WCollider [3: RPlayerCamera@89060, RestartCamera@88d70, ~RPlayerCamera@81bf0]
  +0x360  w[4] R/W -> WWorldMath::GetPlaneY, WWorldPos::FindClosestFace, dummyNullFunction [3: GetSafeWRoadNavPosition@81110, RPlayerCamera@89060, ~RPlayerCamera@81bf0]
  +0x364  w[4] R/W -> WRoadNav::~WRoadNav [3: GetSafeWRoadNavPosition@81110, RPlayerCamera@89060, ~RPlayerCamera@81bf0]
  +0x368  w[4] R/W -> RDirectorQueue::AppendData, RDirectorQueue::ProcessDirectorLogic, RDirectorQueue::RestartDirectorQueue, RDirectorQueue:: [13: AbortCinematic@89010, RPlayerCamera@89060, RestartCamera@88d70, SetAutoDriveCamera@88b10, SetCameraModeByIndex@83f50, SetMissileCamera@88be0…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8, 16] R/W [3: RestartCamera@1bbc90, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0]
  +0x008  w[8] W [1: RestartCamera@1bbc90]
  +0x00c  w[4] W [3: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateRelativeAnimationCam@1b5998]
  +0x010  w[8, 16] LEA/R/W addr-taken [3: RestartCamera@1bbc90, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0]
  +0x018  w[8] W [1: RestartCamera@1bbc90]
  +0x01c  w[4] W [3: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateRelativeAnimationCam@1b5998]
  +0x020  w[4, 8, 16] LEA/R/W float addr-taken [7: DirectorChangeCameraMode@1ba7a0, GetForwardAimVec4@1bcca0, RestartCamera@1bbc90, UpdateADTargetAngles@1b6848, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160…]
  +0x024  w[4] R float [3: DirectorChangeCameraMode@1ba7a0, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x028  w[4, 8] R/W float [5: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateADTargetAngles@1b6848, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x02c  w[4] W [3: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateRelativeAnimationCam@1b5998]
  +0x030  w[4, 8, 16] R/W float [8: CheckForCameraShaking@1b9e98, RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateFixedCam@1b5120…]
  +0x034  w[4] W float [2: UpdateAIPathAnimationCam@1b6160, UpdateRelativeAnimationCam@1b5998]
  +0x038  w[4, 8] R/W float [7: RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateFixedCam@1b5120, UpdateRelativeAnimationCam@1b5998…]
  +0x03c  w[4] W float [2: UpdateAIPathAnimationCam@1b6160, UpdateRelativeAnimationCam@1b5998]
  +0x040  w[4, 16] LEA/W float addr-taken [5: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateFixedCam@1b5120, UpdateRelativeAnimationCam@1b5998, UpdateWorldAnimationCam@1b53c0]
  +0x044  w[4] W float [1: UpdateAIPathAnimationCam@1b6160]
  +0x048  w[4] W float [1: UpdateAIPathAnimationCam@1b6160]
  +0x050  w[4] W [9: CheckForCameraShaking@1b9e98, DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0…]
  +0x0a0  w[4] R [1: UpdateCamera@1b29e0]
  +0x0a4  w[4] R/W float [8: GetZoomPercent@1b8960, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0, UpdateBumperCam@1b3b40, UpdateDashboardCam@1b3ce0…]
  +0x0a8  w[4] R/W [4: SetAutoDriveCamera@1bb7c0, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, ~RPlayerCamera@1b2728]
  +0x0b0  w[4, 8, 16] LEA/R/W float addr-taken [16: CheckForCameraShaking@1b9e98, GetAutoDriveWeapTransform@1bce68, ShakeCamera@1ba2f8, TriggerAnimationCamera@1bc510, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160…]
  +0x0b4  w[4] R/W float [6: UpdateAIPathAnimationCam@1b6160, UpdateBumperCam@1b3b40, UpdateDashboardCam@1b3ce0, UpdateRelativeAnimationCam@1b5998, UpdateSplineCam@1b4430, UpdateTumbleCam@1b3788]
  +0x0b8  w[4, 8] R/W float [11: GetAutoDriveWeapTransform@1bce68, UpdateADWeaponAnims@1b62d0, UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateBumperCam@1b3b40, UpdateDashboardCam@1b3ce0…]
  +0x0bc  w[4] W float [2: UpdateAutoDriveCam@1b72f0, UpdateDashboardCam@1b3ce0]
  +0x0c0  w[8, 16] R/W [2: UpdateAIPathAnimationCam@1b6160, UpdateFixedCam@1b5120]
  +0x0c8  w[8] R [1: UpdateFixedCam@1b5120]
  +0x0cc  w[4] W float [1: UpdateDashboardCam@1b3ce0]
  +0x0d0  w[8] W [1: SetOldCameraMode@1bb340]
  +0x0d8  w[8] W [1: SetOldCameraMode@1bb340]
  +0x0e0  w[4] R/W [19: DirectorChangeCameraMode@1ba7a0, DoSmoothModeChange@1bbf70, EndMissileCamera@1bb970, InitWeaponChange@1bd3c8, ResetCamera@1bbc58, RestartCamera@1bbc90…]
  +0x0e4  w[4] R/W float [3: TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateWorldAnimationCam@1b53c0]
  +0x0e8  w[4] R/W float [3: TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateWorldAnimationCam@1b53c0]
  +0x104  w[4] R [2: UpdateRelativeAnimationCam@1b5998, UpdateWorldAnimationCam@1b53c0]
  +0x110  w[4] R [1: UpdateAIPathAnimationCam@1b6160]
  +0x118  w[4] R [5: CheckObjectCollisions@1b8ae0, DirectorSetAnchor@1bba38, SetOldCameraMode@1bb340, UpdateDashboardCam@1b3ce0, UpdateMomentumHeliCam@1b2b48]
  +0x120  w[4] R/W [28: CheckForCameraShaking@1b9e98, DirectorChangeCameraMode@1ba7a0, DirectorSetAnchor@1bba38, DoSmoothModeChange@1bbf70, GetZoomPercent@1b8960, InitWeaponChange@1bd3c8…]
  +0x124  w[4] R/W [5: DirectorChangeCameraMode@1ba7a0, DoSmoothModeChange@1bbf70, SetLastSelectableCameraMode@1bb1f8, UpdateAutoDriveCam@1b72f0, UpdateTumbleCam@1b3788]
  +0x128  w[4] R/W [3: DirectorChangeCameraMode@1ba7a0, SetLastSelectableCameraMode@1bb1f8, UpdateMomentumHeliCam@1b2b48]
  +0x12c  w[4] R/W float [11: AbortCinematic@1bd2b8, ClearTransition@1b9e58, DeincrementTransition@1b9e80, SetTransition@1b9e68, UpdateAutoDriveCam@1b72f0, UpdateEllipseCam@1b4c30…]
  +0x130  w[4] R/W [5: ResetCamera@1bbc58, RestartCamera@1bbc90, SetCollisionCam@1bbb78, SetTumbleCam@1bbbf8, UpdateTumbleCam@1b3788]
  +0x134  w[4] R [1: UpdateMomentumHeliCam@1b2b48]
  +0x13c  w[4] R/W [5: RestartCamera@1bbc90, SetAutoDriveCamera@1bb7c0, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateCamera@1b29e0]
  +0x140  w[4] R/W [3: CameraLockOn@1bc400, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x150  w[4, 16] R/W float [2: CameraLockOn@1bc400, UpdateAutoDriveCam@1b72f0]
  +0x154  w[4] R float [1: UpdateAutoDriveCam@1b72f0]
  +0x158  w[4] R float [1: UpdateAutoDriveCam@1b72f0]
  +0x160  w[4] R/W [3: CameraLockOn@1bc400, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x164  w[4] R/W [3: CameraLockOn@1bc400, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x168  w[4] R/W [3: RestartCamera@1bbc90, SetAutoDriveForwardLock@1bd070, UpdateAutoDriveCam@1b72f0]
  +0x170  w[16] LEA/W addr-taken [4: DirectorChangeCameraMode@1ba7a0, GetForwardAimVec4@1bcca0, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x180  w[16] LEA/W addr-taken [3: GetAimMatrix4@1bcd10, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x190  w[16] W [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1a0  w[16] W [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1b0  w[16] W [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1c0  w[4] W float [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1c4  w[4] R/W float [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1c8  w[4] W [1: RestartCamera@1bbc90]
  +0x1cc  w[4] W [1: RestartCamera@1bbc90]
  +0x1d0  w[4] R/W float [4: GetAimMatrix4@1bcd10, InitAimZoom@1bd440, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1d4  w[4] R/W float [4: GetAimMatrix4@1bcd10, InitAimZoom@1bd440, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1d8  w[4] LEA/R/W float addr-taken [4: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x1dc  w[4] LEA/R/W float addr-taken [4: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x1e0  w[4] R/W float [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1e4  w[4] R/W float [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x1e8  w[4] R/W [3: RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x1ec  w[4] R/W [5: DirectorChangeCameraMode@1ba7a0, GetAutoDriveWeapTransform@1bce68, RestartCamera@1bbc90, UpdateADWeaponAnims@1b62d0, UpdateAutoDriveCam@1b72f0]
  +0x1f0  w[4] W [1: RestartCamera@1bbc90]
  +0x1f4  w[4] R/W [3: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0, WeaponFired@1bd288]
  +0x1f8  w[4] R/W [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x210  w[4] W [1: RestartCamera@1bbc90]
  +0x214  w[4] W [1: RestartCamera@1bbc90]
  +0x218  w[4] R/W [2: ResolveAllCollisions@1b9198, RestartCamera@1bbc90]
  +0x21c  w[4] R/W float [10: DirectorSetAnchor@1bba38, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0, UpdateDashboardCam@1b3ce0, UpdateFixedCam@1b5120, UpdateMomentumHeliCam@1b2b48…]
  +0x220  w[4] R/W float [5: RestartCamera@1bbc90, UpdateMomentumHeliCam@1b2b48, UpdateRelativeAnimationCam@1b5998, UpdateTransition@1b9de8, UpdateWorldAnimationCam@1b53c0]
  +0x23c  w[4] W [1: RestartCamera@1bbc90]
  +0x240  w[4] R float [1: UpdateDashboardCam@1b3ce0]
  +0x248  w[4] R float [1: UpdateDashboardCam@1b3ce0]
  +0x24c  w[4] W [1: RestartCamera@1bbc90]
  +0x250  w[4] R/W float [2: RestartCamera@1bbc90, UpdateDashboardCam@1b3ce0]
  +0x254  w[4] R/W float [2: RestartCamera@1bbc90, UpdateDashboardCam@1b3ce0]
  +0x258  w[4] R/W float [4: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateDashboardCam@1b3ce0, UpdateMomentumHeliCam@1b2b48]
  +0x25c  w[4] R/W float [3: RestartCamera@1bbc90, UpdateDashboardCam@1b3ce0, UpdateMomentumHeliCam@1b2b48]
  +0x260  w[4] R/W float [4: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0, UpdateRelativeAnimationCam@1b5998]
  +0x264  w[4] R/W [2: RestartCamera@1bbc90, UpdateEllipseCam@1b4c30]
  +0x268  w[4] R/W [2: RestartCamera@1bbc90, UpdateEllipseCam@1b4c30]
  +0x26c  w[4] R/W [5: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateMomentumHeliCam@1b2b48, UpdateRelativeAnimationCam@1b5998, UpdateWorldAnimationCam@1b53c0]
  +0x270  w[4] R/W [4: CheckForCameraShaking@1b9e98, DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, ShakeCamera@1ba2f8]
  +0x274  w[4] R/W float [3: CheckForCameraShaking@1b9e98, RestartCamera@1bbc90, ShakeCamera@1ba2f8]
  +0x278  w[4] R/W [4: CheckForCameraShaking@1b9e98, RestartCamera@1bbc90, ShakeCamera@1ba2f8, UpdateCamera@1b29e0]
  +0x27c  w[1] R/W [7: ClearTransition@1b9e58, SetTransition@1b9e68, UpdateAutoDriveCam@1b72f0, UpdateEllipseCam@1b4c30, UpdateFixedCam@1b5120, UpdateMomentumHeliCam@1b2b48…]
  +0x27d  w[1] R/W [4: ClearTransition@1b9e58, DeincrementTransition@1b9e80, SetTransition@1b9e68, UpdateMomentumHeliCam@1b2b48]
  +0x27e  w[1] R/W [5: DirectorChangeCameraMode@1ba7a0, RestartCamera@1bbc90, UpdateCurrentArm@1bb010, UpdateMomentumHeliCam@1b2b48, UpdateTumbleCam@1b3788]
  +0x27f  w[1] R/W -> VU0_quattom4 [9: GetAutoDriveWeapTransform@1bce68, GetZoomPercent@1b8960, InitAimZoom@1bd440, InitWeaponChange@1bd3c8, RestartCamera@1bbc90, ToggleAutoDriveZoom@1bc4c0…]
  +0x280  w[1] W [2: RestartCamera@1bbc90, UpdateAutoDriveCam@1b72f0]
  +0x290  w[4, 8] R/W float [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x294  w[4] R/W float [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x298  w[4] R/W float [3: SetupCameraZoom@1b89e0, ToggleAutoDriveZoom@1bc4c0, UpdateAutoDriveCam@1b72f0]
  +0x29c  w[4] W float [1: SetupCameraZoom@1b89e0]
  +0x2a0  w[4] W float [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x2a4  w[4] W float [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x2a8  w[4] W [5: CameraLockOn@1bc400, RestartCamera@1bbc90, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510, UpdateAutoDriveCam@1b72f0]
  +0x2ac  w[4] R/W float [7: ResetZoomSlope@1bd4a0, RestartCamera@1bbc90, SetAutoDriveZoom@1bc4a0, SetupCameraZoom@1b89e0, TriggerAIPathAnimationCamera@1bc890, TriggerAnimationCamera@1bc510…]
  +0x2b0  w[4] W float [1: SetupCameraZoom@1b89e0]
  +0x2b4  w[4] R/W float [8: CameraLockOn@1bc400, ResetZoomSlope@1bd4a0, RestartCamera@1bbc90, SetAutoDriveZoom@1bc4a0, SetCameraZoom@1b8740, SetupCameraZoom@1b89e0…]
  +0x2c0  w[16] LEA/W addr-taken [1: UpdateMomentumHeliCam@1b2b48]
  +0x2c4  w[4] W float [1: RestartCamera@1bbc90]
  +0x2ec  w[4] R/W float [4: InitTransition@1b9cf8, UpdateRelativeAnimationCam@1b5998, UpdateTransition@1b9de8, UpdateWorldAnimationCam@1b53c0]
  +0x2f0  w[4, 8] LEA/W float addr-taken [7: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateDashboardCam@1b3ce0, UpdateEllipseCam@1b4c30, UpdateMomentumHeliCam@1b2b48, UpdateTumbleCam@1b3788…]
  +0x2f4  w[4] W float [2: UpdateAIPathAnimationCam@1b6160, UpdateTumbleCam@1b3788]
  +0x2f8  w[4, 8] W float [7: UpdateAIPathAnimationCam@1b6160, UpdateAutoDriveCam@1b72f0, UpdateDashboardCam@1b3ce0, UpdateEllipseCam@1b4c30, UpdateMomentumHeliCam@1b2b48, UpdateTumbleCam@1b3788…]
  +0x2fc  w[4] W float [2: UpdateAIPathAnimationCam@1b6160, UpdateRelativeAnimationCam@1b5998]
  +0x300  w[8] LEA/R/W addr-taken [2: TriggerFixedCamera@1bcab8, UpdateFixedCam@1b5120]
  +0x308  w[8] R/W [2: TriggerFixedCamera@1bcab8, UpdateFixedCam@1b5120]
  +0x310  w[8] LEA/W addr-taken [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x318  w[8] W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x320  w[8] W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x328  w[8] W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x330  w[8] LEA/W addr-taken [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x338  w[8] W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x340  w[8] R/W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x348  w[8] R/W [2: RestartCamera@1bbc90, UpdateRelativeAnimationCam@1b5998]
  +0x350  w[4] R -> RCameraSpline::AddToSplinePtList, RCameraSpline::EvaluateSpline, RCameraSpline::~RCameraSpline, RDirectorQueue::~RDirect [5: DirectorChangeCameraMode@1ba7a0, TriggerAnimationCamera@1bc510, UpdateRelativeAnimationCam@1b5998, UpdateWorldAnimationCam@1b53c0, ~RPlayerCamera@1b2728]
  +0x354  w[4] R -> RCameraSpline::~RCameraSpline, RPlayerCamState::AutoDriveCamInputHandler, RPlayerCamState::ResetStateForAnimation, RPlay [23: AbortCinematic@1bd2b8, CameraAiming@1bd2f8, CameraInputCallback@1bcc00, CameraLockOn@1bc400, DirectorChangeCameraMode@1ba7a0, EndCameraAnim@1bb098…]
  +0x358  w[4] R -> RCameraIniLoader::~RCameraIniLoader, RPlayerCamState::~RPlayerCamState [1: ~RPlayerCamera@1b2728]
  +0x35c  w[4] LEA/R/W addr-taken -> RCameraIniLoader::~RCameraIniLoader, WCollider::~WCollider [3: ResolveAllCollisions@1b9198, RestartCamera@1bbc90, ~RPlayerCamera@1b2728]
  +0x360  w[4] R -> WWorldPos::~WWorldPos [2: GetSafeWRoadNavPosition@1b9ab8, ~RPlayerCamera@1b2728]
  +0x364  w[4] R -> WRoadNav::IncNavPosition, WRoadNav::InitAtPoint, WRoadNav::ReverseNavDirection, WRoadNav::~WRoadNav [3: GetSafeWRoadNavPosition@1b9ab8, UpdateSplineCam@1b4430, ~RPlayerCamera@1b2728]
  +0x368  w[4] R -> FUN_001aacd0, RDirectorQueue::RestartDirectorQueue, RDirectorQueue::~RDirectorQueue, WWorldPos::~WWorldPos, __builtin_ne [13: AbortCinematic@1bd2b8, RestartCamera@1bbc90, SetAutoDriveCamera@1bb7c0, SetCollisionCam@1bbb78, SetMissileCamera@1bb8a8, SetPauseCamera@1bb9e8…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetAutoDriveForwardLock: W +0x178 w1
  SetControlToCPU: R +0x354 w4
  CameraAiming: R +0x354 w4
