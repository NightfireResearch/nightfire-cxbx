# EAGL::DynamicModel

FastAlloc/constructed sizes under its tag: {'allocated': [88], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xe9760 first calls: []

Xbox methods (16):
  0xe9760 undefined DynamicModel(void)
  0xe9890 undefined AddGeoPrim(undefined4 param_1)
  0xe9990 undefined DrawNoTransform(void)
  0xe99e0 undefined Draw(void)
  0xe9a80 undefined SetModelMatrix(undefined4 param_1)
  0xe9aa0 undefined SetPrimitiveType(undefined4 param_1, undefined4 param_2)
  0xe9b40 undefined GetIndexFromName(undefined4 param_1, undefined4 param_2)
  0xe9be0 undefined SetVar(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xe9c90 undefined SetVar(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xe9d40 undefined SetStream(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xe9df0 undefined SetStream(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xe9ea0 undefined SetParamName(undefined4 param_1, undefined4 param_2)
  0xe9f40 undefined Lock(undefined4 param_1)
  0xe9fe0 undefined Unlock(undefined4 param_1)
  0xea080 undefined SetNumVerts(undefined4 param_1, undefined4 param_2)
  0xea720 undefined ~DynamicModel(void)

PS2 methods (18):
  0x2850b0 EAGL::DynamicModel::Preallocate
  0x2851f8 EAGL::DynamicModel::AddGeoPrim
  0x285388 EAGL::DynamicModel::Draw
  0x286018 EAGL::DynamicModel::DynamicModel
  0x286080 EAGL::DynamicModel::~DynamicModel
  0x286170 EAGL::DynamicModel::DrawNoTransform
  0x286210 EAGL::DynamicModel::GetModelMatrix
  0x286218 EAGL::DynamicModel::SetModelMatrix
  0x2862a0 EAGL::DynamicModel::SetPrimitiveType
  0x286348 EAGL::DynamicModel::GetIndexFromName
  0x2863f0 EAGL::DynamicModel::SetVar
  0x2864c0 EAGL::DynamicModel::SetVar
  0x286590 EAGL::DynamicModel::SetStream
  0x286660 EAGL::DynamicModel::SetStream
  0x286730 EAGL::DynamicModel::SetParamName
  0x2867d8 EAGL::DynamicModel::Lock
  0x286878 EAGL::DynamicModel::Unlock
  0x286918 EAGL::DynamicModel::SetNumVerts

Sheet rows:
  EAGL::DynamicModel::Preallocate(int)
  EAGL::DynamicModel::AddGeoPrim(EAGL::GeoPrim *)
  EAGL::DynamicModel::Draw(void)
  EAGL::DynamicModel::DynamicModel(void)
  EAGL::DynamicModel::~DynamicModel(void)
  EAGL::DynamicModel::DrawNoTransform(void)
  EAGL::DynamicModel::GetModelMatrix(void)
  EAGL::DynamicModel::SetModelMatrix(MATRIX4 &)
  EAGL::DynamicModel::SetPrimitiveType(int, EAGL::PrimitiveType)
  EAGL::DynamicModel::GetIndexFromName(int, char *)
  EAGL::DynamicModel::SetVar(int, char *, void *, int)
  EAGL::DynamicModel::SetVar(int, int, void *, int)
  EAGL::DynamicModel::SetStream(int, char *, void *, int)
  EAGL::DynamicModel::SetStream(int, int, void *, int)
  EAGL::DynamicModel::SetParamName(int, char *)
  EAGL::DynamicModel::Lock(int)
  EAGL::DynamicModel::Unlock(int)
  EAGL::DynamicModel::SetNumVerts(int, int)

Xbox methods treated as members (16 of 16; untyped ones count when ECX is read before it is written): AddGeoPrim, Draw, DrawNoTransform, DynamicModel, GetIndexFromName, Lock, SetModelMatrix, SetNumVerts, SetParamName, SetPrimitiveType, SetStream, SetVar, Unlock, ~DynamicModel

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: DynamicModel@e9760]
  +0x004  w[4] W [1: DynamicModel@e9760]
  +0x008  w[4] W [1: DynamicModel@e9760]
  +0x00c  w[4] W [1: DynamicModel@e9760]
  +0x010  w[4] W [1: DynamicModel@e9760]
  +0x014  w[4] W [1: DynamicModel@e9760]
  +0x018  w[4] W [1: DynamicModel@e9760]
  +0x01c  w[4] W [1: DynamicModel@e9760]
  +0x020  w[4] W [1: DynamicModel@e9760]
  +0x024  w[4] W [1: DynamicModel@e9760]
  +0x028  w[4] W [1: DynamicModel@e9760]
  +0x02c  w[4] W [1: DynamicModel@e9760]
  +0x030  w[4] W [1: DynamicModel@e9760]
  +0x034  w[4] W [1: DynamicModel@e9760]
  +0x038  w[4] W [1: DynamicModel@e9760]
  +0x03c  w[4] W [1: DynamicModel@e9760]
  +0x040  w[4] W [1: DynamicModel@e9760]
  +0x044  w[4] R/W [3: AddGeoPrim@e9890, DynamicModel@e9760, ~DynamicModel@ea720]
  +0x048  w[4] R/W [4: AddGeoPrim@e9890, DrawNoTransform@e9990, DynamicModel@e9760, ~DynamicModel@ea720]
  +0x04c  w[4] R/W [14: AddGeoPrim@e9890, DrawNoTransform@e9990, DynamicModel@e9760, GetIndexFromName@e9b40, Lock@e9f40, SetNumVerts@ea080…]
  +0x050  w[4] R/W [14: AddGeoPrim@e9890, DrawNoTransform@e9990, DynamicModel@e9760, GetIndexFromName@e9b40, Lock@e9f40, SetNumVerts@ea080…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x004  w[4] W [1: DynamicModel@286018]
  +0x008  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x00c  w[4] W [1: DynamicModel@286018]
  +0x010  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x014  w[4] W float [1: DynamicModel@286018]
  +0x018  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x01c  w[4] W [1: DynamicModel@286018]
  +0x020  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x024  w[4] W [1: DynamicModel@286018]
  +0x028  w[4, 8] R/W float [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x02c  w[4] W [1: DynamicModel@286018]
  +0x030  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x034  w[4] W [1: DynamicModel@286018]
  +0x038  w[4, 8] R/W [3: Draw@285388, DynamicModel@286018, SetModelMatrix@286218]
  +0x03c  w[4] W float [1: DynamicModel@286018]
  +0x040  w[4] W [1: DynamicModel@286018]
  +0x044  w[4] R/W [4: AddGeoPrim@2851f8, DynamicModel@286018, Preallocate@2850b0, ~DynamicModel@286080]
  +0x048  w[4] R/W [6: AddGeoPrim@2851f8, Draw@285388, DrawNoTransform@286170, DynamicModel@286018, Preallocate@2850b0, ~DynamicModel@286080]
  +0x04c  w[4] R/W [16: AddGeoPrim@2851f8, Draw@285388, DrawNoTransform@286170, DynamicModel@286018, GetIndexFromName@286348, Lock@2867d8…]
  +0x050  w[4] R/W [16: AddGeoPrim@2851f8, Draw@285388, DrawNoTransform@286170, DynamicModel@286018, GetIndexFromName@286348, Lock@2867d8…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
