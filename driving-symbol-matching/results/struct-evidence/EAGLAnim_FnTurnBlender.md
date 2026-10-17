# EAGLAnim::FnTurnBlender

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x104fa0 frees/deletes with size 0x5c (call to None)
Xbox vtable 0x001a0c6c (15 slots) stored by its constructor
Xbox vtable 0x001a14b4 (15 slots) stored by its constructor
PS2 sheet virtual table row: ['EAGLAnim::FnTurnBlender virtual table']
constructor 0x1042d0 first calls: []

Xbox methods (11):
  0x1042d0 undefined FnTurnBlender(void)
  0x104310 undefined ~FnTurnBlender(void)
  0x1043b0 undefined SetWeight(undefined4 param_1)
  0x104580 undefined BlendVel(undefined4 param_1)
  0x104820 undefined AlignVel(undefined4 param_1)
  0x104870 undefined BlendBeginFacing(undefined4 param_1)
  0x104a40 undefined BlendEndFacing(undefined4 param_1)
  0x104af0 undefined AlignCycleBeginEnd(undefined4 param_1)
  0x104bf0 undefined EvalSQT(void)
  0x104d60 undefined EvalVel2D(undefined4 param_1, undefined4 param_2)
  0x104fa0 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (20):
  0x279aa0 EAGLAnim::FnTurnBlender::EvalSQT
  0x279c70 EAGLAnim::FnTurnBlender::SetWeight
  0x279e30 EAGLAnim::FnTurnBlender::EvalVel2D
  0x27a048 EAGLAnim::FnTurnBlender::BlendVel
  0x27a1a8 EAGLAnim::FnTurnBlender::AlignCycleBeginEnd
  0x27a448 EAGLAnim::FnTurnBlender::AlignVel
  0x27a520 EAGLAnim::FnTurnBlender::BlendBeginFacing
  0x27a6b8 EAGLAnim::FnTurnBlender::BlendEndFacing
  0x27a8a0 EAGLAnim::FnTurnBlender::operator_new
  0x27a8c8 EAGLAnim::FnTurnBlender::operator_new
  0x27a8f0 EAGLAnim::FnTurnBlender::operator_delete
  0x27a918 EAGLAnim::FnTurnBlender::operator_new_array
  0x27a940 EAGLAnim::FnTurnBlender::operator_new_array
  0x27a968 EAGLAnim::FnTurnBlender::operator_delete_array
  0x27a990 EAGLAnim::FnTurnBlender::operator_new
  0x27a998 EAGLAnim::FnTurnBlender::operator_delete
  0x27a9a0 EAGLAnim::FnTurnBlender::operator_new_array
  0x27a9a8 EAGLAnim::FnTurnBlender::operator_delete_array
  0x27a9b0 EAGLAnim::FnTurnBlender::GetWeight
  0x27a9b8 EAGLAnim::FnTurnBlender::FnTurnBlender

