# EAGLAnim::FnDeltaF3

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x1008e0 frees/deletes with size 0x30 (call to None)
Xbox vtable 0x001a12e8 (18 slots) stored by its constructor
PS2 sheet virtual table row: ['EAGLAnim::FnDeltaF3 virtual table']
constructor 0xff280 first calls: ['EAGLAnim::FnAnimMemoryMap::FnAnimMemoryMap']

Xbox methods (11):
  0xff280 undefined FnDeltaF3(void)
  0xff2b0 undefined Eval(void)
  0xff2d0 undefined EvalSQTMask(void)
  0xffce0 undefined EvalWeights(void)
  0xffd00 undefined EvalVel2D(void)
  0xffd20 undefined InitBuffersAsRequired(void)
  0xfff10 undefined EvalSQT(void)
  0x100880 undefined SetAnimMemoryMap(void)
  0x1008a0 undefined GetLength(void)
  0x1008e0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x100910 undefined ~FnDeltaF3(void)

PS2 methods (16):
  0x26e4e0 EAGLAnim::FnDeltaF3::EvalSQT
  0x26eff0 EAGLAnim::FnDeltaF3::EvalSQTMask
  0x26fc80 EAGLAnim::FnDeltaF3::InitBuffersAsRequired
  0x26fea0 EAGLAnim::FnDeltaF3::operator_new
  0x26fec8 EAGLAnim::FnDeltaF3::operator_new
  0x26fef0 EAGLAnim::FnDeltaF3::operator_delete
  0x26ff18 EAGLAnim::FnDeltaF3::operator_new_array
  0x26ff40 EAGLAnim::FnDeltaF3::operator_new_array
  0x26ff68 EAGLAnim::FnDeltaF3::operator_delete_array
  0x26ffb0 EAGLAnim::FnDeltaF3::FnDeltaF3
  0x270008 EAGLAnim::FnDeltaF3::~FnDeltaF3
  0x2700d0 EAGLAnim::FnDeltaF3::SetAnimMemoryMap
  0x2700e8 EAGLAnim::FnDeltaF3::GetLength
  0x270128 EAGLAnim::FnDeltaF3::Eval
  0x270158 EAGLAnim::FnDeltaF3::EvalWeights
  0x270188 EAGLAnim::FnDeltaF3::EvalVel2D

Sheet rows:
  EAGLAnim::FnDeltaF3::EvalSQT(float, float *, EAGLAnim::BoneMask
  EAGLAnim::FnDeltaF3::EvalSQTMask(float, float *, EAGLAnim::Bone
  EAGLAnim::FnDeltaF3::InitBuffersAsRequired(void)
  EAGLAnim::FnDeltaF3 type_info function
  EAGLAnim::FnDeltaF3::operator new(unsigned int)
  EAGLAnim::FnDeltaF3::operator new(unsigned int, char *)
  EAGLAnim::FnDeltaF3::operator delete(void *, unsigned int)
  EAGLAnim::FnDeltaF3::operator new [](unsigned int)
  EAGLAnim::FnDeltaF3::operator new [](unsigned int, char *)
  EAGLAnim::FnDeltaF3::operator delete [](void *, unsigned int)
  EAGLAnim::FnDeltaF3::operator new(unsigned int, void *)
  EAGLAnim::FnDeltaF3::operator delete(void *, void *)
  EAGLAnim::FnDeltaF3::operator new [](unsigned int, void *)
  EAGLAnim::FnDeltaF3::operator delete [](void *, void *)
  EAGLAnim::FnDeltaF3::FnDeltaF3(void)
  EAGLAnim::FnDeltaF3::~FnDeltaF3(void)
  EAGLAnim::FnDeltaF3::SetAnimMemoryMap(EAGLAnim::AnimMemoryMap *
  EAGLAnim::FnDeltaF3::GetLength(float &) const
  EAGLAnim::FnDeltaF3::Eval(float, float, float *)
  EAGLAnim::FnDeltaF3::EvalWeights(float, float *)
  EAGLAnim::FnDeltaF3::EvalVel2D(float, float *)
  EAGLAnim::FnDeltaF3 virtual table
  EAGLAnim::FnDeltaF3 type_info node

Xbox methods treated as members (11 of 11; untyped ones count when ECX is read before it is written): Eval, EvalSQT, EvalSQTMask, EvalVel2D, EvalWeights, FnDeltaF3, GetLength, InitBuffersAsRequired, SetAnimMemoryMap, scalar_deleting_destructor, ~FnDeltaF3

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [5: Eval@ff2b0, EvalVel2D@ffd00, EvalWeights@ffce0, FnDeltaF3@ff280, ~FnDeltaF3@100910]
  +0x008  w[4] W [1: FnDeltaF3@ff280]
  +0x00c  w[4] R/W [3: GetLength@1008a0, InitBuffersAsRequired@ffd20, SetAnimMemoryMap@100880]
  +0x010  w[4] W [3: EvalSQTMask@ff2d0, FnDeltaF3@ff280, SetAnimMemoryMap@100880]
  +0x014  w[4] R/W [3: FnDeltaF3@ff280, InitBuffersAsRequired@ffd20, ~FnDeltaF3@100910]
  +0x018  w[4] R/W [4: EvalSQT@fff10, FnDeltaF3@ff280, InitBuffersAsRequired@ffd20, ~FnDeltaF3@100910]
  +0x01c  w[4] W [2: FnDeltaF3@ff280, SetAnimMemoryMap@100880]
  +0x020  w[4] R/W [3: FnDeltaF3@ff280, InitBuffersAsRequired@ffd20, ~FnDeltaF3@100910]
  +0x024  w[4] W [2: FnDeltaF3@ff280, InitBuffersAsRequired@ffd20]
  +0x028  w[4] R [1: EvalSQTMask@ff2d0]
  +0x02c  w[4] R/W [2: InitBuffersAsRequired@ffd20, ~FnDeltaF3@100910]

PS2 this-relative accesses (PS2 offsets):
  +0x008  w[4] R/W [4: Eval@270128, EvalVel2D@270188, EvalWeights@270158, ~FnDeltaF3@270008]
  +0x00c  w[4] R/W [5: EvalSQT@26e4e0, EvalSQTMask@26eff0, GetLength@2700e8, InitBuffersAsRequired@26fc80, SetAnimMemoryMap@2700d0]
  +0x010  w[4] R/W [3: EvalSQT@26e4e0, EvalSQTMask@26eff0, SetAnimMemoryMap@2700d0]
  +0x014  w[4] R/W [2: InitBuffersAsRequired@26fc80, ~FnDeltaF3@270008]
  +0x018  w[4] R/W [4: EvalSQT@26e4e0, EvalSQTMask@26eff0, InitBuffersAsRequired@26fc80, ~FnDeltaF3@270008]
  +0x01c  w[4] R/W [3: EvalSQT@26e4e0, EvalSQTMask@26eff0, SetAnimMemoryMap@2700d0]
  +0x020  w[4] R/W [2: InitBuffersAsRequired@26fc80, ~FnDeltaF3@270008]
  +0x024  w[4] R/W [3: EvalSQT@26e4e0, EvalSQTMask@26eff0, InitBuffersAsRequired@26fc80]
  +0x028  w[4] R/W [2: EvalSQT@26e4e0, EvalSQTMask@26eff0]
  +0x02c  w[4] R/W [4: EvalSQT@26e4e0, EvalSQTMask@26eff0, InitBuffersAsRequired@26fc80, ~FnDeltaF3@270008]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Eval: R +0x0 w4
