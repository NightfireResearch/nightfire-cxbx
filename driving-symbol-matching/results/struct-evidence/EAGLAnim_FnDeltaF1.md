# EAGLAnim::FnDeltaF1

FastAlloc/constructed sizes under its tag: None
deleting destructor 0xff1e0 frees/deletes with size 0x30 (call to None)
Xbox vtable 0x001a1270 (18 slots) stored by its constructor
PS2 sheet virtual table row: ['EAGLAnim::FnDeltaF1 virtual table']
constructor 0xfe280 first calls: ['EAGLAnim::FnAnimMemoryMap::FnAnimMemoryMap']

Xbox methods (9):
  0xfe280 undefined FnDeltaF1(void)
  0xfe2b0 undefined Eval(void)
  0xfe2d0 undefined EvalSQTMask(void)
  0xfeaa0 undefined EvalWeights(void)
  0xfeac0 undefined EvalVel2D(void)
  0xfeae0 undefined InitBuffersAsRequired(void)
  0xfebd0 undefined EvalSQT(void)
  0xff1e0 undefined scalar_deleting_destructor(undefined1 param_1)
  0xff210 undefined ~FnDeltaF1(void)

PS2 methods (20):
  0x2701b8 EAGLAnim::FnDeltaF1::EvalSQT
  0x270a30 EAGLAnim::FnDeltaF1::EvalSQTMask
  0x2713f0 EAGLAnim::FnDeltaF1::InitBuffersAsRequired
  0x2715a8 EAGLAnim::FnDeltaF1::operator_new
  0x2715d0 EAGLAnim::FnDeltaF1::operator_new
  0x2715f8 EAGLAnim::FnDeltaF1::operator_delete
  0x271620 EAGLAnim::FnDeltaF1::operator_new_array
  0x271648 EAGLAnim::FnDeltaF1::operator_new_array
  0x271670 EAGLAnim::FnDeltaF1::operator_delete_array
  0x271698 EAGLAnim::FnDeltaF1::operator_new
  0x2716a0 EAGLAnim::FnDeltaF1::operator_delete
  0x2716a8 EAGLAnim::FnDeltaF1::operator_new_array
  0x2716b0 EAGLAnim::FnDeltaF1::operator_delete_array
  0x2716b8 EAGLAnim::FnDeltaF1::FnDeltaF1
  0x271710 EAGLAnim::FnDeltaF1::~FnDeltaF1
  0x2717d8 EAGLAnim::FnDeltaF1::SetAnimMemoryMap
  0x2717f0 EAGLAnim::FnDeltaF1::GetLength
  0x271830 EAGLAnim::FnDeltaF1::Eval
  0x271860 EAGLAnim::FnDeltaF1::EvalWeights
  0x271890 EAGLAnim::FnDeltaF1::EvalVel2D

