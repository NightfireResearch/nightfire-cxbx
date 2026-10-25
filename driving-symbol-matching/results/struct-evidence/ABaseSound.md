# ABaseSound

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x1c090 frees/deletes with size 0xc0 (call to ABaseSound::operator_delete)
Xbox vtable 0x0018a510 (4 slots) stored by its constructor
PS2 sheet virtual table row: ['ABaseSound virtual table']
constructor 0x1bfa0 first calls: ['AMix::Add']

Xbox methods (8):
  0x1bfa0 undefined4 * __thiscall ABaseSound(ABaseSound * this, undefined4 param_1, int param_2)
  0x1c090 undefined scalar_deleting_destructor(undefined1 param_1)
  0x11c6f0 undefined ~ABaseSound(void)
  0x11c710 undefined GetName(void)
  0x11c720 undefined StartFade(undefined4 param_1)
  0x11c750 undefined GetFade(void)
  0x11c830 undefined operator_delete(undefined4 param_1, undefined4 param_2)
  0x11c910 undefined8 * __cdecl operator_new(ulong param_1, char * param_2)

PS2 methods (38):
  0x2fca38 ABaseSound::operator_new
  0x2fcad8 ABaseSound::operator_delete
  0x2fcb28 ABaseSound::~ABaseSound
  0x2fcb80 ABaseSound::GetName
  0x2fcb88 ABaseSound::StartFade
  0x2fcba0 ABaseSound::GetFade
  0x2fcdb0 ABaseSound::ABaseSound
  0x2fced8 ABaseSound::SetMix
  0x2fcf18 ABaseSound::SetPosition
  0x2fcf38 ABaseSound::SetVelocity
  0x2fcf58 ABaseSound::SetForward
  0x2fcf78 ABaseSound::SetRight
  0x2fcf98 ABaseSound::SetUp
  0x2fcfb8 ABaseSound::SetCameraAim
  0x2fcfd8 ABaseSound::GetPosition
  0x2fcfe0 ABaseSound::GetVelocity
  0x2fcfe8 ABaseSound::GetForward
  0x2fcff0 ABaseSound::GetRight
  0x2fcff8 ABaseSound::GetUp
  0x2fd000 ABaseSound::GetCameraAim
  0x2fd008 ABaseSound::SetMinDistance
  0x2fd010 ABaseSound::SetMaxDistance
  0x2fd020 ABaseSound::SetFalloff
  0x2fd028 ABaseSound::GetMinDistance
  0x2fd030 ABaseSound::GetMaxDistance
  0x2fd038 ABaseSound::GetMaxDistanceSq
  0x2fd040 ABaseSound::GetFalloff
  0x2fd048 ABaseSound::IsFading
  0x2fd050 ABaseSound::SetVolume
  0x2fd058 ABaseSound::SetPitch
  0x2fd060 ABaseSound::GetPitch
  0x2fd068 ABaseSound::GetViewSet
  0x2fd070 ABaseSound::GetMix
  0x2fd078 ABaseSound::IsOneShot
  0x2fd080 ABaseSound::GetVolume
  0x2fd0b0 ABaseSound::IsPlaying
  0x2fd0f0 ABaseSound::SetPV
  0x2fd128 ABaseSound::SetOrientation

