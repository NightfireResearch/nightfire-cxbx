# AnimationController

FastAlloc/constructed sizes under its tag: {'allocated': [48, 64], 'constructed': []}
Xbox vtable 0x00189e04 (2 slots) stored by its constructor
PS2 sheet virtual table row: ['AnimationController virtual table']

Xbox methods (6):
  0x11020 undefined GetCurrentFrame(void)
  0x11030 undefined GetTotalFrames(void)
  0x11040 undefined GetCurrentTime(void)
  0x11050 undefined GetTotalTime(void)
  0x11060 undefined GetRemainingTime(void)
  0x12610 undefined ~AnimationController(void)

PS2 methods (10):
  0x107338 AnimationController::AnimationController
  0x107358 AnimationController::~AnimationController
  0x107388 AnimationController::GetCurrentFrame
  0x107398 AnimationController::GetTotalFrames
  0x1073a8 AnimationController::GetCurrentTime
  0x1073c0 AnimationController::GetTotalTime
  0x1073d8 AnimationController::GetRemainingTime
  0x10bb60 AnimationController::operator_new
  0x10bb80 AnimationController::operator_delete
  0x10bba0 AnimationController::GetActPoser

Sheet rows:
  AnimationController::AnimationController(ActAnimGroup *, ActPos
  AnimationController::~AnimationController(void)
  AnimationController::GetCurrentFrame(void)
  AnimationController::GetTotalFrames(void)
  AnimationController::GetCurrentTime(void)
  AnimationController::GetTotalTime(void)
  AnimationController::GetRemainingTime(void)
  AnimationController type_info function
  AnimationController::operator new(unsigned int)
  AnimationController::operator delete(void *, unsigned int)
  AnimationController::GetActPoser(void)
  AnimationController virtual table
  AnimationController type_info node

Xbox methods treated as members (6 of 6; untyped ones count when ECX is read before it is written): GetCurrentFrame, GetCurrentTime, GetRemainingTime, GetTotalFrames, GetTotalTime, ~AnimationController

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: ~AnimationController@12610]
  +0x014  w[4] R [5: GetCurrentFrame@11020, GetCurrentTime@11040, GetRemainingTime@11060, GetTotalFrames@11030, GetTotalTime@11050]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: AnimationController@107338]
  +0x004  w[4] R/W [6: AnimationController@107338, GetActPoser@10bba0, GetCurrentFrame@107388, GetCurrentTime@1073a8, GetTotalFrames@107398, GetTotalTime@1073c0]
  +0x010  w[4] W [1: AnimationController@107338]
  +0x014  w[4] W [2: AnimationController@107338, ~AnimationController@107358]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetCurrentFrame: R +0x14 w4
  GetTotalFrames: R +0x14 w4
  GetCurrentTime: R +0x14 w4
  GetTotalTime: R +0x14 w4
  GetRemainingTime: R +0x14 w4
  ~AnimationController: W +0x0 w4
