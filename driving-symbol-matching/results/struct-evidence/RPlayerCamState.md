# RPlayerCamState

FastAlloc/constructed sizes under its tag: {'allocated': [52], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x89df0 first calls: ['RPlayerCamState::ResetState']

Xbox methods (7):
  0x89b70 undefined ResetState(void)
  0x89bd0 undefined ResetStateForAnimation(void)
  0x89be0 undefined DriveCamInputHandler(undefined4 param_1, undefined4 param_2)
  0x89df0 undefined RPlayerCamState(undefined4 param_1)
  0x89e10 undefined AutoDriveCamInputHandler(undefined4 param_1, undefined4 param_2)
  0x8a310 undefined AimZoom(void)
  0x8a390 undefined AimRelease(void)

PS2 methods (9):
  0x1bdcd0 RPlayerCamState::RPlayerCamState
  0x1bdd08 RPlayerCamState::~RPlayerCamState
  0x1bdd30 RPlayerCamState::ResetState
  0x1bddb0 RPlayerCamState::ResetStateForAnimation
  0x1bddc8 RPlayerCamState::DriveCamInputHandler
  0x1be038 RPlayerCamState::AutoDriveCamInputHandler
  0x1be570 RPlayerCamState::AimZoom
  0x1be660 RPlayerCamState::AimRelease
  0x1be8b8 RPlayerCamState::RPlayerCamState_global_ctors

Sheet rows:
  RPlayerCamState::RPlayerCamState(RPlayerCamera *)
  RPlayerCamState::~RPlayerCamState(void)
  RPlayerCamState::ResetState(void)
  RPlayerCamState::ResetStateForAnimation(void)
  RPlayerCamState::DriveCamInputHandler(int, float)
  RPlayerCamState::AutoDriveCamInputHandler(int, float)
  RPlayerCamState::AimZoom(void)
  RPlayerCamState::AimRelease(void)
  RPlayerCamState::IsDriveMissileOn(void)

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): AimRelease, AimZoom, AutoDriveCamInputHandler, DriveCamInputHandler, RPlayerCamState, ResetState, ResetStateForAnimation

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> RPlayerCamera::InitAimZoom, RPlayerCamera::InitSpin, RPlayerCamera::InitWeaponChange, RPlayerCamera::NextCameraMode, RPl [5: AimRelease@8a390, AimZoom@8a310, AutoDriveCamInputHandler@89e10, DriveCamInputHandler@89be0, RPlayerCamState@89df0]
  +0x004  w[1] R/W [2: RPlayerCamState@89df0, ResetState@89b70]
  +0x005  w[1] R/W [3: DriveCamInputHandler@89be0, ResetState@89b70, ResetStateForAnimation@89bd0]
  +0x006  w[1] R/W [2: DriveCamInputHandler@89be0, ResetState@89b70]
  +0x007  w[1] R/W [2: DriveCamInputHandler@89be0, ResetState@89b70]
  +0x008  w[1] R/W [2: DriveCamInputHandler@89be0, ResetState@89b70]
  +0x009  w[1] R/W [3: AutoDriveCamInputHandler@89e10, DriveCamInputHandler@89be0, ResetState@89b70]
  +0x00b  w[1] W [3: AimRelease@8a390, AimZoom@8a310, ResetState@89b70]
  +0x00c  w[1] R/W [4: AimRelease@8a390, AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x00d  w[1] W [3: AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x00e  w[1] R/W [3: AimRelease@8a390, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x00f  w[1] R/W [3: AimRelease@8a390, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x010  w[4] R/W [3: AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x014  w[4] R/W [3: AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x018  w[4] R/W [3: AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x01c  w[4] R/W [3: AimZoom@8a310, AutoDriveCamInputHandler@89e10, ResetState@89b70]
  +0x020  w[1] W [2: ResetState@89b70, ResetStateForAnimation@89bd0]
  +0x024  w[4] W [1: ResetState@89b70]
  +0x028  w[4] W [1: ResetState@89b70]
  +0x02c  w[1] W [1: ResetState@89b70]
  +0x030  w[4] W [1: ResetState@89b70]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> GHud::TheApp, RPlayerCamera::InitAimZoom, RPlayerCamera::NextCameraMode, RPlayerCamera::ResetZoomSlope, RPlayerCamera::S [5: AimRelease@1be660, AimZoom@1be570, AutoDriveCamInputHandler@1be038, DriveCamInputHandler@1bddc8, RPlayerCamState@1bdcd0]
  +0x004  w[4] R/W [2: RPlayerCamState@1bdcd0, ResetState@1bdd30]
  +0x008  w[4] R/W [3: DriveCamInputHandler@1bddc8, ResetState@1bdd30, ResetStateForAnimation@1bddb0]
  +0x00c  w[4] R/W [2: DriveCamInputHandler@1bddc8, ResetState@1bdd30]
  +0x010  w[4] R/W [2: DriveCamInputHandler@1bddc8, ResetState@1bdd30]
  +0x014  w[4] R/W [2: DriveCamInputHandler@1bddc8, ResetState@1bdd30]
  +0x018  w[4] R/W [3: AutoDriveCamInputHandler@1be038, DriveCamInputHandler@1bddc8, ResetState@1bdd30]
  +0x020  w[4] W [3: AimRelease@1be660, AimZoom@1be570, ResetState@1bdd30]
  +0x024  w[4] R/W [4: AimRelease@1be660, AimZoom@1be570, AutoDriveCamInputHandler@1be038, ResetState@1bdd30]
  +0x028  w[4] W [3: AimZoom@1be570, AutoDriveCamInputHandler@1be038, ResetState@1bdd30]
  +0x02c  w[4] R/W [2: AimRelease@1be660, ResetState@1bdd30]
  +0x030  w[4] R/W [3: AimRelease@1be660, AutoDriveCamInputHandler@1be038, ResetState@1bdd30]
  +0x034  w[4] R/W float [3: AimZoom@1be570, AutoDriveCamInputHandler@1be038, ResetState@1bdd30]
  +0x038  w[4] R/W float [3: AimZoom@1be570, AutoDriveCamInputHandler@1be038, ResetState@1bdd30]
  +0x03c  w[4] W float [2: AimZoom@1be570, ResetState@1bdd30]
  +0x040  w[4] W float [2: AimZoom@1be570, ResetState@1bdd30]
  +0x044  w[4] W [2: ResetState@1bdd30, ResetStateForAnimation@1bddb0]
  +0x04c  w[4] W [1: ResetState@1bdd30]
  +0x050  w[4] W [1: ResetState@1bdd30]
  +0x054  w[4] W [1: ResetState@1bdd30]
  +0x058  w[4] W [1: ResetState@1bdd30]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
