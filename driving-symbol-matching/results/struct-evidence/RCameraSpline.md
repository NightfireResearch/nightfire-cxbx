# RCameraSpline

FastAlloc/constructed sizes under its tag: {'allocated': [112], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x7acd0 first calls: ['FUN_0007a950', 'VU0_v4Init', 'VU0_v4Init']

Xbox methods (10):
  0x7a530 undefined EvaluateSpline(undefined4 param_1, undefined4 param_2)
  0x7a5c0 undefined EvaluateTangent(undefined4 param_1, undefined4 param_2)
  0x7a640 undefined GetPointListSize(void)
  0x7a650 undefined BuildSplineEx(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x7a6d0 undefined EvaluateSpline(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0x7a9b0 undefined ~RCameraSpline(void)
  0x7a9f0 undefined ClearSplinePtList(void)
  0x7aa20 undefined BuildSpline(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0x7acd0 undefined RCameraSpline(void)
  0x7ae00 undefined AddToSplinePtList(undefined4 param_1)

PS2 methods (14):
  0x1a7ba0 RCameraSpline::RCameraSpline
  0x1a7c80 RCameraSpline::~RCameraSpline
  0x1a7cd8 RCameraSpline::ClearSplinePtList
  0x1a7cf8 RCameraSpline::AddToSplinePtList
  0x1a7d90 RCameraSpline::GetPointListSize
  0x1a7dd0 RCameraSpline::GetBasisMatrix
  0x1a7e00 RCameraSpline::GetTangentBasisMatrix
  0x1a7e30 RCameraSpline::Smooth
  0x1a7f58 RCameraSpline::BuildSpline
  0x1a8380 RCameraSpline::BuildSplineEx
  0x1a8440 RCameraSpline::EvaluateSpline
  0x1a85d0 RCameraSpline::EvaluateSpline
  0x1a86a8 RCameraSpline::EvaluateTangent
  0x1a88e8 RCameraSpline::RCameraSpline_global_ctors

Sheet rows:
  RCameraSpline::RCameraSpline(void)
  RCameraSpline::~RCameraSpline(void)
  RCameraSpline::ClearSplinePtList(void)
  RCameraSpline::AddToSplinePtList(COORD4 &)
  RCameraSpline::GetPointListSize(void) const
  RCameraSpline::GetBasisMatrix(void) const
  RCameraSpline::GetTangentBasisMatrix(void) const
  RCameraSpline::Smooth(float) const
  RCameraSpline::BuildSpline(COORD4 &, COORD4 &, COORD4 &, COORD4
  RCameraSpline::BuildSplineEx(COORD3 &, COORD3 &, COORD3 &, COOR
  RCameraSpline::EvaluateSpline(float, MATRIX4 *, bool)
  RCameraSpline::EvaluateSpline(float, COORD4 &)
  RCameraSpline::EvaluateTangent(float, COORD4 &)

Xbox methods treated as members (10 of 10; untyped ones count when ECX is read before it is written): AddToSplinePtList, BuildSpline, BuildSplineEx, ClearSplinePtList, EvaluateSpline, EvaluateTangent, GetPointListSize, RCameraSpline, ~RCameraSpline

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x004  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x008  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x00c  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x010  w- LEA addr-taken [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x01c  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x020  w- LEA addr-taken [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x02c  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x030  w- LEA addr-taken [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x03c  w[4] W [2: BuildSpline@7aa20, BuildSplineEx@7a650]
  +0x040  w- LEA addr-taken [2: BuildSpline@7aa20, RCameraSpline@7acd0]
  +0x050  w- LEA addr-taken -> VU0_v4Init [1: RCameraSpline@7acd0]
  +0x060  w[4] R/W [6: BuildSpline@7aa20, BuildSplineEx@7a650, EvaluateSpline@7a530, EvaluateSpline@7a6d0, EvaluateTangent@7a5c0, RCameraSpline@7acd0]
  +0x064  w- LEA addr-taken [4: AddToSplinePtList@7ae00, BuildSpline@7aa20, RCameraSpline@7acd0, ~RCameraSpline@7a9b0]
  +0x068  w[4] R [4: AddToSplinePtList@7ae00, BuildSpline@7aa20, ClearSplinePtList@7a9f0, ~RCameraSpline@7a9b0]
  +0x06c  w[4] R [3: BuildSpline@7aa20, BuildSplineEx@7a650, GetPointListSize@7a640]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x008  w[4, 8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x00c  w[4] W [2: BuildSpline@1a7f58, BuildSplineEx@1a8380]
  +0x010  w[8] R/W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x018  w[4, 8] R/W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x01c  w[4] W [2: BuildSpline@1a7f58, BuildSplineEx@1a8380]
  +0x020  w[8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x028  w[4, 8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x02c  w[4] W [2: BuildSpline@1a7f58, BuildSplineEx@1a8380]
  +0x030  w[8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x038  w[4, 8] W [3: BuildSpline@1a7f58, BuildSplineEx@1a8380, RCameraSpline@1a7ba0]
  +0x03c  w[4] W [2: BuildSpline@1a7f58, BuildSplineEx@1a8380]
  +0x040  w[16] LEA/W addr-taken [2: BuildSpline@1a7f58, EvaluateSpline@1a8440]
  +0x050  w[16] LEA/W addr-taken [2: BuildSpline@1a7f58, EvaluateSpline@1a8440]
  +0x060  w[4] R/W [8: BuildSpline@1a7f58, BuildSplineEx@1a8380, EvaluateSpline@1a8440, EvaluateSpline@1a85d0, GetBasisMatrix@1a7dd0, GetTangentBasisMatrix@1a7e00…]
  +0x064  w[4] LEA/R/W addr-taken -> UMemory::FastFree [6: AddToSplinePtList@1a7cf8, BuildSpline@1a7f58, BuildSplineEx@1a8380, GetPointListSize@1a7d90, RCameraSpline@1a7ba0, ~RCameraSpline@1a7c80]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetPointListSize: R +0x6c w4
