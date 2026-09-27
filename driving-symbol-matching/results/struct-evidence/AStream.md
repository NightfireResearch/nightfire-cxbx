# AStream

FastAlloc/constructed sizes under its tag: {'allocated': [208], 'constructed': []}
deleting destructor 0x122550 frees/deletes with size 0xd0 (call to ABaseSound::operator_delete)
Xbox vtable 0x001a2910 (4 slots) stored by its constructor
PS2 sheet virtual table row: ['AStream virtual table']
constructor 0x122370 first calls: ['ABaseSound::ABaseSound', 'TIMER_getfrequency', 'UMemory::FastAlloc']

Xbox methods (22):
  0x121e80 undefined __cdecl SetPath(char * param_1)
  0x121f30 undefined SetFilter(undefined4 param_1)
  0x121f40 bool __thiscall IsOver(AStream * this)
  0x121f50 bool __thiscall IsNotFound(AStream * this)
  0x121f60 bool __thiscall IsPaused(AStream * this)
  0x121f70 bool __thiscall IsLoop(AStream * this)
  0x121f80 undefined IsEffect(void)
  0x121f90 undefined FadeOut(void)
  0x121fc0 undefined GetLatency(void)
  0x122020 undefined GetInternalVolume(void)
  0x122030 undefined GetFile(void)
  0x122050 undefined Next(void)
  0x122370 undefined8 __thiscall AStream(AStream * this, ABaseSound * param_1_00, char * param_2, char * param_3, char * 
  0x122430 void __thiscall Stop(AStream * this)
  0x122450 undefined ~AStream(void)
  0x1224d0 undefined DecodeError(undefined4 param_1)
  0x122550 void __thiscall scalar_deleting_destructor(AStream * this)
  0x122580 undefined Play(undefined4 param_1)
  0x1234d0 undefined Event(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 par
  0x1236c0 undefined8 __cdecl Create(char * param_1, char * param_2, uint param_3)
  0x1237d0 undefined4 __stdcall Get(undefined4 param_1)
  0x1237f0 undefined4 __cdecl Remove(char * param_1)

PS2 methods (30):
  0x2e86a0 AStream::SetPath
  0x2e8a90 AStream::AStream
  0x2e8c00 AStream::~AStream
  0x2e8c70 AStream::Play
  0x2e9350 AStream::DecodeError
  0x2e9578 AStream::Create
  0x2e9688 AStream::Get
  0x2e96b8 AStream::Remove
  0x2e9718 AStream::SetFilter
  0x2e9728 AStream::Event
  0x2e9958 AStream::IsOver
  0x2e9968 AStream::IsQueueEmpty
  0x2e9980 AStream::IsNotFound
  0x2e9990 AStream::IsPaused
  0x2e99a0 AStream::IsLoop
  0x2e99b0 AStream::IsEffect
  0x2e99c0 AStream::IsPreBuffered
  0x2e99d0 AStream::Next
  0x2e9ae0 AStream::Last
  0x2e9bc8 AStream::Stop
  0x2e9bf8 AStream::FadeOut
  0x2e9c38 AStream::GetState
  0x2e9c48 AStream::GetLatency
  0x2e9d00 AStream::QueryTime
  0x2e9d38 AStream::QueryLibTime
  0x2e9d80 AStream::GetInternalVolume
  0x2e9d90 AStream::GetFile
  0x2eb6c8 AStream::GetHoldState
  0x2eb6d8 AStream::operator_new
  0x2eb708 AStream::Release

Sheet rows:
  AStream::SetPath(char *)
  AStream::AStream(char *, char *, char *, unsigned int)
  AStream::~AStream(void)
  AStream::Play(APath &)
  AStream::DecodeError(int)
  AStream::Create(char *, char *, unsigned int)
  AStream::Get(char *)
  AStream::Remove(char *)
  AStream::SetFilter(float)
  AStream::Event(char *, float, bool, bool, bool)
  AStream::IsOver(void)
  AStream::IsQueueEmpty(void)
  AStream::IsNotFound(void)
  AStream::IsPaused(void)
  AStream::IsLoop(void)
  AStream::IsEffect(void)
  AStream::IsPreBuffered(void)
  AStream::Next(void)
  AStream::Last(void)
  AStream::Stop(void)
  AStream::FadeOut(void)
  AStream::GetState(void)
  AStream::GetLatency(void)
  AStream::QueryTime(void)
  AStream::QueryLibTime(void)
  AStream::GetInternalVolume(void)
  AStream::GetFile(void)
  AStream type_info function
  AStream::GetHoldState(void)
  AStream::operator new(unsigned int)
  AStream::GetPath(void)
  AStream::Release(void)
  AStream::fgPath
  AStream::fTrigger
  AStream::fHoldBegin
  AStream virtual table
  AStream type_info node

Xbox methods treated as members (19 of 22; untyped ones count when ECX is read before it is written): AStream, DecodeError, Event, FadeOut, GetFile, GetInternalVolume, GetLatency, IsEffect, IsLoop, IsNotFound, IsOver, IsPaused, Next, Play, SetFilter, SetPath, Stop, scalar_deleting_destructor, ~AStream

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AStream@122370, ~AStream@122450]
  +0x0c0  w[4] R/W -> AStreamPriv::~AStreamPriv, FUN_001220f0, FUN_00123380 [17: AStream@122370, DecodeError@1224d0, Event@1234d0, FadeOut@121f90, GetFile@122030, GetInternalVolume@122020…]
  +0x0c4  w[4] W [1: AStream@122370]
  +0x0c8  w[4] W [1: AStream@122370]
  +0x0cc  w[4] W [1: AStream@122370]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: AStream@2e8a90]
  +0x004  w[4] W [1: AStream@2e8a90]
  +0x008  w[4] W [1: AStream@2e8a90]
  +0x010  w[4] W [1: AStream@2e8a90]
  +0x014  w[4] W [1: AStream@2e8a90]
  +0x018  w[4] W [1: AStream@2e8a90]
  +0x020  w[4] W [1: AStream@2e8a90]
  +0x024  w[4] W [1: AStream@2e8a90]
  +0x028  w[4] W float [1: AStream@2e8a90]
  +0x030  w[4] W [1: AStream@2e8a90]
  +0x034  w[4] W [1: AStream@2e8a90]
  +0x038  w[4] W float [1: AStream@2e8a90]
  +0x040  w[4] W [1: AStream@2e8a90]
  +0x044  w[4] W [1: AStream@2e8a90]
  +0x048  w[4] W float [1: AStream@2e8a90]
  +0x05c  w[4] W float [1: AStream@2e8a90]
  +0x060  w[4] R/W float [2: AStream@2e8a90, Play@2e8c70]
  +0x064  w[4] W float [1: AStream@2e8a90]
  +0x068  w[4] W [1: AStream@2e8a90]
  +0x06c  w[4] R/W float [2: AStream@2e8a90, Play@2e8c70]
  +0x070  w[4] W float [1: AStream@2e8a90]
  +0x07c  w- LEA addr-taken [1: AStream@2e8a90]
  +0x080  w[4] W [1: AStream@2e8a90]
  +0x084  w[4] R/W -> strcasecmp [2: AStream@2e8a90, Play@2e8c70]
  +0x088  w[4] R/W [1: AStream@2e8a90]
  +0x09c  w[4] W [1: AStream@2e8a90]
  +0x0a0  w[4] W [1: AStream@2e8a90]
  +0x0a4  w[4] W [1: AStream@2e8a90]
  +0x0a8  w[4] W [1: AStream@2e8a90]
  +0x0ac  w[4] W [2: AStream@2e8a90, ~AStream@2e8c00]
  +0x0b0  w[4] R/W -> AStream::Next, AStreamPriv::~AStreamPriv [21: AStream@2e8a90, Event@2e9728, GetFile@2e9d90, GetInternalVolume@2e9d80, GetLatency@2e9c48, GetState@2e9c38…]
  +0x0b4  w[4] R/W [2: AStream@2e8a90, Play@2e8c70]
  +0x0b8  w[4] R/W [2: AStream@2e8a90, Play@2e8c70]
  +0x0bc  w[4] R/W [3: AStream@2e8a90, Play@2e8c70, QueryTime@2e9d00]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetFilter: R +0xc0 w4
  IsOver: R +0xc0 w4
  IsNotFound: R +0xc0 w4
  IsPaused: R +0xc0 w4
  IsLoop: R +0xc0 w4
  IsEffect: R +0xc0 w4
  GetInternalVolume: R +0xc0 w4
  GetFile: R +0xc0 w4
  Stop: R +0xc0 w4
