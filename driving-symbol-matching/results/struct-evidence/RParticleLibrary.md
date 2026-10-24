# RParticleLibrary

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xa2e70 first calls: ['UMemory::FastAlloc', 'FUN_000a2e20']

Xbox methods (7):
  0xa1e40 undefined FindSystemViaIndex(undefined4 param_1)
  0xa1e60 undefined MungeData(undefined4 param_1, undefined4 param_2)
  0xa2100 undefined FindSystemViaTag(undefined4 param_1)
  0xa2d10 undefined AddSystem(undefined4 param_1, undefined4 param_2)
  0xa2d50 undefined Reload(void)
  0xa2e70 undefined4 * __fastcall RParticleLibrary(undefined4 * param_1)
  0xab460 undefined MungeDataForPlatform(undefined4 param_1, undefined4 param_2)

PS2 methods (10):
  0x1e7e10 RParticleLibrary::RParticleLibrary
  0x1e7eb0 RParticleLibrary::~RParticleLibrary
  0x1e7f60 RParticleLibrary::Reload
  0x1e8008 RParticleLibrary::FindSystemViaTag
  0x1e8028 RParticleLibrary::FindSystemViaIndex
  0x1e8040 RParticleLibrary::AddSystem
  0x1e8090 RParticleLibrary::MungeData
  0x1e80b0 RParticleLibrary::MungeData
  0x1f3180 RParticleLibrary::ByteSwap
  0x1f3188 RParticleLibrary::MungeDataForPlatform

Sheet rows:
  RParticleLibrary::RPartLibraryData::AddSystem(GenericParticleDa
  RParticleLibrary::RPartLibraryData::FindSystemViaTag(unsigned i
  RParticleLibrary::RParticleLibrary(void)
  RParticleLibrary::~RParticleLibrary(void)
  RParticleLibrary::Reload(void)
  RParticleLibrary::FindSystemViaTag(unsigned int)
  RParticleLibrary::FindSystemViaIndex(unsigned int)
  RParticleLibrary::AddSystem(GenericParticleData &, unsigned int
  RParticleLibrary::MungeData(GenericParticleData &)
  RParticleLibrary::MungeData(GenericParticleData &, GenericParti
  RParticleLibrary::ByteSwap(GenericParticleData &)
  RParticleLibrary::MungeDataForPlatform(GenericParticleData &, G

Xbox methods treated as members (5 of 7; untyped ones count when ECX is read before it is written): AddSystem, FindSystemViaIndex, FindSystemViaTag, RParticleLibrary, Reload

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> RParticleLibrary::RPartLibraryData::AddSystem [5: AddSystem@a2d10, FindSystemViaIndex@a1e40, FindSystemViaTag@a2100, RParticleLibrary@a2e70, Reload@a2d50]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W [6: AddSystem@1e8040, FindSystemViaIndex@1e8028, MungeDataForPlatform@1f3188, RParticleLibrary@1e7e10, Reload@1e7f60, ~RParticleLibrary@1e7eb0]
  +0x008  w[8] R [1: MungeDataForPlatform@1f3188]
  +0x010  w[8] R [1: MungeDataForPlatform@1f3188]
  +0x018  w[8] R [1: MungeDataForPlatform@1f3188]
  +0x0a0  w- LEA addr-taken [1: MungeDataForPlatform@1f3188]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  FindSystemViaIndex: R +0x0 w4