Sheet rows:
  ABaseSound::operator new(unsigned int, char *)
  ABaseSound::operator delete(void *, unsigned int)
  ABaseSound::~ABaseSound(void)
  ABaseSound::GetName(void) const
  ABaseSound::StartFade(int)
  ABaseSound::GetFade(void)
  ABaseSound type_info function
  ABaseSound::ABaseSound(char *, AView)
  ABaseSound::SetMix(char *)
  ABaseSound::SetPosition(COORD3 &)
  ABaseSound::SetVelocity(COORD3 &)
  ABaseSound::SetForward(COORD3 &)
  ABaseSound::SetRight(COORD3 &)
  ABaseSound::SetUp(COORD3 &)
  ABaseSound::SetCameraAim(COORD3 &)
  ABaseSound::GetPosition(void) const
  ABaseSound::GetVelocity(void) const
  ABaseSound::GetForward(void) const
  ABaseSound::GetRight(void) const
  ABaseSound::GetUp(void) const
  ABaseSound::GetCameraAim(void) const
  ABaseSound::SetMinDistance(float)
  ABaseSound::SetMaxDistance(float)
  ABaseSound::SetFalloff(float)
  ABaseSound::GetMinDistance(void) const
  ABaseSound::GetMaxDistance(void) const
  ABaseSound::GetMaxDistanceSq(void) const
  ABaseSound::GetFalloff(void) const
  ABaseSound::IsFading(void)
  ABaseSound::SetVolume(float)
  ABaseSound::SetPitch(float)
  ABaseSound::GetPitch(void) const
  ABaseSound::GetViewSet(void)
  ABaseSound::GetMix(void) const
  ABaseSound::IsOneShot(void)
  ABaseSound::GetVolume(void) const
  ABaseSound::IsPlaying(void) const
  ABaseSound::SetPV(COORD3 &, COORD3 &)
  ABaseSound::SetOrientation(COORD3 &, COORD3 &, COORD3 &)
  ABaseSound virtual table
  ABaseSound type_info node

