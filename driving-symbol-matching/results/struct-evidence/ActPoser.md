# ActPoser

FastAlloc/constructed sizes under its tag: {'allocated': [112, 220], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x18550 first calls: ['__builtin_new', '__builtin_new', '__builtin_new']

Xbox methods (25):
  0x18550 undefined ActPoser(undefined4 param_1, undefined4 param_2, undefined1 param_3, undefined4 param_4)
  0x186e0 undefined4 __thiscall GetBoneIndex(ActPoser * this, undefined4 * name)
  0x186f0 undefined GetRootBonePosOri(undefined4 param_1)
  0x18720 undefined GetWeaponBonePosOri(undefined4 param_1, undefined4 param_2)
  0x18750 undefined ChangeAnimationOrigin(undefined4 param_1)
  0x18780 undefined GetAnimationOrigin(undefined4 param_1)
  0x187b0 undefined GetInitialTbOu(undefined4 param_1)
  0x187d0 undefined SetupCrossFadeBlend(undefined4 param_1, undefined4 param_2)
  0x18850 undefined CalcCrossFadeTimes(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, u
  0x18940 undefined Skin(void)
  0x18960 undefined DoEventPose(void)
  0x18b30 undefined SetAnimationOrigin(undefined4 param_1)
  0x18b80 undefined SetInitialTbOu(undefined4 param_1)
  0x18bd0 undefined Rotate(undefined4 param_1)
  0x18cf0 undefined RotateX(undefined4 param_1)
  0x18e10 undefined CalcSnapAndCorrectionMatrices(undefined4 param_1)
  0x18fe0 undefined SetNormalAnimation(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x19040 undefined SetCrossFadeBlendAnimation(undefined4 param_1, undefined4 param_2)
  0x190b0 undefined DoMainPose(void)
  0x19580 undefined ~ActPoser(void)
  0x19610 undefined FinishCrossFadeBlend(void)
  0x196d0 undefined DoIK(void)
  0x19710 undefined AdvanceTime(void)
  0x19750 undefined DoSkeletonPose(void)
  0x197c0 undefined DoInitialPoses(void)

PS2 methods (36):
  0x112928 ActPoser::ActPoser
  0x112a70 ActPoser::~ActPoser
  0x112b58 ActPoser::GetBoneIndex
  0x112b78 ActPoser::GetRootBonePosOri
  0x112c18 ActPoser::GetWeaponBonePosOri
  0x112cc0 ActPoser::ChangeAnimationOrigin
  0x112ce8 ActPoser::GetAnimationOrigin
  0x112d10 ActPoser::SetAnimationOrigin
  0x112d50 ActPoser::GetInitialTbOu
  0x112d80 ActPoser::SetInitialTbOu
  0x112dc0 ActPoser::Rotate
  0x112ef0 ActPoser::RotateX
  0x113020 ActPoser::CalculateFinalCorrectionMatrix
  0x113068 ActPoser::SnapMatrixLevelWithGround
  0x1130f8 ActPoser::CalcSnapAndCorrectionMatrices
  0x1133d0 ActPoser::SetNormalAnimation
  0x113430 ActPoser::SetNormalAnimation
  0x113470 ActPoser::SetupCrossFadeBlend
  0x113760 ActPoser::SetCrossFadeBlendAnimation
  0x1137f0 ActPoser::FinishCrossFadeBlend
  0x113898 ActPoser::ShutdownCrossFadeBlend
  0x113908 ActPoser::CalcCrossFadeTimes
  0x113a38 ActPoser::SetManualAnimation
  0x113ae8 ActPoser::DoSkeletonPose
  0x113b48 ActPoser::DoMainPose
  0x114180 ActPoser::DoIK
  0x1141e8 ActPoser::TransformLocalPoseToWorldPose
  0x114218 ActPoser::Skin
  0x114250 ActPoser::DoEventPose
  0x1143b8 ActPoser::DoInitialPoses
  0x1143f0 ActPoser::GetWeaponRange
  0x1145c0 ActPoser::AdvanceTime
  0x1146b8 ActPoser::FrameStep
  0x1146d0 ActPoser::GetCurrentFrame
  0x1146d8 ActPoser::GetTotalAnimFrames
  0x1146e0 ActPoser::ActPoser_global_ctors

Sheet rows:
  ActPoser::ActPoser(ActSkeleton *, ActEvents *, bool, bool)
  ActPoser::~ActPoser(void)
  ActPoser::GetBoneIndex(char *)
  ActPoser::GetRootBonePosOri(MATRIX4 &)
  ActPoser::GetWeaponBonePosOri(int, MATRIX4 &)
  ActPoser::ChangeAnimationOrigin(COORD3 &)
  ActPoser::GetAnimationOrigin(COORD3 &)
  ActPoser::SetAnimationOrigin(COORD3 &)
  ActPoser::GetInitialTbOu(MATRIX4 &)
  ActPoser::SetInitialTbOu(MATRIX4 &)
  ActPoser::Rotate(float)
  ActPoser::RotateX(float)
  ActPoser::CalculateFinalCorrectionMatrix(void)
  ActPoser::SnapMatrixLevelWithGround(MATRIX4 &)
  ActPoser::CalcSnapAndCorrectionMatrices(MATRIX4 &)
  ActPoser::SetNormalAnimation(ActAnimGroup *, float)
  ActPoser::SetNormalAnimation(ActAnimGroup *, float, MATRIX4 &)
  ActPoser::SetupCrossFadeBlend(float, bool)
  ActPoser::SetCrossFadeBlendAnimation(ActAnimGroup *, MATRIX4 &)
  ActPoser::FinishCrossFadeBlend(void)
  ActPoser::ShutdownCrossFadeBlend(void)
  ActPoser::CalcCrossFadeTimes(float &, float &, float &, float &
  ActPoser::SetManualAnimation(ActAnimGroup *, ActPoserManualPara
  ActPoser::DoSkeletonPose(void)
  ActPoser::DoMainPose(void)
  ActPoser::DoIK(void)
  ActPoser::TransformLocalPoseToWorldPose(void)
  ActPoser::Skin(void)
  ActPoser::DoEventPose(void)
  ActPoser::DoInitialPoses(void)
  ActPoser::GetWeaponRange(void)
  ActPoser::AdvanceTime(void)
  ActPoser::FrameStep(void)
  ActPoser::GetCurrentFrame(void)
  ActPoser::GetTotalAnimFrames(void)

Xbox methods treated as members (25 of 25; untyped ones count when ECX is read before it is written): ActPoser, AdvanceTime, CalcCrossFadeTimes, CalcSnapAndCorrectionMatrices, ChangeAnimationOrigin, DoEventPose, DoIK, DoInitialPoses, DoMainPose, DoSkeletonPose, FinishCrossFadeBlend, GetAnimationOrigin, GetBoneIndex, GetInitialTbOu, GetRootBonePosOri, GetWeaponBonePosOri, Rotate, RotateX, SetAnimationOrigin, SetCrossFadeBlendAnimation, SetInitialTbOu, SetNormalAnimation, SetupCrossFadeBlend, Skin, ~ActPoser

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[1] R/W [2: ActPoser@18550, CalcSnapAndCorrectionMatrices@18e10]
  +0x001  w[1] R/W [6: ActPoser@18550, CalcSnapAndCorrectionMatrices@18e10, DoMainPose@190b0, DoSkeletonPose@19750, Rotate@18bd0, RotateX@18cf0]
  +0x004  w[4] R/W -> ActSkeleton::BlendBones, ActSkeleton::GetNumBones, ActSkeleton::GetStillPose [9: ActPoser@18550, CalcSnapAndCorrectionMatrices@18e10, DoIK@196d0, DoMainPose@190b0, DoSkeletonPose@19750, GetBoneIndex@186e0…]
  +0x008  w[4] R/W float [9: AdvanceTime@19710, CalcCrossFadeTimes@18850, CalcSnapAndCorrectionMatrices@18e10, DoEventPose@18960, DoMainPose@190b0, FinishCrossFadeBlend@19610…]
  +0x00c  w[4] R/W float [6: AdvanceTime@19710, DoInitialPoses@197c0, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040, SetNormalAnimation@18fe0]
  +0x010  w[4] R/W float [6: CalcSnapAndCorrectionMatrices@18e10, DoEventPose@18960, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040, SetNormalAnimation@18fe0]
  +0x014  w[4] R/W float [5: CalcCrossFadeTimes@18850, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040, SetNormalAnimation@18fe0, SetupCrossFadeBlend@187d0]
  +0x018  w[4] W float [4: AdvanceTime@19710, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040, SetNormalAnimation@18fe0]
  +0x01c  w[4] R/W [7: CalcSnapAndCorrectionMatrices@18e10, DoEventPose@18960, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040, SetNormalAnimation@18fe0…]
  +0x020  w[4] R/W [2: ActPoser@18550, DoEventPose@18960]
  +0x024  w[4] R/W -> MatrixCopy [17: ActPoser@18550, CalcSnapAndCorrectionMatrices@18e10, ChangeAnimationOrigin@18750, DoIK@196d0, DoMainPose@190b0, DoSkeletonPose@19750…]
  +0x028  w[4] W float [8: ActPoser@18550, AdvanceTime@19710, CalcCrossFadeTimes@18850, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040…]
  +0x02c  w[4] W float [2: ActPoser@18550, FinishCrossFadeBlend@19610]
  +0x030  w[4] W float [7: ActPoser@18550, AdvanceTime@19710, CalcCrossFadeTimes@18850, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040…]
  +0x034  w[4] W float [7: ActPoser@18550, AdvanceTime@19710, CalcCrossFadeTimes@18850, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040…]
  +0x038  w[4] R/W -> __builtin_new [2: ActPoser@18550, ~ActPoser@19580]
  +0x03c  w[4] R/W -> __builtin_delete [2: ActPoser@18550, ~ActPoser@19580]
  +0x040  w[4] R/W -> FUN_00018ac0 [8: ActPoser@18550, CalcCrossFadeTimes@18850, DoEventPose@18960, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040…]
  +0x044  w[4] R/W -> ActIKSolverArray::Solve, ActIKSolverArray::~ActIKSolverArray [3: ActPoser@18550, DoIK@196d0, ~ActPoser@19580]
  +0x048  w[4] R/W -> ActGlobalPoseOverrideArray::DoGlobalOverrides, ActGlobalPoseOverrideArray::~ActGlobalPoseOverrideArray [3: ActPoser@18550, DoSkeletonPose@19750, ~ActPoser@19580]
  +0x04c  w[1] R/W [2: ActPoser@18550, DoMainPose@190b0]
  +0x050  w[4] R/W [7: ActPoser@18550, AdvanceTime@19710, DoEventPose@18960, DoMainPose@190b0, FinishCrossFadeBlend@19610, SetCrossFadeBlendAnimation@19040…]
  +0x054  w[4] R [1: DoMainPose@190b0]
  +0x058  w[4] R [1: DoMainPose@190b0]
  +0x05c  w[4] R [1: DoMainPose@190b0]
  +0x060  w[4] R [1: DoMainPose@190b0]
  +0x064  w[4] R [1: DoMainPose@190b0]
  +0x068  w[4] R [1: DoEventPose@18960]
  +0x06c  w[4] R [1: DoEventPose@18960]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [2: ActPoser@112928, CalcSnapAndCorrectionMatrices@1130f8]
  +0x004  w[4] R/W [5: ActPoser@112928, CalcSnapAndCorrectionMatrices@1130f8, DoMainPose@113b48, Rotate@112dc0, RotateX@112ef0]
  +0x008  w[4] R/W -> ActSkeleton::BlendBones, ActSkeleton::GetNumBones, ActSkeleton::GetStillPose [8: ActPoser@112928, CalcSnapAndCorrectionMatrices@1130f8, DoMainPose@113b48, GetRootBonePosOri@112b78, GetWeaponBonePosOri@112c18, GetWeaponRange@1143f0…]
  +0x00c  w[4] R/W float [10: AdvanceTime@1145c0, CalcCrossFadeTimes@113908, CalcSnapAndCorrectionMatrices@1130f8, DoEventPose@114250, DoMainPose@113b48, GetCurrentFrame@1146d0…]
  +0x010  w[4] R/W float [6: AdvanceTime@1145c0, DoInitialPoses@1143b8, DoMainPose@113b48, SetCrossFadeBlendAnimation@113760, SetManualAnimation@113a38, SetNormalAnimation@1133d0]
  +0x014  w[4] R/W float [6: CalcSnapAndCorrectionMatrices@1130f8, DoEventPose@114250, DoMainPose@113b48, SetCrossFadeBlendAnimation@113760, SetManualAnimation@113a38, SetNormalAnimation@1133d0]
  +0x018  w[4] R/W float [6: CalcCrossFadeTimes@113908, FinishCrossFadeBlend@1137f0, SetCrossFadeBlendAnimation@113760, SetManualAnimation@113a38, SetNormalAnimation@1133d0, SetupCrossFadeBlend@113470]
  +0x01c  w[4] R/W float [5: FinishCrossFadeBlend@1137f0, GetTotalAnimFrames@1146d8, SetCrossFadeBlendAnimation@113760, SetManualAnimation@113a38, SetNormalAnimation@1133d0]
  +0x020  w[4] R/W [9: CalcSnapAndCorrectionMatrices@1130f8, DoEventPose@114250, DoMainPose@113b48, FinishCrossFadeBlend@1137f0, GetWeaponRange@1143f0, SetCrossFadeBlendAnimation@113760…]
  +0x024  w[4] R/W [2: ActPoser@112928, DoEventPose@114250]
  +0x028  w[4] R/W [18: ActPoser@112928, CalcSnapAndCorrectionMatrices@1130f8, CalculateFinalCorrectionMatrix@113020, ChangeAnimationOrigin@112cc0, DoMainPose@113b48, GetAnimationOrigin@112ce8…]
  +0x02c  w[4] R/W float [10: ActPoser@112928, AdvanceTime@1145c0, CalcCrossFadeTimes@113908, DoMainPose@113b48, FrameStep@1146b8, GetWeaponRange@1143f0…]
  +0x030  w[4] R/W float [2: ActPoser@112928, FinishCrossFadeBlend@1137f0]
  +0x034  w[4] R/W float [9: ActPoser@112928, AdvanceTime@1145c0, CalcCrossFadeTimes@113908, DoMainPose@113b48, FrameStep@1146b8, GetWeaponRange@1143f0…]
  +0x038  w[4] R/W float [9: ActPoser@112928, AdvanceTime@1145c0, CalcCrossFadeTimes@113908, DoMainPose@113b48, FrameStep@1146b8, GetWeaponRange@1143f0…]
  +0x03c  w[4] R/W [2: ActPoser@112928, ~ActPoser@112a70]
  +0x040  w[4] R/W -> __builtin_delete [2: ActPoser@112928, ~ActPoser@112a70]
  +0x044  w[4] R/W [9: ActPoser@112928, CalcCrossFadeTimes@113908, DoEventPose@114250, DoMainPose@113b48, FinishCrossFadeBlend@1137f0, SetCrossFadeBlendAnimation@113760…]
  +0x048  w[4] R/W -> ActIKSolverArray::Solve, ActIKSolverArray::~ActIKSolverArray [3: ActPoser@112928, DoIK@114180, ~ActPoser@112a70]
  +0x04c  w[4] R/W -> ActGlobalPoseOverrideArray::~ActGlobalPoseOverrideArray [2: ActPoser@112928, ~ActPoser@112a70]
  +0x050  w[4] R/W [2: ActPoser@112928, DoMainPose@113b48]
  +0x054  w[4] R/W [7: ActPoser@112928, AdvanceTime@1145c0, DoEventPose@114250, DoMainPose@113b48, SetCrossFadeBlendAnimation@113760, SetManualAnimation@113a38…]
  +0x058  w[4, 8] R/W float [2: DoMainPose@113b48, SetManualAnimation@113a38]
  +0x05c  w[4] R float [1: DoMainPose@113b48]
  +0x060  w[4, 8] R/W float [2: DoMainPose@113b48, SetManualAnimation@113a38]
  +0x064  w[4] R float [1: DoMainPose@113b48]
  +0x068  w[4, 8] R/W float [2: DoMainPose@113b48, SetManualAnimation@113a38]
  +0x06c  w[4] R float [1: DoEventPose@114250]
  +0x070  w[4] R/W float [2: DoEventPose@114250, SetManualAnimation@113a38]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetBoneIndex: R +0x4 w4
  GetInitialTbOu: R +0x24 w4
