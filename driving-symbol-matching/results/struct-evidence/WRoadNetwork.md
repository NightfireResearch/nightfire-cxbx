# WRoadNetwork

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (18):
  0xc8940 WRoadNetwork * __stdcall Get(void)
  0xc8950 undefined Init(void)
  0xc8de0 undefined __stdcall Restart(void)
  0xc8e60 undefined GetSpecialRoads(undefined4 param_1, undefined4 param_2)
  0xc8eb0 undefined GetSegmentLaneType(undefined4 param_1)
  0xc8ef0 undefined GetSegmentLaneIndex(undefined4 param_1, undefined4 param_2)
  0xc8fd0 undefined GetSegmentOppNode(undefined4 param_1, undefined4 param_2)
  0xc9000 undefined GetLinePointIntersect(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4
  0xc9af0 undefined __stdcall Shutdown(void)
  0xc9ba0 undefined GetSegmentLeftDrivableDist(undefined4 param_1)
  0xc9cd0 undefined GetSegmentRightDrivableDist(undefined4 param_1)
  0xc9e00 undefined GetSegmentLaneWeights(undefined4 param_1, undefined1 param_2, undefined4 param_3, undefined4 param_4
  0xc9f70 undefined GetSegmentPointIntersect(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 para
  0xc9fe0 undefined GetSegmentCurveStep(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, 
  0xcb3f0 undefined GetSegmentLeftDrivableDist(undefined4 param_1)
  0xcb410 undefined GetSegmentRightDrivableDist(undefined4 param_1)
  0xcb430 undefined GetPointOnSegment(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0xcbe70 undefined GetPointOnSegment(undefined4 param_1, undefined4 param_2, undefined4 param_3)

PS2 methods (46):
  0x220c18 WRoadNetwork::Get
  0x220c28 WRoadNetwork::Init
  0x2212f8 WRoadNetwork::Shutdown
  0x2213d8 WRoadNetwork::Restart
  0x221468 WRoadNetwork::GetRoadAtPos
  0x221528 WRoadNetwork::GetSpecialRoads
  0x221590 WRoadNetwork::GetSegmentNodes
  0x2215f8 WRoadNetwork::GetSegmentNodes
  0x221660 WRoadNetwork::GetSegmentForwardVector
  0x2216b0 WRoadNetwork::GetSegmentForwardVector
  0x2216d0 WRoadNetwork::GetSegmentEndVecs
  0x221730 WRoadNetwork::GetSegmentEndVecs
  0x221828 WRoadNetwork::GetSegmentEndPoints
  0x221888 WRoadNetwork::GetSegmentEndPoints
  0x221910 WRoadNetwork::GetSegmentEndPoints
  0x2219d0 WRoadNetwork::GetSegmentLength
  0x221a10 WRoadNetwork::GetSegmentLength
  0x221a88 WRoadNetwork::GetSegmentLeftDrivableDist
  0x221ac8 WRoadNetwork::GetSegmentLeftDrivableDist
  0x221c70 WRoadNetwork::GetSegmentRightDrivableDist
  0x221cb0 WRoadNetwork::GetSegmentRightDrivableDist
  0x221e58 WRoadNetwork::GetSegmentLaneType
  0x221e98 WRoadNetwork::GetSegmentLaneType
  0x221ed0 WRoadNetwork::GetSegmentLaneIndex
  0x221f20 WRoadNetwork::GetSegmentLaneIndex
  0x222060 WRoadNetwork::GetSegmentLaneWeights
  0x2222b0 WRoadNetwork::GetSegmentOppNode
  0x222300 WRoadNetwork::GetSegmentOppNode
  0x222350 WRoadNetwork::GetPointOnSegment
  0x2223b0 WRoadNetwork::GetPointOnSegment
  0x222468 WRoadNetwork::GetPointOnSegment
  0x2224c0 WRoadNetwork::GetPointAndVecOnSegment
  0x222530 WRoadNetwork::GetPointAndVecOnSegment
  0x2225d8 WRoadNetwork::GetPointAndVecOnSegment
  0x2226a0 WRoadNetwork::GetPointOnSegmentDist
  0x222700 WRoadNetwork::GetPointOnSegmentDist
  0x222790 WRoadNetwork::GetPointAndVecOnSegmentLane
  0x222810 WRoadNetwork::GetPointAndVecOnSegmentLane
  0x2228e8 WRoadNetwork::GetSegmentPointIntersect
  0x222968 WRoadNetwork::GetSegmentPointIntersect
  0x222a30 WRoadNetwork::GetLinePointIntersect
  0x222c30 WRoadNetwork::GetSegmentCurveStep
  0x222ca0 WRoadNetwork::GetSegmentCurveStep
  0x222d40 WRoadNetwork::GetSegmentCurveStep
  0x222e28 WRoadNetwork::CalcSegmentDrivable
  0x2273b8 WRoadNetwork::fgRoadNetwork_global_ctors

Sheet rows:
  WRoadNetwork::Get(void)
  WRoadNetwork::Init(void)
  WRoadNetwork::Shutdown(void)
  WRoadNetwork::Restart(void)
  WRoadNetwork::GetRoadAtPos(COORD3 &, COORD3 &)
  WRoadNetwork::GetSpecialRoads(int *, int)
  WRoadNetwork::GetSegmentNodes(WRoadSegment *, WRoadNode **)
  WRoadNetwork::GetSegmentNodes(WRoadSegment &, WRoadNode **)
  WRoadNetwork::GetSegmentForwardVector(int, COORD3 &)
  WRoadNetwork::GetSegmentForwardVector(WRoadSegment &, COORD3 &)
  WRoadNetwork::GetSegmentEndVecs(int, COORD3 &, COORD3 &)
  WRoadNetwork::GetSegmentEndVecs(WRoadSegment &, COORD3 &, COORD
  WRoadNetwork::GetSegmentEndPoints(int, COORD3 &, COORD3 &)
  WRoadNetwork::GetSegmentEndPoints(WRoadSegment &, COORD3 &, COO
  WRoadNetwork::GetSegmentEndPoints(WRoadSegment &, float, COORD3
  WRoadNetwork::GetSegmentLength(int)
  WRoadNetwork::GetSegmentLength(WRoadSegment &)
  WRoadNetwork::GetSegmentLeftDrivableDist(int)
  WRoadNetwork::GetSegmentLeftDrivableDist(WRoadSegment &)
  WRoadNetwork::GetSegmentRightDrivableDist(int)
  WRoadNetwork::GetSegmentRightDrivableDist(WRoadSegment &)
  WRoadNetwork::GetSegmentLaneType(int)
  WRoadNetwork::GetSegmentLaneType(WRoadSegment &)
  WRoadNetwork::GetSegmentLaneIndex(int, float *)
  WRoadNetwork::GetSegmentLaneIndex(WRoadSegment &, float *)
  WRoadNetwork::GetSegmentLaneWeights(int, char, unsigned char *,
  WRoadNetwork::GetSegmentOppNode(int, WRoadNode *)
  WRoadNetwork::GetSegmentOppNode(WRoadSegment &, WRoadNode *)
  WRoadNetwork::GetPointOnSegment(int, float, COORD3 &)
  WRoadNetwork::GetPointOnSegment(WRoadSegment &, float, COORD3 &
  WRoadNetwork::GetPointOnSegment(COORD3 &, COORD3 &, WRoadSegmen
  WRoadNetwork::GetPointAndVecOnSegment(int, float, COORD3 &, COO
  WRoadNetwork::GetPointAndVecOnSegment(WRoadSegment &, float, CO
  WRoadNetwork::GetPointAndVecOnSegment(COORD3 &, COORD3 &, WRoad
  WRoadNetwork::GetPointOnSegmentDist(int, float, COORD3 &)
  WRoadNetwork::GetPointOnSegmentDist(WRoadSegment &, float, COOR
  WRoadNetwork::GetPointAndVecOnSegmentLane(int, float, int, COOR
  WRoadNetwork::GetPointAndVecOnSegmentLane(WRoadSegment &, float
  WRoadNetwork::GetSegmentPointIntersect(int, COORD3 &, COORD3 &,
  WRoadNetwork::GetSegmentPointIntersect(WRoadSegment &, COORD3 &
  WRoadNetwork::GetLinePointIntersect(COORD3 &, COORD3 &, COORD3
  WRoadNetwork::GetSegmentCurveStep(int, float, COORD3 &)
  WRoadNetwork::GetSegmentCurveStep(WRoadSegment &, float, COORD3
  WRoadNetwork::GetSegmentCurveStep(COORD3 &, COORD3 &, WRoadSegm
  WRoadNetwork::CalcSegmentDrivable(COORD3 &, COORD3 &, float &,
  WRoadNetwork::fgRoadNetwork
  WRoadNetwork::fNodes
  WRoadNetwork::fSegments
  WRoadNetwork::fIntersections
  WRoadNetwork::fJunctions
  WRoadNetwork::fRoads
  WRoadNetwork::fSegmentStamp
  WRoadNetwork::kHalfRoadWidth
  WRoadNetwork::kMaxRoadWidth
  WRoadNetwork::kTramWidth
  WRoadNetwork::kMeridianWidth
  WRoadNetwork::kSidewalkWidth
  WRoadNetwork::kLaneWidth
  WRoadNetwork::kLaneHalfWidth
  WRoadNetwork::kLaneTypeStandard
  WRoadNetwork::kLaneTypeMeridian
  WRoadNetwork::kLaneTypeHighwayMeridian
  WRoadNetwork::kLaneTypeTram
  WRoadNetwork::kLaneWeight2Lane
  WRoadNetwork::kLaneWeight4Lane
  WRoadNetwork::kLaneWeight2LaneMeridian
  WRoadNetwork::kLaneWeight4LaneMeridian
  WRoadNetwork::kLaneWeight2LaneHighway
  WRoadNetwork::kLaneWeight4LaneHighway
  WRoadNetwork::kLaneWeight2LaneTram
  WRoadNetwork::kLaneWeight4LaneTram
  WRoadNetwork::kLaneWeight1Lane
  WRoadNetwork::kLaneWeight2LaneNoSidewalk
  WRoadNetwork::fValid
  WRoadNetwork::fNumRoads
  WRoadNetwork::fValidTrafficRoads
  WRoadNetwork::fRightSideSystem
  WRoadNetwork::fNumSegments
  WRoadNetwork::fNumNodes
  WRoadNetwork::fNumJunctions

Xbox methods treated as members (6 of 18; untyped ones count when ECX is read before it is written): Get, GetPointOnSegment, GetSegmentLeftDrivableDist, GetSegmentRightDrivableDist, GetSpecialRoads, Restart

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):

Short single-field methods (accessor candidates; check they touch this, not a pointee):
