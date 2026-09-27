# RTextureContext

FastAlloc/constructed sizes under its tag: {'allocated': [16], 'constructed': []}
deleting destructor 0x93e80 frees/deletes with size 0xc (call to UMemory::FastFree)
deleting destructor 0x93e80 frees/deletes with size 0x10 (call to UMemory::FastFree)
Xbox vtable 0x0019219c stored by constructor 0x93dd0 (not in vtables.json; slot count unknown)
PS2 sheet virtual table row: ['RTextureContext virtual table']
constructor 0x93dd0 first calls: ['UMemory::FastAlloc', 'FUN_00093310', 'UFileLoader::FileLoadz']

Xbox methods (7):
  0x93390 undefined Unload(void)
  0x93b20 undefined FindWithoutOwning(undefined4 param_1, undefined4 param_2)
  0x93ba0 void * __thiscall FindOrCreateTexture(RTextureContext * this, uint name, uint flags)
  0x93c70 undefined Find(undefined4 param_1, undefined4 param_2)
  0x93dd0 undefined RTextureContext(undefined4 param_1, undefined4 param_2)
  0x93e80 undefined scalar_deleting_destructor(undefined1 param_1)
  0x95080 undefined4 * __thiscall RTextureContext_or_ContextManager(RTextureContext * this)

PS2 methods (11):
  0x1cef60 RTextureContext::RTextureContext
  0x1cf010 RTextureContext::~RTextureContext
  0x1cf0c0 RTextureContext::Load
  0x1cf0f8 RTextureContext::Unload
  0x1cf230 RTextureContext::Find
  0x1cf2a8 RTextureContext::FindWithoutOwning
  0x1cf340 RTextureContext::FindOrCreateTexture
  0x1cf420 RTextureContext::FindShape
  0x1cf480 RTextureContext::NameLookup
  0x1cfcd0 RTextureContext::operator_new
  0x1cfcf0 RTextureContext::operator_delete

Sheet rows:
  RTextureContext::RTextureContext(char *, unsigned int)
  RTextureContext::~RTextureContext(void)
  RTextureContext::Load(char *)
  RTextureContext::Unload(void)
  RTextureContext::Find(unsigned int, unsigned int) const
  RTextureContext::FindWithoutOwning(unsigned int, unsigned int)
  RTextureContext::FindOrCreateTexture(unsigned int, unsigned int
  RTextureContext::FindShape(unsigned int)
  RTextureContext::NameLookup(char *, unsigned int &) const
  RTextureContext type_info function
  RTextureContext::operator new(unsigned int)
  RTextureContext::operator delete(void *, unsigned int)
  RTextureContext::GetShapeFile(void) const
  RTextureContext virtual table
  RTextureContext type_info node

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): Find, FindOrCreateTexture, FindWithoutOwning, RTextureContext, RTextureContext_or_ContextManager, Unload, scalar_deleting_destructor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [3: RTextureContext@93dd0, RTextureContext_or_ContextManager@95080, scalar_deleting_destructor@93e80]
  +0x004  w[4] W [2: RTextureContext@93dd0, RTextureContext_or_ContextManager@95080]
  +0x008  w[4] R/W [4: FindOrCreateTexture@93ba0, FindWithoutOwning@93b20, RTextureContext@93dd0, Unload@93390]
  +0x00c  w[4] R/W -> FUN_000932b0, FUN_00093350, FUN_00093d90 [6: Find@93c70, FindOrCreateTexture@93ba0, FindWithoutOwning@93b20, RTextureContext@93dd0, Unload@93390, scalar_deleting_destructor@93e80]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RTextureContext@1cef60, ~RTextureContext@1cf010]
  +0x004  w[4] W [1: RTextureContext@1cef60]
  +0x008  w[4] R/W -> UMemory::Free [5: FindOrCreateTexture@1cf340, FindWithoutOwning@1cf2a8, Load@1cf0c0, RTextureContext@1cef60, Unload@1cf0f8]
  +0x00c  w[4] R/W -> FUN_001cfbd0 [6: FindOrCreateTexture@1cf340, FindShape@1cf420, FindWithoutOwning@1cf2a8, RTextureContext@1cef60, Unload@1cf0f8, ~RTextureContext@1cf010]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
