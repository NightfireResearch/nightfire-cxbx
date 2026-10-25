# AICharacterEnemyGround

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x1df40 frees/deletes with size 0x1c0 (call to UMemory::FastFree)
Xbox vtable 0x0018a840 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemyGround virtual table']
constructor 0x1dc80 first calls: ['AICharacter::AICharacter', 'UMemory::FastAlloc', 'WTargetable::WTargetable']

Xbox methods (24):
  0x1da60 undefined DoDodging(void)
  0x1da70 undefined DoTilting(void)
  0x1da80 undefined AvoidThreat(undefined4 param_1, undefined4 param_2)
  0x1dc50 undefined GetZoneHitPointScale(void)
  0x1dc80 undefined AICharacterEnemyGround(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_
  0x1de80 undefined IsAlive(void)
  0x1dea0 undefined ~AICharacterEnemyGround(void)
  0x1df40 undefined scalar_deleting_destructor(undefined1 param_1)
  0x1df70 undefined DoInitial(void)
  0x1dfc0 undefined DoNeutral(void)
  0x1e010 undefined DoAvoiding(void)
  0x1e070 undefined DoArming(void)
  0x1e090 undefined DoArmed(void)
  0x1e380 undefined DoDying(void)
  0x1e400 undefined UpdateRotPos(void)
  0x1e440 undefined ThreatenedByVehicle(void)
  0x1e9a0 undefined GetAngleToTarget(undefined4 param_1, undefined1 param_2)
  0x1ea60 undefined PlayZoneInjuryAnim(undefined param_1, undefined4 param_2, undefined4 param_3)
  0x1ee60 undefined DoWandering(void)
  0x1f000 undefined DoAiming(void)
  0x1f0c0 undefined DoFiring(void)
  0x1f180 undefined DoRunning(void)
  0x1f3c0 undefined HandleInterrupts(void)
  0x1f8f0 undefined TargetIsAcquired(void)

PS2 methods (33):
  0x11b518 AICharacterEnemyGround::AICharacterEnemyGround
  0x11b768 AICharacterEnemyGround::~AICharacterEnemyGround
  0x11b800 AICharacterEnemyGround::DoInitial
  0x11b860 AICharacterEnemyGround::DoNeutral
  0x11b8d0 AICharacterEnemyGround::DoIdling
  0x11b908 AICharacterEnemyGround::DoWandering
  0x11bad8 AICharacterEnemyGround::DoAvoiding
  0x11bb78 AICharacterEnemyGround::DoDodging
  0x11bbb8 AICharacterEnemyGround::DoArming
  0x11bbf0 AICharacterEnemyGround::DoArmed
  0x11bf80 AICharacterEnemyGround::DoAiming
  0x11c070 AICharacterEnemyGround::DoFiring
  0x11c160 AICharacterEnemyGround::DoReloading
  0x11c198 AICharacterEnemyGround::DoUnarming
  0x11c1d0 AICharacterEnemyGround::DoHurting
  0x11c208 AICharacterEnemyGround::DoDying
  0x11c2a0 AICharacterEnemyGround::DoTilting
  0x11c2c0 AICharacterEnemyGround::DoRunning
  0x11c638 AICharacterEnemyGround::DoDead
  0x11c640 AICharacterEnemyGround::HandleInterrupts
  0x11cd10 AICharacterEnemyGround::UpdateRotPos
  0x11cd68 AICharacterEnemyGround::TargetIsAcquired
  0x11cf80 AICharacterEnemyGround::ThreatenedByVehicle
  0x11d608 AICharacterEnemyGround::AvoidThreat
  0x11d878 AICharacterEnemyGround::GetAngleToTarget
  0x11d9b0 AICharacterEnemyGround::GetZoneHitPointScale
  0x11d9f0 AICharacterEnemyGround::PlayZoneInjuryAnim
  0x11e130 AICharacterEnemyGround::SetInitialAnim
  0x11e138 AICharacterEnemyGround::SetInitialBank
  0x11e140 AICharacterEnemyGround::SetClaustrophobic
  0x11e148 AICharacterEnemyGround::SetWorker
  0x11e150 AICharacterEnemyGround::IsAlive
  0x11e178 AICharacterEnemyGround::AICharacterEnemyGround_global_ctors

Sheet rows:
  AICharacterEnemyGround::AICharacterEnemyGround(MATRIX4 &, int,
  AICharacterEnemyGround::~AICharacterEnemyGround(void)
  AICharacterEnemyGround::DoInitial(void)
  AICharacterEnemyGround::DoNeutral(void)
  AICharacterEnemyGround::DoIdling(void)
  AICharacterEnemyGround::DoWandering(void)
  AICharacterEnemyGround::DoAvoiding(void)
  AICharacterEnemyGround::DoDodging(void)
  AICharacterEnemyGround::DoArming(void)
  AICharacterEnemyGround::DoArmed(void)
  AICharacterEnemyGround::DoAiming(void)
  AICharacterEnemyGround::DoFiring(void)
  AICharacterEnemyGround::DoReloading(void)
  AICharacterEnemyGround::DoUnarming(void)
  AICharacterEnemyGround::DoHurting(void)
  AICharacterEnemyGround::DoDying(void)
  AICharacterEnemyGround::DoTilting(void)
  AICharacterEnemyGround::DoRunning(void)
  AICharacterEnemyGround::DoDead(void)
  AICharacterEnemyGround::HandleInterrupts(void)
  AICharacterEnemyGround::UpdateRotPos(void)
  AICharacterEnemyGround::TargetIsAcquired(void)
  AICharacterEnemyGround::ThreatenedByVehicle(void)
  AICharacterEnemyGround::AvoidThreat(char &, char &)
  AICharacterEnemyGround::GetAngleToTarget(COORD3 &, char)
  AICharacterEnemyGround::GetZoneHitPointScale(int, float)
  AICharacterEnemyGround::PlayZoneInjuryAnim(int, float, int)
  AICharacterEnemyGround type_info function
  AICharacterEnemyGround::SetInitialAnim(int)
  AICharacterEnemyGround::SetInitialBank(int)
  AICharacterEnemyGround::SetClaustrophobic(int)
  AICharacterEnemyGround::SetWorker(int)
  AICharacterEnemyGround::IsAlive(void)
  AICharacterEnemyGround virtual table
  AICharacterEnemyGround type_info node

Xbox methods treated as members (24 of 24; untyped ones count when ECX is read before it is written): AICharacterEnemyGround, AvoidThreat, DoAiming, DoArmed, DoArming, DoAvoiding, DoDodging, DoDying, DoFiring, DoInitial, DoNeutral, DoRunning, DoTilting, DoWandering, GetAngleToTarget, GetZoneHitPointScale, HandleInterrupts, IsAlive, PlayZoneInjuryAnim, TargetIsAcquired, ThreatenedByVehicle, UpdateRotPos, scalar_deleting_destructor, ~AICharacterEnemyGround

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [5: AICharacterEnemyGround@1dc80, DoArmed@1e090, DoDodging@1da60, DoNeutral@1dfc0, ~AICharacterEnemyGround@1dea0]
  +0x010  w- LEA addr-taken [3: AICharacterEnemyGround@1dc80, HandleInterrupts@1f3c0, UpdateRotPos@1e400]
  +0x030  w[4] R [1: GetAngleToTarget@1e9a0]
  +0x038  w[4] R -> atan_turns [1: GetAngleToTarget@1e9a0]
  +0x040  w[4] LEA/W float addr-taken [7: AvoidThreat@1da80, DoAvoiding@1e010, DoRunning@1f180, GetAngleToTarget@1e9a0, HandleInterrupts@1f3c0, TargetIsAcquired@1f8f0…]
  +0x044  w[4] W float [1: DoRunning@1f180]
  +0x048  w[4] W float [1: GetAngleToTarget@1e9a0]
  +0x05c  w[4] R/W [13: DoAiming@1f000, DoArmed@1e090, DoArming@1e070, DoAvoiding@1e010, DoDying@1e380, DoFiring@1f0c0…]
  +0x060  w[4] R/W [4: DoDodging@1da60, DoInitial@1df70, DoWandering@1ee60, HandleInterrupts@1f3c0]
  +0x064  w[4] R/W [4: DoArmed@1e090, DoNeutral@1dfc0, HandleInterrupts@1f3c0, PlayZoneInjuryAnim@1ea60]
  +0x068  w[4] R/W [7: AICharacterEnemyGround@1dc80, AvoidThreat@1da80, DoArmed@1e090, DoNeutral@1dfc0, DoWandering@1ee60, HandleInterrupts@1f3c0…]
  +0x070  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x078  w[4] R [2: HandleInterrupts@1f3c0, PlayZoneInjuryAnim@1ea60]
  +0x07c  w[4] W float [1: HandleInterrupts@1f3c0]
  +0x080  w[4] W float [1: HandleInterrupts@1f3c0]
  +0x084  w[4] R [1: HandleInterrupts@1f3c0]
  +0x088  w[4] R/W [6: AICharacterEnemyGround@1dc80, DoArmed@1e090, DoNeutral@1dfc0, DoWandering@1ee60, HandleInterrupts@1f3c0, PlayZoneInjuryAnim@1ea60]
  +0x08c  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x090  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x094  w[4] W [1: TargetIsAcquired@1f8f0]
  +0x098  w[4] R/W [1: TargetIsAcquired@1f8f0]
  +0x0a8  w[4] R [14: DoAiming@1f000, DoArmed@1e090, DoArming@1e070, DoAvoiding@1e010, DoDying@1e380, DoFiring@1f0c0…]
  +0x0ac  w[4] R -> AnimationController::GetCurrentFrame [1: DoWandering@1ee60]
  +0x0b0  w[4] R/W [5: DoArmed@1e090, DoNeutral@1dfc0, DoRunning@1f180, HandleInterrupts@1f3c0, PlayZoneInjuryAnim@1ea60]
  +0x0b4  w[4] R/W [2: AICharacterEnemyGround@1dc80, AvoidThreat@1da80]
  +0x0b8  w[4] R/W [1: PlayZoneInjuryAnim@1ea60]
  +0x0bd  w[1] R/W [2: DoDying@1e380, HandleInterrupts@1f3c0]
  +0x0c0  w- LEA addr-taken [3: DoAiming@1f000, DoFiring@1f0c0, TargetIsAcquired@1f8f0]
  +0x0cc  w[1] R [3: DoAiming@1f000, DoFiring@1f0c0, TargetIsAcquired@1f8f0]
  +0x0d0  w- LEA addr-taken [1: HandleInterrupts@1f3c0]
  +0x100  w- LEA addr-taken [1: DoTilting@1da70]
  +0x110  w- LEA addr-taken [1: HandleInterrupts@1f3c0]
  +0x11c  w[1] R/W [2: HandleInterrupts@1f3c0, UpdateRotPos@1e400]
  +0x120  w[4] W float [1: DoAvoiding@1e010]
  +0x130  w- LEA addr-taken [1: DoAvoiding@1e010]
  +0x13c  w[1] R [2: AvoidThreat@1da80, DoAvoiding@1e010]
  +0x140  w[4] R/W -> WTargetable::RemoveReference [3: AICharacterEnemyGround@1dc80, PlayZoneInjuryAnim@1ea60, ~AICharacterEnemyGround@1dea0]
  +0x144  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x148  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x14c  w[4] R/W [2: AICharacterEnemyGround@1dc80, DoRunning@1f180]
  +0x150  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x160  w[4] LEA/W float addr-taken [2: AICharacterEnemyGround@1dc80, DoRunning@1f180]
  +0x164  w[4] W float [1: DoRunning@1f180]
  +0x168  w[4] R [1: DoRunning@1f180]
  +0x16c  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x170  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x174  w[4] W [2: AICharacterEnemyGround@1dc80, TargetIsAcquired@1f8f0]
  +0x17c  w[4] R/W [2: AICharacterEnemyGround@1dc80, HandleInterrupts@1f3c0]
  +0x180  w[4] R/W [2: AICharacterEnemyGround@1dc80, PlayZoneInjuryAnim@1ea60]
  +0x184  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x188  w[4] W [1: AICharacterEnemyGround@1dc80]
  +0x18c  w[4] R/W [3: AICharacterEnemyGround@1dc80, AvoidThreat@1da80, DoAvoiding@1e010]
  +0x190  w[4] W float [2: AICharacterEnemyGround@1dc80, HandleInterrupts@1f3c0]
  +0x1a0  w- LEA addr-taken [2: AICharacterEnemyGround@1dc80, HandleInterrupts@1f3c0]
  +0x1ac  w[4] R/W [2: AICharacterEnemyGround@1dc80, DoInitial@1df70]
  +0x1b0  w[4] R/W -> WCollider::GetWorldNormal, WCollider::InRegion, WCollider::~WCollider [4: AICharacterEnemyGround@1dc80, DoRunning@1f180, HandleInterrupts@1f3c0, ~AICharacterEnemyGround@1dea0]
  +0x1b4  w[4] W [1: AICharacterEnemyGround@1dc80]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8, 16] R/W [3: AICharacterEnemyGround@11b518, HandleInterrupts@11c640, UpdateRotPos@11cd10]
  +0x008  w[8] W [1: AICharacterEnemyGround@11b518]
  +0x010  w[8, 16] R/W [3: AICharacterEnemyGround@11b518, HandleInterrupts@11c640, UpdateRotPos@11cd10]
  +0x018  w[8] W [1: AICharacterEnemyGround@11b518]
  +0x020  w[4, 8, 16] LEA/R/W float addr-taken [5: AICharacterEnemyGround@11b518, GetAngleToTarget@11d878, HandleInterrupts@11c640, ThreatenedByVehicle@11cf80, UpdateRotPos@11cd10]
  +0x028  w[4, 8] R/W float [2: AICharacterEnemyGround@11b518, GetAngleToTarget@11d878]
  +0x030  w[4, 8, 16] LEA/R/W float addr-taken [6: AICharacterEnemyGround@11b518, AvoidThreat@11d608, DoRunning@11c2c0, GetAngleToTarget@11d878, HandleInterrupts@11c640, UpdateRotPos@11cd10]
  +0x038  w[4, 8] R/W float [4: AICharacterEnemyGround@11b518, AvoidThreat@11d608, GetAngleToTarget@11d878, HandleInterrupts@11c640]
  +0x04c  w[4] R/W [16: DoAiming@11bf80, DoArmed@11bbf0, DoArming@11bbb8, DoAvoiding@11bad8, DoDying@11c208, DoFiring@11c070…]
  +0x050  w[4] R/W [4: DoDodging@11bb78, DoInitial@11b800, DoWandering@11b908, HandleInterrupts@11c640]
  +0x054  w[4] R/W [7: DoArmed@11bbf0, DoInitial@11b800, DoNeutral@11b860, DoRunning@11c2c0, HandleInterrupts@11c640, PlayZoneInjuryAnim@11d9f0…]
  +0x058  w[4] R/W [7: AICharacterEnemyGround@11b518, AvoidThreat@11d608, DoArmed@11bbf0, DoNeutral@11b860, DoRunning@11c2c0, HandleInterrupts@11c640…]
  +0x060  w[4] W [1: AICharacterEnemyGround@11b518]
  +0x068  w[4] R [2: HandleInterrupts@11c640, PlayZoneInjuryAnim@11d9f0]
  +0x06c  w[4] R/W float [1: HandleInterrupts@11c640]
  +0x070  w[4] R/W float [1: HandleInterrupts@11c640]
  +0x074  w[4] R -> WCollider::~WCollider, WWorldPos::FindClosestFace, WWorldPos::HeightAtPoint [1: HandleInterrupts@11c640]
  +0x078  w[4] R/W [6: AICharacterEnemyGround@11b518, DoArmed@11bbf0, DoNeutral@11b860, DoRunning@11c2c0, HandleInterrupts@11c640, PlayZoneInjuryAnim@11d9f0]
  +0x07c  w[4] R/W [2: AICharacterEnemyGround@11b518, DoArmed@11bbf0]
  +0x080  w[4] R/W [2: AICharacterEnemyGround@11b518, DoArmed@11bbf0]
  +0x084  w[4] R/W [3: DoArmed@11bbf0, TargetIsAcquired@11cd68, ThreatenedByVehicle@11cf80]
  +0x088  w[4] R/W [3: DoArmed@11bbf0, TargetIsAcquired@11cd68, ThreatenedByVehicle@11cf80]
  +0x08c  w[4] R [1: DoArmed@11bbf0]
  +0x090  w[4] R/W [1: DoArmed@11bbf0]
  +0x098  w[4] R [18: DoAiming@11bf80, DoArmed@11bbf0, DoArming@11bbb8, DoAvoiding@11bad8, DoDying@11c208, DoFiring@11c070…]
  +0x0a0  w[4] R/W [5: DoArmed@11bbf0, DoNeutral@11b860, DoRunning@11c2c0, HandleInterrupts@11c640, PlayZoneInjuryAnim@11d9f0]
  +0x0a4  w[4] R/W [2: AICharacterEnemyGround@11b518, AvoidThreat@11d608]
  +0x0a8  w[4] R/W [1: PlayZoneInjuryAnim@11d9f0]
  +0x0ac  w[4] R [1: DoArmed@11bbf0]
  +0x0b0  w[4] R/W [2: DoDying@11c208, HandleInterrupts@11c640]
  +0x0c0  w[8] R [3: DoAiming@11bf80, DoFiring@11c070, TargetIsAcquired@11cd68]
  +0x0c8  w[4] R [3: DoAiming@11bf80, DoFiring@11c070, TargetIsAcquired@11cd68]
  +0x0cc  w[4] R [3: DoAiming@11bf80, DoFiring@11c070, TargetIsAcquired@11cd68]
  +0x0d0  w[16] LEA/W addr-taken [2: HandleInterrupts@11c640, UpdateRotPos@11cd10]
  +0x0e0  w[16] W [1: HandleInterrupts@11c640]
  +0x0f0  w[16] W [1: HandleInterrupts@11c640]
  +0x100  w[16] W [1: HandleInterrupts@11c640]
  +0x110  w[8] W [1: HandleInterrupts@11c640]
  +0x118  w[4] W [1: HandleInterrupts@11c640]
  +0x11c  w[4] R/W [2: HandleInterrupts@11c640, UpdateRotPos@11cd10]
  +0x124  w[4] R float [1: DoAvoiding@11bad8]
  +0x13c  w[4] R [2: AvoidThreat@11d608, DoAvoiding@11bad8]
  +0x140  w[4] R/W [5: AICharacterEnemyGround@11b518, DoArmed@11bbf0, DoDodging@11bb78, DoNeutral@11b860, ~AICharacterEnemyGround@11b768]
  +0x150  w[4] R/W -> WCollider::~WCollider [3: AICharacterEnemyGround@11b518, PlayZoneInjuryAnim@11d9f0, ~AICharacterEnemyGround@11b768]
  +0x154  w[4] W [1: AICharacterEnemyGround@11b518]
  +0x158  w[4] W [1: AICharacterEnemyGround@11b518]
  +0x15c  w[4] R/W [3: AICharacterEnemyGround@11b518, DoRunning@11c2c0, ThreatenedByVehicle@11cf80]
  +0x160  w[4] R/W float [2: AICharacterEnemyGround@11b518, ThreatenedByVehicle@11cf80]
  +0x170  w[4, 8] R/W float [3: AICharacterEnemyGround@11b518, DoRunning@11c2c0, ThreatenedByVehicle@11cf80]
  +0x174  w[4] R float [1: DoRunning@11c2c0]
  +0x178  w[4] R/W float [3: AICharacterEnemyGround@11b518, DoRunning@11c2c0, ThreatenedByVehicle@11cf80]
  +0x17c  w[4] R/W float [2: AICharacterEnemyGround@11b518, ThreatenedByVehicle@11cf80]
  +0x180  w[4] W [1: AICharacterEnemyGround@11b518]
  +0x184  w[4] R/W [3: AICharacterEnemyGround@11b518, DoArmed@11bbf0, TargetIsAcquired@11cd68]
  +0x188  w[4] W [1: SetInitialBank@11e138]
  +0x18c  w[4] R/W [2: AICharacterEnemyGround@11b518, HandleInterrupts@11c640]
  +0x190  w[4] R/W [3: AICharacterEnemyGround@11b518, PlayZoneInjuryAnim@11d9f0, SetClaustrophobic@11e140]
  +0x194  w[4] R/W [2: AICharacterEnemyGround@11b518, DoArmed@11bbf0]
  +0x198  w[4, 8] R/W [2: AICharacterEnemyGround@11b518, DoArmed@11bbf0]
  +0x19c  w[4] R/W [4: AICharacterEnemyGround@11b518, AvoidThreat@11d608, DoArmed@11bbf0, DoAvoiding@11bad8]
  +0x1a0  w[4] R/W float [2: AICharacterEnemyGround@11b518, HandleInterrupts@11c640]
  +0x1b0  w[8] R/W [2: AICharacterEnemyGround@11b518, HandleInterrupts@11c640]
  +0x1b8  w[4] R/W [2: AICharacterEnemyGround@11b518, HandleInterrupts@11c640]
  +0x1bc  w[4] R/W [3: AICharacterEnemyGround@11b518, DoInitial@11b800, SetWorker@11e148]
  +0x1c0  w[4] R/W -> WCollider::InRegion, WCollider::~WCollider [4: AICharacterEnemyGround@11b518, DoRunning@11c2c0, HandleInterrupts@11c640, ~AICharacterEnemyGround@11b768]
  +0x1c4  w[4] R/W [2: AICharacterEnemyGround@11b518, DoArmed@11bbf0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  DoTilting: LEA +0x100 w0
