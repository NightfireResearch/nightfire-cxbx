# WRoadNav

FastAlloc/constructed sizes under its tag: {'allocated': [192], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xca090 first calls: ['UMemory::FastAlloc', 'RCameraSpline::RCameraSpline', 'WRoadNav::Reset']

Xbox methods (18):
  0xc9130 undefined Reset(void)
  0xc9240 undefined CalcNextSegmentRandom(undefined4 param_1, undefined4 param_2)
  0xc95a0 undefined ReverseNavDirection(void)
  0xc96b0 undefined SetBoundPos(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4)
  0xc97b0 undefined PathShareRoadSegment(undefined2 param_1)
  0xc9810 undefined PathForwardRoadSegment(undefined4 param_1, undefined4 param_2)
  0xc99f0 undefined UpdateLaneChange(undefined4 param_1)
  0xca090 undefined WRoadNav(void)
  0xca100 undefined ~WRoadNav(void)
  0xca120 undefined CalcNextSegmentDirection(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xca530 undefined CalcNextSegmentLane(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xcad40 undefined CalcNextSegmentSidewalk(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xcb280 undefined SetControlPos(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0xcb4b0 undefined IncNavPosition(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0xcb900 undefined InitAtSegment(undefined2 param_1, undefined1 param_2, undefined4 param_3, undefined1 param_4)
  0xcbcf0 undefined ChangeLanes(undefined4 param_1, undefined4 param_2)
  0xcbf20 undefined FindClosestSegmentInd(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xcc100 undefined InitAtPoint(undefined4 param_1, undefined4 param_2, undefined1 param_3)

PS2 methods (24):
  0x223338 WRoadNav::WRoadNav
  0x223380 WRoadNav::~WRoadNav
  0x2233d0 WRoadNav::Reset
  0x2235c8 WRoadNav::CalcNextSegmentRandom
  0x223a30 WRoadNav::CalcNextSegmentDirection
  0x223f98 WRoadNav::CalcNextSegmentLane
  0x2247b0 WRoadNav::CalcNextSegmentSidewalk
  0x224e00 WRoadNav::IncNavPosition
  0x225358 WRoadNav::ReverseNavDirection
  0x225490 WRoadNav::FindClosestSegmentInd
  0x2257c0 WRoadNav::InitAtSegment
  0x225c48 WRoadNav::InitAtPoint
  0x226538 WRoadNav::SetControlPos
  0x226788 WRoadNav::SetStartEndControls
  0x2267d8 WRoadNav::SetBoundPos
  0x226a28 WRoadNav::SetStartEndPos
  0x226a88 WRoadNav::PathGetPrevIntersection
  0x226ae0 WRoadNav::PathGetNextIntersection
  0x226b30 WRoadNav::PathGetDistPrevIntersection
  0x226b88 WRoadNav::PathGetDistNextIntersection
  0x226bd8 WRoadNav::PathShareRoadSegment
  0x226c88 WRoadNav::PathForwardRoadSegment
  0x226f18 WRoadNav::ChangeLanes
  0x227058 WRoadNav::UpdateLaneChange

Sheet rows:
  WRoadNav::WRoadNav(void)
  WRoadNav::~WRoadNav(void)
  WRoadNav::Reset(void)
  WRoadNav::CalcNextSegmentRandom(COORD3 &, char &, bool &)
  WRoadNav::CalcNextSegmentDirection(COORD3 &, short &, char &, b
  WRoadNav::CalcNextSegmentLane(float &, char &, bool &)
  WRoadNav::CalcNextSegmentSidewalk(float &, char &, bool &)
  WRoadNav::IncNavPosition(float, COORD3 &, short)
  WRoadNav::ReverseNavDirection(void)
  WRoadNav::FindClosestSegmentInd(COORD3 &, COORD3 &, float &)
  WRoadNav::InitAtSegment(short, char, float, bool)
  WRoadNav::InitAtPoint(COORD3 &, COORD3 &, bool)
  WRoadNav::SetControlPos(WRoadSegment &, COORD3 &, bool)
  WRoadNav::SetStartEndControls(WRoadSegment &)
  WRoadNav::SetBoundPos(WRoadSegment &, float, COORD3 &, bool)
  WRoadNav::SetStartEndPos(WRoadSegment &, float, float)
  WRoadNav::PathGetPrevIntersection(void)
  WRoadNav::PathGetNextIntersection(void)
  WRoadNav::PathGetDistPrevIntersection(void)
  WRoadNav::PathGetDistNextIntersection(void)
  WRoadNav::PathShareRoadSegment(short)
  WRoadNav::PathForwardRoadSegment(short, bool *)
  WRoadNav::ChangeLanes(float, float)
  WRoadNav::UpdateLaneChange(float)

Xbox methods treated as members (18 of 18; untyped ones count when ECX is read before it is written): CalcNextSegmentDirection, CalcNextSegmentLane, CalcNextSegmentRandom, CalcNextSegmentSidewalk, ChangeLanes, FindClosestSegmentInd, IncNavPosition, InitAtPoint, InitAtSegment, PathForwardRoadSegment, PathShareRoadSegment, Reset, ReverseNavDirection, SetBoundPos, SetControlPos, UpdateLaneChange, WRoadNav, ~WRoadNav

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130]
  +0x004  w[1] W [1: Reset@c9130]
  +0x010  w- LEA addr-taken [2: IncNavPosition@cb4b0, Reset@c9130]
  +0x020  w- LEA addr-taken [3: IncNavPosition@cb4b0, InitAtPoint@cc100, Reset@c9130]
  +0x030  w[4] LEA/W float addr-taken [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x034  w[4] W float [2: IncNavPosition@cb4b0, ReverseNavDirection@c95a0]
  +0x038  w[4] W float [2: IncNavPosition@cb4b0, ReverseNavDirection@c95a0]
  +0x03c  w[1] LEA/R/W addr-taken [10: CalcNextSegmentDirection@ca120, CalcNextSegmentLane@ca530, CalcNextSegmentRandom@c9240, CalcNextSegmentSidewalk@cad40, IncNavPosition@cb4b0, InitAtSegment@cb900…]
  +0x03e  w[2] R/W [9: CalcNextSegmentDirection@ca120, CalcNextSegmentLane@ca530, CalcNextSegmentRandom@c9240, CalcNextSegmentSidewalk@cad40, IncNavPosition@cb4b0, InitAtPoint@cc100…]
  +0x040  w[4] R/W float [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x050  w- LEA addr-taken [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x058  w[4] W float [1: IncNavPosition@cb4b0]
  +0x060  w[4] LEA/W float addr-taken [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x068  w[4] W float [1: IncNavPosition@cb4b0]
  +0x070  w- LEA addr-taken [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x080  w- LEA addr-taken [4: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x090  w[4] R/W -> RCameraSpline::EvaluateSpline, RCameraSpline::~RCameraSpline [4: IncNavPosition@cb4b0, InitAtSegment@cb900, WRoadNav@ca090, ~WRoadNav@ca100]
  +0x094  w[1] W [3: IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130]
  +0x095  w[1] R/W [5: CalcNextSegmentLane@ca530, CalcNextSegmentSidewalk@cad40, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x096  w[1] W [3: InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0]
  +0x098  w[4] R/W float [7: CalcNextSegmentSidewalk@cad40, ChangeLanes@cbcf0, IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0…]
  +0x09c  w[4] W float [6: ChangeLanes@cbcf0, IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0, UpdateLaneChange@c99f0]
  +0x0a0  w[4] R/W float [6: ChangeLanes@cbcf0, IncNavPosition@cb4b0, InitAtSegment@cb900, Reset@c9130, ReverseNavDirection@c95a0, UpdateLaneChange@c99f0]
  +0x0a4  w[4] W float [3: ChangeLanes@cbcf0, Reset@c9130, UpdateLaneChange@c99f0]
  +0x0a8  w[4] W float [3: ChangeLanes@cbcf0, Reset@c9130, UpdateLaneChange@c99f0]
  +0x0ac  w[4] W float [3: ChangeLanes@cbcf0, Reset@c9130, UpdateLaneChange@c99f0]
  +0x0b0  w[1] R/W [2: CalcNextSegmentRandom@c9240, Reset@c9130]
  +0x0b1  w[1] R/W [2: CalcNextSegmentRandom@c9240, Reset@c9130]
  +0x0b2  w[1] R/W [2: CalcNextSegmentRandom@c9240, Reset@c9130]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [5: FindClosestSegmentInd@225490, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x004  w[1] R/W [2: FindClosestSegmentInd@225490, Reset@2233d0]
  +0x010  w[8] LEA/R/W addr-taken [5: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x018  w[4] R/W [5: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x020  w[8] LEA/R/W addr-taken [5: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x028  w[4] R/W [5: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x030  w[4, 8] LEA/R/W float addr-taken [5: IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x034  w[4] R float [3: IncNavPosition@224e00, InitAtPoint@225c48, ReverseNavDirection@225358]
  +0x038  w[4] R/W float [5: IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x03c  w[1] LEA/R/W addr-taken [12: CalcNextSegmentDirection@223a30, CalcNextSegmentLane@223f98, CalcNextSegmentRandom@2235c8, CalcNextSegmentSidewalk@2247b0, IncNavPosition@224e00, InitAtPoint@225c48…]
  +0x03e  w[2] R/W [11: CalcNextSegmentDirection@223a30, CalcNextSegmentLane@223f98, CalcNextSegmentRandom@2235c8, CalcNextSegmentSidewalk@2247b0, ChangeLanes@226f18, IncNavPosition@224e00…]
  +0x040  w[4] R/W float [6: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x050  w[4, 8] LEA/R/W float addr-taken [8: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x058  w[4] R/W float [5: IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x060  w[4, 8] LEA/R/W float addr-taken [8: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x068  w[4] R/W float [5: IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x070  w[8] LEA/R/W addr-taken [7: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x078  w[4] R/W [2: Reset@2233d0, ReverseNavDirection@225358]
  +0x080  w[8] LEA/R/W addr-taken [7: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x088  w[4] R/W [2: Reset@2233d0, ReverseNavDirection@225358]
  +0x090  w[4] R/W -> RCameraSpline::BuildSplineEx, RCameraSpline::EvaluateSpline, RCameraSpline::~RCameraSpline [6: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, WRoadNav@223338, ~WRoadNav@223380]
  +0x094  w[1] W [4: IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0]
  +0x095  w[1] R/W [6: CalcNextSegmentLane@223f98, CalcNextSegmentSidewalk@2247b0, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x096  w[1] W [6: CalcNextSegmentLane@223f98, CalcNextSegmentSidewalk@2247b0, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358]
  +0x098  w[4] R/W float [9: CalcNextSegmentLane@223f98, CalcNextSegmentSidewalk@2247b0, ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0…]
  +0x09c  w[4] R/W float [7: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x0a0  w[4] R/W float [7: ChangeLanes@226f18, IncNavPosition@224e00, InitAtPoint@225c48, InitAtSegment@2257c0, Reset@2233d0, ReverseNavDirection@225358…]
  +0x0a4  w[4] W float [3: ChangeLanes@226f18, Reset@2233d0, UpdateLaneChange@227058]
  +0x0a8  w[4] R/W float [3: ChangeLanes@226f18, Reset@2233d0, UpdateLaneChange@227058]
  +0x0ac  w[4] R/W float [3: ChangeLanes@226f18, Reset@2233d0, UpdateLaneChange@227058]
  +0x0b0  w[1, 8] R/W [4: CalcNextSegmentDirection@223a30, CalcNextSegmentRandom@2235c8, InitAtPoint@225c48, Reset@2233d0]
  +0x0b1  w[1] W [1: Reset@2233d0]
  +0x0b2  w[1] W [1: Reset@2233d0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
