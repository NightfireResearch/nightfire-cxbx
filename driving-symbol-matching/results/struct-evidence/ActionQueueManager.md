# ActionQueueManager

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (5):
  0x4f360 undefined FeedQueue(undefined4 param_1)
  0x4f3b0 void __fastcall FlushAllQueues(int this)
  0x4f440 undefined UnRegisterQueue(undefined4 param_1)
  0x4f7d0 undefined4 * __stdcall GetActionQueueManager(void)
  0x4f8a0 undefined RegisterQueue(undefined1 param_1)

PS2 methods (5):
  0x165720 ActionQueueManager::GetActionQueueManager
  0x165778 ActionQueueManager::RegisterQueue
  0x1657c0 ActionQueueManager::UnRegisterQueue
  0x165850 ActionQueueManager::FeedQueue
  0x165900 ActionQueueManager::FlushAllQueues

Sheet rows:
  ActionQueueManager::GetActionQueueManager(void)
  ActionQueueManager::RegisterQueue(ActionQueue *)
  ActionQueueManager::UnRegisterQueue(ActionQueue *)
  ActionQueueManager::FeedQueue(ActionData)
  ActionQueueManager::FlushAllQueues(void)

Xbox methods treated as members (3 of 5; untyped ones count when ECX is read before it is written): FeedQueue, FlushAllQueues, UnRegisterQueue

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R [3: FeedQueue@4f360, FlushAllQueues@4f3b0, UnRegisterQueue@4f440]
  +0x008  w[4] R/RW [3: FeedQueue@4f360, FlushAllQueues@4f3b0, UnRegisterQueue@4f440]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R [3: FeedQueue@165850, FlushAllQueues@165900, UnRegisterQueue@1657c0]
  +0x004  w[4] R/W [4: FeedQueue@165850, FlushAllQueues@165900, RegisterQueue@165778, UnRegisterQueue@1657c0]
  +0x008  w[4] R [1: RegisterQueue@165778]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
