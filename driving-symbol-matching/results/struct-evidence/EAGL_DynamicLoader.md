# EAGL::DynamicLoader

FastAlloc/constructed sizes under its tag: {'allocated': [28], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xe62f0 first calls: ['EAGL::DynamicLoader::Initialize', 'EAGL::DynamicLoader::Resolve']
constructor 0xe6330 first calls: ['EAGL::DynamicLoader::Initialize', 'EAGL::DynamicLoader::Resolve']

Xbox methods (17):
  0xe5220 undefined RunDestructors(void)
  0xe52a0 undefined Initialize(undefined4 param_1)
  0xe57e0 undefined __thiscall GetAddr(undefined4 param_1_00, char * param_2, undefined4 param_3, undefined4 param_4)
  0xe5a10 undefined GetSymbol(undefined4 param_1, undefined4 param_2)
  0xe5bc0 undefined GetNextSymbol(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xe5c80 undefined GetNextAddr(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xe5cc0 void __cdecl RegisterVar(char * param_1, void * param_2)
  0xe5ce0 undefined __cdecl UnRegisterVar(char * param_1)
  0xe5cf0 undefined GetRegisteredVar(undefined4 param_1, undefined4 param_2)
  0xe5d10 undefined Release(void)
  0xe5d70 undefined RunConstructors(void)
  0xe5e80 undefined Resolve(void)
  0xe62f0 undefined DynamicLoader(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xe6330 undefined DynamicLoader(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4, undefi
  0xe6380 void __thiscall ~DynamicLoader(DynamicLoader * this)
  0xede60 undefined __cdecl RegisterShapes(int param_1)
  0xedf50 undefined __cdecl UnRegisterShapes(int param_1)

PS2 methods (24):
  0x29e3b0 EAGL::DynamicLoader::RunConstructors
  0x29e518 EAGL::DynamicLoader::Resolve
  0x29ea00 EAGL::DynamicLoader::Initialize
  0x29f118 EAGL::DynamicLoader::GetAddr
  0x29f330 EAGL::DynamicLoader::GetIndex
  0x29f478 EAGL::DynamicLoader::GetSymbol
  0x29f728 EAGL::DynamicLoader::_global_ctor_dtor
  0x29f7c8 EAGL::DynamicLoader::DynamicLoader
  0x29f820 EAGL::DynamicLoader::DynamicLoader
  0x29f890 EAGL::DynamicLoader::~DynamicLoader
  0x29f8e8 EAGL::DynamicLoader::Release
  0x29f988 EAGL::DynamicLoader::RunDestructors
  0x29fa68 EAGL::DynamicLoader::GetAddr
  0x29fb10 EAGL::DynamicLoader::GetCount
  0x29fb28 EAGL::DynamicLoader::GetElfData
  0x29fb30 EAGL::DynamicLoader::GetNextSymbol
  0x29fbf8 EAGL::DynamicLoader::GetNextAddr
  0x29fce8 EAGL::DynamicLoader::RegisterVar
  0x29fd10 EAGL::DynamicLoader::UnRegisterVar
  0x29fd38 EAGL::DynamicLoader::GetRegisteredVar
  0x29fd60 EAGL::DynamicLoader::ModelType_global_ctors
  0x29fd80 EAGL::DynamicLoader::ModelType_global_dtors
  0x29fda0 EAGL::DynamicLoader::RegisterShapes
  0x29ff00 EAGL::DynamicLoader::UnRegisterShapes

Sheet rows:
  EAGL::DynamicLoader::DoVersionCheck(void)
  EAGL::DynamicLoader::RunConstructors(void)
  EAGL::DynamicLoader::Resolve(void)
  EAGL::DynamicLoader::Initialize(void *(*)(char *, bool &))
  EAGL::DynamicLoader::GetAddr(char *, char *, void *&) const
  EAGL::DynamicLoader::GetIndex(char *) const
  EAGL::DynamicLoader::GetSymbol(int) const
  EAGL::DynamicLoader::DynamicLoader(void *, unsigned int, void *
  EAGL::DynamicLoader::DynamicLoader(void *, unsigned int, void *
  EAGL::DynamicLoader::~DynamicLoader(void)
  EAGL::DynamicLoader::Release(void)
  EAGL::DynamicLoader::RunDestructors(void)
  EAGL::DynamicLoader::GetAddr(char *) const
  EAGL::DynamicLoader::GetCount(void) const
  EAGL::DynamicLoader::GetElfData(void) const
  EAGL::DynamicLoader::GetNextSymbol(char *, int &, EAGL::Dynamic
  EAGL::DynamicLoader::GetNextAddr(char *, int &, void *&) const
  EAGL::DynamicLoader::RegisterVar(char *, void *)
  EAGL::DynamicLoader::UnRegisterVar(char *)
  EAGL::DynamicLoader::GetRegisteredVar(char *, bool &)
  EAGL::DynamicLoader::RegisterShapes(char *)
  EAGL::DynamicLoader::UnRegisterShapes(char *)
  EAGL::DynamicLoader::VersionCheck
  EAGL::DynamicLoader::gSymbolPool
  EAGL::DynamicLoader::gConsPool
  EAGL::DynamicLoader::gRuntimeAllocConsPool
  EAGL::DynamicLoader::ModelType
  EAGL::DynamicLoader::BBoxType
  EAGL::DynamicLoader::TARType
  EAGL::DynamicLoader::ShapeType
  EAGL::DynamicLoader::AnimBankType
  EAGL::DynamicLoader::BoneType
  EAGL::DynamicLoader::SkeletonType
  EAGL::DynamicLoader::MorphType
  EAGL::DynamicLoader::VersionType
  EAGL::DynamicLoader::AnimClipSetType
  EAGL::DynamicLoader::AnimIdType

Xbox methods treated as members (11 of 17; untyped ones count when ECX is read before it is written): DynamicLoader, GetAddr, GetNextSymbol, GetSymbol, Initialize, Release, Resolve, RunConstructors, RunDestructors, ~DynamicLoader

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [6: GetAddr@e57e0, GetNextSymbol@e5bc0, GetSymbol@e5a10, Release@e5d10, Resolve@e5e80, RunConstructors@e5d70]
  +0x004  w[4] R/W [4: DynamicLoader@e62f0, DynamicLoader@e6330, RunConstructors@e5d70, RunDestructors@e5220]
  +0x008  w[4] R/W [4: DynamicLoader@e62f0, DynamicLoader@e6330, RunConstructors@e5d70, RunDestructors@e5220]
  +0x00c  w[4] R/W [3: DynamicLoader@e62f0, DynamicLoader@e6330, RunDestructors@e5220]
  +0x010  w[4] R/W [3: DynamicLoader@e62f0, DynamicLoader@e6330, Initialize@e52a0]
  +0x014  w[4] R/W [3: DynamicLoader@e62f0, DynamicLoader@e6330, Initialize@e52a0]
  +0x018  w[4] R/W [3: DynamicLoader@e62f0, DynamicLoader@e6330, Initialize@e52a0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W -> FUN_0029f5b0 [9: GetAddr@29f118, GetAddr@29fa68, GetCount@29fb10, GetIndex@29f330, GetNextAddr@29fbf8, GetNextSymbol@29fb30…]
  +0x004  w[4] R/W [4: DynamicLoader@29f7c8, DynamicLoader@29f820, RunConstructors@29e3b0, RunDestructors@29f988]
  +0x008  w[4, 8] R/W [7: DynamicLoader@29f7c8, DynamicLoader@29f820, GetSymbol@29f478, RegisterShapes@29fda0, RunConstructors@29e3b0, RunDestructors@29f988…]
  +0x00c  w[4] R/W [4: DynamicLoader@29f7c8, DynamicLoader@29f820, Resolve@29e518, RunDestructors@29f988]
  +0x010  w[4] R/W [3: DynamicLoader@29f7c8, DynamicLoader@29f820, GetElfData@29fb28]
  +0x014  w[4] W [2: DynamicLoader@29f7c8, DynamicLoader@29f820]
  +0x018  w[4] W [2: DynamicLoader@29f7c8, DynamicLoader@29f820]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