Xbox methods treated as members (6 of 8; untyped ones count when ECX is read before it is written): ABaseSound, GetFade, GetName, StartFade, scalar_deleting_destructor, ~ABaseSound

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: ABaseSound@1bfa0, ~ABaseSound@11c6f0]
  +0x010  w[4] W [1: ABaseSound@1bfa0]
  +0x014  w[4] W [1: ABaseSound@1bfa0]
  +0x018  w[4] W [1: ABaseSound@1bfa0]
  +0x020  w[4] W [1: ABaseSound@1bfa0]
  +0x024  w[4] W [1: ABaseSound@1bfa0]
  +0x028  w[4] W [1: ABaseSound@1bfa0]
  +0x030  w[4] W [1: ABaseSound@1bfa0]
  +0x034  w[4] W [1: ABaseSound@1bfa0]
  +0x038  w[4] W [1: ABaseSound@1bfa0]
  +0x040  w[4] W [1: ABaseSound@1bfa0]
  +0x044  w[4] W [1: ABaseSound@1bfa0]
  +0x048  w[4] W [1: ABaseSound@1bfa0]
  +0x050  w[4] W [1: ABaseSound@1bfa0]
  +0x054  w[4] W [1: ABaseSound@1bfa0]
  +0x058  w[4] W [1: ABaseSound@1bfa0]
  +0x06c  w[4] W [1: ABaseSound@1bfa0]
  +0x070  w[4] W [1: ABaseSound@1bfa0]
  +0x074  w[4] W [1: ABaseSound@1bfa0]
  +0x078  w[4] W [1: ABaseSound@1bfa0]
  +0x07c  w[4] W [1: ABaseSound@1bfa0]
  +0x080  w[4] W [1: ABaseSound@1bfa0]
  +0x084  w[4] W [1: ABaseSound@1bfa0]
  +0x088  w[4] W [1: ABaseSound@1bfa0]
  +0x08c  w[4] W [1: ABaseSound@1bfa0]
  +0x090  w[1] W [1: ABaseSound@1bfa0]
  +0x094  w[4] R/W [2: ABaseSound@1bfa0, ~ABaseSound@11c6f0]
  +0x098  w[4] R/W [1: ABaseSound@1bfa0]
  +0x09c  w- LEA addr-taken [1: GetName@11c710]
  +0x0ac  w[4] W float [3: ABaseSound@1bfa0, GetFade@11c750, StartFade@11c720]
  +0x0b0  w[4] R/W [3: ABaseSound@1bfa0, GetFade@11c750, StartFade@11c720]
  +0x0b4  w[4] R/W [3: ABaseSound@1bfa0, GetFade@11c750, StartFade@11c720]
  +0x0b8  w[1] R/W [3: ABaseSound@1bfa0, GetFade@11c750, StartFade@11c720]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [4: ABaseSound@2fcdb0, IsPlaying@2fd0b0, SetPV@2fd0f0, SetPosition@2fcf18]
  +0x004  w[4] W float [2: ABaseSound@2fcdb0, SetPV@2fd0f0]
  +0x008  w[4] W float [3: ABaseSound@2fcdb0, SetPV@2fd0f0, SetPosition@2fcf18]
  +0x010  w[4, 8] LEA/W float addr-taken [4: ABaseSound@2fcdb0, GetVelocity@2fcfe0, SetPV@2fd0f0, SetVelocity@2fcf38]
  +0x014  w[4] W float [2: ABaseSound@2fcdb0, SetPV@2fd0f0]
  +0x018  w[4] W float [3: ABaseSound@2fcdb0, SetPV@2fd0f0, SetVelocity@2fcf38]
  +0x020  w[4, 8] LEA/W float addr-taken [4: ABaseSound@2fcdb0, GetForward@2fcfe8, SetForward@2fcf58, SetOrientation@2fd128]
  +0x024  w[4] W float [2: ABaseSound@2fcdb0, SetOrientation@2fd128]
  +0x028  w[4] W float [3: ABaseSound@2fcdb0, SetForward@2fcf58, SetOrientation@2fd128]
  +0x030  w[4, 8] LEA/W float addr-taken [4: ABaseSound@2fcdb0, GetRight@2fcff0, SetOrientation@2fd128, SetRight@2fcf78]
  +0x034  w[4] W float [2: ABaseSound@2fcdb0, SetOrientation@2fd128]
  +0x038  w[4] W float [3: ABaseSound@2fcdb0, SetOrientation@2fd128, SetRight@2fcf78]
  +0x040  w[4, 8] LEA/W float addr-taken [4: ABaseSound@2fcdb0, GetUp@2fcff8, SetOrientation@2fd128, SetUp@2fcf98]
  +0x044  w[4] W float [2: ABaseSound@2fcdb0, SetOrientation@2fd128]
  +0x048  w[4] W float [3: ABaseSound@2fcdb0, SetOrientation@2fd128, SetUp@2fcf98]
  +0x050  w[8] LEA/W addr-taken [2: GetCameraAim@2fd000, SetCameraAim@2fcfb8]
  +0x058  w[4] W [1: SetCameraAim@2fcfb8]
  +0x05c  w[4] R/W float [3: ABaseSound@2fcdb0, GetMinDistance@2fd028, SetMinDistance@2fd008]
  +0x060  w[4] R/W float [3: ABaseSound@2fcdb0, GetMaxDistance@2fd030, SetMaxDistance@2fd010]
  +0x064  w[4] R/W float [3: ABaseSound@2fcdb0, GetMaxDistanceSq@2fd038, SetMaxDistance@2fd010]
  +0x068  w[4] R/W float [3: ABaseSound@2fcdb0, GetFalloff@2fd040, SetFalloff@2fd020]
  +0x06c  w[4] R/W float [3: ABaseSound@2fcdb0, GetVolume@2fd080, SetVolume@2fd050]
  +0x070  w[4] R/W float [3: ABaseSound@2fcdb0, GetPitch@2fd060, SetPitch@2fd058]
  +0x07c  w- LEA addr-taken [1: ABaseSound@2fcdb0]
  +0x080  w[4] W [1: ABaseSound@2fcdb0]
  +0x084  w[4] R/W -> ABaseSound::operator_delete, AMix::Add [5: ABaseSound@2fcdb0, GetMix@2fd070, GetVolume@2fd080, SetMix@2fced8, ~ABaseSound@2fcb28]
  +0x088  w[4] LEA/R/W addr-taken [2: ABaseSound@2fcdb0, GetViewSet@2fd068]
  +0x08c  w- LEA addr-taken [1: GetName@2fcb80]
  +0x09c  w[4] R/W float [3: ABaseSound@2fcdb0, GetFade@2fcba0, StartFade@2fcb88]
  +0x0a0  w[4] R/W [3: ABaseSound@2fcdb0, GetFade@2fcba0, StartFade@2fcb88]
  +0x0a4  w[4] R/W [3: ABaseSound@2fcdb0, GetFade@2fcba0, StartFade@2fcb88]
  +0x0a8  w[4] R/W [4: ABaseSound@2fcdb0, GetFade@2fcba0, IsFading@2fd048, StartFade@2fcb88]
  +0x0ac  w[4] W [2: ABaseSound@2fcdb0, ~ABaseSound@2fcb28]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetName: LEA +0x9c w0
