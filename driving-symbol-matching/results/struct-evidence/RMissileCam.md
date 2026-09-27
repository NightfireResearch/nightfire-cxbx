# RMissileCam

FastAlloc/constructed sizes under its tag: {'allocated': [28], 'constructed': []}
deleting destructor 0xa0e70 frees/deletes with size 0x1c (call to UMemory::FastFree)
Xbox vtable 0x00192d38 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RMissileCam virtual table']
constructor 0xa0a50 first calls: ['RTextureContextManager::GetContext', 'RTextureContext::FindOrCreateTexture', None]

Xbox methods (6):
  0x8bdd0 undefined __stdcall Init(void)
  0xa0a50 undefined RMissileCam(void)
  0xa0b30 undefined __stdcall Kill(void)
  0xa0b50 undefined __thiscall Draw(RMissileCam * this)
  0xa0df0 undefined ~RMissileCam(void)
  0xa0e70 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (12):
  0x1e3518 RMissileCam::RMissileCam
  0x1e3688 RMissileCam::~RMissileCam
  0x1e3738 RMissileCam::Draw
  0x1e3d28 RMissileCam::Get
  0x1e3d38 RMissileCam::Init
  0x1e3d70 RMissileCam::Kill
  0x1e3da8 RMissileCam::operator_new
  0x1e3dc8 RMissileCam::operator_delete
  0x1e3de8 RMissileCam::Reset
  0x1e3df0 RMissileCam::Show
  0x1e3e00 RMissileCam::Hide
  0x1e3e08 RMissileCam::fgThis_RMissileCam_global_ctors

Sheet rows:
  RMissileCam::RMissileCam(void)
  RMissileCam::~RMissileCam(void)
  RMissileCam::Draw(void)
  RMissileCam type_info function
  RMissileCam::Get(void)
  RMissileCam::Init(void)
  RMissileCam::Kill(void)
  RMissileCam::operator new(unsigned int)
  RMissileCam::operator delete(void *, unsigned int)
  RMissileCam::Reset(void)
  RMissileCam::Show(unsigned int)
  RMissileCam::Hide(void)
  RMissileCam::fgThis_RMissileCam
  RMissileCam::fFrameName
  RMissileCam virtual table
  RMissileCam type_info node

Xbox methods treated as members (5 of 6; untyped ones count when ECX is read before it is written): Draw, Init, RMissileCam, scalar_deleting_destructor, ~RMissileCam

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RMissileCam@a0a50, ~RMissileCam@a0df0]
  +0x004  w[4] LEA/W addr-taken [2: RMissileCam@a0a50, ~RMissileCam@a0df0]
  +0x008  w[4] W [1: ~RMissileCam@a0df0]
  +0x00c  w[4] W [1: ~RMissileCam@a0df0]
  +0x010  w[1] R/W [2: Draw@a0b50, RMissileCam@a0a50]
  +0x014  w[4] R [1: Draw@a0b50]
  +0x018  w[4] R/W [3: Draw@a0b50, RMissileCam@a0a50, ~RMissileCam@a0df0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RMissileCam@1e3518, ~RMissileCam@1e3688]
  +0x004  w- LEA addr-taken [1: RMissileCam@1e3518]
  +0x00c  w- LEA addr-taken [1: ~RMissileCam@1e3688]
  +0x010  w[4] R/W [4: Draw@1e3738, Hide@1e3e00, RMissileCam@1e3518, Show@1e3df0]
  +0x014  w[4] R/W [2: Draw@1e3738, Show@1e3df0]
  +0x018  w[4] R/W [3: Draw@1e3738, RMissileCam@1e3518, ~RMissileCam@1e3688]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
