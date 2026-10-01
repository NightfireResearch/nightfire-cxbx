# AIElementController

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x28190 first calls: ['__builtin_vec_new', '__builtin_vec_new', '__builtin_vec_new']

Xbox methods (15):
  0x28190 undefined AIElementController(undefined4 param_1)
  0x285b0 undefined Construct(undefined4 param_1)
  0x28620 undefined GetStats(void)
  0x28630 undefined EveryoneChill(void)
  0x286b0 undefined OkToSpawnHench(void)
  0x286d0 undefined RedirectCarpElement(undefined4 param_1)
  0x28700 undefined GetCarpElementTracking(undefined4 param_1)
  0x28730 undefined CalcMightAsWellBeDead(void)
  0x287c0 undefined GetNumMightAsWellBeDead(void)
  0x287d0 undefined GoToSleep(undefined4 param_1)
  0x289f0 undefined ForceVehicleToSleep(undefined4 param_1)
  0x28a40 undefined SyncRuleInfo(undefined4 param_1)
  0x28b60 undefined Destruct(void)
  0x28bb0 undefined WakeUp(undefined4 param_1)
  0x29880 undefined Update(void)

PS2 methods (20):
  0x12b8b0 AIElementController::AIElementController
  0x12bd70 AIElementController::~AIElementController
  0x12be48 AIElementController::Construct
  0x12be90 AIElementController::Destruct
  0x12bec8 AIElementController::Update
  0x12c440 AIElementController::GetStats
  0x12c448 AIElementController::EveryoneToSleep
  0x12c4c0 AIElementController::EveryoneChill
  0x12c5d0 AIElementController::OkToSpawnHench
  0x12c608 AIElementController::RedirectCarpElement
  0x12c640 AIElementController::GetCarpElementTracking
  0x12c678 AIElementController::CalcMightAsWellBeDead
  0x12c738 AIElementController::GetNumMightAsWellBeDead
  0x12c740 AIElementController::GoToSleep
  0x12cb08 AIElementController::WakeUp
  0x12da78 AIElementController::ForceVehicleToSleep
  0x12daf0 AIElementController::SyncRuleInfo
  0x12dc18 AIElementController::CompareInRangePrio
  0x12dc40 AIElementController::GetElementTrackingCount
  0x12dc48 AIElementController::GetElementTracking

Sheet rows:
  AIElementController::AIElementController(UData *)
  AIElementController::~AIElementController(void)
  AIElementController::Construct(UData *)
  AIElementController::Destruct(void)
  AIElementController::Update(void)
  AIElementController::GetStats(void)
  AIElementController::EveryoneToSleep(void)
  AIElementController::EveryoneChill(void)
  AIElementController::OkToSpawnHench(void)
  AIElementController::GetElement(int)
  AIElementController::RedirectCarpElement(CARP::AIElement *)
  AIElementController::GetCarpElementTracking(CARP::AIElement *)
  AIElementController::CalcMightAsWellBeDead(void)
  AIElementController::GetNumMightAsWellBeDead(void)
  AIElementController::GoToSleep(CARP::AIElement &)
  AIElementController::WakeUp(CARP::AIElement &)
  AIElementController::IsAICharInVehicle(AIVehicle *)
  AIElementController::ForceVehicleToSleep(AIVehicle *)
  AIElementController::SyncRuleInfo(CARP::AIElement &)
  AIElementController::CompareInRangePrio(void *, void *)
  AIElementController::WillingToSleep(CARP::AIElement &)
  AIElementController::WillingToDie(CARP::AIElement &)
  AIElementController::GetElementTrackingCount(void)
  AIElementController::GetElementTracking(int)
  AIElementController::fObj

