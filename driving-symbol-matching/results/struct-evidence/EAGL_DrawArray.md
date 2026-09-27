# EAGL::DrawArray

FastAlloc/constructed sizes under its tag: {'allocated': [76], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (14):
  0xf55a0 undefined SetGeoPrim(undefined4 param_1)
  0xf5640 undefined SetPrimitiveType(undefined4 param_1)
  0xf56c0 undefined GetIndexFromName(undefined4 param_1)
  0xf5730 undefined GetNameFromIndex(undefined4 param_1)
  0xf5750 undefined SetParamName(undefined4 param_1)
  0xf5770 undefined SetVar(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf57d0 undefined Lock(void)
  0xf57e0 undefined Unlock(void)
  0xf57f0 undefined SetNumVerts(undefined4 param_1)
  0xf5800 undefined SetLocalMatrix(undefined4 param_1)
  0xf5820 undefined SetUpGeoPrim(void)
  0xf5c30 undefined ~DrawArray(void)
  0xf5c80 undefined SetVar(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf5cb0 undefined Draw(undefined4 param_1)

PS2 methods (17):
  0x299da8 EAGL::DrawArray::SetGeoPrim
  0x299ee8 EAGL::DrawArray::SetUpGeoPrim
  0x29bf28 EAGL::DrawArray::ParamBufAlloc
  0x29bf88 EAGL::DrawArray::ParamBufFree
  0x29bfe0 EAGL::DrawArray::DrawArray
  0x29c070 EAGL::DrawArray::~DrawArray
  0x29c0f8 EAGL::DrawArray::SetPrimitiveType
  0x29c1a0 EAGL::DrawArray::GetIndexFromName
  0x29c230 EAGL::DrawArray::GetNameFromIndex
  0x29c250 EAGL::DrawArray::SetParamName
  0x29c2e8 EAGL::DrawArray::SetVar
  0x29c3b0 EAGL::DrawArray::SetVar
  0x29c488 EAGL::DrawArray::Lock
  0x29c4a0 EAGL::DrawArray::Unlock
  0x29c4b0 EAGL::DrawArray::Draw
  0x29c558 EAGL::DrawArray::SetNumVerts
  0x29c568 EAGL::DrawArray::SetLocalMatrix

Sheet rows:
  EAGL::DrawArray::SetGeoPrim(EAGL::GeoPrim *)
  EAGL::DrawArray::SetUpGeoPrim(int)
  EAGL::DrawArray::ParamBufAlloc(int)
  EAGL::DrawArray::ParamBufFree(void)
  EAGL::DrawArray::DrawArray(void)
  EAGL::DrawArray::~DrawArray(void)
  EAGL::DrawArray::SetPrimitiveType(EAGL::PrimitiveType)
  EAGL::DrawArray::GetIndexFromName(char *)
  EAGL::DrawArray::GetNameFromIndex(int)
  EAGL::DrawArray::SetParamName(char *)
  EAGL::DrawArray::SetVar(char *, void *, int, int)
  EAGL::DrawArray::SetVar(int, void *, int, int)
  EAGL::DrawArray::Lock(void)
  EAGL::DrawArray::Unlock(void)
  EAGL::DrawArray::Draw(int)
  EAGL::DrawArray::SetNumVerts(int)
  EAGL::DrawArray::SetLocalMatrix(MATRIX4 &)
  EAGL::DrawArray::gParamPool
  EAGL::DrawArray::gParamCur
  EAGL::DrawArray::gNumbParamBlocks

Xbox methods treated as members (14 of 14; untyped ones count when ECX is read before it is written): Draw, GetIndexFromName, GetNameFromIndex, Lock, SetGeoPrim, SetLocalMatrix, SetNumVerts, SetParamName, SetPrimitiveType, SetUpGeoPrim, SetVar, Unlock, ~DrawArray

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: SetPrimitiveType@f5640]
  +0x004  w[4] R -> EAGL::DynamicModel::AddGeoPrim, EAGL::DynamicModel::Draw, EAGL::DynamicModel::SetModelMatrix, EAGL::DynamicModel::~Dynam [4: Draw@f5cb0, SetGeoPrim@f55a0, SetLocalMatrix@f5800, ~DrawArray@f5c30]
  +0x008  w[4] R/W [5: GetIndexFromName@f56c0, GetNameFromIndex@f5730, SetGeoPrim@f55a0, SetUpGeoPrim@f5820, SetVar@f5770]
  +0x00c  w[4] R/W [4: SetGeoPrim@f55a0, SetUpGeoPrim@f5820, SetVar@f5770, ~DrawArray@f5c30]
  +0x010  w[4] R/W [4: GetIndexFromName@f56c0, SetGeoPrim@f55a0, SetUpGeoPrim@f5820, ~DrawArray@f5c30]
  +0x018  w[1] R/W [4: Draw@f5cb0, Lock@f57d0, Unlock@f57e0, ~DrawArray@f5c30]
  +0x01c  w[4] R/W [2: Draw@f5cb0, SetUpGeoPrim@f5820]
  +0x020  w[4] R/W [2: SetPrimitiveType@f5640, SetUpGeoPrim@f5820]
  +0x024  w[4] W [1: SetPrimitiveType@f5640]
  +0x028  w[4] W [1: SetPrimitiveType@f5640]
  +0x02c  w[4] R/W [2: SetParamName@f5750, SetUpGeoPrim@f5820]
  +0x030  w[4] R/W [1: SetPrimitiveType@f5640]
  +0x034  w[1] R/W [3: Draw@f5cb0, Lock@f57d0, SetUpGeoPrim@f5820]
  +0x035  w[1] R/W [2: SetUpGeoPrim@f5820, SetVar@f5770]
  +0x038  w[4] W [2: Draw@f5cb0, SetNumVerts@f57f0]
  +0x03c  w- LEA addr-taken [1: SetUpGeoPrim@f5820]
  +0x040  w[4] W [1: SetUpGeoPrim@f5820]
  +0x044  w[4] W [1: SetUpGeoPrim@f5820]
  +0x048  w[4] W [1: SetUpGeoPrim@f5820]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: DrawArray@29bfe0, SetPrimitiveType@29c0f8]
  +0x004  w[4] R/W -> EAGL::DynamicModel::AddGeoPrim, EAGL::DynamicModel::~DynamicModel [4: Draw@29c4b0, DrawArray@29bfe0, SetGeoPrim@299da8, ~DrawArray@29c070]
  +0x008  w[4] R/W [9: DrawArray@29bfe0, GetIndexFromName@29c1a0, GetNameFromIndex@29c230, SetGeoPrim@299da8, SetParamName@29c250, SetPrimitiveType@29c0f8…]
  +0x00c  w[4] R/W [5: DrawArray@29bfe0, SetGeoPrim@299da8, SetUpGeoPrim@299ee8, SetVar@29c3b0, ~DrawArray@29c070]
  +0x010  w[4] R/W [7: DrawArray@29bfe0, GetIndexFromName@29c1a0, SetGeoPrim@299da8, SetParamName@29c250, SetUpGeoPrim@299ee8, SetVar@29c2e8…]
  +0x014  w[4] R/W [2: Draw@29c4b0, DrawArray@29bfe0]
  +0x018  w[4] R/W [5: Draw@29c4b0, DrawArray@29bfe0, Lock@29c488, Unlock@29c4a0, ~DrawArray@29c070]
  +0x01c  w[4] R/W [3: Draw@29c4b0, DrawArray@29bfe0, SetUpGeoPrim@299ee8]
  +0x020  w[4] R/W [3: DrawArray@29bfe0, SetPrimitiveType@29c0f8, SetUpGeoPrim@299ee8]
  +0x024  w[4] R/W [4: Draw@29c4b0, DrawArray@29bfe0, SetPrimitiveType@29c0f8, SetUpGeoPrim@299ee8]
  +0x028  w[4] W [2: DrawArray@29bfe0, SetPrimitiveType@29c0f8]
  +0x02c  w[4] R/W [3: DrawArray@29bfe0, SetParamName@29c250, SetUpGeoPrim@299ee8]
  +0x030  w[4] R/W [3: Draw@29c4b0, DrawArray@29bfe0, SetPrimitiveType@29c0f8]
  +0x034  w[4] W [3: Draw@29c4b0, DrawArray@29bfe0, Lock@29c488]
  +0x038  w[4] W [2: DrawArray@29bfe0, SetVar@29c3b0]
  +0x03c  w[4] W [3: Draw@29c4b0, DrawArray@29bfe0, SetNumVerts@29c558]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetNameFromIndex: R +0x8 w4
  Unlock: W +0x18 w1
  SetNumVerts: W +0x38 w4
  SetLocalMatrix: R +0x4 w4
