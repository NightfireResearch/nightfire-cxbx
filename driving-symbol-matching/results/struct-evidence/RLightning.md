# RLightning

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [1792]}
Xbox vtable 0x00192d20 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RLightning virtual table']
constructor 0xa02e0 first calls: ['UVolatileMaterial::UVolatileMaterial', 'RLightning::Reset', 'RTextureContextManager::GetContext']

Xbox methods (18):
  0x8b930 undefined Init(void)
  0x9e8e0 undefined LoadAttributes(void)
  0x9ed50 undefined CalcRandomTables(void)
  0x9ef00 undefined JitterStripPoints(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x9f020 undefined RenderBolts(void)
  0x9f520 undefined BeginBoltDraw(undefined4 param_1, undefined4 param_2)
  0x9f840 undefined IsBoltAlive(undefined4 param_1)
  0x9f970 undefined BuildNewSegment(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, unde
  0x9fb90 undefined Update(void)
  0x9fd20 undefined __thiscall Draw(RLightning * this)
  0x9fdc0 undefined ClearAllBolts(void)
  0x9fe30 undefined Reset(void)
  0x9fec0 undefined ~RLightning(void)
  0x9ff60 undefined __stdcall Kill(void)
  0xa0000 undefined scalar_deleting_destructor(undefined1 param_1)
  0xa02e0 undefined RLightning(void)
  0xa0400 undefined AddBolt(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 p
  0xa0500 undefined AddRoundedBolt(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undef

PS2 methods (34):
  0x1e0900 RLightning::RLightning
  0x1e09d0 RLightning::~RLightning
  0x1e0a70 RLightning::Reset
  0x1e0ae8 RLightning::LoadAttributes
  0x1e0d60 RLightning::Update
  0x1e0eb0 RLightning::Draw
  0x1e0f78 RLightning::ClearAllBolts
  0x1e1010 RLightning::AddBolt
  0x1e1280 RLightning::AddRoundedBolt
  0x1e18c0 RLightning::IsBoltAlive
  0x1e18e0 RLightning::BuildNewSegment
  0x1e1c58 RLightning::AllocBoltHandle
  0x1e1c70 RLightning::FindBoltByHandle
  0x1e1cb0 RLightning::CalcRandomTables
  0x1e1f50 RLightning::RandomSyncWithSim
  0x1e1f88 RLightning::GetRandomCoord
  0x1e1fa8 RLightning::GetRandomColor
  0x1e1fc8 RLightning::GetRandomNum
  0x1e1fe8 RLightning::JitterStripPoints
  0x1e2158 RLightning::ResetBoltRendering
  0x1e2178 RLightning::BeginBoltDraw
  0x1e2320 RLightning::AdvanceBoltDraw
  0x1e2370 RLightning::FinishBoltDraw
  0x1e2380 RLightning::RenderBolts
  0x1e2678 RLightning::ResetMetrics
  0x1e2698 RLightning::UpdateMetrics
  0x1e3398 RLightning::Get
  0x1e33a8 RLightning::Init
  0x1e33e0 RLightning::Kill
  0x1e3418 RLightning::GetSettings
  0x1e3420 RLightning::GetDriftRate
  0x1e3428 RLightning::GetDriftVariance
  0x1e3430 RLightning::GetLifeVariance
  0x1e34f8 RLightning::fgThis_RLightning_global_ctors

Sheet rows:
  RLightning::RLightning(void)
  RLightning::~RLightning(void)
  RLightning::Reset(void)
  RLightning::LoadAttributes(void)
  RLightning::Debug(void)
  RLightning::Update(void)
  RLightning::Draw(void)
  RLightning::ClearAllBolts(void)
  RLightning::AddBolt(COORD3 &, RSceneObj *, CARP::Effect *, COOR
  RLightning::AddRoundedBolt(RSceneObj *, CARP::Effect *, CARP::E
  RLightning::IsBoltAlive(int)
  RLightning::BuildNewSegment(RLightning::Segment &, RLightning::
  RLightning::AllocBoltHandle(void)
  RLightning::FindBoltByHandle(int)
  RLightning::CalcRandomTables(void)
  RLightning::RandomSyncWithSim(int)
  RLightning::GetRandomCoord(void)
  RLightning::GetRandomColor(void)
  RLightning::GetRandomNum(void)
  RLightning::JitterStripPoints(COORD3 &, COORD4 *, EAGL::Colour
  RLightning::ResetBoltRendering(void)
  RLightning::BeginBoltDraw(COORD3 &, float)
  RLightning::AdvanceBoltDraw(COORD3 &)
  RLightning::FinishBoltDraw(void)
  RLightning::RenderBolts(void)
  RLightning::ResetMetrics(void)
  RLightning::UpdateMetrics(void)
  RLightning::Point::Update(int)
  RLightning::ControlPoint::Init(COORD3 &, RSceneObj *, CARP::Eff
  RLightning::ControlPoint::CalcWorldPos(void)
  RLightning::MidPoint::CalcWorldPos(COORD3 &, COORD3 &)
  RLightning::MidPoint::Respawn(float, float)
  RLightning::Segment::Update(int)
  RLightning::Segment::Draw(RLightning &)
  RLightning::Bolt::Update(int)
  RLightning::Bolt::Draw(RLightning &)
  RLightning::Bolt::~Bolt(void)
  RLightning type_info function
  RLightning::Get(void)
  RLightning::Init(void)
  RLightning::Kill(void)
  RLightning::GetSettings(void)
  RLightning::GetDriftRate(void)
  RLightning::GetDriftVariance(void)
  RLightning::GetLifeVariance(void)
  RLightning::fgThis_RLightning
  RLightning virtual table
  RLightning type_info node

Xbox methods treated as members (17 of 18; untyped ones count when ECX is read before it is written): AddBolt, AddRoundedBolt, BeginBoltDraw, BuildNewSegment, CalcRandomTables, ClearAllBolts, Draw, Init, IsBoltAlive, JitterStripPoints, LoadAttributes, RLightning, RenderBolts, Reset, Update, scalar_deleting_destructor, ~RLightning

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RLightning@a02e0, ~RLightning@9fec0]
  +0x004  w- LEA addr-taken -> FUN_000a0390 [1: AddBolt@a0400]
  +0x008  w[4] R/W [7: AddRoundedBolt@a0500, ClearAllBolts@9fdc0, Draw@9fd20, IsBoltAlive@9f840, RLightning@a02e0, Update@9fb90…]
  +0x00c  w[4] R/RW/W [6: ClearAllBolts@9fdc0, Draw@9fd20, IsBoltAlive@9f840, RLightning@a02e0, Update@9fb90, ~RLightning@9fec0]
  +0x010  w[4] R/W [3: ClearAllBolts@9fdc0, RLightning@a02e0, ~RLightning@9fec0]
  +0x014  w[4] R/W [3: AddBolt@a0400, AddRoundedBolt@a0500, Reset@9fe30]
  +0x018  w[4] R/W [2: Reset@9fe30, Update@9fb90]
  +0x01c  w[4] LEA/R addr-taken [2: JitterStripPoints@9ef00, LoadAttributes@9e8e0]
  +0x020  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x024  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x028  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x02c  w- LEA addr-taken [1: LoadAttributes@9e8e0]
  +0x030  w- LEA addr-taken [1: LoadAttributes@9e8e0]
  +0x034  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x038  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x03c  w- LEA addr-taken [1: LoadAttributes@9e8e0]
  +0x040  w[4] LEA/R addr-taken [3: AddBolt@a0400, AddRoundedBolt@a0500, LoadAttributes@9e8e0]
  +0x044  w[4] LEA/R addr-taken [2: AddBolt@a0400, LoadAttributes@9e8e0]
  +0x048  w[4] R [1: Draw@9fd20]
  +0x050  w- LEA addr-taken [1: CalcRandomTables@9ed50]
  +0x450  w- LEA addr-taken [1: RLightning@a02e0]
  +0x550  w- LEA addr-taken [1: CalcRandomTables@9ed50]
  +0x650  w[4] R/W [4: CalcRandomTables@9ed50, Draw@9fd20, JitterStripPoints@9ef00, Update@9fb90]
  +0x654  w[4] R/W [4: CalcRandomTables@9ed50, Draw@9fd20, JitterStripPoints@9ef00, Update@9fb90]
  +0x658  w[4] W [3: CalcRandomTables@9ed50, Draw@9fd20, Update@9fb90]
  +0x660  w- LEA addr-taken -> UVolatileMaterial::UVolatileMaterial, thunk_FUN_000ef490 [3: RLightning@a02e0, RenderBolts@9f020, ~RLightning@9fec0]
  +0x6c0  w[4] R/W [4: BeginBoltDraw@9f520, RLightning@a02e0, RenderBolts@9f020, Reset@9fe30]
  +0x6c4  w[4] R/W [4: BeginBoltDraw@9f520, Draw@9fd20, RenderBolts@9f020, Reset@9fe30]
  +0x6c8  w[1] R/W [3: BeginBoltDraw@9f520, Draw@9fd20, Reset@9fe30]
  +0x6cc  w[4] LEA/W addr-taken [2: RLightning@a02e0, ~RLightning@9fec0]
  +0x6d0  w[4] W [1: ~RLightning@9fec0]
  +0x6d4  w[4] W [1: ~RLightning@9fec0]
  +0x6d8  w[4] W [1: ~RLightning@9fec0]
  +0x6dc  w[4] R/W [3: Draw@9fd20, RenderBolts@9f020, Reset@9fe30]
  +0x6e0  w[4] W float [4: BeginBoltDraw@9f520, Draw@9fd20, JitterStripPoints@9ef00, Reset@9fe30]
  +0x6e4  w[1] W [1: Reset@9fe30]
  +0x6e8  w[4] R/W [1: Reset@9fe30]
  +0x6ec  w[4] W [1: Reset@9fe30]
  +0x6f0  w[4] W [2: RenderBolts@9f020, Reset@9fe30]
  +0x6f4  w[4] W [1: Reset@9fe30]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RLightning@1e0900, ~RLightning@1e09d0]
  +0x004  w[4] LEA/R/W addr-taken [8: AddBolt@1e1010, AddRoundedBolt@1e1280, ClearAllBolts@1e0f78, Draw@1e0eb0, FindBoltByHandle@1e1c70, RLightning@1e0900…]
  +0x008  w[4] R [1: FindBoltByHandle@1e1c70]
  +0x00c  w[4] R [1: ~RLightning@1e09d0]
  +0x010  w[4] R/W [2: AllocBoltHandle@1e1c58, Reset@1e0a70]
  +0x014  w[4] R/W [2: Reset@1e0a70, Update@1e0d60]
  +0x018  w[4] LEA/W float addr-taken [2: GetSettings@1e3418, LoadAttributes@1e0ae8]
  +0x01c  w[4] LEA/R/W float addr-taken [3: AddBolt@1e1010, AddRoundedBolt@1e1280, LoadAttributes@1e0ae8]
  +0x020  w[4] LEA/R/W float addr-taken [2: AddRoundedBolt@1e1280, LoadAttributes@1e0ae8]
  +0x024  w[4] LEA/R/W float addr-taken [4: AddBolt@1e1010, AddRoundedBolt@1e1280, GetDriftRate@1e3420, LoadAttributes@1e0ae8]
  +0x028  w[4] LEA/R/W float addr-taken [2: GetDriftVariance@1e3428, LoadAttributes@1e0ae8]
  +0x02c  w[4] LEA/R/W float addr-taken [2: GetLifeVariance@1e3430, LoadAttributes@1e0ae8]
  +0x030  w[4] LEA/R/W float addr-taken [3: AddBolt@1e1010, AddRoundedBolt@1e1280, LoadAttributes@1e0ae8]
  +0x034  w[4] LEA/R/W float addr-taken [3: AddBolt@1e1010, AddRoundedBolt@1e1280, LoadAttributes@1e0ae8]
  +0x038  w[4] LEA/W float addr-taken [1: LoadAttributes@1e0ae8]
  +0x03c  w[4] LEA/R/W addr-taken [3: AddBolt@1e1010, AddRoundedBolt@1e1280, LoadAttributes@1e0ae8]
  +0x040  w[4] LEA/R/W addr-taken [2: AddBolt@1e1010, LoadAttributes@1e0ae8]
  +0x044  w[4] LEA/R/W addr-taken [2: Draw@1e0eb0, LoadAttributes@1e0ae8]
  +0x450  w- LEA addr-taken [1: RLightning@1e0900]
  +0x550  w- LEA addr-taken [1: CalcRandomTables@1e1cb0]
  +0x650  w[4] R/W [3: CalcRandomTables@1e1cb0, GetRandomCoord@1e1f88, RandomSyncWithSim@1e1f50]
  +0x654  w[4] R/W [3: CalcRandomTables@1e1cb0, GetRandomColor@1e1fa8, RandomSyncWithSim@1e1f50]
  +0x658  w[4] R/W [3: CalcRandomTables@1e1cb0, GetRandomNum@1e1fc8, RandomSyncWithSim@1e1f50]
  +0x660  w- LEA addr-taken [3: RLightning@1e0900, RenderBolts@1e2380, ~RLightning@1e09d0]
  +0x670  w[8] R/W [1: RenderBolts@1e2380]
  +0x690  w[8] R/W [1: RenderBolts@1e2380]
  +0x6f0  w[4] R/W -> __builtin_delete [5: AdvanceBoltDraw@1e2320, BeginBoltDraw@1e2178, RLightning@1e0900, RenderBolts@1e2380, Reset@1e0a70]
  +0x6f4  w[4] R/W [4: AdvanceBoltDraw@1e2320, BeginBoltDraw@1e2178, RenderBolts@1e2380, ResetBoltRendering@1e2158]
  +0x6f8  w[4] R/W [3: BeginBoltDraw@1e2178, FinishBoltDraw@1e2370, ResetBoltRendering@1e2158]
  +0x6fc  w- LEA addr-taken [1: RLightning@1e0900]
  +0x708  w- LEA addr-taken [1: ~RLightning@1e09d0]
  +0x70c  w[4] R/W [2: RenderBolts@1e2380, ResetBoltRendering@1e2158]
  +0x710  w[4] W float [2: BeginBoltDraw@1e2178, ResetBoltRendering@1e2158]
  +0x714  w[4] W [1: Reset@1e0a70]
  +0x718  w[4] R/W [2: Reset@1e0a70, ResetMetrics@1e2678]
  +0x71c  w[4] W [1: ResetMetrics@1e2678]
  +0x720  w[4] W [2: RenderBolts@1e2380, ResetMetrics@1e2678]
  +0x724  w[4] W [1: ResetMetrics@1e2678]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
