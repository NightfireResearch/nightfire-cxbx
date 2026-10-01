# RWorldCamera

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x97eb0 frees/deletes with size 0x130 (call to UMemory::FastFree)
Xbox vtable 0x00190350 (3 slots) stored by its constructor
Xbox vtable 0x001924e4 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['RWorldCamera virtual table']
constructor 0x98070 first calls: ['RCamera::RCamera', 'RWorldCamera::RestartCamera']

Xbox methods (25):
  0x97100 undefined SetViewingTransform(undefined4 param_1, undefined4 param_2)
  0x97190 undefined SetViewingTransform(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x97240 undefined GetAnchorSpeed(void)
  0x972c0 undefined GetAnchorAcceleration(undefined4 param_1)
  0x973c0 undefined GetAnchorMatrix4(void)
  0x97470 undefined GetAnchorPosition(void)
  0x974e0 undefined GetAnchorResetAvailable(void)
  0x97530 undefined SetAnchor(undefined4 param_1)
  0x975d0 undefined GetAnchorRenderOffset(void)
  0x97620 undefined StartCameraInputReceiver(void)
  0x97690 undefined ReceiveCameraInput(void)
  0x97710 undefined SetCameraZoom(undefined4 param_1, undefined4 param_2)
  0x97770 undefined LoadSingleAnimation(undefined4 param_1)
  0x977d0 undefined LoadAISplinePathAnimation(undefined4 param_1)
  0x97870 undefined LoadSingleAnimationFromList(undefined4 param_1)
  0x97930 undefined PlayCurrentAnimation(void)
  0x97970 char * __cdecl ReadAnchorInfo(IniFiles.conflict * param_1, char * param_2, undefined4 * param_3)
  0x979f0 undefined ~RWorldCamera(void)
  0x97aa0 undefined UpdateAnimationCam(undefined4 param_1, undefined1 param_2)
  0x97c60 undefined AnchorCamera(undefined1 param_1, undefined4 param_2)
  0x97d70 undefined AnchorRelativeCamera(undefined1 param_1, undefined4 param_2)
  0x97eb0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x97ee0 undefined RestartCamera(void)
  0x97fc0 undefined GetAnchorLinearVelocity(void)
  0x98070 undefined RWorldCamera(void)

PS2 methods (35):
  0x1d5450 RWorldCamera::RWorldCamera
  0x1d54a8 RWorldCamera::~RWorldCamera
  0x1d5538 RWorldCamera::RestartCamera
  0x1d5600 RWorldCamera::Init
  0x1d5608 RWorldCamera::Shutdown
  0x1d5610 RWorldCamera::SetViewingTransform
  0x1d5838 RWorldCamera::IsAnchorValid
  0x1d5888 RWorldCamera::GetAnchorSpeed
  0x1d5940 RWorldCamera::GetAnchorAcceleration
  0x1d5a58 RWorldCamera::GetAnchorAngularAcceleration
  0x1d5b90 RWorldCamera::GetAnchorMatrix4
  0x1d5cd0 RWorldCamera::GetAnchorPosition
  0x1d5d50 RWorldCamera::GetAnchorLinearVelocity
  0x1d5e38 RWorldCamera::GetAnchorResetAvailable
  0x1d5e90 RWorldCamera::CheckAnchorIsAirborn
  0x1d5ef0 RWorldCamera::SetAnchor
  0x1d5fa8 RWorldCamera::GetAnchorRenderOffset
  0x1d5ff8 RWorldCamera::GetAnchorWheelHeight
  0x1d60a0 RWorldCamera::StartCameraInputReceiver
  0x1d60f0 RWorldCamera::StopCameraInputReceiver
  0x1d6128 RWorldCamera::ReceiveCameraInput
  0x1d61b8 RWorldCamera::UpdateAnimationCam
  0x1d6498 RWorldCamera::AnchorCamera
  0x1d65e0 RWorldCamera::AnchorRelativeCamera
  0x1d6720 RWorldCamera::SetCameraZoom
  0x1d6778 RWorldCamera::LoadSingleAnimation
  0x1d67f0 RWorldCamera::LoadAISplinePathAnimation
  0x1d6860 RWorldCamera::LoadSingleAnimationFromList
  0x1d6970 RWorldCamera::PlayCurrentAnimation
  0x1d69c8 RWorldCamera::ReadAnchorInfo
  0x1d6cd0 RWorldCamera::operator_new
  0x1d6cf0 RWorldCamera::operator_delete
  0x1d6d10 RWorldCamera::CreateDBVars
  0x1d6d30 RWorldCamera::CameraInputCallback
  0x1d6d38 RWorldCamera::RWorldCamera_global_ctors

Sheet rows:
  RWorldCamera::RWorldCamera(void)
  RWorldCamera::~RWorldCamera(void)
  RWorldCamera::RestartCamera(void)
  RWorldCamera::Init(void)
  RWorldCamera::Shutdown(void)
  RWorldCamera::SetViewingTransform(COORD4 &, COORD4 *, char)
  RWorldCamera::SetViewingTransform(COORD4 &, COORD4 &, COORD4 *,
  RWorldCamera::IsAnchorValid(void)
  RWorldCamera::GetAnchorSpeed(void)
  RWorldCamera::GetAnchorAcceleration(COORD4 &)
  RWorldCamera::GetAnchorAngularAcceleration(COORD4 &)
  RWorldCamera::GetAnchorMatrix4(void)
  RWorldCamera::GetAnchorPosition(void)
  RWorldCamera::GetAnchorLinearVelocity(void)
  RWorldCamera::GetAnchorResetAvailable(void)
  RWorldCamera::CheckAnchorIsAirborn(void)
  RWorldCamera::SetAnchor(PhysicsObject *)
  RWorldCamera::GetAnchorRenderOffset(void)
  RWorldCamera::GetAnchorWheelHeight(int)
  RWorldCamera::StartCameraInputReceiver(void)
  RWorldCamera::StopCameraInputReceiver(void)
  RWorldCamera::ReceiveCameraInput(void)
  RWorldCamera::UpdateAnimationCam(MATRIX4 &, char)
  RWorldCamera::AnchorCamera(bool, RWorldCamera::AnchorInfo &)
  RWorldCamera::AnchorRelativeCamera(bool, RWorldCamera::AnchorIn
  RWorldCamera::SetCameraZoom(float, float)
  RWorldCamera::LoadSingleAnimation(CARP::Instance *)
  RWorldCamera::LoadAISplinePathAnimation(CARP::AISpline *)
  RWorldCamera::LoadSingleAnimationFromList(unsigned int)
  RWorldCamera::PlayCurrentAnimation(void)
  RWorldCamera::ReadAnchorInfo(IniFiles *, char *, RWorldCamera::
  RWorldCamera type_info function
  RWorldCamera::operator new(unsigned int)
  RWorldCamera::operator delete(void *, unsigned int)
  RWorldCamera::CreateDBVars(void)
  RWorldCamera::SetModeChangeFlag(unsigned int)
  RWorldCamera::GetAnchor(void)
  RWorldCamera::CameraInputCallback(int, float)
  RWorldCamera virtual table
  RWorldCamera type_info node

Xbox methods treated as members (24 of 25; untyped ones count when ECX is read before it is written): AnchorCamera, AnchorRelativeCamera, GetAnchorAcceleration, GetAnchorLinearVelocity, GetAnchorMatrix4, GetAnchorPosition, GetAnchorRenderOffset, GetAnchorResetAvailable, GetAnchorSpeed, LoadAISplinePathAnimation, LoadSingleAnimation, LoadSingleAnimationFromList, PlayCurrentAnimation, RWorldCamera, ReceiveCameraInput, RestartCamera, SetAnchor, SetCameraZoom, SetViewingTransform, StartCameraInputReceiver, UpdateAnimationCam, scalar_deleting_destructor, ~RWorldCamera

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: RWorldCamera@98070, ReceiveCameraInput@97690, ~RWorldCamera@979f0]
  +0x010  w- LEA addr-taken [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x020  w- LEA addr-taken [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x030  w- LEA addr-taken [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x040  w- LEA addr-taken [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x04c  w[4] W [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x060  w[1] W [2: SetViewingTransform@97100, SetViewingTransform@97190]
  +0x0b4  w[4] R/W float [2: RestartCamera@97ee0, SetCameraZoom@97710]
  +0x0c0  w- LEA addr-taken [3: RestartCamera@97ee0, SetViewingTransform@97100, SetViewingTransform@97190]
  +0x0d0  w- LEA addr-taken [3: RestartCamera@97ee0, SetViewingTransform@97100, SetViewingTransform@97190]
  +0x0e0  w- LEA addr-taken [1: RestartCamera@97ee0]
  +0x0f0  w[4] R/W [1: RestartCamera@97ee0]
  +0x0f4  w[4] W [1: RestartCamera@97ee0]
  +0x0f8  w[4] W [1: RestartCamera@97ee0]
  +0x100  w- LEA addr-taken -> VU0_v4Init [1: RestartCamera@97ee0]
  +0x110  w[4] R/W [3: LoadSingleAnimation@97770, RestartCamera@97ee0, UpdateAnimationCam@97aa0]
  +0x114  w[4] R/W -> RAnimEngine::Handle::Stop [6: LoadSingleAnimation@97770, PlayCurrentAnimation@97930, RWorldCamera@98070, RestartCamera@97ee0, UpdateAnimationCam@97aa0, ~RWorldCamera@979f0]
  +0x118  w[4] R/W [2: LoadSingleAnimationFromList@97870, RWorldCamera@98070]
  +0x11c  w[4] W [1: RWorldCamera@98070]
  +0x120  w[4] R/W -> AISplinePath::~AISplinePath, __builtin_new [4: LoadAISplinePathAnimation@977d0, RWorldCamera@98070, RestartCamera@97ee0, ~RWorldCamera@979f0]
  +0x124  w[4] R/W -> ActionQueue::GetAction, ActionQueue::IsEmpty, ActionQueue::PopAction, ActionQueue::~ActionQueue [5: RWorldCamera@98070, ReceiveCameraInput@97690, RestartCamera@97ee0, StartCameraInputReceiver@97620, ~RWorldCamera@979f0]
  +0x128  w[4] R/W [9: GetAnchorAcceleration@972c0, GetAnchorLinearVelocity@97fc0, GetAnchorMatrix4@973c0, GetAnchorPosition@97470, GetAnchorRenderOffset@975d0, GetAnchorResetAvailable@974e0…]

PS2 this-relative accesses (PS2 offsets):
  +0x00c  w[4] W [1: SetViewingTransform@1d5610]
  +0x020  w- LEA addr-taken [1: SetViewingTransform@1d5610]
  +0x030  w[16] LEA/W addr-taken [1: SetViewingTransform@1d5610]
  +0x040  w[16] W [1: SetViewingTransform@1d5610]
  +0x050  w[4] W [1: SetViewingTransform@1d5610]
  +0x0a4  w[4] R/W float [2: RestartCamera@1d5538, SetCameraZoom@1d6720]
  +0x0a8  w[4] R/W [2: ReceiveCameraInput@1d6128, ~RWorldCamera@1d54a8]
  +0x0b0  w[4] LEA/R/W float addr-taken [2: SetViewingTransform@1d5610, UpdateAnimationCam@1d61b8]
  +0x0bc  w[4] W float [1: UpdateAnimationCam@1d61b8]
  +0x0d0  w[16] W [1: AnchorRelativeCamera@1d65e0]
  +0x0e0  w[4] R/W [2: RestartCamera@1d5538, SetAnchor@1d5ef0]
  +0x0e4  w[4] W float [1: RestartCamera@1d5538]
  +0x0e8  w[4] W float [1: RestartCamera@1d5538]
  +0x100  w[4] R/W [5: LoadSingleAnimation@1d6778, LoadSingleAnimationFromList@1d6860, PlayCurrentAnimation@1d6970, RestartCamera@1d5538, UpdateAnimationCam@1d61b8]
  +0x104  w[4] R/W -> RAnimEngine::Handle::GetFirstSystemInstance, RAnimEngine::Handle::IsSystemPlaying, RAnimEngine::Handle::~Handle [6: LoadSingleAnimation@1d6778, LoadSingleAnimationFromList@1d6860, PlayCurrentAnimation@1d6970, RestartCamera@1d5538, UpdateAnimationCam@1d61b8, ~RWorldCamera@1d54a8]
  +0x108  w[4] R/W [2: LoadSingleAnimationFromList@1d6860, SetAnchor@1d5ef0]
  +0x10c  w[4] R/W [2: LoadSingleAnimationFromList@1d6860, SetAnchor@1d5ef0]
  +0x110  w[4] R/W -> AISplinePath::~AISplinePath, RAnimEngine::Handle::~Handle, __builtin_new [3: LoadAISplinePathAnimation@1d67f0, RestartCamera@1d5538, ~RWorldCamera@1d54a8]
  +0x114  w[4] R/W -> AISplinePath::~AISplinePath, ActionQueue::IsEmpty, ActionQueue::~ActionQueue [5: ReceiveCameraInput@1d6128, RestartCamera@1d5538, StartCameraInputReceiver@1d60a0, StopCameraInputReceiver@1d60f0, ~RWorldCamera@1d54a8]
  +0x118  w[4] R/W [8: GetAnchorAcceleration@1d5940, GetAnchorAngularAcceleration@1d5a58, GetAnchorLinearVelocity@1d5d50, GetAnchorMatrix4@1d5b90, GetAnchorSpeed@1d5888, GetAnchorWheelHeight@1d5ff8…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
