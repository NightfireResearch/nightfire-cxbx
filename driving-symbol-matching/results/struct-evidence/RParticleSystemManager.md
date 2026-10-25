# RParticleSystemManager

FastAlloc/constructed sizes under its tag: {'allocated': [65584], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xa2f00 first calls: ['RParticleLibrary::RParticleLibrary', 'RParticleParticleCache::RParticleParticleCache', 'UMemory::FastAlloc']

Xbox methods (8):
  0xa1c60 undefined CreateParticles(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xa1e30 void __thiscall UpdateAndRenderAllSystems(RParticleSystemManager * this)
  0xa2930 undefined __thiscall UpdateSpawnAllSystems(RParticleSystemManager * this)
  0xa29c0 undefined Reset(void)
  0xa2bf0 undefined AddOrRefresh(undefined1 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xa2f00 undefined4 * __thiscall RParticleSystemManager(RParticleSystemManager * this)
  0xa3140 void __cdecl Init(char * param_1)
  0xa31e0 undefined __stdcall Shutdown(void)

PS2 methods (11):
  0x1e61f8 RParticleSystemManager::AddOrRefresh
  0x1e62e0 RParticleSystemManager::GetSystem
  0x1e6330 RParticleSystemManager::CreateParticles
  0x1e67e0 RParticleSystemManager::UpdateSpawnAllSystems
  0x1e6e48 RParticleSystemManager::UpdateAndRenderAllSystems
  0x1e6e68 RParticleSystemManager::RenderAllSystems
  0x1e6e88 RParticleSystemManager::Init
  0x1e6f40 RParticleSystemManager::Shutdown
  0x1e6f78 RParticleSystemManager::RParticleSystemManager
  0x1e7028 RParticleSystemManager::~RParticleSystemManager
  0x1e76c8 RParticleSystemManager::Reset

Sheet rows:
  RParticleSystemManager::AddOrRefresh(unsigned int, void *, RAbs
  RParticleSystemManager::GetSystem(void *)
  RParticleSystemManager::CreateParticles(MungedGenericParticleDa
  RParticleSystemManager::UpdateSpawnAllSystems(void)
  RParticleSystemManager::UpdateAndRenderAllSystems(void)
  RParticleSystemManager::RenderAllSystems(void)
  RParticleSystemManager::Init(char *)
  RParticleSystemManager::Shutdown(void)
  RParticleSystemManager::RParticleSystemManager(void)
  RParticleSystemManager::~RParticleSystemManager(void)
  RParticleSystemManager::Reset(void)
  RParticleSystemManager::fManager

Xbox methods treated as members (5 of 8; untyped ones count when ECX is read before it is written): AddOrRefresh, RParticleSystemManager, Reset, UpdateAndRenderAllSystems, UpdateSpawnAllSystems

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: RParticleSystemManager@a2f00, UpdateSpawnAllSystems@a2930]
  +0x004  w[4] R/W -> FUN_000a2660, FUN_000a2a70 [4: AddOrRefresh@a2bf0, RParticleSystemManager@a2f00, Reset@a29c0, UpdateSpawnAllSystems@a2930]
  +0x008  w- LEA addr-taken [1: RParticleSystemManager@a2f00]
  +0x010  w- LEA addr-taken -> RParticleParticleCache::RParticleParticleCache [1: RParticleSystemManager@a2f00]
  +0x014  w[4] W [1: Reset@a29c0]
  +0x018  w[4] W [1: Reset@a29c0]
  +0x10020  w[4] W [1: Reset@a29c0]
  +0x10024  w[4] W [1: Reset@a29c0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [2: RParticleSystemManager@1e6f78, UpdateSpawnAllSystems@1e67e0]
  +0x004  w[4] R/W [6: AddOrRefresh@1e61f8, GetSystem@1e62e0, RParticleSystemManager@1e6f78, Reset@1e76c8, UpdateSpawnAllSystems@1e67e0, ~RParticleSystemManager@1e7028]
  +0x008  w- LEA addr-taken [2: RParticleSystemManager@1e6f78, ~RParticleSystemManager@1e7028]
  +0x040  w- LEA addr-taken [3: RParticleSystemManager@1e6f78, Reset@1e76c8, ~RParticleSystemManager@1e7028]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
