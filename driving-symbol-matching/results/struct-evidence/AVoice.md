# AVoice

FastAlloc/constructed sizes under its tag: {'allocated': [88], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x123ca0 first calls: ['??_L@YGXPAXIHP6EX0@Z1@Z', 'AVoice::Set']

Xbox methods (5):
  0x123870 undefined Set(undefined4 param_1, undefined4 param_2)
  0x123a30 undefined __stdcall PlayVoices(void)
  0x123ca0 undefined AVoice(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x1243c0 undefined BuildMap(void)
  0x124690 undefined Play(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 para

PS2 methods (9):
  0x2dd698 AVoice::Set
  0x2dd738 AVoice::Set
  0x2dd790 AVoice::AVoice
  0x2dd818 AVoice::AVoice
  0x2dd918 AVoice::BuildMap
  0x2dda50 AVoice::PlayVoices
  0x2ddfd0 AVoice::Play
  0x2de938 AVoice::Set_global_ctors
  0x2de958 AVoice::Set_global_dtors

Sheet rows:
  AVoice::Set(int, int)
  AVoice::Set(char *, char *)
  AVoice::AVoice(AMix &, int, int)
  AVoice::AVoice(AMix &, char *, char *)
  AVoice::View::Push(void)
  AVoice::BuildMap(void)
  AVoice::PlayVoices(void)
  AVoice::View::Stop(void)
  AVoice::View::Play(float, float, float, float, float)
  AVoice::Play(AView, float, float, float, float, float)
  AVoice::View::Restart(void)
  AVoice::View::~View(void)

Xbox methods treated as members (3 of 5; untyped ones count when ECX is read before it is written): AVoice, Play, Set

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: AVoice@123ca0, Play@124690]
  +0x004  w- LEA addr-taken -> ??_L@YGXPAXIHP6EX0@Z1@Z [1: AVoice@123ca0]
  +0x010  w- LEA addr-taken [1: Set@123870]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [3: AVoice@2dd790, AVoice@2dd818, Play@2ddfd0]
  +0x004  w- LEA addr-taken [3: AVoice@2dd790, AVoice@2dd818, Set@2dd698]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
