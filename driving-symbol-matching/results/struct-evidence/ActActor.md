# ActActor

FastAlloc/constructed sizes under its tag: {'allocated': [80], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x12ca0 first calls: ['__builtin_new', 'WWorldPos::WWorldPos', 'UMemory::FastAlloc']

Xbox methods (32):
  0x112c0 int __thiscall GetBoneIndex(ActActor * this, char * name)
  0x112d0 undefined Fire(void)
  0x112e0 undefined CalculateMatrices(undefined1 param_1)
  0x11650 undefined GetActorLocalPosOri(undefined4 param_1)
  0x11670 undefined GetActorWorldPosition(undefined4 param_1)
  0x116a0 undefined GetWeaponPosition(undefined4 param_1, undefined param_2, undefined4 param_3)
  0x116d0 undefined CreateIKs(void)
  0x116e0 undefined SetIKInfoArray(undefined4 param_1, undefined4 param_2)
  0x11760 undefined CreateGlobalPoseOverrides(void)
  0x11770 undefined SetGlobalPoseOverride(void)
  0x11780 undefined SetGlobalPoseOverrides(void)
  0x11790 undefined SetupFOVConversion(undefined4 param_1)
  0x118a0 undefined DrawWeapons(undefined4 param_1, undefined1 param_2)
  0x11ff0 undefined SpawnWeapon(void)
  0x12140 undefined IsAnimationDone(void)
  0x12180 undefined SetTimeScale(undefined4 param_1)
  0x12190 undefined ChangeAnimationOrigin(undefined4 param_1)
  0x121f0 undefined GetAnimationOrigin(undefined4 param_1)
  0x12220 undefined SetAnimationOrigin(undefined4 param_1)
  0x12280 undefined CurrentPositionRotateY(undefined4 param_1)
  0x12350 undefined RotateActor(undefined4 param_1)
  0x12420 undefined RotateActorX(undefined4 param_1)
  0x124f0 undefined SetSuppressAnimationTranslation(undefined1 param_1)
  0x12570 undefined TurnShadowsOff(void)
  0x12640 undefined SetNewAnimation(undefined4 param_1, undefined4 param_2)
  0x126e0 undefined SetNewCrossFadeAnimation(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x127a0 void __thiscall InitializeMatrices(ActActor * this, MATRIX4 * param_1)
  0x12840 undefined Update(void)
  0x129c0 undefined Draw(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0x12c30 undefined DropWeapon(undefined4 param_1)
  0x12ca0 ulong __thiscall ActActor(ActActor * this, ActActor * param_1_00, int param_2, ulong param_3, char * param_4, 
  0x12ec0 undefined ~ActActor(void)

PS2 methods (50):
  0x107c68 ActActor::SetNewAnimation
  0x107d08 ActActor::SetNewCrossFadeAnimation
  0x107dd0 ActActor::SetNewBlendedAnimation
  0x107e98 ActActor::SetNewAimedAnimation
  0x107f58 ActActor::SetNewAimedShootingAnimation
  0x108038 ActActor::ActActor
  0x108248 ActActor::ChangeActor
  0x108358 ActActor::~ActActor
  0x108420 ActActor::SetMirrored
  0x108438 ActActor::GetBoneIndex
  0x108458 ActActor::CheckValidActor
  0x108460 ActActor::InitializeMatrices
  0x108610 ActActor::Fire
  0x108638 ActActor::CalculateMatrices
  0x108d48 ActActor::GetActorLocalPosOri
  0x108e08 ActActor::GetActorLocalPosition
  0x108e98 ActActor::GetActorWorldPosition
  0x108ec8 ActActor::GetWeaponPosition
  0x108f60 ActActor::CreateIKs
  0x108f80 ActActor::DeleteIKs
  0x108fa0 ActActor::SetIKInfo
  0x109050 ActActor::SetIKInfoArray
  0x1090c0 ActActor::CreateGlobalPoseOverrides
  0x1090e0 ActActor::DeleteGlobalPoseOverrides
  0x109100 ActActor::SetGlobalPoseOverride
  0x109120 ActActor::SetGlobalPoseOverrides
  0x109140 ActActor::Update
  0x1092f8 ActActor::SetupFOVConversion
  0x109500 ActActor::Draw
  0x109918 ActActor::DrawWeapons
  0x109a08 ActActor::DropWeapon
  0x10aa58 ActActor::SpawnWeapon
  0x10abe0 ActActor::SetWeaponVelocity
  0x10ac58 ActActor::IsAnimationDone
  0x10acc0 ActActor::SetStartPosOri
  0x10ace0 ActActor::SetTimeScale
  0x10acf0 ActActor::GetTimeScale
  0x10ad00 ActActor::ChangeAnimationOrigin
  0x10ad90 ActActor::GetAnimationOrigin
  0x10adf8 ActActor::SetAnimationOrigin
  0x10ae88 ActActor::GetInitialTrOu
  0x10aed8 ActActor::SetInitialTrOu
  0x10af10 ActActor::CurrentPositionRotateY
  0x10afc0 ActActor::RotateActor
  0x10b098 ActActor::RotateActorX
  0x10b170 ActActor::GetSuppressAnimationTranslation
  0x10b180 ActActor::SetSuppressAnimationTranslation
  0x10b1e8 ActActor::GetWeaponRange
  0x10b208 ActActor::TurnShadowsOn
  0x10b218 ActActor::TurnShadowsOff

Sheet rows:
  ActActor::SetNewAnimation(int, int)
  ActActor::SetNewCrossFadeAnimation(int, int, float)
  ActActor::SetNewBlendedAnimation(int, int, float)
  ActActor::SetNewAimedAnimation(int, int, float, float)
  ActActor::SetNewAimedShootingAnimation(int, int, float, float,
  ActActor::ActActor(int, char *, char *, char *, void (*)(int, M
  ActActor::ChangeActor(char *, char *, char *)
  ActActor::~ActActor(void)
  ActActor::SetMirrored(bool)
  ActActor::GetBoneIndex(char *)
  ActActor::CheckValidActor(void)
  ActActor::InitializeMatrices(MATRIX4 &)
  ActActor::Fire(float, float, float, float)
  ActActor::CalculateMatrices(bool)
  ActActor::GetActorLocalPosOri(MATRIX4 &)
  ActActor::GetActorLocalPosition(COORD4 &)
  ActActor::GetActorWorldPosOri(MATRIX4 &)
  ActActor::GetActorWorldPosition(COORD4 &)
  ActActor::GetWeaponPosition(MATRIX4 &, bool, int)
  ActActor::CreateIKs(int, int *, COORD4 *, bool *)
  ActActor::DeleteIKs(void)
  ActActor::SetIKInfo(int, ActIKSolveInfo &)
  ActActor::SetIKInfoArray(int, ActIKSolveInfo *)
  ActActor::CreateGlobalPoseOverrides(int, int *, bool)
  ActActor::DeleteGlobalPoseOverrides(void)
  ActActor::SetGlobalPoseOverride(int, MATRIX4 &, float)
  ActActor::SetGlobalPoseOverrides(int, MATRIX4 *, float *)
  ActActor::Update(void)
  ActActor::SetupFOVConversion(RViewCamera *)
  ActActor::Draw(RViewCamera *, bool, bool)
  ActActor::DrawWeapons(RViewCamera *, bool)
  ActActor::DropWeapon(float)
  ActActor::SpawnWeapon(void)
  ActActor::SetWeaponVelocity(void)
  ActActor::IsAnimationDone(void)
  ActActor::SetStartPosOri(void)
  ActActor::SetTimeScale(float)
  ActActor::GetTimeScale(void)
  ActActor::ChangeAnimationOrigin(COORD3 &)
  ActActor::GetAnimationOrigin(COORD3 &)
  ActActor::SetAnimationOrigin(COORD3 &)
  ActActor::GetInitialTrOu(MATRIX4 &)
  ActActor::SetInitialTrOu(MATRIX4 &)
  ActActor::CurrentPositionRotateY(float)
  ActActor::RotateActor(float)
  ActActor::RotateActorX(float)
  ActActor::GetSuppressAnimationTranslation(void)
  ActActor::SetSuppressAnimationTranslation(bool)
  ActActor::GetWeaponRange(void)
  ActActor::TurnShadowsOn(void)
  ActActor::TurnShadowsOff(void)

Xbox methods treated as members (32 of 32; untyped ones count when ECX is read before it is written): ActActor, CalculateMatrices, ChangeAnimationOrigin, CreateGlobalPoseOverrides, CreateIKs, CurrentPositionRotateY, Draw, DrawWeapons, DropWeapon, Fire, GetActorLocalPosOri, GetActorWorldPosition, GetAnimationOrigin, GetBoneIndex, GetWeaponPosition, InitializeMatrices, IsAnimationDone, RotateActor, RotateActorX, SetAnimationOrigin, SetGlobalPoseOverride, SetGlobalPoseOverrides, SetIKInfoArray, SetNewAnimation, SetNewCrossFadeAnimation, SetSuppressAnimationTranslation, SetTimeScale, SetupFOVConversion, SpawnWeapon, TurnShadowsOff, Update, ~ActActor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> ActCharacter::CalculateMuzzleFlashIntensity, ActCharacter::Draw, ActCharacter::DrawWeapon, ActCharacter::GetShadowTriang [8: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0, Fire@112d0, SpawnWeapon@11ff0, TurnShadowsOff@12570…]
  +0x004  w[4] R/W -> ActPoser::ChangeAnimationOrigin, ActPoser::DoEventPose, ActPoser::DoInitialPoses, ActPoser::DoSkeletonPose, ActPoser::Ge [21: ActActor@12ca0, ChangeAnimationOrigin@12190, CreateGlobalPoseOverrides@11760, CreateIKs@116d0, CurrentPositionRotateY@12280, Draw@129c0…]
  +0x008  w[4] R/W -> ActAnimGroup::~ActAnimGroup [4: ActActor@12ca0, SetNewAnimation@12640, SetNewCrossFadeAnimation@126e0, ~ActActor@12ec0]
  +0x00c  w[4] R/W -> WWorldPos::FindClosestFace, WWorldPos::HeightAtPoint, dummyNullFunction [18: ActActor@12ca0, CalculateMatrices@112e0, ChangeAnimationOrigin@12190, CurrentPositionRotateY@12280, Draw@129c0, GetActorLocalPosOri@11650…]
  +0x010  w[4] R/W [4: ActActor@12ca0, CalculateMatrices@112e0, SpawnWeapon@11ff0, Update@12840]
  +0x014  w[1] R/W [6: ActActor@12ca0, CalculateMatrices@112e0, ChangeAnimationOrigin@12190, RotateActor@12350, RotateActorX@12420, SetAnimationOrigin@12220]
  +0x018  w[4] R/W [4: ActActor@12ca0, CalculateMatrices@112e0, SpawnWeapon@11ff0, Update@12840]
  +0x01c  w[1] R/W [2: ActActor@12ca0, CalculateMatrices@112e0]
  +0x01d  w[1] R/W [2: ActActor@12ca0, CalculateMatrices@112e0]
  +0x01e  w[1] R/W [4: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0, DropWeapon@12c30]
  +0x01f  w[1] R/W [3: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0]
  +0x020  w[4] R/W [5: ActActor@12ca0, SetNewAnimation@12640, SetNewCrossFadeAnimation@126e0, Update@12840, ~ActActor@12ec0]
  +0x024  w[1] R/W [4: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0, SetupFOVConversion@11790]
  +0x025  w[1] W [1: ActActor@12ca0]
  +0x026  w[1] R/W [2: ActActor@12ca0, CalculateMatrices@112e0]
  +0x028  w[4] W float [2: ActActor@12ca0, Update@12840]
  +0x02c  w[4] W float [2: ActActor@12ca0, Update@12840]
  +0x030  w[1] R/W [1: ActActor@12ca0]
  +0x034  w[4] LEA/R/W float addr-taken [5: ActActor@12ca0, CalculateMatrices@112e0, CurrentPositionRotateY@12280, GetAnimationOrigin@121f0, SetSuppressAnimationTranslation@124f0]
  +0x038  w[4] LEA/R/W float addr-taken [6: ActActor@12ca0, ChangeAnimationOrigin@12190, CurrentPositionRotateY@12280, InitializeMatrices@127a0, SetAnimationOrigin@12220, SetIKInfoArray@116e0]
  +0x03c  w[4] W float [2: ActActor@12ca0, CalculateMatrices@112e0]
  +0x040  w[1] W [1: ActActor@12ca0]
  +0x044  w[1, 4] R/W [3: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0]
  +0x048  w[4] W float [2: ActActor@12ca0, Draw@129c0]
  +0x04c  w[1] R/W [4: ActActor@12ca0, Draw@129c0, DrawWeapons@118a0, SetupFOVConversion@11790]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> ActCharacter::CalculateMuzzleFlashIntensity, ActCharacter::ChangeCharacter, ActCharacter::Draw, ActCharacter::DrawWeapon [11: ActActor@108038, ChangeActor@108248, Draw@109500, DrawWeapons@109918, SetWeaponVelocity@10abe0, SetupFOVConversion@1092f8…]
  +0x004  w[4] R/W -> ActAnimGroup::~ActAnimGroup, ActCharacter::Draw, ActPoser::DoSkeletonPose, ActPoser::GetRootBonePosOri, ActPoser::GetWea [29: ActActor@108038, ChangeActor@108248, ChangeAnimationOrigin@10ad00, CreateGlobalPoseOverrides@1090c0, CreateIKs@108f60, DeleteGlobalPoseOverrides@1090e0…]
  +0x008  w[4] R/W -> ActAnimGroup::~ActAnimGroup [7: ActActor@108038, SetNewAimedAnimation@107e98, SetNewAimedShootingAnimation@107f58, SetNewAnimation@107c68, SetNewBlendedAnimation@107dd0, SetNewCrossFadeAnimation@107d08…]
  +0x00c  w[4] R/W -> BuildScale, VU0_MATRIX4_mult, VU0_m4toquat, WWorldPos::HeightAtPoint [22: ActActor@108038, CalculateMatrices@108638, ChangeActor@108248, ChangeAnimationOrigin@10ad00, CurrentPositionRotateY@10af10, Draw@109500…]
  +0x010  w[4] R/W [4: ActActor@108038, CalculateMatrices@108638, SetWeaponVelocity@10abe0, SpawnWeapon@10aa58]
  +0x014  w[4] R/W [7: ActActor@108038, CalculateMatrices@108638, ChangeActor@108248, ChangeAnimationOrigin@10ad00, RotateActor@10afc0, RotateActorX@10b098…]
  +0x018  w[4] R/W [4: ActActor@108038, CalculateMatrices@108638, SetWeaponVelocity@10abe0, SpawnWeapon@10aa58]
  +0x01c  w[4] LEA/R/W addr-taken [4: ActActor@108038, CalculateMatrices@108638, ChangeActor@108248, Update@109140]
  +0x020  w[4] R/W [3: ActActor@108038, CalculateMatrices@108638, ChangeActor@108248]
  +0x024  w[4] LEA/R/W addr-taken [6: ActActor@108038, ChangeActor@108248, Draw@109500, DrawWeapons@109918, DropWeapon@109a08, Update@109140]
  +0x028  w[4] R/W [4: ActActor@108038, ChangeActor@108248, Draw@109500, DrawWeapons@109918]
  +0x02c  w[4] R/W [8: ActActor@108038, SetNewAimedAnimation@107e98, SetNewAimedShootingAnimation@107f58, SetNewAnimation@107c68, SetNewBlendedAnimation@107dd0, SetNewCrossFadeAnimation@107d08…]
  +0x030  w[4] R/W [4: ActActor@108038, Draw@109500, DrawWeapons@109918, SetupFOVConversion@1092f8]
  +0x034  w[4] W [1: ActActor@108038]
  +0x038  w[4] R/W [2: ActActor@108038, CalculateMatrices@108638]
  +0x03c  w[4] R/W float [2: ActActor@108038, Update@109140]
  +0x040  w[4] R/W float [2: ActActor@108038, Update@109140]
  +0x044  w[4] R/W [2: ActActor@108038, ChangeActor@108248]
  +0x048  w[4] LEA/R float addr-taken [5: ActActor@108038, CalculateMatrices@108638, ChangeActor@108248, GetAnimationOrigin@10ad90, GetInitialTrOu@10ae88]
  +0x04c  w[4] LEA/R float addr-taken [7: ActActor@108038, ChangeActor@108248, ChangeAnimationOrigin@10ad00, InitializeMatrices@108460, SetAnimationOrigin@10adf8, SetIKInfo@108fa0…]
  +0x050  w[4] W float [2: ActActor@108038, CalculateMatrices@108638]
  +0x054  w[4] W [1: ActActor@108038]
  +0x058  w[4] R/W [3: ActActor@108038, Draw@109500, DrawWeapons@109918]
  +0x05c  w[4] R/W float [3: ActActor@108038, Draw@109500, SetupFOVConversion@1092f8]
  +0x060  w[4] R/W [4: ActActor@108038, Draw@109500, DrawWeapons@109918, SetupFOVConversion@1092f8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetBoneIndex: R +0x4 w4
  Fire: R +0x0 w4
  CreateIKs: R +0x4 w4
  CreateGlobalPoseOverrides: R +0x4 w4
  SetGlobalPoseOverride: R +0x4 w4
  SetGlobalPoseOverrides: R +0x4 w4
  SetTimeScale: R +0x4 w4
  TurnShadowsOff: R +0x0 w4
