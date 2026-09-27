# RDirectorQueue

FastAlloc/constructed sizes under its tag: {'allocated': [20], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x7c380 first calls: ['FUN_0007c2c0']

Xbox methods (5):
  0x7c380 undefined RDirectorQueue(undefined4 param_1)
  0x7c3b0 undefined ~RDirectorQueue(void)
  0x7c3f0 undefined ProcessDirectorLogic(void)
  0x7c5e0 undefined RestartDirectorQueue(void)
  0x7c6a0 undefined AppendData(undefined4 param_1)

PS2 methods (5):
  0x1aa7b8 RDirectorQueue::RDirectorQueue
  0x1aa820 RDirectorQueue::~RDirectorQueue
  0x1aa870 RDirectorQueue::RestartDirectorQueue
  0x1aa890 RDirectorQueue::ProcessDirectorLogic
  0x1aab80 RDirectorQueue::AppendData

Sheet rows:
  RDirectorQueue::RDirectorQueue(RPlayerCamera *)
  RDirectorQueue::~RDirectorQueue(void)
  RDirectorQueue::RestartDirectorQueue(void)
  RDirectorQueue::ProcessDirectorLogic(void)
  RDirectorQueue::AppendData(RDirectorQueueData &)

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): AppendData, ProcessDirectorLogic, RDirectorQueue, RestartDirectorQueue, ~RDirectorQueue

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [4: AppendData@7c6a0, ProcessDirectorLogic@7c3f0, RDirectorQueue@7c380, ~RDirectorQueue@7c3b0]
  +0x008  w[4] R/W [3: ProcessDirectorLogic@7c3f0, RDirectorQueue@7c380, ~RDirectorQueue@7c3b0]
  +0x00c  w[4] R/W -> RPlayerCamera::DirectorChangeCameraMode, RPlayerCamera::DirectorSetAnchor, RPlayerCamera::GetMaxTumble, RPlayerCamera::S [2: ProcessDirectorLogic@7c3f0, RDirectorQueue@7c380]
  +0x010  w[1] R/RW/W [3: ProcessDirectorLogic@7c3f0, RDirectorQueue@7c380, RestartDirectorQueue@7c5e0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> UMemory::FastFree [4: AppendData@1aab80, ProcessDirectorLogic@1aa890, RDirectorQueue@1aa7b8, ~RDirectorQueue@1aa820]
  +0x004  w[4] R/W -> RPlayerCamera::DirectorChangeCameraMode, RPlayerCamera::DirectorSetAnchor, RPlayerCamera::SetTumbleCam [2: ProcessDirectorLogic@1aa890, RDirectorQueue@1aa7b8]
  +0x008  w[1] R/W [2: ProcessDirectorLogic@1aa890, RDirectorQueue@1aa7b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  RestartDirectorQueue: W +0x10 w1
