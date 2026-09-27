# EAGL::Model

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (13):
  0xe8bf0 undefined GetChild(undefined4 param_1)
  0xe8f60 undefined GetGeometry(undefined4 param_1)
  0xe8ff0 undefined SetModelMatrix(undefined4 param_1)
  0xe9170 undefined GetTARList(undefined4 param_1, undefined4 param_2)
  0xe9210 undefined SetTexture(undefined4 param_1, undefined4 param_2)
  0xe9260 undefined DrawInstances(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xe9410 undefined Optimize(void)
  0xe9510 undefined Patc(undefined4 param_1)
  0xe96c0 undefined ClearMorphModel(void)
  0xea1e0 undefined SetTexture(undefined4 param_1, undefined4 param_2)
  0xea210 void __thiscall Draw(Model * this, MATRIX * param_1)
  0xea480 undefined MorphModel(void)
  0xea7b0 undefined Call(undefined4 param_1)

PS2 methods (22):
  0x2840c0 EAGL::Model::RemoveModel
  0x2843a0 EAGL::Model::Draw
  0x2848b0 EAGL::Model::DrawInstances
  0x284c40 EAGL::Model::Optimize
  0x284d88 EAGL::Model::MorphModel
  0x285780 EAGL::Model::GetChild
  0x2858d8 EAGL::Model::GetGeometry
  0x285970 EAGL::Model::SetModelMatrix
  0x2859f8 EAGL::Model::SetTexture
  0x285a48 EAGL::Model::GetTARList
  0x285b08 EAGL::Model::SetTexture
  0x285ba0 EAGL::Model::PS2SetOverlay
  0x285c80 EAGL::Model::IsOn
  0x285c90 EAGL::Model::On
  0x285ca0 EAGL::Model::Off
  0x285ca8 EAGL::Model::Patch
  0x285cf0 EAGL::Model::Model
  0x285df0 EAGL::Model::Model
  0x285e88 EAGL::Model::~Model
  0x285eb8 EAGL::Model::Link
  0x285ec0 EAGL::Model::Call
  0x285f48 EAGL::Model::ClearMorphModel

Sheet rows:
  EAGL::Model::RemoveModel(char *)
  EAGL::Model::Draw(MATRIX4 &)
  EAGL::Model::DrawInstances(EAGL::InstanceArray *, int, int)
  EAGL::Model::Optimize(void)
  EAGL::Model::MorphModel(void)
  EAGL::Model::VariableTable::find_end(char *) const
  EAGL::Model::TARList::TARList(void)
  EAGL::Model::GetName(void)
  EAGL::Model::GetChild(char *)
  EAGL::Model::AddFrozenModel(EAGL::Model *)
  EAGL::Model::AddHeirarchyModel(EAGL::Model *)
  EAGL::Model::AddTransformedModel(EAGL::Model *)
  EAGL::Model::GetGeometry(char *)
  EAGL::Model::GetModelMatrix(void)
  EAGL::Model::SetModelMatrix(MATRIX4 &)
  EAGL::Model::SetTextures(char *)
  EAGL::Model::SetTexture(char *, SHAPE *)
  EAGL::Model::GetTARList(char *) const
  EAGL::Model::SetTexture(EAGL::Model::TARList &, SHAPE *)
  EAGL::Model::PS2SetOverlay(char *, EAGL::Overlay *)
  EAGL::Model::IsOn(void)
  EAGL::Model::On(void)
  EAGL::Model::Off(void)
  EAGL::Model::Patch(EAGL::Variation *)
  EAGL::Model::Model(EAGL::Model &)
  EAGL::Model::Model(void)
  EAGL::Model::~Model(void)
  EAGL::Model::Link(EAGL::Morph *)
  EAGL::Model::Call(char *)
  EAGL::Model::ClearMorphModel(void)
  EAGL::Model::VariableTable::find_first(char *) const
  EAGL::Model::Variable::IsModifiable(void) const
  EAGL::Model::Variable::CopyData(void *, void *, int)
  EAGL::Model::PlatformCall(char *)
  EAGL::Model::PreLight(EAGL::LightBlock &)

Xbox methods treated as members (12 of 13; untyped ones count when ECX is read before it is written): Call, ClearMorphModel, Draw, DrawInstances, GetChild, GetGeometry, GetTARList, MorphModel, Optimize, SetModelMatrix, SetTexture

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x00c  w- LEA addr-taken [1: SetModelMatrix@e8ff0]
  +0x04c  w- LEA addr-taken -> EAGLInternal::ModelSetScale [1: Draw@ea210]
  +0x05c  w- LEA addr-taken [1: Draw@ea210]
  +0x08c  w[4] W float [1: Draw@ea210]
  +0x090  w[4] W float [1: Draw@ea210]
  +0x094  w[4] W float [1: Draw@ea210]
  +0x098  w[4] R [1: Draw@ea210]
  +0x09c  w[4] R [1: GetGeometry@e8f60]
  +0x0a0  w[4] R [1: GetGeometry@e8f60]
  +0x0a4  w[4] R [2: Draw@ea210, GetChild@e8bf0]
  +0x0a8  w[4] R [2: Draw@ea210, GetChild@e8bf0]
  +0x0ac  w[4] R [2: Draw@ea210, GetChild@e8bf0]
  +0x0b8  w[4] R [1: ClearMorphModel@e96c0]
  +0x0bc  w[4] R [1: GetTARList@e9170]
  +0x0c0  w[4] R [3: Draw@ea210, GetTARList@e9170, SetTexture@e9210]
  +0x0c4  w[4] R [1: Draw@ea210]
  +0x0c8  w[4] R [2: Draw@ea210, DrawInstances@e9260]
  +0x0cc  w[4] R [2: Draw@ea210, Optimize@e9410]
  +0x0d0  w[4] R [1: Draw@ea210]
  +0x0d4  w[4] R [1: MorphModel@ea480]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W [3: GetTARList@285a48, Model@285cf0, Model@285df0]
  +0x004  w[4] W [1: Model@285df0]
  +0x008  w[4] W [1: GetTARList@285a48]
  +0x00c  w[4, 8] R/W float [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x010  w[4] W [1: Model@285df0]
  +0x014  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x018  w[4] W [1: Model@285df0]
  +0x01c  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x020  w[4] W float [1: Model@285df0]
  +0x024  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x028  w[4] W [1: Model@285df0]
  +0x02c  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x030  w[4] W [1: Model@285df0]
  +0x034  w[4, 8] R/W float [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x038  w[4] W [1: Model@285df0]
  +0x03c  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x040  w[4] W [1: Model@285df0]
  +0x044  w[4, 8] R/W [3: Model@285cf0, Model@285df0, SetModelMatrix@285970]
  +0x048  w[4] W float [1: Model@285df0]
  +0x05c  w- LEA addr-taken [1: Draw@2843a0]
  +0x08c  w[4] R float [1: Draw@2843a0]
  +0x090  w[4] R float [1: Draw@2843a0]
  +0x094  w[4] R float [1: Draw@2843a0]
  +0x098  w[4] R float [1: Draw@2843a0]
  +0x09c  w[4] R/W [3: GetGeometry@2858d8, Model@285cf0, Model@285df0]
  +0x0a0  w[4] LEA/R/W addr-taken [3: GetGeometry@2858d8, Model@285cf0, Model@285df0]
  +0x0a4  w[4] R/W [5: Draw@2843a0, GetChild@285780, Model@285cf0, Model@285df0, RemoveModel@2840c0]
  +0x0a8  w[4] R/W [5: Draw@2843a0, GetChild@285780, Model@285cf0, Model@285df0, RemoveModel@2840c0]
  +0x0ac  w[4] R/W [3: Draw@2843a0, GetChild@285780, RemoveModel@2840c0]
  +0x0b0  w[4] R/W [2: Model@285cf0, Model@285df0]
  +0x0b4  w[4] R/W [2: Model@285cf0, Model@285df0]
  +0x0b8  w[4] R/W [4: ClearMorphModel@285f48, Model@285cf0, Model@285df0, MorphModel@284d88]
  +0x0bc  w[4] R/W [3: Model@285cf0, Model@285df0, PS2SetOverlay@285ba0]
  +0x0c0  w[4] R/W -> memcpy [7: ClearMorphModel@285f48, Draw@2843a0, Model@285cf0, Model@285df0, MorphModel@284d88, PS2SetOverlay@285ba0…]
  +0x0c4  w[4] R/W [5: Draw@2843a0, DrawInstances@2848b0, GetGeometry@2858d8, Model@285cf0, Model@285df0]
  +0x0c8  w[4] R/W [5: Draw@2843a0, DrawInstances@2848b0, IsOn@285c80, Off@285ca0, On@285c90]
  +0x0cc  w[4] R/W [5: Draw@2843a0, DrawInstances@2848b0, Model@285cf0, Model@285df0, Optimize@284c40]
  +0x0d0  w[4] R/W [3: Draw@2843a0, Model@285cf0, Model@285df0]
  +0x0d4  w[4] R/W [4: Link@285eb8, Model@285cf0, Model@285df0, MorphModel@284d88]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
