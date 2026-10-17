# SMissionManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [7280]}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xb8c30 first calls: ['FUN_000b8490', 'FUN_000b8490', 'FUN_000b8490']

Xbox methods (73):
  0xb60a0 void __thiscall SetCarType(SMissionManager * this)
  0xb6140 char * __fastcall GetCarType(int this)
  0xb6160 undefined LoadWeaponSet(void)
  0xb6380 undefined Start(void)
  0xb63a0 void __thiscall Stop(SMissionManager * this)
  0xb63c0 void __thiscall RestoreLives(SMissionManager * this)
  0xb63f0 undefined ClearScoreBuffer(undefined4 param_1)
  0xb64b0 void __thiscall ChangeStage(SMissionManager * this, int param_1_00, undefined4 param_2)
  0xb64c0 void __thiscall SetAutoDrive(SMissionManager * this, bool param_1, char param_2)
  0xb6590 undefined GoingToSkipCinematic(void)
  0xb65b0 bool __thiscall IsSkippingCinematic(SMissionManager * this)
  0xb65f0 void __thiscall BoostPlayerHealth(int param_1_00, float param_2)
  0xb6640 void __thiscall BoostPlayerShield(int param_1_00, float param_2)
  0xb66c0 undefined IncPlayerDamage(undefined4 param_1)
  0xb66f0 undefined IncShotsFired(undefined4 param_1)
  0xb6720 void __thiscall IncShotsHit(SMissionManager * this, long param_1)
  0xb6770 undefined IncKills(undefined4 param_1)
  0xb67a0 undefined4 __thiscall GetMagicCounter(SMissionManager * this, int idx)
  0xb67b0 undefined SetMagicCounterThreshold(undefined4 param_1, undefined4 param_2)
  0xb67d0 float __thiscall GetPlayerHealth(SMissionManager * this, bool asProportionOfMaximum)
  0xb67f0 void __thiscall ResetCharInfo(SMissionManager * this)
  0xb6b20 undefined UpdateCharInfo(void)
  0xb6bd0 undefined StartTimedAction(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und
  0xb6c30 undefined SetObjectiveComment(undefined4 param_1, undefined4 param_2)
  0xb6c50 undefined ClearObjective(undefined4 param_1)
  0xb6c70 undefined GetCurrObjective(undefined4 param_1)
  0xb6cc0 undefined GetObjectiveComment(undefined4 param_1)
  0xb6cd0 undefined GetObjectiveArrowHeight(undefined4 param_1)
  0xb6d40 undefined IsObjectiveVisibleToUser(undefined4 param_1)
  0xb6d50 undefined CheckObjectiveVisibleToUser(undefined4 param_1)
  0xb6e30 undefined AddScore(undefined4 param_1, undefined4 param_2)
  0xb6e80 undefined CalcEndMissionScores(void)
  0xb7260 undefined GetEnemyAccuracyBoost(void)
  0xb7290 undefined GetEnemyGlueFactor(void)
  0xb72c0 undefined ShowMissionText(void)
  0xb72f0 void __thiscall ProgrammerDefinedEvent(SMissionManager * this, int eventIndex, char * unused)
  0xb7300 undefined4 __thiscall CheckProgrammerEvent(SMissionManager * this, int param_1)
  0xb7310 void __thiscall ClearProgrammerEvent(SMissionManager * this, int param_1)
  0xb7320 undefined StartSectionProfile(undefined4 param_1)
  0xb7470 void __thiscall TallyScores(SMissionManager * this, void * scoreTable, int param_2)
  0xb7950 undefined ClearCinematicMode(undefined4 param_1)
  0xb79d0 void __thiscall SkipCinematic(SMissionManager * this)
  0xb7a40 void __thiscall IncMagicCounter(SMissionManager * this, int idx)
  0xb7ac0 void __thiscall SetMagicCounter(SMissionManager * this, int idx, int countValue)
  0xb7ae0 undefined UpdateAutoDriveMissile(void)
  0xb7de0 undefined CompleteTimedAction(undefined4 param_1)
  0xb7f10 undefined UpdateTimedActions(void)
  0xb7f60 undefined SetObjective(undefined4 param_1, undefined4 param_2)
  0xb7fb0 undefined SetObjective(undefined4 param_1, undefined4 param_2)
  0xb8000 undefined SetObjective(undefined4 param_1, undefined4 param_2)
  0xb8090 undefined InitPlayerCar(void)
  0xb80b0 undefined SetDriveMissile(undefined4 param_1)
  0xb8160 undefined SetCinematicMode(undefined4 param_1)
  0xb81a0 undefined UpdateObjectiveSpecial(void)
  0xb8460 undefined GetObjectiveByIndex(undefined4 param_1)
  0xb84b0 void __thiscall Win(SMissionManager * this, int param_1)
  0xb8580 undefined Lose(void)
  0xb85d0 undefined CallStage(undefined4 param_1)
  0xb8610 void __thiscall ProcessRules(SMissionManager * this)
  0xb8680 undefined Update(void)
  0xb87f0 undefined PassObjective(undefined4 param_1)
  0xb8840 undefined FailObjective(undefined4 param_1)
  0xb8880 undefined SetCurrentObjective(undefined4 param_1)
  0xb88c0 undefined SetIncompleteObjective(undefined4 param_1)
  0xb8900 undefined DestroyRuleList(undefined4 param_1)
  0xb8960 undefined ClearMessageList(void)
  0xb8a30 void __thiscall ClearState(SMissionManager * this)
  0xb8bf0 undefined Reset(void)
  0xb8c30 int __thiscall SMissionManager(SMissionManager * this)
  0xb8e70 undefined ~SMissionManager(void)
  0xb8ff0 undefined __stdcall Construct(void)
  0xb9060 undefined __stdcall Destruct(void)
  0xb9140 undefined Load(undefined4 param_1, undefined4 param_2)

PS2 methods (89):
  0x204d18 SMissionManager::SetCarType
  0x204de8 SMissionManager::SMissionManager
  0x205048 SMissionManager::~SMissionManager
  0x205188 SMissionManager::Construct
  0x2051c0 SMissionManager::Destruct
  0x2051f8 SMissionManager::GetCarType
  0x205220 SMissionManager::Load
  0x205498 SMissionManager::LoadWeaponSet
  0x205788 SMissionManager::Unload
  0x2057f0 SMissionManager::ClearState
  0x205a38 SMissionManager::Reset
  0x205a90 SMissionManager::InitPlayerCar
  0x205ac8 SMissionManager::Start
  0x205ae8 SMissionManager::Stop
  0x205b08 SMissionManager::Win
  0x205bf0 SMissionManager::Lose
  0x205c30 SMissionManager::TallyScores
  0x206108 SMissionManager::RestoreLives
  0x206168 SMissionManager::ClearScoreBuffer
  0x206228 SMissionManager::ProcessRuleList
  0x206280 SMissionManager::DestroyRuleList
  0x206310 SMissionManager::CallStage
  0x206338 SMissionManager::ChangeStage
  0x206340 SMissionManager::ProcessRules
  0x206398 SMissionManager::SetAutoDrive
  0x2064d8 SMissionManager::SetDriveMissile
  0x2065d0 SMissionManager::SetCinematicMode
  0x206620 SMissionManager::ClearCinematicMode
  0x2066d0 SMissionManager::SkipCinematic
  0x206788 SMissionManager::GoingToSkipCinematic
  0x2067a8 SMissionManager::IsSkippingCinematic
  0x206810 SMissionManager::BoostPlayerHealth
  0x206850 SMissionManager::BoostPlayerShield
  0x2068b8 SMissionManager::IncPlayerDamage
  0x2068e0 SMissionManager::IncShotsFired
  0x206900 SMissionManager::IncShotsHit
  0x206960 SMissionManager::IncKills
  0x206980 SMissionManager::GetMagicCounter
  0x206990 SMissionManager::IncMagicCounter
  0x206ad0 SMissionManager::SetMagicCounter
  0x206b00 SMissionManager::SetMagicCounterThreshold
  0x206b20 SMissionManager::GetPlayerHealth
  0x206b40 SMissionManager::GetPlayerShield
  0x206b88 SMissionManager::Update
  0x206ca0 SMissionManager::UpdateAutoDriveMissile
  0x206ec0 SMissionManager::ResetCharInfo
  0x207228 SMissionManager::UpdateCharInfo
  0x207378 SMissionManager::StartTimedAction
  0x2073d0 SMissionManager::CompleteTimedAction
  0x207500 SMissionManager::UpdateTimedActions
  0x2075a8 SMissionManager::SetObjective
  0x207600 SMissionManager::SetObjective
  0x207658 SMissionManager::SetObjective
  0x207720 SMissionManager::SetObjectiveComment
  0x207730 SMissionManager::ClearObjective
  0x207748 SMissionManager::GetCurrObjective
  0x2077d8 SMissionManager::GetObjectiveComment
  0x2077e8 SMissionManager::GetObjectiveArrowHeight
  0x207958 SMissionManager::IsObjectiveVisibleToUser
  0x207968 SMissionManager::CheckObjectiveVisibleToUser
  0x207ab0 SMissionManager::UpdateObjectiveSpecial
  0x207ea8 SMissionManager::AddScore
  0x207f20 SMissionManager::CalcEndMissionScores
  0x2083e8 SMissionManager::GetScore
  0x2083f8 SMissionManager::GetScoreTotal
  0x208428 SMissionManager::GetEnemyAccuracyBoost
  0x208468 SMissionManager::GetEnemyGlueFactor
  0x2084a8 SMissionManager::ShowMissionText
  0x208540 SMissionManager::ProgrammerDefinedEvent
  0x208558 SMissionManager::CheckProgrammerEvent
  0x208568 SMissionManager::ClearProgrammerEvent
  0x208578 SMissionManager::AddObjective
  0x2086e0 SMissionManager::InsertObjective
  0x2088b8 SMissionManager::PassObjective
  0x208918 SMissionManager::FailObjective
  0x208968 SMissionManager::SetCurrentObjective
  0x2089b8 SMissionManager::SetIncompleteObjective
  0x208a00 SMissionManager::GetObjectiveByID
  0x208a48 SMissionManager::GetObjectiveByIndex
  0x208aa0 SMissionManager::ResetObjectiveList
  0x208b40 SMissionManager::ClearObjectiveList
  0x208bb8 SMissionManager::AddMessage
  0x208c70 SMissionManager::GetMessageByID
  0x208cc8 SMissionManager::GetMessageByIndex
  0x208d20 SMissionManager::ClearMessageList
  0x208d98 SMissionManager::StartSectionProfile
  0x208eb0 SMissionManager::UpdateSectionProfile
  0x208f20 SMissionManager::GetCurrentSectionProfile
  0x208f48 SMissionManager::DumpSectionProfiles

Sheet rows:
  SMissionManager::SetCarType(void)
  SMissionManager::SMissionManager(void)
  SMissionManager::~SMissionManager(void)
  SMissionManager::Construct(void)
  SMissionManager::Destruct(void)
  SMissionManager::GetCarType(void)
  SMissionManager::Load(UData *, UData *)
  SMissionManager::LoadWeaponSet(void)
  SMissionManager::Unload(void)
  SMissionManager::ClearState(void)
  SMissionManager::Reset(void)
  SMissionManager::InitPlayerCar(void)
  SMissionManager::Start(void)
  SMissionManager::Stop(void)
  SMissionManager::Win(void)
  SMissionManager::Lose(void)
  SMissionManager::TallyScores(UberBond &, bool)
  SMissionManager::RestoreLives(void)
  SMissionManager::ClearScoreBuffer(UberBond &)
  SMissionManager::ProcessRuleList(SMissionRuleList &)
  SMissionManager::DestroyRuleList(SMissionRuleList &)
  SMissionManager::CallStage(int)
  SMissionManager::ChangeStage(int)
  SMissionManager::ProcessRules(void)
  SMissionManager::SetAutoDrive(bool, bool)
  SMissionManager::SetDriveMissile(Missile *)
  SMissionManager::SetCinematicMode(int)
  SMissionManager::ClearCinematicMode(int)
  SMissionManager::SkipCinematic(void)
  SMissionManager::GoingToSkipCinematic(void)
  SMissionManager::IsSkippingCinematic(void)
  SMissionManager::BoostPlayerHealth(float)
  SMissionManager::BoostPlayerShield(float)
  SMissionManager::IncPlayerDamage(float)
  SMissionManager::IncShotsFired(int)
  SMissionManager::IncShotsHit(bool)
  SMissionManager::IncKills(int)
  SMissionManager::GetMagicCounter(int) const
  SMissionManager::IncMagicCounter(int)
  SMissionManager::SetMagicCounter(int, int)
  SMissionManager::SetMagicCounterThreshold(int, int)
  SMissionManager::GetPlayerHealth(bool) const
  SMissionManager::GetPlayerShield(bool) const
  SMissionManager::Update(void)
  SMissionManager::UpdateAutoDriveMissile(void)
  SMissionManager::ResetCharInfo(void)
  SMissionManager::UpdateCharInfo(void)
  SMissionManager::StartTimedAction(int, float, float, int, int)
  SMissionManager::CompleteTimedAction(int)
  SMissionManager::UpdateTimedActions(void)
  SMissionManager::SetObjective(MissionObjectiveType, CARP::Trigg
  SMissionManager::SetObjective(MissionObjectiveType, CARP::Insta
  SMissionManager::SetObjective(MissionObjectiveType, CARP::AIEle
  SMissionManager::SetObjectiveComment(MissionObjectiveType, int)
  SMissionManager::ClearObjective(MissionObjectiveType)
  SMissionManager::GetCurrObjective(MissionObjectiveType)
  SMissionManager::GetObjectiveComment(MissionObjectiveType)
  SMissionManager::GetObjectiveArrowHeight(MissionObjectiveType)
  SMissionManager::IsObjectiveVisibleToUser(MissionObjectiveType)
  SMissionManager::CheckObjectiveVisibleToUser(MissionObjectiveTy
  SMissionManager::UpdateObjectiveSpecial(void)
  SMissionManager::AddScore(MissionScoreCategory, int)
  SMissionManager::CalcEndMissionScores(void)
  SMissionManager::GetScore(MissionScoreCategory)
  SMissionManager::GetScoreTotal(void)
  SMissionManager::GetEnemyAccuracyBoost(void)
  SMissionManager::GetEnemyGlueFactor(void)
  SMissionManager::GetEnemyHitPointScale(void)
  SMissionManager::ShowMissionText(int, int)
  SMissionManager::GetMessageIdx(void)
  SMissionManager::ProgrammerDefinedEvent(int, char *)
  SMissionManager::CheckProgrammerEvent(int)
  SMissionManager::ClearProgrammerEvent(int)
  SMissionManager::AddObjective(unsigned int, unsigned int, unsig
  SMissionManager::InsertObjective(unsigned int, unsigned int, un
  SMissionManager::PassObjective(unsigned int)
  SMissionManager::FailObjective(unsigned int)
  SMissionManager::SetCurrentObjective(unsigned int)
  SMissionManager::SetIncompleteObjective(unsigned int)
  SMissionManager::GetObjectiveByID(unsigned int)

Xbox methods treated as members (70 of 73; untyped ones count when ECX is read before it is written): AddScore, BoostPlayerHealth, BoostPlayerShield, CalcEndMissionScores, CallStage, ChangeStage, CheckProgrammerEvent, ClearCinematicMode, ClearMessageList, ClearObjective, ClearProgrammerEvent, ClearScoreBuffer, ClearState, CompleteTimedAction, Construct, FailObjective, GetCarType, GetCurrObjective, GetEnemyAccuracyBoost, GetEnemyGlueFactor, GetMagicCounter, GetObjectiveArrowHeight, GetObjectiveByIndex, GetObjectiveComment, GetPlayerHealth, GoingToSkipCinematic, IncKills, IncMagicCounter, IncPlayerDamage, IncShotsFired, IncShotsHit, InitPlayerCar, IsObjectiveVisibleToUser, IsSkippingCinematic, Load, LoadWeaponSet, Lose, PassObjective, ProcessRules, ProgrammerDefinedEvent, Reset, ResetCharInfo, RestoreLives, SMissionManager, SetAutoDrive, SetCarType, SetCinematicMode, SetCurrentObjective, SetDriveMissile, SetIncompleteObjective, SetMagicCounter, SetMagicCounterThreshold, SetObjective, SetObjectiveComment, ShowMissionText, SkipCinematic, Start, StartSectionProfile, StartTimedAction, Stop, TallyScores, Update, UpdateAutoDriveMissile, UpdateCharInfo, UpdateObjectiveSpecial, UpdateTimedActions, Win, ~SMissionManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [8: ClearMessageList@b8960, FailObjective@b8840, GetObjectiveByIndex@b8460, PassObjective@b87f0, SMissionManager@b8c30, SetCurrentObjective@b8880…]
  +0x008  w[4] W [2: SMissionManager@b8c30, ~SMissionManager@b8e70]
  +0x00c  w- LEA addr-taken [2: SMissionManager@b8c30, ~SMissionManager@b8e70]
  +0x010  w[4] R [1: ~SMissionManager@b8e70]
  +0x018  w[4] R/W [3: ClearState@b8a30, Load@b9140, ~SMissionManager@b8e70]
  +0x01c  w[4] R/W [4: ClearState@b8a30, Load@b9140, ProcessRules@b8610, ~SMissionManager@b8e70]
  +0x020  w[4] R/W [6: ChangeStage@b64b0, ClearState@b8a30, Load@b9140, ProcessRules@b8610, Reset@b8bf0, ~SMissionManager@b8e70]
  +0x024  w[4] W [2: Load@b9140, SMissionManager@b8c30]
  +0x028  w[4] W [1: Load@b9140]
  +0x02c  w[4] R/W [10: CalcEndMissionScores@b6e80, GetEnemyAccuracyBoost@b7260, GetEnemyGlueFactor@b7290, Load@b9140, LoadWeaponSet@b6160, ResetCharInfo@b67f0…]
  +0x030  w- LEA addr-taken [3: Load@b9140, SMissionManager@b8c30, ~SMissionManager@b8e70]
  +0x034  w[4] R [2: Load@b9140, ProcessRules@b8610]
  +0x03c  w- LEA addr-taken [3: Load@b9140, SMissionManager@b8c30, ~SMissionManager@b8e70]
  +0x360  w- LEA addr-taken -> ??_L@YGXPAXIHP6EX0@Z1@Z, SMissionTimer::Reset [4: ClearState@b8a30, Reset@b8bf0, SMissionManager@b8c30, ~SMissionManager@b8e70]
  +0x428  w[4] W [1: SMissionManager@b8c30]
  +0x434  w- LEA addr-taken [1: ClearState@b8a30]
  +0x437  w[1] W [3: ClearCinematicMode@b7950, SkipCinematic@b79d0, UpdateAutoDriveMissile@b7ae0]
  +0x438  w[1] W [1: UpdateObjectiveSpecial@b81a0]
  +0x439  w[1] W [1: Load@b9140]
  +0x440  w[1] W [1: IncMagicCounter@b7a40]
  +0x441  w[1] W [1: IncMagicCounter@b7a40]
  +0x442  w[1] W [1: IncMagicCounter@b7a40]
  +0x443  w[1] W [1: IncMagicCounter@b7a40]
  +0x444  w[1] W [1: IncMagicCounter@b7a40]
  +0x44c  w[1] W [1: ClearState@b8a30]
  +0x474  w[4] R/W [10: Lose@b8580, ProcessRules@b8610, Reset@b8bf0, SMissionManager@b8c30, SkipCinematic@b79d0, Start@b6380…]
  +0x478  w[1] R/W [3: SMissionManager@b8c30, SetAutoDrive@b64c0, SetDriveMissile@b80b0]
  +0x479  w[1] R/W [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x47c  w[4] R/W -> Missile::SetTarget, Missile::SetUserSteerX, Missile::SetUserSteerY [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x480  w[4] R/W float [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x484  w[4] R/W float [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x488  w[4] R/W float [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x48c  w[4] R/W float [3: SMissionManager@b8c30, SetDriveMissile@b80b0, UpdateAutoDriveMissile@b7ae0]
  +0x490  w[4] R/W -> ActionQueue::Flush, ActionQueue::GetAction, ActionQueue::IsEmpty, ActionQueue::PopAction [4: Reset@b8bf0, SMissionManager@b8c30, UpdateAutoDriveMissile@b7ae0, ~SMissionManager@b8e70]
  +0x494  w[4] W [1: ResetCharInfo@b67f0]
  +0x498  w[4] W [1: ResetCharInfo@b67f0]
  +0x49c  w[4] W float [6: BoostPlayerHealth@b65f0, CalcEndMissionScores@b6e80, GetPlayerHealth@b67d0, ResetCharInfo@b67f0, TallyScores@b7470, UpdateCharInfo@b6b20]
  +0x4a0  w[4] R/W float [6: BoostPlayerHealth@b65f0, CalcEndMissionScores@b6e80, GetPlayerHealth@b67d0, ResetCharInfo@b67f0, TallyScores@b7470, UpdateCharInfo@b6b20]
  +0x4a4  w[4] W float [4: BoostPlayerShield@b6640, CalcEndMissionScores@b6e80, ResetCharInfo@b67f0, TallyScores@b7470]
  +0x4a8  w[4] R/W float [4: BoostPlayerShield@b6640, CalcEndMissionScores@b6e80, ResetCharInfo@b67f0, TallyScores@b7470]
  +0x4ac  w[4] W [1: ResetCharInfo@b67f0]
  +0x4b0  w[4] W [1: ResetCharInfo@b67f0]
  +0x4b4  w[4] W [1: ResetCharInfo@b67f0]
  +0x4b8  w[4] W [1: ResetCharInfo@b67f0]
  +0x4bc  w[4] W [1: ResetCharInfo@b67f0]
  +0x4c0  w[4] W [1: ResetCharInfo@b67f0]
  +0x4c4  w[4] W [1: ResetCharInfo@b67f0]
  +0x4c8  w[4] W [1: ResetCharInfo@b67f0]
  +0x4cc  w[4] W [1: ResetCharInfo@b67f0]
  +0x4d0  w[4] W [1: ResetCharInfo@b67f0]
  +0x4d4  w[4] W [1: ResetCharInfo@b67f0]
  +0x4d8  w[4] W [1: ResetCharInfo@b67f0]
  +0x4dc  w[4] R/W [6: GetEnemyAccuracyBoost@b7260, GetEnemyGlueFactor@b7290, ResetCharInfo@b67f0, RestoreLives@b63c0, SMissionManager@b8c30, ShowMissionText@b72c0]
  +0x4e0  w[4] R/W [5: CalcEndMissionScores@b6e80, ClearState@b8a30, IncShotsFired@b66f0, IncShotsHit@b6720, TallyScores@b7470]
  +0x4e4  w[4] R/W float [4: CalcEndMissionScores@b6e80, ClearState@b8a30, IncShotsHit@b6720, TallyScores@b7470]
  +0x4e8  w[4] R/RW/W float [5: CalcEndMissionScores@b6e80, ClearState@b8a30, IncKills@b6770, TallyScores@b7470, Win@b84b0]
  +0x4ec  w[4] W float [4: CalcEndMissionScores@b6e80, ClearState@b8a30, IncPlayerDamage@b66c0, TallyScores@b7470]
  +0x4f0  w[4] R/W -> GHud::GetBlackFadePct [11: ClearCinematicMode@b7950, ClearState@b8a30, GoingToSkipCinematic@b6590, IncPlayerDamage@b66c0, IsSkippingCinematic@b65b0, Lose@b8580…]
  +0x4f4  w[1] R/W [8: ClearCinematicMode@b7950, ClearState@b8a30, GoingToSkipCinematic@b6590, IsSkippingCinematic@b65b0, Lose@b8580, SkipCinematic@b79d0…]
  +0x4f8  w[4] R/W [2: ClearState@b8a30, UpdateObjectiveSpecial@b81a0]
  +0x4fc  w[4] R/W [2: ClearState@b8a30, TallyScores@b7470]
  +0x500  w[4] R/W [3: CalcEndMissionScores@b6e80, ClearState@b8a30, TallyScores@b7470]
  +0x504  w[4] R/W [3: ClearState@b8a30, IncPlayerDamage@b66c0, UpdateCharInfo@b6b20]
  +0x538  w[4] R [1: IncMagicCounter@b7a40]
  +0x540  w[4] R [1: IncMagicCounter@b7a40]
  +0x608  w- LEA addr-taken [1: ClearState@b8a30]
  +0x708  w[4] R/RW/W [4: AddScore@b6e30, ClearState@b8a30, CompleteTimedAction@b7de0, TallyScores@b7470]
  +0x70c  w[4] R/W [3: ClearState@b8a30, TallyScores@b7470, Win@b84b0]
  +0x710  w[1] R/W [1: ClearState@b8a30]
  +0x714  w[4] R/W [3: ClearState@b8a30, IncShotsFired@b66f0, IncShotsHit@b6720]
  +0x718  w[4] R/W [3: Load@b9140, ResetCharInfo@b67f0, SMissionManager@b8c30]
  +0x71c  w[4] LEA/R addr-taken [1: UpdateObjectiveSpecial@b81a0]
  +0x720  w[4] R [1: UpdateObjectiveSpecial@b81a0]
  +0x724  w[4] LEA/R addr-taken [2: ClearState@b8a30, UpdateObjectiveSpecial@b81a0]
  +0x728  w[4] R [1: UpdateObjectiveSpecial@b81a0]
  +0x738  w- LEA addr-taken [2: SMissionManager@b8c30, TallyScores@b7470]
  +0x768  w- LEA addr-taken [2: ClearState@b8a30, TallyScores@b7470]
  +0x76c  w[4] R/W [1: CompleteTimedAction@b7de0]
  +0x770  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x774  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x778  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x77c  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x780  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x784  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x788  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x78c  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x794  w[4] W [1: CalcEndMissionScores@b6e80]
  +0x798  w[4] R/W [5: ClearState@b8a30, SetObjective@b7f60, SetObjective@b7fb0, SetObjective@b8000, UpdateObjectiveSpecial@b81a0]
  +0x79c  w- LEA addr-taken [1: ClearState@b8a30]
  +0x7b0  w- LEA addr-taken [1: UpdateTimedActions@b7f10]
  +0x85c  w- LEA addr-taken [2: SMissionManager@b8c30, StartSectionProfile@b7320]
  +0x8a0  w- LEA addr-taken [1: ClearState@b8a30]
  +0x1c5c  w[4] R/W [2: ClearState@b8a30, StartSectionProfile@b7320]
  +0x1c60  w[4] R/W [3: ClearState@b8a30, StartSectionProfile@b7320, Update@b8680]
  +0x1c64  w[4] R/W [2: GetCarType@b6140, SetCarType@b60a0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> SMissionObjective::~SMissionObjective, UMemory::FastFree [9: AddObjective@208578, ClearObjectiveList@208b40, GetObjectiveByID@208a00, GetObjectiveByIndex@208a48, GetScoreTotal@2083f8, InsertObjective@2086e0…]
  +0x004  w[4] LEA/R/W addr-taken -> UMemory::FastFree, __builtin_delete [6: AddMessage@208bb8, ClearMessageList@208d20, GetMessageByID@208c70, GetMessageByIndex@208cc8, SMissionManager@204de8, ~SMissionManager@205048]
  +0x008  w[4] R/W float [5: BoostPlayerHealth@206810, ClearState@2057f0, GetPlayerHealth@206b20, Load@205220, Unload@205788]
  +0x00c  w[4] R/W float [5: BoostPlayerHealth@206810, ClearState@2057f0, GetPlayerHealth@206b20, ProcessRules@206340, Unload@205788]
  +0x010  w[4] R/W float [6: BoostPlayerShield@206850, ChangeStage@206338, ClearState@2057f0, GetPlayerShield@206b40, ProcessRules@206340, Unload@205788]
  +0x014  w[4] R/W float [4: BoostPlayerShield@206850, GetPlayerShield@206b40, Load@205220, SMissionManager@204de8]
  +0x018  w[4] W [1: Load@205220]
  +0x01c  w[4] R/W [10: CalcEndMissionScores@207f20, GetEnemyAccuracyBoost@208428, GetEnemyGlueFactor@208468, Load@205220, LoadWeaponSet@205498, ResetCharInfo@206ec0…]
  +0x020  w[4] LEA/R/W addr-taken -> UMemory::FastFree [5: Load@205220, ProcessRules@206340, SMissionManager@204de8, Unload@205788, ~SMissionManager@205048]
  +0x024  w- LEA addr-taken [3: SMissionManager@204de8, Unload@205788, ~SMissionManager@205048]
  +0x124  w- LEA addr-taken [1: ~SMissionManager@205048]
  +0x150  w- LEA addr-taken [4: ClearState@2057f0, Reset@205a38, SMissionManager@204de8, ~SMissionManager@205048]
  +0x240  w[4] LEA/W addr-taken [2: SMissionManager@204de8, ~SMissionManager@205048]
  +0x24c  w- LEA addr-taken [1: ClearState@2057f0]
  +0x34c  w[4] R/W [10: Lose@205bf0, ProcessRules@206340, Reset@205a38, SMissionManager@204de8, SkipCinematic@2066d0, Start@205ac8…]
  +0x350  w[4] R/W [3: SMissionManager@204de8, SetAutoDrive@206398, SetDriveMissile@2064d8]
  +0x354  w[4] R/W [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x358  w[4] R/W -> Missile::SetTarget [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x35c  w[4] R/W float [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x360  w[4] R/W float [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x364  w[4] R/W float [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x368  w[4] R/W float [3: SMissionManager@204de8, SetDriveMissile@2064d8, UpdateAutoDriveMissile@206ca0]
  +0x36c  w[4] R/W -> ActionQueue::IsEmpty, ActionQueue::~ActionQueue, SMissionTimer::Reset [4: Reset@205a38, SMissionManager@204de8, UpdateAutoDriveMissile@206ca0, ~SMissionManager@205048]
  +0x370  w[4] LEA/W addr-taken [2: ResetCharInfo@206ec0, UpdateCharInfo@207228]
  +0x374  w[4] W [1: ResetCharInfo@206ec0]
  +0x378  w[4] R/W float [1: ResetCharInfo@206ec0]
  +0x37c  w[4] R/W float [1: ResetCharInfo@206ec0]
  +0x380  w[4] LEA/R/W float addr-taken [2: InitPlayerCar@205a90, ResetCharInfo@206ec0]
  +0x384  w[4] R/W float [1: ResetCharInfo@206ec0]
  +0x388  w[4] W [1: ResetCharInfo@206ec0]
  +0x38c  w[4] W [1: ResetCharInfo@206ec0]
  +0x390  w[4] W float [1: ResetCharInfo@206ec0]
  +0x394  w[4] W float [1: ResetCharInfo@206ec0]
  +0x398  w[4] W [1: ResetCharInfo@206ec0]
  +0x39c  w[4] W float [1: ResetCharInfo@206ec0]
  +0x3a0  w[4] W [1: ResetCharInfo@206ec0]
  +0x3a4  w[4] W [1: ResetCharInfo@206ec0]
  +0x3a8  w[4] W float [1: ResetCharInfo@206ec0]
  +0x3ac  w[4] W float [1: ResetCharInfo@206ec0]
  +0x3b0  w[4] W [1: ResetCharInfo@206ec0]
  +0x3b4  w[4] W float [1: ResetCharInfo@206ec0]
  +0x3b8  w[4] R/W [6: GetEnemyAccuracyBoost@208428, GetEnemyGlueFactor@208468, ResetCharInfo@206ec0, RestoreLives@206108, SMissionManager@204de8, ShowMissionText@2084a8]
  +0x3bc  w[4] R/W [5: CalcEndMissionScores@207f20, ClearState@2057f0, IncShotsFired@2068e0, IncShotsHit@206900, TallyScores@205c30]
  +0x3c0  w[4] R/W float [4: CalcEndMissionScores@207f20, ClearState@2057f0, IncShotsHit@206900, TallyScores@205c30]
  +0x3c4  w[4] R/W float [5: CalcEndMissionScores@207f20, ClearState@2057f0, IncKills@206960, TallyScores@205c30, Win@205b08]
  +0x3c8  w[4] R/W float [4: CalcEndMissionScores@207f20, ClearState@2057f0, IncPlayerDamage@2068b8, TallyScores@205c30]
  +0x3cc  w[4] R/W [9: ClearCinematicMode@206620, ClearState@2057f0, GoingToSkipCinematic@206788, IncPlayerDamage@2068b8, IsSkippingCinematic@2067a8, SetCinematicMode@2065d0…]
  +0x3d0  w[4] R/W [5: ClearCinematicMode@206620, ClearState@2057f0, GoingToSkipCinematic@206788, IsSkippingCinematic@2067a8, SkipCinematic@2066d0]
  +0x3d4  w[4] R/W [2: ClearState@2057f0, UpdateObjectiveSpecial@207ab0]
  +0x3d8  w[4] R/W [2: ClearState@2057f0, TallyScores@205c30]
  +0x3dc  w[4] R/W [3: CalcEndMissionScores@207f20, ClearState@2057f0, TallyScores@205c30]
  +0x3e0  w[4] R/W [3: ClearState@2057f0, IncPlayerDamage@2068b8, UpdateCharInfo@207228]
  +0x3e4  w- LEA addr-taken [2: ClearState@2057f0, IncMagicCounter@206990]
  +0x414  w[4] R [1: IncMagicCounter@206990]
  +0x41c  w[4] R [1: IncMagicCounter@206990]
  +0x4e4  w- LEA addr-taken [1: ClearState@2057f0]
  +0x5e4  w[4] R/W [3: AddScore@207ea8, ClearState@2057f0, TallyScores@205c30]
  +0x5e8  w[4] R/W float [3: ClearState@2057f0, TallyScores@205c30, Win@205b08]
  +0x5ec  w[4] R/W [1: ClearState@2057f0]
  +0x5f0  w[4] R/W [3: ClearState@2057f0, IncShotsFired@2068e0, IncShotsHit@206900]
  +0x5f4  w[4] R/W [3: Load@205220, ResetCharInfo@206ec0, SMissionManager@204de8]
  +0x5f8  w- LEA addr-taken [1: UpdateObjectiveSpecial@207ab0]
  +0x600  w- LEA addr-taken [1: ClearState@2057f0]
  +0x610  w- LEA addr-taken [2: ClearState@2057f0, UpdateObjectiveSpecial@207ab0]
  +0x618  w- LEA addr-taken [2: SMissionManager@204de8, TallyScores@205c30]
  +0x648  w- LEA addr-taken [3: AddScore@207ea8, ClearState@2057f0, TallyScores@205c30]
  +0x650  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x654  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x658  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x65c  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x660  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x664  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x668  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x66c  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x674  w[4] W float [1: CalcEndMissionScores@207f20]
  +0x678  w[4] R/W [5: ClearState@2057f0, SetObjective@2075a8, SetObjective@207600, SetObjective@207658, UpdateObjectiveSpecial@207ab0]
  +0x67c  w- LEA addr-taken [3: ClearState@2057f0, CompleteTimedAction@2073d0, StartTimedAction@207378]
  +0x680  w- LEA addr-taken [1: UpdateTimedActions@207500]
  +0x73c  w- LEA addr-taken [3: ClearState@2057f0, SMissionManager@204de8, StartSectionProfile@208d98]
  +0x1b3c  w[4] R/W [2: ClearState@2057f0, StartSectionProfile@208d98]
  +0x1b40  w[4] R/W [4: ClearState@2057f0, GetCurrentSectionProfile@208f20, StartSectionProfile@208d98, UpdateSectionProfile@208eb0]
  +0x1b44  w[4] R/W -> strcasecmp [2: GetCarType@2051f8, SetCarType@204d18]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetCarType: R +0x1c64 w4
  Start: R +0x474 w4
  Stop: R +0x474 w4
  ChangeStage: W +0x20 w4
  IncKills: R +0x4e8 w4
