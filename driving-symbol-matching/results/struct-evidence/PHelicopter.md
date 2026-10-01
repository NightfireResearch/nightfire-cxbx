# PHelicopter

FastAlloc/constructed sizes under its tag: {'allocated': [320], 'constructed': []}
deleting destructor 0x6e300 frees/deletes with size 0x140 (call to UMemory::FastFree)
Xbox vtable 0x0018f8bc (7 slots) stored by its constructor
PS2 sheet virtual table row: ['PHelicopter virtual table']
constructor 0x6d8f0 first calls: ['PhysicsObject::PhysicsObject', 'v3scale', 'Util_GenerateMatrix']

Xbox methods (7):
  0x6d8f0 undefined PHelicopter(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0x6dc30 undefined GetControllerInput(void)
  0x6dd10 undefined GetDamageZones(void)
  0x6dd30 undefined Simulate(void)
  0x6e290 undefined ~PHelicopter(void)
  0x6e300 undefined scalar_deleting_destructor(undefined1 param_1)
  0x6e330 undefined ApplyDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine

PS2 methods (11):
  0x193408 PHelicopter::PHelicopter
  0x1937b0 PHelicopter::~PHelicopter
  0x193818 PHelicopter::GetControllerInput
  0x193938 PHelicopter::ApplyDamage
  0x1942d8 PHelicopter::GetDamageZones
  0x1942e8 PHelicopter::Simulate
  0x194b50 PHelicopter::operator_new
  0x194b70 PHelicopter::operator_delete
  0x194b98 PHelicopter::SetTargetPos
  0x194bd8 PHelicopter::SetDestPos
  0x194cb0 PHelicopter::PHelicopter_global_ctors

Sheet rows:
  PHelicopter::PHelicopter(char *, float, COORD3, COORD3)
  PHelicopter::~PHelicopter(void)
  PHelicopter::GetControllerInput(void)
  PHelicopter::ApplyDamage(COORD3 &, COORD3 &, float, float, Dama
  PHelicopter::GetDamageZones(unsigned int &)
  PHelicopter::Simulate(void)
  PHelicopter type_info function
  PHelicopter::operator new(unsigned int)
  PHelicopter::operator delete(void *, unsigned int)
  PHelicopter::GetHeliClass(void)
  PHelicopter::SetTargetPos(COORD3)
  PHelicopter::SetDestPos(COORD3)
  PHelicopter::EnablePhysics(void)
  PHelicopter::DisablePhysics(void)
  PHelicopter::GetRoll(void)
  PHelicopter::SetRoll(float)
  PHelicopter::GetControlGas(void)
  PHelicopter::SetControlGas(float)
  PHelicopter::GetControlStrafe(void)
  PHelicopter::SetControlStrafe(float)
  PHelicopter::GetControlSteer(void)
  PHelicopter::SetControlSteer(float)
  PHelicopter::GetControlAltitude(void)
  PHelicopter::SetControlAltitude(float)
  PHelicopter::GetAIHelicopterPtr(void)
  PHelicopter::SetAIHelicopterPtr(AIHelicopter *)
  PHelicopter::SetScoreable(bool)
  PHelicopter::GetScoreable(void)
  PHelicopter::GetProximityDestructEnabled(void)
  PHelicopter::GetDestructDistance(void)
  PHelicopter ** find<PHelicopter **, PHelicopter *>(PHelicopter
  PHelicopter ** remove_copy<PHelicopter **, PHelicopter **, PHel
  PHelicopter ** remove<PHelicopter **, PHelicopter *>(PHelicopte
  PHelicopter virtual table
  PHelicopter type_info node

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): ApplyDamage, GetControllerInput, GetDamageZones, PHelicopter, Simulate, scalar_deleting_destructor, ~PHelicopter

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: PHelicopter@6d8f0, ~PHelicopter@6e290]
  +0x04a  w[2] R [3: ApplyDamage@6e330, PHelicopter@6d8f0, Simulate@6dd30]
  +0x04c  w[4] R -> RSceneObj::ScaleBoundingRadius [3: ApplyDamage@6e330, PHelicopter@6d8f0, Simulate@6dd30]
  +0x050  w[4] R [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x060  w- LEA addr-taken [1: PHelicopter@6d8f0]
  +0x06c  w[4] R/W [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x070  w[4] W float [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x074  w[4] W float [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x078  w[4] W [1: PHelicopter@6d8f0]
  +0x07c  w- LEA addr-taken [1: PHelicopter@6d8f0]
  +0x088  w- LEA addr-taken [1: PHelicopter@6d8f0]
  +0x094  w[4] W float [3: GetControllerInput@6dc30, PHelicopter@6d8f0, Simulate@6dd30]
  +0x098  w[4] R/W float [3: GetControllerInput@6dc30, PHelicopter@6d8f0, Simulate@6dd30]
  +0x09c  w[4] W float [3: GetControllerInput@6dc30, PHelicopter@6d8f0, Simulate@6dd30]
  +0x0a0  w[4] R/W float [3: GetControllerInput@6dc30, PHelicopter@6d8f0, Simulate@6dd30]
  +0x0a4  w[4] R/W -> ActionQueue::GetAction, ActionQueue::IsEmpty, ActionQueue::PopAction, ActionQueue::~ActionQueue [3: GetControllerInput@6dc30, PHelicopter@6d8f0, ~PHelicopter@6e290]
  +0x0a8  w[1] R/W [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x0ac  w- LEA addr-taken [2: GetDamageZones@6dd10, PHelicopter@6d8f0]
  +0x12c  w[4] R/W [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x130  w[4] W [1: PHelicopter@6d8f0]
  +0x134  w[4] R/W [2: PHelicopter@6d8f0, Simulate@6dd30]
  +0x138  w[1] W [1: PHelicopter@6d8f0]
  +0x139  w[1] R/W [1: PHelicopter@6d8f0]
  +0x13c  w[4] W float [1: PHelicopter@6d8f0]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [3: ApplyDamage@193938, PHelicopter@193408, Simulate@1942e8]
  +0x048  w[4] R [3: ApplyDamage@193938, PHelicopter@193408, Simulate@1942e8]
  +0x04c  w[4] R [3: ApplyDamage@193938, PHelicopter@193408, Simulate@1942e8]
  +0x05c  w- LEA addr-taken [1: PHelicopter@193408]
  +0x068  w[4] W [2: PHelicopter@193408, ~PHelicopter@1937b0]
  +0x06c  w[4] R/W [2: PHelicopter@193408, Simulate@1942e8]
  +0x070  w[4] R/W float [2: PHelicopter@193408, Simulate@1942e8]
  +0x074  w[4] R/W float [2: PHelicopter@193408, Simulate@1942e8]
  +0x078  w[4] W float [1: PHelicopter@193408]
  +0x07c  w[8] W [2: PHelicopter@193408, SetTargetPos@194b98]
  +0x084  w[4] W [2: PHelicopter@193408, SetTargetPos@194b98]
  +0x088  w[8] W [2: PHelicopter@193408, SetDestPos@194bd8]
  +0x090  w[4] W [2: PHelicopter@193408, SetDestPos@194bd8]
  +0x094  w[4] R/W float [3: GetControllerInput@193818, PHelicopter@193408, Simulate@1942e8]
  +0x098  w[4] R/W float [3: GetControllerInput@193818, PHelicopter@193408, Simulate@1942e8]
  +0x09c  w[4] R/W float [3: GetControllerInput@193818, PHelicopter@193408, Simulate@1942e8]
  +0x0a0  w[4] R/W float [3: GetControllerInput@193818, PHelicopter@193408, Simulate@1942e8]
  +0x0a4  w[4] R/W -> ActionQueue::IsEmpty, ActionQueue::~ActionQueue [3: GetControllerInput@193818, PHelicopter@193408, ~PHelicopter@1937b0]
  +0x0a8  w[1] R/W [2: PHelicopter@193408, Simulate@1942e8]
  +0x0ac  w- LEA addr-taken [3: ApplyDamage@193938, GetDamageZones@1942d8, PHelicopter@193408]
  +0x0b0  w- LEA addr-taken [1: ApplyDamage@193938]
  +0x12c  w[4] R/W [3: ApplyDamage@193938, PHelicopter@193408, Simulate@1942e8]
  +0x130  w[4] R/W float [2: ApplyDamage@193938, PHelicopter@193408]
  +0x134  w[4] R/W [3: ApplyDamage@193938, PHelicopter@193408, Simulate@1942e8]
  +0x138  w[4] R/W [2: ApplyDamage@193938, PHelicopter@193408]
  +0x13c  w[4] R/W [1: PHelicopter@193408]
  +0x140  w[4] W float [1: PHelicopter@193408]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetDamageZones: LEA +0xac w0
