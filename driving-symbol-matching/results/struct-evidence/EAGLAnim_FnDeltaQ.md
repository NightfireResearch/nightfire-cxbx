# EAGLAnim::FnDeltaQ

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x104200 frees/deletes with size 0x30 (call to None)
Xbox vtable 0x001a1450 (19 slots) stored by its constructor
PS2 sheet virtual table row: ['EAGLAnim::FnDeltaQ virtual table']
constructor 0x1033b0 first calls: ['EAGLAnim::FnAnimMemoryMap::FnAnimMemoryMap']

Xbox methods (8):
  0x1033b0 undefined FnDeltaQ(void)
  0x1033f0 undefined ~FnDeltaQ(void)
  0x103420 undefined SetAnimMemoryMap(void)
  0x103430 undefined GetLength(void)
  0x103480 undefined Eval(void)
  0x1034a0 undefined EvalSQT(void)
  0x1034c0 undefined EvalSQTMasked(void)
  0x104200 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (13):
  0x267938 EAGLAnim::FnDeltaQ::EvalSQTMasked
  0x268ab8 EAGLAnim::FnDeltaQ::operator_new
  0x268ae0 EAGLAnim::FnDeltaQ::operator_new
  0x268b08 EAGLAnim::FnDeltaQ::operator_delete
  0x268b30 EAGLAnim::FnDeltaQ::operator_new_array
  0x268b58 EAGLAnim::FnDeltaQ::operator_new_array
  0x268b80 EAGLAnim::FnDeltaQ::operator_delete_array
  0x268bc8 EAGLAnim::FnDeltaQ::FnDeltaQ
  0x268cb0 EAGLAnim::FnDeltaQ::InitBuffersAsRequired
  0x268d88 EAGLAnim::FnDeltaQ::SetAnimMemoryMap
  0x268d90 EAGLAnim::FnDeltaQ::GetLength
  0x268dd0 EAGLAnim::FnDeltaQ::Eval
  0x268e08 EAGLAnim::FnDeltaQ::EvalSQT

Sheet rows:
  EAGLAnim::FnDeltaQ::~FnDeltaQ(void)
  EAGLAnim::FnDeltaQ::EvalSQTMasked(float, EAGLAnim::BoneMask *,
  EAGLAnim::FnDeltaQ type_info function
  EAGLAnim::FnDeltaQ::operator new(unsigned int)
  EAGLAnim::FnDeltaQ::operator new(unsigned int, char *)
  EAGLAnim::FnDeltaQ::operator delete(void *, unsigned int)
  EAGLAnim::FnDeltaQ::operator new [](unsigned int)
  EAGLAnim::FnDeltaQ::operator new [](unsigned int, char *)
  EAGLAnim::FnDeltaQ::operator delete [](void *, unsigned int)
  EAGLAnim::FnDeltaQ::operator new(unsigned int, void *)
  EAGLAnim::FnDeltaQ::operator delete(void *, void *)
  EAGLAnim::FnDeltaQ::operator new [](unsigned int, void *)
  EAGLAnim::FnDeltaQ::operator delete [](void *, void *)
  EAGLAnim::FnDeltaQ::FnDeltaQ(void)
  EAGLAnim::FnDeltaQ::InitBuffersAsRequired(void)
  EAGLAnim::FnDeltaQ::SetAnimMemoryMap(EAGLAnim::AnimMemoryMap *)
  EAGLAnim::FnDeltaQ::GetLength(float &) const
  EAGLAnim::FnDeltaQ::Eval(float, float, float *)
  EAGLAnim::FnDeltaQ::EvalSQT(float, float *, EAGLAnim::BoneMask
  EAGLAnim::FnDeltaQ virtual table
  EAGLAnim::FnDeltaQ type_info node

Xbox methods treated as members (8 of 8; untyped ones count when ECX is read before it is written): Eval, EvalSQT, EvalSQTMasked, FnDeltaQ, GetLength, SetAnimMemoryMap, scalar_deleting_destructor, ~FnDeltaQ

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [4: Eval@103480, EvalSQT@1034a0, FnDeltaQ@1033b0, ~FnDeltaQ@1033f0]
  +0x008  w[4] W [1: FnDeltaQ@1033b0]
  +0x00c  w[4] R/W [2: GetLength@103430, SetAnimMemoryMap@103420]
  +0x010  w[4] W [1: FnDeltaQ@1033b0]
  +0x014  w[4] W [1: FnDeltaQ@1033b0]
  +0x018  w[4] W [1: FnDeltaQ@1033b0]
  +0x01c  w[4] W [1: FnDeltaQ@1033b0]
  +0x020  w[4] R/W [2: FnDeltaQ@1033b0, ~FnDeltaQ@1033f0]
  +0x024  w[4] W [1: FnDeltaQ@1033b0]
  +0x028  w[4] W [1: FnDeltaQ@1033b0]
  +0x02c  w[4] W [1: FnDeltaQ@1033b0]

PS2 this-relative accesses (PS2 offsets):
  +0x008  w[4] R [2: Eval@268dd0, EvalSQT@268e08]
  +0x00c  w[4] R/W [4: EvalSQTMasked@267938, GetLength@268d90, InitBuffersAsRequired@268cb0, SetAnimMemoryMap@268d88]
  +0x010  w[4] R/W -> EAGLAnim::DeltaQMinRange::UnQuantize [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x014  w[4] R/W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x018  w[4] R/W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x01c  w[4] R/W [1: EvalSQTMasked@267938]
  +0x020  w[4] W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x024  w[4] R/W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x028  w[4] R/W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]
  +0x02c  w[4] R/W [2: EvalSQTMasked@267938, InitBuffersAsRequired@268cb0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetAnimMemoryMap: W +0xc w4
  Eval: R +0x0 w4
