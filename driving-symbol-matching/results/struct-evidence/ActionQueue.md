# ActionQueue

FastAlloc/constructed sizes under its tag: {'allocated': [2420], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x4f320 first calls: ['FUN_0004f2d0', 'ActionQueueManager::GetActionQueueManager', 'ActionQueueManager::RegisterQueue']

Xbox methods (7):
  0x4f1b0 undefined ~ActionQueue(void)
  0x4f220 bool __thiscall IsEmpty(ActionQueue * this)
  0x4f230 undefined ReceiveAction(undefined4 param_1)
  0x4f250 void __thiscall PopAction(ActionQueue * this)
  0x4f280 void __thiscall Flush(ActionQueue * this)
  0x4f290 undefined4 __thiscall GetAction(ActionQueue * this)
  0x4f320 undefined4 * __thiscall ActionQueue(ActionQueue * this)

PS2 methods (10):
  0x165288 ActionQueue::ActionQueue
  0x165338 ActionQueue::ActionQueue
  0x1653f0 ActionQueue::~ActionQueue
  0x165440 ActionQueue::operator[]
  0x165488 ActionQueue::Initialize
  0x1654c8 ActionQueue::IsEmpty
  0x1654d8 ActionQueue::ReceiveAction
  0x165568 ActionQueue::PopAction
  0x1655a8 ActionQueue::Flush
  0x1655c0 ActionQueue::GetAction

Sheet rows:
  ActionQueue::ActionQueue(char *)
  ActionQueue::ActionQueue(ActionQueueManager *, char *)
  ActionQueue::~ActionQueue(void)
  ActionQueue::operator[](int)
  ActionQueue::Initialize(ActionQueueManager *, char *)
  ActionQueue::IsEmpty(void)
  ActionQueue::ReceiveAction(ActionData &)
  ActionQueue::PopAction(void)
  ActionQueue::Flush(void)
  ActionQueue::GetAction(void)

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): ActionQueue, Flush, GetAction, IsEmpty, PopAction, ReceiveAction, ~ActionQueue

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> ActionQueueManager::UnRegisterQueue [2: ActionQueue@4f320, ~ActionQueue@4f1b0]
  +0x004  w[4] LEA/R/RW/W addr-taken [5: ActionQueue@4f320, Flush@4f280, GetAction@4f290, IsEmpty@4f220, PopAction@4f250]
  +0x008  w[4] W [1: Flush@4f280]
  +0x00c  w[4] R/W [3: Flush@4f280, GetAction@4f290, PopAction@4f250]
  +0x010  w[4] R [1: PopAction@4f250]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> UMemory::FastFree [2: Initialize@165488, ~ActionQueue@1653f0]
  +0x004  w[4] LEA/R/W addr-taken [9: ActionQueue@165288, ActionQueue@165338, Flush@1655a8, GetAction@1655c0, Initialize@165488, IsEmpty@1654c8…]
  +0x008  w[4] W [1: Flush@1655a8]
  +0x014  w- LEA addr-taken [2: ActionQueue@165288, ActionQueue@165338]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~ActionQueue: R +0x0 w4
  IsEmpty: R +0x4 w4
