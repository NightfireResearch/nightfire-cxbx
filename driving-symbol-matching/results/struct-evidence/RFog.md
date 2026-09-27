# RFog

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [28]}
Xbox vtable 0x00191514 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RFog virtual table']
constructor 0x7d930 first calls: ['__builtin_new', 'DTuningDBMgr::LoadDatabase', 'dbattrib_float']

Xbox methods (9):
  0x7d790 undefined FogColour(void)
  0x7d7a0 void __thiscall DisableFog(RFog * this)
  0x7d820 void __thiscall EnableFog(RFog * this)
  0x7d840 undefined SetFogParams(void)
  0x7d8c0 undefined UpdateScale(void)
  0x7d930 undefined4 * __thiscall RFog(RFog * this)
  0x7daa0 undefined __stdcall Kill(void)
  0x7dac0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x8b690 undefined __stdcall Init(void)

PS2 methods (15):
  0x1ac668 RFog::RFog
  0x1ac818 RFog::~RFog
  0x1ac848 RFog::FogColour
  0x1ac858 RFog::EnableFog
  0x1ac910 RFog::DisableFog
  0x1ac978 RFog::SetFogParams
  0x1aca28 RFog::Debug
  0x1aca70 RFog::CalcFogEnd
  0x1aca88 RFog::SetFogScale
  0x1acb50 RFog::UpdateScale
  0x1ace58 RFog::Get
  0x1ace68 RFog::Init
  0x1acea0 RFog::Kill
  0x1aced8 RFog::FogInfo
  0x1acee0 RFog::Reset

Sheet rows:
  RFog::RFog(void)
  RFog::~RFog(void)
  RFog::FogColour(void)
  RFog::EnableFog(void)
  RFog::DisableFog(void)
  RFog::SetFogParams(void)
  RFog::Debug(void)
  RFog::CalcFogEnd(void)
  RFog::SetFogScale(float, float)
  RFog::UpdateScale(void)
  RFog type_info function
  RFog::Get(void)
  RFog::Init(void)
  RFog::Kill(void)
  RFog::FogInfo(void)
  RFog::Reset(void)
  RFog::fgThis_RFog
  RFog virtual table
  RFog type_info node

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): DisableFog, EnableFog, FogColour, Init, RFog, SetFogParams, UpdateScale, scalar_deleting_destructor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: RFog@7d930]
  +0x004  w[4] R/W [5: EnableFog@7d820, FogColour@7d790, RFog@7d930, SetFogParams@7d840, UpdateScale@7d8c0]
  +0x008  w[1] R/W [2: RFog@7d930, UpdateScale@7d8c0]
  +0x00c  w[4] R/W [2: RFog@7d930, UpdateScale@7d8c0]
  +0x010  w[4] W float [2: RFog@7d930, UpdateScale@7d8c0]
  +0x018  w[4] R/W float [2: RFog@7d930, UpdateScale@7d8c0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RFog@1ac668, ~RFog@1ac818]
  +0x004  w[4] R/W [7: CalcFogEnd@1aca70, DisableFog@1ac910, FogColour@1ac848, FogInfo@1aced8, RFog@1ac668, SetFogScale@1aca88…]
  +0x008  w[4] R/W [3: RFog@1ac668, SetFogScale@1aca88, UpdateScale@1acb50]
  +0x00c  w[4] R/W [3: RFog@1ac668, SetFogScale@1aca88, UpdateScale@1acb50]
  +0x010  w[4] R/W float [3: RFog@1ac668, SetFogScale@1aca88, UpdateScale@1acb50]
  +0x018  w[4] R/W float [3: RFog@1ac668, SetFogScale@1aca88, UpdateScale@1acb50]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  FogColour: R +0x4 w4
