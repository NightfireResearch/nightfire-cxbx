# AICharacter

FastAlloc/constructed sizes under its tag: {'allocated': [336, 352, 368, 448, 656], 'constructed': []}
deleting destructor 0x1c640 frees/deletes with size 0x1 (call to None)
deleting destructor 0x1c640 frees/deletes with size 0x140 (call to UMemory::FastFree)
Xbox vtable 0x0018a540 (33 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacter virtual table']
constructor 0x1c870 first calls: ['VU0_MATRIX4Init']

Xbox methods (14):
  0x1c190 undefined __stdcall Init(void)
  0x1c1b0 undefined __thiscall Shutdown(AICharacter * this)
  0x1c1d0 undefined ~AICharacter(void)
  0x1c200 undefined IsAlive(void)
  0x1c220 undefined SetActor(undefined4 param_1)
  0x1c230 undefined GetACharacter(void)
  0x1c2b0 undefined Execute(void)
  0x1c5a0 undefined GetZoneLastHit(void)
  0x1c5b0 undefined GetZoneHitPointScale(undefined4 param_1)
  0x1c640 void __thiscall scalar_deleting_destructor(AICharacter * this)
  0x1c690 undefined NotifyZoneDamage(undefined4 param_1)
  0x1c870 undefined AICharacter(void)
  0x1c9c0 undefined MoveToPoint(undefined4 param_1, undefined1 param_2)
  0x1da50 undefined ~AICharacter(void)

PS2 methods (75):
  0x1186b0 AICharacter::Init
  0x1186e8 AICharacter::Shutdown
  0x118720 AICharacter::AICharacter
  0x118900 AICharacter::~AICharacter
  0x118988 AICharacter::GetACharacter
  0x118af0 AICharacter::Execute
  0x118eb0 AICharacter::NotifyZoneDamage
  0x1190f0 AICharacter::GetZoneLastHit
  0x1190f8 AICharacter::GetZoneHitPointScale
  0x119138 AICharacter::PlayZoneInjuryAnim
  0x119140 AICharacter::MoveToPoint
  0x119560 AICharacter::operator_new
  0x119580 AICharacter::operator_delete
  0x1195a0 AICharacter::SetHitPointLoc
  0x1195b8 AICharacter::GetHitPointLoc
  0x1195c0 AICharacter::GetHealth
  0x1195e0 AICharacter::MortallyWounded
  0x119618 AICharacter::GetNewDamage
  0x119620 AICharacter::NotifyNewDamage
  0x119630 AICharacter::ClearNewDamage
  0x119638 AICharacter::IsAlive
  0x119658 AICharacter::GetXAxis
  0x119660 AICharacter::GetYAxis
  0x119668 AICharacter::GetZAxis
  0x119670 AICharacter::GetPosition
  0x119678 AICharacter::GetRotPos
  0x119680 AICharacter::GetCollisionBias
  0x119688 AICharacter::SetInjuryZones
  0x119690 AICharacter::GetInjuryZones
  0x119698 AICharacter::SetActor
  0x1196a8 AICharacter::SetActor
  0x1196b8 AICharacter::GetActor
  0x1196c8 AICharacter::GetVehiclePtr
  0x1196d0 AICharacter::SetHumanPtr
  0x1196d8 AICharacter::GetHumanPtr
  0x1196e0 AICharacter::SetFireFlag
  0x1196e8 AICharacter::GetFireFlag
  0x1196f0 AICharacter::SetScoreable
  0x1196f8 AICharacter::GetScoreable
  0x119700 AICharacter::SetTargetRangeSquared
  0x119708 AICharacter::GetTargetRangeSquared
  0x119710 AICharacter::GetUseCustomTarget
  0x119718 AICharacter::SetUseCustomTarget
  0x119720 AICharacter::GetCustomTarget
  0x119740 AICharacter::SetCustomTarget
  0x119760 AICharacter::SetAvoidZoneDistance
  0x119768 AICharacter::SetAvoidZoneBase
  0x119788 AICharacter::SetUseAvoidZone
  0x119790 AICharacter::DoInitial
  0x119798 AICharacter::DoNeutral
  0x1197a0 AICharacter::DoIdling
  0x1197a8 AICharacter::DoWalking
  0x1197b0 AICharacter::DoWandering
  0x1197b8 AICharacter::DoAvoiding
  0x1197c0 AICharacter::DoStartled
  0x1197c8 AICharacter::DoDodging
  0x1197d0 AICharacter::DoArming
  0x1197d8 AICharacter::DoArmed
  0x1197e0 AICharacter::DoAiming
  0x1197e8 AICharacter::DoFiring
  0x1197f0 AICharacter::DoReloading
  0x1197f8 AICharacter::DoUnarming
  0x119800 AICharacter::DoHurting
  0x119808 AICharacter::DoDying
  0x119810 AICharacter::DoTilting
  0x119818 AICharacter::DoTurning
  0x119820 AICharacter::DoTurnWalk
  0x119828 AICharacter::DoStartRunning
  0x119830 AICharacter::DoRunning
  0x119838 AICharacter::DoLeaning
  0x119840 AICharacter::DoDead
  0x119848 AICharacter::HandleInterrupts
  0x119850 AICharacter::IsTooFarAway
  0x119858 AICharacter::UpdateRotPos
  0x119940 AICharacter::kMissileDeathThresh_global_ctors

Sheet rows:
  AICharacter::Init(void)
  AICharacter::Shutdown(void)
  AICharacter::AICharacter(void)
  AICharacter::~AICharacter(void)
  AICharacter::GetACharacter(void)
  AICharacter::Execute(void)
  AICharacter::NotifyZoneDamage(COORD3 &)
  AICharacter::GetZoneLastHit(void)
  AICharacter::GetZoneHitPointScale(int, float)
  AICharacter::PlayZoneInjuryAnim(int, float, int)
  AICharacter::MoveToPoint(COORD3 &, bool)
  AICharacter type_info function
  AICharacter::operator new(unsigned int)
  AICharacter::operator delete(void *, unsigned int)
  AICharacter::SetHitPointLoc(float *)
  AICharacter::GetHitPointLoc(void) const
  AICharacter::GetHealth(void)
  AICharacter::MortallyWounded(void)
  AICharacter::GetNewDamage(void)
  AICharacter::NotifyNewDamage(float)
  AICharacter::ClearNewDamage(void)
  AICharacter::IsAlive(void)
  AICharacter::GetXAxis(void) const
  AICharacter::GetYAxis(void) const
  AICharacter::GetZAxis(void) const
  AICharacter::GetPosition(void) const
  AICharacter::GetRotPos(void) const
  AICharacter::GetCollisionBias(void) const
  AICharacter::SetInjuryZones(AICharacterInjuryZones *)
  AICharacter::GetInjuryZones(void)
  AICharacter::SetActor(_List_iterator<ActActor *, ActActor *&, A
  AICharacter::SetActor(long)
  AICharacter::GetActor(void)
  AICharacter::GetVehiclePtr(void)
  AICharacter::SetHumanPtr(Human *)
  AICharacter::GetHumanPtr(void)
  AICharacter::SetFireFlag(bool)
  AICharacter::GetFireFlag(void)
  AICharacter::SetScoreable(bool)
  AICharacter::GetScoreable(void)
  AICharacter::SetTargetRangeSquared(float)
  AICharacter::GetTargetRangeSquared(void)
  AICharacter::GetUseCustomTarget(void)
  AICharacter::SetUseCustomTarget(bool)
  AICharacter::GetCustomTarget(COORD3 &)
  AICharacter::SetCustomTarget(COORD3 &)
  AICharacter::SetAvoidZoneDistance(float)
  AICharacter::SetAvoidZoneBase(COORD3 &)
  AICharacter::SetUseAvoidZone(bool)
  AICharacter::DoInitial(void)
  AICharacter::DoNeutral(void)
  AICharacter::DoIdling(void)
  AICharacter::DoWalking(void)
  AICharacter::DoWandering(void)
  AICharacter::DoAvoiding(void)
  AICharacter::DoStartled(void)
  AICharacter::DoDodging(void)
  AICharacter::DoArming(void)
  AICharacter::DoArmed(void)
  AICharacter::DoAiming(void)
  AICharacter::DoFiring(void)
  AICharacter::DoReloading(void)
  AICharacter::DoUnarming(void)
  AICharacter::DoHurting(void)
  AICharacter::DoDying(void)
  AICharacter::DoTilting(void)
  AICharacter::DoTurning(void)
  AICharacter::DoTurnWalk(void)
  AICharacter::DoStartRunning(void)
  AICharacter::DoRunning(void)
  AICharacter::DoLeaning(void)
  AICharacter::DoDead(void)
  AICharacter::HandleInterrupts(void)
  AICharacter::IsTooFarAway(void)
  AICharacter::UpdateRotPos(void)
  AICharacter::kDefaultInjuryZones
  AICharacter::kVehicleInjuryZones
  AICharacter::kMissileDeathThresh
  AICharacter::kDefaultZoneMap
  AICharacter virtual table

Xbox methods treated as members (13 of 14; untyped ones count when ECX is read before it is written): AICharacter, Execute, GetACharacter, GetZoneHitPointScale, GetZoneLastHit, IsAlive, MoveToPoint, NotifyZoneDamage, SetActor, Shutdown, scalar_deleting_destructor, ~AICharacter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [4: AICharacter@1c870, Execute@1c2b0, scalar_deleting_destructor@1c640, ~AICharacter@1c1d0]
  +0x010  w- LEA addr-taken [2: AICharacter@1c870, NotifyZoneDamage@1c690]
  +0x040  w- LEA addr-taken [1: Execute@1c2b0]
  +0x050  w- LEA addr-taken [1: AICharacter@1c870]
  +0x05c  w[4] R/W [3: AICharacter@1c870, Execute@1c2b0, IsAlive@1c200]
  +0x060  w[4] W [1: AICharacter@1c870]
  +0x064  w[4] W [1: AICharacter@1c870]
  +0x068  w[4] W [1: AICharacter@1c870]
  +0x06c  w[4] W [1: AICharacter@1c870]
  +0x070  w[4] W [1: AICharacter@1c870]
  +0x074  w[4] W [1: AICharacter@1c870]
  +0x078  w[4] W [1: AICharacter@1c870]
  +0x07c  w[4] W [1: AICharacter@1c870]
  +0x084  w[4] R/W [2: AICharacter@1c870, Execute@1c2b0]
  +0x088  w[4] W [1: AICharacter@1c870]
  +0x08c  w[4] W [1: AICharacter@1c870]
  +0x090  w[4] W [1: AICharacter@1c870]
  +0x094  w[4] W [1: AICharacter@1c870]
  +0x098  w[4] W [1: AICharacter@1c870]
  +0x09c  w[4] W [1: AICharacter@1c870]
  +0x0a0  w[4] W [1: AICharacter@1c870]
  +0x0a4  w[4] R/W [5: AICharacter@1c870, Execute@1c2b0, GetACharacter@1c230, scalar_deleting_destructor@1c640, ~AICharacter@1c1d0]
  +0x0a8  w[4] R/W -> ActActorDatabase::KillActorByHandle [5: AICharacter@1c870, MoveToPoint@1c9c0, SetActor@1c220, scalar_deleting_destructor@1c640, ~AICharacter@1c1d0]
  +0x0ac  w[4] W [1: AICharacter@1c870]
  +0x0b0  w[4] W [1: AICharacter@1c870]
  +0x0b4  w[4] R/W [2: AICharacter@1c870, NotifyZoneDamage@1c690]
  +0x0b8  w[4] R/W [2: GetZoneLastHit@1c5a0, NotifyZoneDamage@1c690]
  +0x0bc  w[1] W [1: AICharacter@1c870]
  +0x0bd  w[1] W [1: AICharacter@1c870]
  +0x0c0  w[4] W [1: AICharacter@1c870]
  +0x0c4  w[4] W [1: AICharacter@1c870]
  +0x0c8  w[4] W [1: AICharacter@1c870]
  +0x0cc  w[1] W [1: AICharacter@1c870]
  +0x0d0  w- LEA addr-taken [1: AICharacter@1c870]
  +0x100  w[4] LEA/W addr-taken [1: Execute@1c2b0]
  +0x104  w[4] W [1: Execute@1c2b0]
  +0x108  w[4] W [1: Execute@1c2b0]
  +0x110  w[4] LEA/W addr-taken [2: AICharacter@1c870, Execute@1c2b0]
  +0x114  w[4] W float [2: AICharacter@1c870, Execute@1c2b0]
  +0x118  w[4] W float [2: AICharacter@1c870, Execute@1c2b0]
  +0x11c  w[1] R/W [2: AICharacter@1c870, Execute@1c2b0]
  +0x11d  w[1] R/W [2: AICharacter@1c870, Execute@1c2b0]
  +0x120  w[4] W [1: AICharacter@1c870]
  +0x130  w[4] W [1: AICharacter@1c870]
  +0x134  w[4] W [1: AICharacter@1c870]
  +0x138  w[4] W [1: AICharacter@1c870]
  +0x13c  w[1] W [1: AICharacter@1c870]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R/W [2: AICharacter@118720, NotifyZoneDamage@118eb0]
  +0x008  w[8] R/W [2: AICharacter@118720, NotifyZoneDamage@118eb0]
  +0x010  w[8] LEA/R/W addr-taken [3: AICharacter@118720, GetYAxis@119660, NotifyZoneDamage@118eb0]
  +0x018  w[8] R/W [2: AICharacter@118720, NotifyZoneDamage@118eb0]
  +0x020  w[8] LEA/R/W addr-taken [3: AICharacter@118720, GetZAxis@119668, NotifyZoneDamage@118eb0]
  +0x028  w[8] R/W [2: AICharacter@118720, NotifyZoneDamage@118eb0]
  +0x030  w[8] LEA/R/W addr-taken [4: AICharacter@118720, Execute@118af0, GetPosition@119670, NotifyZoneDamage@118eb0]
  +0x038  w[4, 8] R/W [3: AICharacter@118720, Execute@118af0, NotifyZoneDamage@118eb0]
  +0x040  w[8] LEA/W addr-taken [2: AICharacter@118720, GetCollisionBias@119680]
  +0x048  w[4] W [1: AICharacter@118720]
  +0x04c  w[4] R/W [3: AICharacter@118720, Execute@118af0, IsAlive@119638]
  +0x050  w[4] W [1: AICharacter@118720]
  +0x054  w[4] W [1: AICharacter@118720]
  +0x058  w[4] W [1: AICharacter@118720]
  +0x05c  w[4] W [1: AICharacter@118720]
  +0x060  w[4] W [1: AICharacter@118720]
  +0x064  w[4] R/W float [3: AICharacter@118720, GetTargetRangeSquared@119708, SetTargetRangeSquared@119700]
  +0x068  w[4] R/W [5: AICharacter@118720, GetHealth@1195c0, GetHitPointLoc@1195b8, MortallyWounded@1195e0, SetHitPointLoc@1195a0]
  +0x06c  w[4] R/W float [4: AICharacter@118720, ClearNewDamage@119630, GetNewDamage@119618, NotifyNewDamage@119620]
  +0x070  w[4] W float [1: SetHitPointLoc@1195a0]
  +0x074  w[4] R/W -> WWorldPos::FindClosestFace, WWorldPos::HeightAtPoint [4: AICharacter@118720, Execute@118af0, GetHumanPtr@1196d8, SetHumanPtr@1196d0]
  +0x078  w[4] W [1: AICharacter@118720]
  +0x07c  w[4] W [1: AICharacter@118720]
  +0x080  w[4] W [1: AICharacter@118720]
  +0x084  w[4] W [1: AICharacter@118720]
  +0x088  w[4] W [1: AICharacter@118720]
  +0x08c  w[4] W [1: AICharacter@118720]
  +0x090  w[4] W [1: AICharacter@118720]
  +0x094  w[4] R/W [4: AICharacter@118720, Execute@118af0, GetACharacter@118988, ~AICharacter@118900]
  +0x098  w[4] R/W [7: AICharacter@118720, GetActor@1196b8, MoveToPoint@119140, SetActor@119698, SetActor@1196a8, UpdateRotPos@119858…]
  +0x09c  w[4] W [1: AICharacter@118720]
  +0x0a0  w[4] W [1: AICharacter@118720]
  +0x0a4  w[4] R/W [4: AICharacter@118720, GetInjuryZones@119690, NotifyZoneDamage@118eb0, SetInjuryZones@119688]
  +0x0a8  w[4] R/W [2: GetZoneLastHit@1190f0, NotifyZoneDamage@118eb0]
  +0x0ac  w[4] R/W [3: AICharacter@118720, GetFireFlag@1196e8, SetFireFlag@1196e0]
  +0x0b0  w[4] R/W [3: AICharacter@118720, GetScoreable@1196f8, SetScoreable@1196f0]
  +0x0c0  w[4, 8] R/W float [3: AICharacter@118720, GetCustomTarget@119720, SetCustomTarget@119740]
  +0x0c4  w[4] W float [1: AICharacter@118720]
  +0x0c8  w[4] R/W float [3: AICharacter@118720, GetCustomTarget@119720, SetCustomTarget@119740]
  +0x0cc  w[4] R/W [3: AICharacter@118720, GetUseCustomTarget@119710, SetUseCustomTarget@119718]
  +0x0d0  w[16] W [1: AICharacter@118720]
  +0x0e0  w[16] W [1: AICharacter@118720]
  +0x0f0  w[16] W [1: AICharacter@118720]
  +0x100  w[8] W [1: Execute@118af0]
  +0x108  w[4] W [1: Execute@118af0]
  +0x110  w[4, 8] R/W float [2: AICharacter@118720, Execute@118af0]
  +0x114  w[4] R/W float [2: AICharacter@118720, Execute@118af0]
  +0x118  w[4] R/W float [2: AICharacter@118720, Execute@118af0]
  +0x11c  w[4] R/W [2: AICharacter@118720, Execute@118af0]
  +0x120  w[4] R/W [2: AICharacter@118720, Execute@118af0]
  +0x124  w[4] W float [2: AICharacter@118720, SetAvoidZoneDistance@119760]
  +0x130  w[4, 8] W float [2: AICharacter@118720, SetAvoidZoneBase@119768]
  +0x134  w[4] W float [1: AICharacter@118720]
  +0x138  w[4] W float [2: AICharacter@118720, SetAvoidZoneBase@119768]
  +0x13c  w[4] W [2: AICharacter@118720, SetUseAvoidZone@119788]
  +0x140  w[4] R/W [3: AICharacter@118720, Execute@118af0, ~AICharacter@118900]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetActor: W +0xa8 w4
  GetZoneLastHit: R +0xb8 w4