Sheet rows:
  EAGLAnim::FnDeltaF1::EvalSQT(float, float *, EAGLAnim::BoneMask
  EAGLAnim::FnDeltaF1::EvalSQTMask(float, float *, EAGLAnim::Bone
  EAGLAnim::FnDeltaF1::InitBuffersAsRequired(void)
  EAGLAnim::FnDeltaF1 type_info function
  EAGLAnim::FnDeltaF1::operator new(unsigned int)
  EAGLAnim::FnDeltaF1::operator new(unsigned int, char *)
  EAGLAnim::FnDeltaF1::operator delete(void *, unsigned int)
  EAGLAnim::FnDeltaF1::operator new [](unsigned int)
  EAGLAnim::FnDeltaF1::operator new [](unsigned int, char *)
  EAGLAnim::FnDeltaF1::operator delete [](void *, unsigned int)
  EAGLAnim::FnDeltaF1::operator new(unsigned int, void *)
  EAGLAnim::FnDeltaF1::operator delete(void *, void *)
  EAGLAnim::FnDeltaF1::operator new [](unsigned int, void *)
  EAGLAnim::FnDeltaF1::operator delete [](void *, void *)
  EAGLAnim::FnDeltaF1::FnDeltaF1(void)
  EAGLAnim::FnDeltaF1::~FnDeltaF1(void)
  EAGLAnim::FnDeltaF1::SetAnimMemoryMap(EAGLAnim::AnimMemoryMap *
  EAGLAnim::FnDeltaF1::GetLength(float &) const
  EAGLAnim::FnDeltaF1::Eval(float, float, float *)
  EAGLAnim::FnDeltaF1::EvalWeights(float, float *)
  EAGLAnim::FnDeltaF1::EvalVel2D(float, float *)
  EAGLAnim::FnDeltaF1 virtual table
  EAGLAnim::FnDeltaF1 type_info node

Xbox methods treated as members (9 of 9; untyped ones count when ECX is read before it is written): Eval, EvalSQT, EvalSQTMask, EvalVel2D, EvalWeights, FnDeltaF1, InitBuffersAsRequired, scalar_deleting_destructor, ~FnDeltaF1

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [5: Eval@fe2b0, EvalVel2D@feac0, EvalWeights@feaa0, FnDeltaF1@fe280, ~FnDeltaF1@ff210]
  +0x008  w[4] W [1: FnDeltaF1@fe280]
  +0x00c  w[4] R [1: InitBuffersAsRequired@feae0]
  +0x010  w[4] W [2: EvalSQTMask@fe2d0, FnDeltaF1@fe280]
  +0x014  w[4] R/W [3: FnDeltaF1@fe280, InitBuffersAsRequired@feae0, ~FnDeltaF1@ff210]
  +0x018  w[4] R/W [4: EvalSQT@febd0, FnDeltaF1@fe280, InitBuffersAsRequired@feae0, ~FnDeltaF1@ff210]
  +0x01c  w[4] W [1: FnDeltaF1@fe280]
  +0x020  w[4] R/W [3: FnDeltaF1@fe280, InitBuffersAsRequired@feae0, ~FnDeltaF1@ff210]
  +0x024  w[4] W [2: FnDeltaF1@fe280, InitBuffersAsRequired@feae0]
  +0x028  w[4] R [1: EvalSQTMask@fe2d0]
  +0x02c  w[4] R/W [2: InitBuffersAsRequired@feae0, ~FnDeltaF1@ff210]

PS2 this-relative accesses (PS2 offsets):
  +0x008  w[4] R/W [4: Eval@271830, EvalVel2D@271890, EvalWeights@271860, ~FnDeltaF1@271710]
  +0x00c  w[4] R/W [5: EvalSQT@2701b8, EvalSQTMask@270a30, GetLength@2717f0, InitBuffersAsRequired@2713f0, SetAnimMemoryMap@2717d8]
  +0x010  w[4] R/W [3: EvalSQT@2701b8, EvalSQTMask@270a30, SetAnimMemoryMap@2717d8]
  +0x014  w[4] R/W [2: InitBuffersAsRequired@2713f0, ~FnDeltaF1@271710]
  +0x018  w[4] R/W [4: EvalSQT@2701b8, EvalSQTMask@270a30, InitBuffersAsRequired@2713f0, ~FnDeltaF1@271710]
  +0x01c  w[4] R/W [3: EvalSQT@2701b8, EvalSQTMask@270a30, SetAnimMemoryMap@2717d8]
  +0x020  w[4] R/W [2: InitBuffersAsRequired@2713f0, ~FnDeltaF1@271710]
  +0x024  w[4] R/W [3: EvalSQT@2701b8, EvalSQTMask@270a30, InitBuffersAsRequired@2713f0]
  +0x028  w[4] R/W [2: EvalSQT@2701b8, EvalSQTMask@270a30]
  +0x02c  w[4] R/W [4: EvalSQT@2701b8, EvalSQTMask@270a30, InitBuffersAsRequired@2713f0, ~FnDeltaF1@271710]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Eval: R +0x0 w4
