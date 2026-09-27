# WWorldPos

FastAlloc/constructed sizes under its tag: {'allocated': [64], 'constructed': [916]}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xd2fd0 first calls: []

Xbox methods (6):
  0xd2f10 undefined MakeFaceAtPoint(undefined4 param_1)
  0xd2f90 undefined HeightAtPoint(undefined4 param_1)
  0xd2fd0 undefined WWorldPos(void)
  0xd3050 undefined FindClosestFace(undefined4 param_1, undefined4 param_2)
  0xd3110 undefined FindClosestFace(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd31f0 undefined FindClosestFace(undefined4 param_1, undefined1 param_2)

PS2 methods (8):
  0x232180 WWorldPos::WWorldPos
  0x232200 WWorldPos::~WWorldPos
  0x232228 WWorldPos::MakeFaceAtPoint
  0x2322c8 WWorldPos::FindClosestFace
  0x2324e8 WWorldPos::FindClosestFace
  0x232638 WWorldPos::FindClosestFace
  0x2327a0 WWorldPos::HeightAtPoint
  0x232ad8 WWorldPos::WWorldPos_global_ctors

Sheet rows:
  WWorldPos::WWorldPos(void)
  WWorldPos::~WWorldPos(void)
  WWorldPos::MakeFaceAtPoint(COORD3 &)
  WWorldPos::FindClosestFace(COORD3 &, bool)
  WWorldPos::FindClosestFace(WCollisionInstanceCacheList &, COORD
  WWorldPos::FindClosestFace(WCollisionInstanceCacheList &, COORD
  WWorldPos::HeightAtPoint(COORD3 &, bool) const

Xbox methods treated as members (6 of 6; untyped ones count when ECX is read before it is written): FindClosestFace, HeightAtPoint, MakeFaceAtPoint, WWorldPos

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x004  w[4] W [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x008  w[4] W [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x010  w[4] LEA/W float addr-taken [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x014  w[4] W [1: MakeFaceAtPoint@d2f10]
  +0x018  w[4] W [1: MakeFaceAtPoint@d2f10]
  +0x020  w[4] LEA/W addr-taken [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x024  w[4] W [1: MakeFaceAtPoint@d2f10]
  +0x028  w[4] W float [1: MakeFaceAtPoint@d2f10]
  +0x02c  w[1, 2] W [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x02d  w[1] W [1: WWorldPos@d2fd0]
  +0x02e  w[2] W [2: MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x030  w[1] R/W [6: FindClosestFace@d3050, FindClosestFace@d3110, FindClosestFace@d31f0, HeightAtPoint@d2f90, MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x034  w[4] R/W [5: FindClosestFace@d3050, FindClosestFace@d3110, FindClosestFace@d31f0, MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x038  w[4] R/W [5: FindClosestFace@d3050, FindClosestFace@d3110, FindClosestFace@d31f0, MakeFaceAtPoint@d2f10, WWorldPos@d2fd0]
  +0x03c  w[4] W [3: FindClosestFace@d3110, FindClosestFace@d31f0, WWorldPos@d2fd0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x004  w[4] R/W float [2: HeightAtPoint@2327a0, MakeFaceAtPoint@232228]
  +0x008  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x010  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x014  w[4] R/W float [2: HeightAtPoint@2327a0, MakeFaceAtPoint@232228]
  +0x018  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x020  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x024  w[4] R/W float [2: HeightAtPoint@2327a0, MakeFaceAtPoint@232228]
  +0x028  w[4, 8] R/W float [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x02c  w[1] W [2: MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x02d  w[1] W [2: MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x02e  w[2] W [2: MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x030  w[4] R/W [6: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, HeightAtPoint@2327a0, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x034  w[4] R/W [5: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x038  w[4] R/W [5: FindClosestFace@2322c8, FindClosestFace@2324e8, FindClosestFace@232638, MakeFaceAtPoint@232228, WWorldPos@232180]
  +0x03c  w[4] W [3: FindClosestFace@2322c8, FindClosestFace@232638, WWorldPos@232180]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