Sheet rows:
  EAGLAnim::FnTurnBlender::~FnTurnBlender(void)
  EAGLAnim::FnTurnBlender::EvalSQT(float, float *, EAGLAnim::Bone
  EAGLAnim::FnTurnBlender::SetWeight(float)
  EAGLAnim::FnTurnBlender::EvalVel2D(float, float *)
  EAGLAnim::FnTurnBlender::BlendVel(float, float, float *) const
  EAGLAnim::FnTurnBlender::AlignCycleBeginEnd(int)
  EAGLAnim::FnTurnBlender::AlignRootQ(float *) const
  EAGLAnim::FnTurnBlender::AlignVel(float *) const
  EAGLAnim::FnTurnBlender::BlendBeginFacing(float *) const
  EAGLAnim::FnTurnBlender::BlendEndFacing(float *) const
  EAGLAnim::FnTurnBlender type_info function
  EAGLAnim::FnTurnBlender::operator new(unsigned int)
  EAGLAnim::FnTurnBlender::operator new(unsigned int, char *)
  EAGLAnim::FnTurnBlender::operator delete(void *, unsigned int)
  EAGLAnim::FnTurnBlender::operator new [](unsigned int)
  EAGLAnim::FnTurnBlender::operator new [](unsigned int, char *)
  EAGLAnim::FnTurnBlender::operator delete [](void *, unsigned in
  EAGLAnim::FnTurnBlender::operator new(unsigned int, void *)
  EAGLAnim::FnTurnBlender::operator delete(void *, void *)
  EAGLAnim::FnTurnBlender::operator new [](unsigned int, void *)
  EAGLAnim::FnTurnBlender::operator delete [](void *, void *)
  EAGLAnim::FnTurnBlender::GetWeight(void) const
  EAGLAnim::FnTurnBlender::FnTurnBlender(void)
  EAGLAnim::FnTurnBlender::Eval(float, float, float *)
  EAGLAnim::FnTurnBlender::SetAnims(EAGLAnim::Skeleton *, int, EA
  EAGLAnim::FnTurnBlender::EvalPhase(float, EAGLAnim::PhaseValue
  EAGLAnim::FnTurnBlender::GetFrequency(void) const
  EAGLAnim::FnTurnBlender::ComputeCycleIdx(float, float, float) c
  EAGLAnim::FnTurnBlender::ComputeAlignQ(float *, float *, COORD4
  EAGLAnim::FnTurnBlender::CycleTime(float, float, float) const
  EAGLAnim::FnTurnBlender::SetSpeed(float)
  EAGLAnim::FnTurnBlender virtual table
  EAGLAnim::FnTurnBlender type_info node

Xbox methods treated as members (11 of 11; untyped ones count when ECX is read before it is written): AlignCycleBeginEnd, AlignVel, BlendBeginFacing, BlendEndFacing, BlendVel, EvalSQT, EvalVel2D, FnTurnBlender, SetWeight, scalar_deleting_destructor, ~FnTurnBlender

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: FnTurnBlender@1042d0, ~FnTurnBlender@104310]
  +0x004  w[4] W [1: FnTurnBlender@1042d0]
  +0x008  w[4] W [1: FnTurnBlender@1042d0]
  +0x00c  w[4] R/W [2: FnTurnBlender@1042d0, ~FnTurnBlender@104310]
  +0x010  w[4] R/W -> EAGLAnim::FnRunBlender::ComputeBeginRootQ, EAGLAnim::FnRunBlender::ComputeEndRootQ [6: BlendBeginFacing@104870, BlendEndFacing@104a40, BlendVel@104580, EvalSQT@104bf0, EvalVel2D@104d60, FnTurnBlender@1042d0]
  +0x014  w[4] R/W -> EAGLAnim::FnRunBlender::ComputeBeginRootQ, EAGLAnim::FnRunBlender::ComputeEndRootQ [5: BlendBeginFacing@104870, BlendEndFacing@104a40, BlendVel@104580, EvalSQT@104bf0, FnTurnBlender@1042d0]
  +0x018  w[4] R/W float [5: BlendBeginFacing@104870, BlendEndFacing@104a40, BlendVel@104580, EvalSQT@104bf0, FnTurnBlender@1042d0]
  +0x01c  w[4] R/W [2: FnTurnBlender@1042d0, ~FnTurnBlender@104310]
  +0x020  w[4] W [1: FnTurnBlender@1042d0]
  +0x024  w[4] R -> FUN_000fa340 [1: EvalSQT@104bf0]
  +0x028  w[4] W float [2: EvalSQT@104bf0, EvalVel2D@104d60]
  +0x02c  w[4] W float [2: EvalSQT@104bf0, EvalVel2D@104d60]
  +0x030  w[4] W float [2: EvalSQT@104bf0, EvalVel2D@104d60]
  +0x034  w[4] W float [2: EvalSQT@104bf0, EvalVel2D@104d60]
  +0x038  w[4] W float [3: EvalSQT@104bf0, EvalVel2D@104d60, FnTurnBlender@1042d0]
  +0x03c  w[4] W [3: EvalSQT@104bf0, EvalVel2D@104d60, FnTurnBlender@1042d0]
  +0x040  w[4] W float [3: EvalSQT@104bf0, EvalVel2D@104d60, FnTurnBlender@1042d0]
  +0x044  w[4] R/W [2: AlignCycleBeginEnd@104af0, FnTurnBlender@1042d0]
  +0x048  w[4] LEA/W addr-taken [1: AlignCycleBeginEnd@104af0]
  +0x04c  w[4] W float [1: AlignCycleBeginEnd@104af0]
  +0x050  w[4] W float [1: AlignCycleBeginEnd@104af0]
  +0x054  w[4] W float [1: AlignCycleBeginEnd@104af0]
  +0x058  w[1] R/W [2: AlignCycleBeginEnd@104af0, FnTurnBlender@1042d0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: FnTurnBlender@27a9b8]
  +0x004  w[4] W [1: FnTurnBlender@27a9b8]
  +0x008  w[4] W [1: FnTurnBlender@27a9b8]
  +0x00c  w[4] R/W [2: FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x010  w[4] R/W -> EAGLAnim::FnRunBlender::ComputeBeginRootQ, EAGLAnim::FnRunBlender::ComputeEndRootQ [7: BlendBeginFacing@27a520, BlendEndFacing@27a6b8, BlendVel@27a048, EvalSQT@279aa0, EvalVel2D@279e30, FnTurnBlender@27a9b8…]
  +0x014  w[4] R/W -> EAGLAnim::FnRunBlender::ComputeBeginRootQ, EAGLAnim::FnRunBlender::ComputeEndRootQ [6: BlendBeginFacing@27a520, BlendEndFacing@27a6b8, BlendVel@27a048, EvalSQT@279aa0, FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x018  w[4] R/W float [7: BlendBeginFacing@27a520, BlendEndFacing@27a6b8, BlendVel@27a048, EvalSQT@279aa0, FnTurnBlender@27a9b8, GetWeight@27a9b0…]
  +0x01c  w[4] R/W [2: FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x020  w[4] R/W [2: FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x024  w[4] R -> FUN_00262aa8 [1: EvalSQT@279aa0]
  +0x028  w[4] R/W float [3: EvalSQT@279aa0, EvalVel2D@279e30, SetWeight@279c70]
  +0x02c  w[4] R/W float [3: EvalSQT@279aa0, EvalVel2D@279e30, SetWeight@279c70]
  +0x030  w[4] R/W float [3: EvalSQT@279aa0, EvalVel2D@279e30, SetWeight@279c70]
  +0x034  w[4] R/W float [3: EvalSQT@279aa0, EvalVel2D@279e30, SetWeight@279c70]
  +0x038  w[4] R/W float [4: EvalSQT@279aa0, EvalVel2D@279e30, FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x03c  w[4] R/W float [4: EvalSQT@279aa0, EvalVel2D@279e30, FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x040  w[4] R/W float [4: EvalSQT@279aa0, EvalVel2D@279e30, FnTurnBlender@27a9b8, SetWeight@279c70]
  +0x044  w[4] R/W [2: AlignCycleBeginEnd@27a1a8, FnTurnBlender@27a9b8]
  +0x048  w[4, 8] LEA/R/W float addr-taken [2: AlignCycleBeginEnd@27a1a8, AlignVel@27a448]
  +0x04c  w[4] R/W float [1: AlignCycleBeginEnd@27a1a8]
  +0x050  w[4, 8] R/W float [1: AlignCycleBeginEnd@27a1a8]
  +0x054  w[4] R/W float [1: AlignCycleBeginEnd@27a1a8]
  +0x058  w[4] R/W [2: AlignCycleBeginEnd@27a1a8, FnTurnBlender@27a9b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
