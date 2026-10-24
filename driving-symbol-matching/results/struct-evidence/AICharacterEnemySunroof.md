# AICharacterEnemySunroof

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x21230 frees/deletes with size 0x170 (call to UMemory::FastFree)
Xbox vtable 0x0018aac0 (36 slots) stored by its constructor
PS2 sheet virtual table row: ['AICharacterEnemySunroof virtual table']
constructor 0x210f0 first calls: ['AICharacter::AICharacter', 'Simulation::GetRigidBody', 'UMemory::FastAlloc']

Xbox methods (14):
  0x20ec0 undefined ~AICharacterEnemySunroof(void)
  0x210f0 undefined AICharacterEnemySunroof(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param
  0x21230 undefined scalar_deleting_destructor(undefined1 param_1)
  0x21260 undefined DoInitial(void)
  0x21300 undefined DoNeutral(void)
  0x213c0 undefined DoArmed(void)
  0x21630 undefined DoUnarming(void)
  0x21670 undefined DoDying(void)
  0x21710 undefined DoRunning(void)
  0x21740 undefined DoLeaning(void)
  0x217e0 undefined HandleInterrupts(void)
  0x218f0 undefined TargetIsInRange(void)
  0x219e0 undefined TargetIsAcquired(void)
  0x21b70 undefined PlayZoneInjuryAnim(undefined param_1, undefined4 param_2)

PS2 methods (26):
  0x1208f8 AICharacterEnemySunroof::GetRotPos
  0x120a18 AICharacterEnemySunroof::GetRotPos
  0x120b70 AICharacterEnemySunroof::AICharacterEnemySunroof
  0x120d18 AICharacterEnemySunroof::~AICharacterEnemySunroof
  0x120d98 AICharacterEnemySunroof::DoInitial
  0x120e80 AICharacterEnemySunroof::DoNeutral
  0x120f90 AICharacterEnemySunroof::DoArming
  0x120fe0 AICharacterEnemySunroof::DoArmed
  0x121250 AICharacterEnemySunroof::DoAiming
  0x1212d8 AICharacterEnemySunroof::DoFiring
  0x121328 AICharacterEnemySunroof::DoReloading
  0x121378 AICharacterEnemySunroof::DoUnarming
  0x1213c8 AICharacterEnemySunroof::DoHurting
  0x121400 AICharacterEnemySunroof::DoDying
  0x1214d0 AICharacterEnemySunroof::DoTilting
  0x121568 AICharacterEnemySunroof::DoRunning
  0x1215a8 AICharacterEnemySunroof::DoLeaning
  0x121678 AICharacterEnemySunroof::DoDead
  0x121680 AICharacterEnemySunroof::HandleInterrupts
  0x1217c8 AICharacterEnemySunroof::UpdateRotPos
  0x121810 AICharacterEnemySunroof::GetVehiclePtr
  0x121818 AICharacterEnemySunroof::TargetIsInRange
  0x1219c0 AICharacterEnemySunroof::TargetIsAcquired
  0x121c10 AICharacterEnemySunroof::VehicleIsAccelerating
  0x121dd8 AICharacterEnemySunroof::PlayZoneInjuryAnim
  0x122210 AICharacterEnemySunroof::GetCarPtr

Sheet rows:
  AICharacterEnemySunroof::GetRotPos(int, MATRIX4 &)
  AICharacterEnemySunroof::GetRotPos(int, MATRIX4 &, COORD3 &)
  AICharacterEnemySunroof::AICharacterEnemySunroof(MATRIX4 &, int
  AICharacterEnemySunroof::~AICharacterEnemySunroof(void)
  AICharacterEnemySunroof::DoInitial(void)
  AICharacterEnemySunroof::DoNeutral(void)
  AICharacterEnemySunroof::DoArming(void)
  AICharacterEnemySunroof::DoArmed(void)
  AICharacterEnemySunroof::DoAiming(void)
  AICharacterEnemySunroof::DoFiring(void)
  AICharacterEnemySunroof::DoReloading(void)
  AICharacterEnemySunroof::DoUnarming(void)
  AICharacterEnemySunroof::DoHurting(void)
  AICharacterEnemySunroof::DoDying(void)
  AICharacterEnemySunroof::DoTilting(void)
  AICharacterEnemySunroof::DoRunning(void)
  AICharacterEnemySunroof::DoLeaning(void)
  AICharacterEnemySunroof::DoDead(void)
  AICharacterEnemySunroof::HandleInterrupts(void)
  AICharacterEnemySunroof::UpdateRotPos(void)
  AICharacterEnemySunroof::GetVehiclePtr(void)
  AICharacterEnemySunroof::TargetIsInRange(void)
  AICharacterEnemySunroof::TargetIsAcquired(void)
  AICharacterEnemySunroof::VehicleIsAccelerating(void)
  AICharacterEnemySunroof::PlayZoneInjuryAnim(int, float, int)
  AICharacterEnemySunroof type_info function
  AICharacterEnemySunroof::GetCarPtr(void)
  AICharacterEnemySunroof virtual table
  AICharacterEnemySunroof type_info node

Xbox methods treated as members (14 of 14; untyped ones count when ECX is read before it is written): AICharacterEnemySunroof, DoArmed, DoDying, DoInitial, DoLeaning, DoNeutral, DoRunning, DoUnarming, HandleInterrupts, PlayZoneInjuryAnim, TargetIsAcquired, TargetIsInRange, scalar_deleting_destructor, ~AICharacterEnemySunroof

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [7: AICharacterEnemySunroof@210f0, DoArmed@213c0, DoDying@21670, DoNeutral@21300, TargetIsAcquired@219e0, TargetIsInRange@218f0…]
  +0x010  w- LEA addr-taken [2: AICharacterEnemySunroof@210f0, TargetIsAcquired@219e0]
  +0x040  w- LEA addr-taken [1: TargetIsInRange@218f0]
  +0x05c  w[4] R/W [8: DoArmed@213c0, DoDying@21670, DoInitial@21260, DoNeutral@21300, DoRunning@21710, DoUnarming@21630…]
  +0x060  w[4] R/W [3: DoArmed@213c0, DoRunning@21710, HandleInterrupts@217e0]
  +0x064  w[4] W [4: DoArmed@213c0, DoLeaning@21740, DoNeutral@21300, PlayZoneInjuryAnim@21b70]
  +0x068  w[4] R/W [6: AICharacterEnemySunroof@210f0, DoArmed@213c0, DoLeaning@21740, DoNeutral@21300, PlayZoneInjuryAnim@21b70, TargetIsAcquired@219e0]
  +0x070  w[4] W [1: AICharacterEnemySunroof@210f0]
  +0x074  w[4] W float [1: TargetIsInRange@218f0]
  +0x078  w[4] R [1: PlayZoneInjuryAnim@21b70]
  +0x084  w[4] R -> Human::Activate, Human::Deactivate [4: DoDying@21670, DoInitial@21260, DoNeutral@21300, DoUnarming@21630]
  +0x088  w[4] R/W [5: AICharacterEnemySunroof@210f0, DoArmed@213c0, DoLeaning@21740, DoNeutral@21300, PlayZoneInjuryAnim@21b70]
  +0x08c  w[4] W [1: AICharacterEnemySunroof@210f0]
  +0x090  w[4] W [1: AICharacterEnemySunroof@210f0]
  +0x094  w[4] W [1: TargetIsAcquired@219e0]
  +0x0a8  w[4] R [10: DoArmed@213c0, DoDying@21670, DoInitial@21260, DoLeaning@21740, DoNeutral@21300, DoRunning@21710…]
  +0x0b0  w[4] R/W [4: DoArmed@213c0, DoLeaning@21740, DoNeutral@21300, PlayZoneInjuryAnim@21b70]
  +0x0b4  w[4] W [1: AICharacterEnemySunroof@210f0]
  +0x0b8  w[4] R [1: PlayZoneInjuryAnim@21b70]
  +0x0bd  w[1] R/W [1: DoDying@21670]
  +0x0c0  w- LEA addr-taken [1: TargetIsAcquired@219e0]
  +0x0cc  w[1] W [1: TargetIsAcquired@219e0]
  +0x140  w[4] R/W -> PhysicsObject::GetHitPoints [7: AICharacterEnemySunroof@210f0, DoArmed@213c0, DoDying@21670, DoInitial@21260, DoNeutral@21300, HandleInterrupts@217e0…]
  +0x150  w- LEA addr-taken [1: AICharacterEnemySunroof@210f0]
  +0x15c  w[4] R/W [3: AICharacterEnemySunroof@210f0, DoArmed@213c0, DoLeaning@21740]
  +0x160  w[4] R/W [3: AICharacterEnemySunroof@210f0, DoInitial@21260, TargetIsInRange@218f0]
  +0x164  w[4] R/W [3: AICharacterEnemySunroof@210f0, DoInitial@21260, TargetIsInRange@218f0]
  +0x168  w[4] R/W -> WTargetable::RemoveReference [4: AICharacterEnemySunroof@210f0, DoDying@21670, DoNeutral@21300, ~AICharacterEnemySunroof@20ec0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x008  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x010  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x018  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x020  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x028  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x030  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x038  w[8] W [1: AICharacterEnemySunroof@120b70]
  +0x04c  w[4] R/W [12: DoArmed@120fe0, DoArming@120f90, DoDying@121400, DoFiring@1212d8, DoHurting@1213c8, DoInitial@120d98…]
  +0x050  w[4] R/W [5: DoArmed@120fe0, DoLeaning@1215a8, DoRunning@121568, DoTilting@1214d0, HandleInterrupts@121680]
  +0x054  w[4] W [5: DoArmed@120fe0, DoLeaning@1215a8, DoNeutral@120e80, HandleInterrupts@121680, PlayZoneInjuryAnim@121dd8]
  +0x058  w[4] R/W [7: AICharacterEnemySunroof@120b70, DoArmed@120fe0, DoLeaning@1215a8, DoNeutral@120e80, HandleInterrupts@121680, PlayZoneInjuryAnim@121dd8…]
  +0x060  w[4] W [1: AICharacterEnemySunroof@120b70]
  +0x064  w[4] R float [1: TargetIsInRange@121818]
  +0x068  w[4] R [2: HandleInterrupts@121680, PlayZoneInjuryAnim@121dd8]
  +0x06c  w[4] R/W float [1: HandleInterrupts@121680]
  +0x070  w[4] R/W float [1: HandleInterrupts@121680]
  +0x074  w[4] R [4: DoDying@121400, DoInitial@120d98, DoNeutral@120e80, DoUnarming@121378]
  +0x078  w[4] R/W [6: AICharacterEnemySunroof@120b70, DoArmed@120fe0, DoLeaning@1215a8, DoNeutral@120e80, HandleInterrupts@121680, PlayZoneInjuryAnim@121dd8]
  +0x07c  w[4] R/W [2: AICharacterEnemySunroof@120b70, DoArmed@120fe0]
  +0x080  w[4] R/W [2: AICharacterEnemySunroof@120b70, DoArmed@120fe0]
  +0x084  w[4] R/W [2: DoAiming@121250, TargetIsAcquired@1219c0]
  +0x08c  w[4] R [1: DoArmed@120fe0]
  +0x090  w[4] R/W [1: DoArmed@120fe0]
  +0x098  w[4] R -> Human::Deactivate [15: DoAiming@121250, DoArmed@120fe0, DoArming@120f90, DoDying@121400, DoFiring@1212d8, DoHurting@1213c8…]
  +0x0a0  w[4] R/W [5: DoArmed@120fe0, DoLeaning@1215a8, DoNeutral@120e80, HandleInterrupts@121680, PlayZoneInjuryAnim@121dd8]
  +0x0a4  w[4] W [1: AICharacterEnemySunroof@120b70]
  +0x0a8  w[4] R [1: PlayZoneInjuryAnim@121dd8]
  +0x0ac  w[4] R [1: DoArmed@120fe0]
  +0x0b0  w[4] R/W [1: DoDying@121400]
  +0x0c0  w[8] W [1: TargetIsAcquired@1219c0]
  +0x0c8  w[4] W [1: TargetIsAcquired@1219c0]
  +0x0cc  w[4] W [1: TargetIsAcquired@1219c0]
  +0x140  w[4] R/W [12: AICharacterEnemySunroof@120b70, DoAiming@121250, DoArmed@120fe0, DoArming@120f90, DoDying@121400, DoFiring@1212d8…]
  +0x150  w[4] R/W [13: AICharacterEnemySunroof@120b70, DoArmed@120fe0, DoDying@121400, DoInitial@120d98, DoNeutral@120e80, DoTilting@1214d0…]
  +0x160  w[8] W [2: AICharacterEnemySunroof@120b70, DoTilting@1214d0]
  +0x168  w[4] W [2: AICharacterEnemySunroof@120b70, DoTilting@1214d0]
  +0x16c  w[4] R/W [4: AICharacterEnemySunroof@120b70, DoArmed@120fe0, DoLeaning@1215a8, VehicleIsAccelerating@121c10]
  +0x170  w[4] R/W [3: AICharacterEnemySunroof@120b70, DoInitial@120d98, TargetIsInRange@121818]
  +0x174  w[4] R/W [3: AICharacterEnemySunroof@120b70, DoInitial@120d98, TargetIsInRange@121818]
  +0x178  w[4] R/W -> AICharacter::~AICharacter [5: AICharacterEnemySunroof@120b70, DoArmed@120fe0, DoDying@121400, DoNeutral@120e80, ~AICharacterEnemySunroof@120d18]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
