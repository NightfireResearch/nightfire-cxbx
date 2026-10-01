# WWorld

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [60]}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xd1230 first calls: ['AttributeSet::AttributeSet', 'UMemory::FastAlloc', 'WSoundGroup::WSoundGroup']

Xbox methods (11):
  0x595c0 undefined InitSingleton(void)
  0xd1210 undefined __cdecl SetTrackName(char * missionName, bool param_2)
  0xd1230 int __thiscall WWorld(WWorld * this)
  0xd12d0 void __thiscall LoadTrackFile(WWorld * this, WWorld.conflict * param_1_00, char * path, char * missionName)
  0xd1470 int __thiscall GetEnviroDesc(WWorld * this, int param_1_00, int param_2)
  0xd14a0 undefined4 __thiscall GetSceneObjFromInstance(WWorld * this, int param_1_00, uint param_2)
  0xd14e0 undefined GetProcAnimStateFromInstance(undefined4 param_1)
  0xd1610 void __thiscall ~WWorld(WWorld * this)
  0xd1750 void __thiscall Reset(WWorld * this)
  0xd1b30 undefined Close(void)
  0xd21a0 bool __thiscall Open(WWorld * this)

PS2 methods (13):
  0x22f7e8 WWorld::SetTrackName
  0x22f820 WWorld::WWorld
  0x22f898 WWorld::LoadTrackFile
  0x22fa30 WWorld::GetTrackFile
  0x22fa40 WWorld::~WWorld
  0x22faf8 WWorld::Open
  0x230648 WWorld::Close
  0x230730 WWorld::Reset
  0x230ab0 WWorld::GetPathByName
  0x230bb8 WWorld::GetEnviroDesc
  0x230bf0 WWorld::GetSceneObjFromInstance
  0x230c48 WWorld::GetProcAnimStateFromInstance
  0x2313f8 WWorld::SetTrackName_global_ctors

Sheet rows:
  WWorld::SetTrackName(char *, bool)
  WWorld::WWorld(void)
  WWorld::LoadTrackFile(char *, char *)
  WWorld::GetTrackFile(void)
  WWorld::~WWorld(void)
  WWorld::Open(void)
  WWorld::Close(void)
  WWorld::Reset(void)
  WWorld::GetPathByName(char *) const
  WWorld::GetEnviroDesc(WWorldPos &) const
  WWorld::GetSceneObjFromInstance(CARP::Instance *) const
  WWorld::GetProcAnimStateFromInstance(CARP::Instance *) const
  WWorld::fgWorld
  WWorld::fgSetName

Xbox methods treated as members (11 of 11; untyped ones count when ECX is read before it is written): Close, GetEnviroDesc, GetProcAnimStateFromInstance, GetSceneObjFromInstance, InitSingleton, LoadTrackFile, Open, Reset, SetTrackName, WWorld, ~WWorld

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [3: LoadTrackFile@d12d0, Open@d21a0, ~WWorld@d1610]
  +0x008  w[4] R/W [3: LoadTrackFile@d12d0, WWorld@d1230, ~WWorld@d1610]
  +0x00c  w[4] R/W [3: LoadTrackFile@d12d0, WWorld@d1230, ~WWorld@d1610]
  +0x010  w[4] R/W [3: LoadTrackFile@d12d0, WWorld@d1230, ~WWorld@d1610]
  +0x014  w[4] R/W -> UGroup::GetArray, UGroup::GroupLocateTag, WGrid::Init [5: LoadTrackFile@d12d0, Open@d21a0, Reset@d1750, WWorld@d1230, ~WWorld@d1610]
  +0x018  w[4] R/W [4: LoadTrackFile@d12d0, Open@d21a0, WWorld@d1230, ~WWorld@d1610]
  +0x01c  w[4] W [1: Open@d21a0]
  +0x020  w[4] R/W [6: GetEnviroDesc@d1470, GetProcAnimStateFromInstance@d14e0, GetSceneObjFromInstance@d14a0, Open@d21a0, Reset@d1750, WWorld@d1230]
  +0x024  w[4] R/W [4: GetProcAnimStateFromInstance@d14e0, GetSceneObjFromInstance@d14a0, Open@d21a0, WWorld@d1230]
  +0x028  w[4] R/W -> RPathEngine::AddInstanceList [5: GetProcAnimStateFromInstance@d14e0, GetSceneObjFromInstance@d14a0, Open@d21a0, Reset@d1750, WWorld@d1230]
  +0x02c  w[4] R/W [2: GetEnviroDesc@d1470, Open@d21a0]
  +0x030  w[4] R/W [3: GetEnviroDesc@d1470, Open@d21a0, WWorld@d1230]
  +0x034  w[4] R/W -> FUN_000d1ae0, FUN_000d2110 [4: Close@d1b30, Open@d21a0, Reset@d1750, ~WWorld@d1610]
  +0x038  w[4] R/W -> FUN_000cd540, WSoundGroup::Add, WSoundGroup::End, WSoundGroup::Start, WSoundGroup::~WSoundGroup [3: Close@d1b30, Open@d21a0, WWorld@d1230]

PS2 this-relative accesses (PS2 offsets):
  +0x004  w[4] R/W -> __builtin_vec_delete [3: LoadTrackFile@22f898, Open@22faf8, ~WWorld@22fa40]
  +0x008  w[4] R/W -> __builtin_vec_delete [3: LoadTrackFile@22f898, WWorld@22f820, ~WWorld@22fa40]
  +0x00c  w[4] R/W -> __builtin_vec_delete [3: LoadTrackFile@22f898, WWorld@22f820, ~WWorld@22fa40]
  +0x010  w[4] R/W -> __builtin_vec_delete [3: LoadTrackFile@22f898, WWorld@22f820, ~WWorld@22fa40]
  +0x014  w[4] R/W -> UGroup::DataLocateTag, UGroup::GroupLocateTag, WCollisionMgr::Init [5: LoadTrackFile@22f898, Open@22faf8, Reset@230730, WWorld@22f820, ~WWorld@22fa40]
  +0x018  w[4] R/W -> RCARPFile::~RCARPFile [5: GetTrackFile@22fa30, LoadTrackFile@22f898, Open@22faf8, WWorld@22f820, ~WWorld@22fa40]
  +0x01c  w[4] W [1: Open@22faf8]
  +0x020  w[4] R/W [6: GetEnviroDesc@230bb8, GetProcAnimStateFromInstance@230c48, GetSceneObjFromInstance@230bf0, Open@22faf8, Reset@230730, WWorld@22f820]
  +0x024  w[4] R/W [4: GetProcAnimStateFromInstance@230c48, GetSceneObjFromInstance@230bf0, Open@22faf8, WWorld@22f820]
  +0x028  w[4] R/W [5: GetProcAnimStateFromInstance@230c48, GetSceneObjFromInstance@230bf0, Open@22faf8, Reset@230730, WWorld@22f820]
  +0x02c  w[4] R/W [2: GetEnviroDesc@230bb8, Open@22faf8]
  +0x030  w[4] R/W [3: GetEnviroDesc@230bb8, Open@22faf8, WWorld@22f820]
  +0x034  w[4] R/W [4: Close@230648, Open@22faf8, Reset@230730, ~WWorld@22fa40]
  +0x038  w[4] R/W -> UGroup::DataLocateTag, WSoundGroup::~WSoundGroup [3: Close@230648, Open@22faf8, WWorld@22f820]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
