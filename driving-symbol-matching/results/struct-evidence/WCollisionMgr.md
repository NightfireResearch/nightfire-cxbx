# WCollisionMgr

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xc5500 first calls: ['FUN_00053580', 'WSoundMap::WSoundMap']

Xbox methods (31):
  0xbedd0 undefined SurfaceBumpHeight(undefined4 param_1, undefined4 param_2)
  0xbf0e0 undefined FindFaceInTriStrip(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xbf210 undefined GetWorldHeightAtPoint(undefined4 param_1, undefined4 param_2)
  0xbf2d0 undefined GetGroundCollision(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xbf3a0 undefined ClosestCollisionInfo(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xbf8c0 undefined GetOBBObjectIntersection(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 para
  0xbffe0 undefined FindFaceInCInst(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc01d0 undefined FindFaceInCInst(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, unde
  0xc0440 undefined GetClosestIntersectingCylObject(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc05f0 undefined GetClosestIntersectingOBBObject(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc0660 undefined GetClosestIntersectingBarrier(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc0890 undefined GetBarrierNormal(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc0e00 undefined GetWorldNormal(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undef
  0xc2ff0 undefined SetCollisionArticle(undefined4 param_1, undefined4 param_2)
  0xc31f0 undefined GetInstanceListGuts(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc3410 undefined GetInstanceList(undefined4 param_1, undefined4 param_2)
  0xc34c0 undefined GetObjectListsGuts(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, u
  0xc36a0 undefined GetObjectLists(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xc37c0 undefined GetObjectLists(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc3880 undefined GetBarrierList(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc3b40 undefined CheckHitWorld(undefined4 param_1, undefined4 param_2)
  0xc4000 undefined StepCheckHitWorld(undefined4 param_1)
  0xc4120 undefined GetInstanceStripList(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4,
  0xc43c0 undefined GetInstanceListGuts(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, 
  0xc4510 undefined GetInstanceList(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, unde
  0xc4d70 undefined CheckHitWindow(undefined4 param_1, undefined1 param_2, undefined4 param_3)
  0xc5380 undefined Restart(void)
  0xc5460 undefined ~WCollisionMgr(void)
  0xc5500 undefined WCollisionMgr(void)
  0xc55b0 undefined __cdecl Init(int param_1)
  0xc56d0 undefined __stdcall Shutdown(void)

PS2 methods (35):
  0x213110 WCollisionMgr::WCollisionMgr
  0x2131e8 WCollisionMgr::~WCollisionMgr
  0x2132b8 WCollisionMgr::Init
  0x213470 WCollisionMgr::Shutdown
  0x2134a8 WCollisionMgr::Restart
  0x213618 WCollisionMgr::SurfaceBumpHeight
  0x213720 WCollisionMgr::SetCollisionArticle
  0x2138b0 WCollisionMgr::FindFaceInTriStrip
  0x213d68 WCollisionMgr::FindFaceInTriStrip
  0x213ff8 WCollisionMgr::FindFaceInCInst
  0x214338 WCollisionMgr::FindFaceInCInst
  0x214678 WCollisionMgr::FindFaceInCInst
  0x214c08 WCollisionMgr::FindFaceInCInst
  0x2151b8 WCollisionMgr::GetWorldHeightAtPoint
  0x2153b0 WCollisionMgr::CheckHitWindow
  0x215cc8 WCollisionMgr::CheckHitWorld
  0x216500 WCollisionMgr::StepCheckHitWorld
  0x2166f8 WCollisionMgr::GetWorldNormal
  0x216b60 WCollisionMgr::GetGroundCollision
  0x216d30 WCollisionMgr::ClosestCollisionInfo
  0x216ec0 WCollisionMgr::GetInstanceStripList
  0x217228 WCollisionMgr::GetInstanceListGuts
  0x217588 WCollisionMgr::GetInstanceList
  0x217768 WCollisionMgr::GetInstanceListGuts
  0x217c88 WCollisionMgr::GetInstanceList
  0x217e30 WCollisionMgr::GetObjectListsGuts
  0x218150 WCollisionMgr::GetObjectLists
  0x218af0 WCollisionMgr::GetClosestIntersectingCylObject
  0x218d10 WCollisionMgr::GetOBBObjectIntersection
  0x219280 WCollisionMgr::GetClosestIntersectingOBBObject
  0x219328 WCollisionMgr::GetClosestIntersectingBarrier
  0x219630 WCollisionMgr::GetBarrierNormal
  0x219a50 WCollisionMgr::GetBarrierList
  0x219f48 WCollisionMgr::GetBarrierList
  0x21c0d0 WCollisionMgr::fgCollisionMgr_global_ctors

Sheet rows:
  WCollisionMgr::WCollisionMgr(void)
  WCollisionMgr::~WCollisionMgr(void)
  WCollisionMgr::Init(UGroup *)
  WCollisionMgr::Shutdown(void)
  WCollisionMgr::Restart(void)
  WCollisionMgr::SurfaceBumpHeight(COORD3 &, WSurface &)
  WCollisionMgr::SetCollisionArticle(CARP::Instance *, unsigned i
  WCollisionMgr::FindFaceInTriStrip(COORD3 &, WCollisionStrip *&)
  WCollisionMgr::FindFaceInTriStrip(MATRIX4 &, COORD3 &, WCollisi
  WCollisionMgr::FindFaceInCInst(COORD3 &, WCollisionInstance &,
  WCollisionMgr::FindFaceInCInst(COORD3 &, WCollisionInstanceCach
  WCollisionMgr::FindFaceInCInst(MATRIX4 &, COORD3 &, WCollisionI
  WCollisionMgr::FindFaceInCInst(MATRIX4 &, COORD3 &, WCollisionI
  WCollisionMgr::GetWorldHeightAtPoint(COORD3 &, float &, bool)
  WCollisionMgr::CheckHitWindow(WCollisionMgr::WorldCollisionInfo
  WCollisionMgr::CheckHitWorld(COORD4 *, WCollisionMgr::WorldColl
  WCollisionMgr::StepCheckHitWorld(COORD4 *, float)
  WCollisionMgr::GetWorldNormal(WCollisionInstanceCacheList &, WC
  WCollisionMgr::GetGroundCollision(COORD4 *, WWorldPos &, WColli
  WCollisionMgr::ClosestCollisionInfo(COORD4 *, WCollisionMgr::Wo
  WCollisionMgr::GetInstanceStripList(WCollisionInstance &, COORD
  WCollisionMgr::GetInstanceListGuts(vector<unsigned int, allocat
  WCollisionMgr::GetInstanceList(WCollisionInstanceCacheList &, C
  WCollisionMgr::GetInstanceListGuts(vector<unsigned int, allocat
  WCollisionMgr::GetInstanceList(WCollisionInstanceCacheList &, C
  WCollisionMgr::GetObjectListsGuts(vector<unsigned int, allocato
  WCollisionMgr::GetObjectLists(WCollisionObjectList &, WCollisio
  WCollisionMgr::GetObjectLists(WCollisionObjectList &, WCollisio
  WCollisionMgr::GetObjectOBB(WCollisionObjectList &, COORD3 &, W
  WCollisionMgr::GetCylObjectNormal(COORD4 *, WCollisionObjectLis
  WCollisionMgr::GetOBBObjectNormal(COORD4 *, WCollisionObjectLis
  WCollisionMgr::GetClosestIntersectingCylObject(COORD4 *, COORD4
  WCollisionMgr::GetOBBObjectIntersection(COORD4 *, MATRIX4 &, CO
  WCollisionMgr::GetClosestIntersectingOBBObject(COORD4 *, COORD4
  WCollisionMgr::GetClosestIntersectingBarrier(WCollisionBarrierL
  WCollisionMgr::GetBarrierNormal(WCollisionInstanceCacheList &,
  WCollisionMgr::GetBarrierList(WCollisionBarrierList &, WCollisi
  WCollisionMgr::GetBarrierList(WCollisionBarrierList &, COORD3 &
  WCollisionMgr::GetBarrierNormal(WCollisionBarrierList &, COORD4
  WCollisionMgr::fgCollisionMgr

Xbox methods treated as members (21 of 31; untyped ones count when ECX is read before it is written): CheckHitWindow, CheckHitWorld, FindFaceInCInst, FindFaceInTriStrip, GetBarrierList, GetBarrierNormal, GetClosestIntersectingOBBObject, GetInstanceList, GetInstanceListGuts, GetObjectLists, GetObjectListsGuts, GetWorldNormal, Init, Restart, SetCollisionArticle, WCollisionMgr, ~WCollisionMgr

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: WCollisionMgr@c5500]
  +0x004  w[4] R/RW/W [3: GetInstanceListGuts@c31f0, GetInstanceListGuts@c43c0, WCollisionMgr@c5500]
  +0x008  w[4] R/W [4: GetInstanceListGuts@c31f0, GetInstanceListGuts@c43c0, SetCollisionArticle@c2ff0, WCollisionMgr@c5500]
  +0x00c  w[4] R/W [2: SetCollisionArticle@c2ff0, WCollisionMgr@c5500]
  +0x010  w[4] W [1: WCollisionMgr@c5500]
  +0x014  w- LEA addr-taken [2: WCollisionMgr@c5500, ~WCollisionMgr@c5460]
  +0x018  w[4] R [1: ~WCollisionMgr@c5460]
  +0x020  w- LEA addr-taken [2: WCollisionMgr@c5500, ~WCollisionMgr@c5460]
  +0x024  w[4] R [1: ~WCollisionMgr@c5460]
  +0x02c  w[4] W [1: WCollisionMgr@c5500]
  +0x030  w[1] W [1: WCollisionMgr@c5500]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] W [2: GetInstanceStripList@216ec0, WCollisionMgr@213110]
  +0x004  w[4] R/W [4: GetBarrierList@219f48, GetInstanceListGuts@217228, GetInstanceListGuts@217768, WCollisionMgr@213110]
  +0x008  w[4] R/W [4: GetInstanceListGuts@217228, GetInstanceListGuts@217768, SetCollisionArticle@213720, WCollisionMgr@213110]
  +0x00c  w[4] R/W [2: SetCollisionArticle@213720, WCollisionMgr@213110]
  +0x010  w[4] R/W [2: GetObjectListsGuts@217e30, WCollisionMgr@213110]
  +0x014  w[4] LEA/R/W addr-taken -> UMemory::FastFree [3: CheckHitWindow@2153b0, WCollisionMgr@213110, ~WCollisionMgr@2131e8]
  +0x020  w[4] LEA/R/W addr-taken -> UMemory::FastFree [3: SetCollisionArticle@213720, WCollisionMgr@213110, ~WCollisionMgr@2131e8]
  +0x02c  w[4] R/W [2: FindFaceInTriStrip@2138b0, WCollisionMgr@213110]
  +0x030  w[4] W [1: WCollisionMgr@213110]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