Xbox methods treated as members (13 of 15; untyped ones count when ECX is read before it is written): AIElementController, CalcMightAsWellBeDead, Construct, EveryoneChill, ForceVehicleToSleep, GetCarpElementTracking, GetNumMightAsWellBeDead, GetStats, GoToSleep, OkToSpawnHench, RedirectCarpElement, Update, WakeUp

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: AIElementController@28190, Update@29880]
  +0x004  w[4] R/W [3: AIElementController@28190, OkToSpawnHench@286b0, Update@29880]
  +0x008  w[4] R/W [2: AIElementController@28190, Update@29880]
  +0x00c  w[4] R/W -> RSceneObj::PreLoad [6: AIElementController@28190, CalcMightAsWellBeDead@28730, EveryoneChill@28630, ForceVehicleToSleep@289f0, RedirectCarpElement@286d0, Update@29880]
  +0x010  w[4] W [1: AIElementController@28190]
  +0x014  w[4] R/W [3: AIElementController@28190, GetCarpElementTracking@28700, Update@29880]
  +0x018  w[4] W [1: AIElementController@28190]
  +0x01c  w[4] LEA/W addr-taken [2: GetStats@28620, Update@29880]
  +0x020  w[4] W [1: Update@29880]
  +0x024  w[4] W [1: Update@29880]
  +0x028  w[4] W [1: Update@29880]
  +0x02c  w[4] W [1: Update@29880]
  +0x030  w[4] W [1: Update@29880]
  +0x034  w[4] W float [1: Update@29880]
  +0x038  w[4] R/W [2: AIElementController@28190, Update@29880]
  +0x03c  w[4] R/W -> qsort [2: AIElementController@28190, Update@29880]
  +0x040  w[4] R/W [5: AIElementController@28190, CalcMightAsWellBeDead@28730, EveryoneChill@28630, ForceVehicleToSleep@289f0, Update@29880]
  +0x044  w[4] R/RW/W [3: AIElementController@28190, GoToSleep@287d0, Update@29880]
  +0x048  w[4] R/RW/W [3: AIElementController@28190, GoToSleep@287d0, Update@29880]
  +0x04c  w[4] R/RW/W [4: AIElementController@28190, GoToSleep@287d0, Update@29880, WakeUp@28bb0]
  +0x050  w[4] R/W [3: AIElementController@28190, OkToSpawnHench@286b0, Update@29880]
  +0x054  w[4] R/W [3: AIElementController@28190, CalcMightAsWellBeDead@28730, GetNumMightAsWellBeDead@287c0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [2: AIElementController@12b8b0, Update@12bec8]
  +0x004  w[4] R/W [3: AIElementController@12b8b0, OkToSpawnHench@12c5d0, Update@12bec8]
  +0x008  w[4] R/W [2: AIElementController@12b8b0, Update@12bec8]
  +0x00c  w[4] R/W -> __builtin_vec_delete, memcpy [8: AIElementController@12b8b0, CalcMightAsWellBeDead@12c678, EveryoneChill@12c4c0, EveryoneToSleep@12c448, ForceVehicleToSleep@12da78, RedirectCarpElement@12c608…]
  +0x010  w[4] W [1: AIElementController@12b8b0]
  +0x014  w[4] R/W -> AIElementTracking::Update, AIElementTracking::~AIElementTracking [5: AIElementController@12b8b0, GetCarpElementTracking@12c640, GetElementTracking@12dc48, Update@12bec8, ~AIElementController@12bd70]
  +0x018  w[4] R/W -> __builtin_vec_delete [2: AIElementController@12b8b0, ~AIElementController@12bd70]
  +0x01c  w[4] LEA/W addr-taken [2: GetStats@12c440, Update@12bec8]
  +0x020  w[4] W float [1: Update@12bec8]
  +0x024  w[4] W [1: Update@12bec8]
  +0x028  w[4] W [1: Update@12bec8]
  +0x02c  w[4] W float [1: Update@12bec8]
  +0x030  w[4] W [1: Update@12bec8]
  +0x034  w[4] R/W float [1: Update@12bec8]
  +0x038  w[4] R/W -> __builtin_vec_delete [3: AIElementController@12b8b0, Update@12bec8, ~AIElementController@12bd70]
  +0x03c  w[4] R/W -> __builtin_vec_delete, qsort [3: AIElementController@12b8b0, Update@12bec8, ~AIElementController@12bd70]
  +0x040  w[4] R/W -> __builtin_vec_new [8: AIElementController@12b8b0, CalcMightAsWellBeDead@12c678, EveryoneChill@12c4c0, EveryoneToSleep@12c448, ForceVehicleToSleep@12da78, GetElementTrackingCount@12dc40…]
  +0x044  w[4] R/W [3: AIElementController@12b8b0, GoToSleep@12c740, Update@12bec8]
  +0x048  w[4] R/W [3: AIElementController@12b8b0, GoToSleep@12c740, Update@12bec8]
  +0x04c  w[4] R/W [3: AIElementController@12b8b0, GoToSleep@12c740, Update@12bec8]
  +0x050  w[4] R/W [3: AIElementController@12b8b0, OkToSpawnHench@12c5d0, Update@12bec8]
  +0x054  w[4] R/W [3: AIElementController@12b8b0, CalcMightAsWellBeDead@12c678, GetNumMightAsWellBeDead@12c738]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetStats: LEA +0x1c w0
  GetNumMightAsWellBeDead: R +0x54 w4
