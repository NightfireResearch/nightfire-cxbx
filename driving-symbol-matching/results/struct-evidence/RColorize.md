# RColorize

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [96]}
Xbox vtable 0x001927c8 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RColorize virtual table']
constructor 0x9a420 first calls: ['EAGL::RenderContextExtension::GetFrontBuffer']

Xbox methods (9):
  0x8b770 undefined __stdcall Init(void)
  0x9a420 undefined RColorize(void)
  0x9a480 undefined __stdcall Kill(void)
  0x9a4a0 undefined SetAreaBrightness(undefined4 param_1)
  0x9a4d0 undefined EnableMotionBlur(undefined4 param_1)
  0x9a4f0 undefined DisableMotionBlur(void)
  0x9a500 undefined SetEnabled(undefined4 param_1)
  0x9a6e0 undefined __thiscall Draw(RColorize * this)
  0x9ac00 undefined Reset(void)

PS2 methods (16):
  0x1d8418 RColorize::RColorize
  0x1d8440 RColorize::~RColorize
  0x1d8470 RColorize::SetEnabled
  0x1d8740 RColorize::SetAreaBrightness
  0x1d8770 RColorize::GetAreaBrightness
  0x1d87b0 RColorize::Draw
  0x1d9010 RColorize::EnableMotionBlur
  0x1d9020 RColorize::DisableMotionBlur
  0x1d92e8 RColorize::Get
  0x1d92f8 RColorize::Init
  0x1d9330 RColorize::Kill
  0x1d9368 RColorize::Enabled
  0x1d9378 RColorize::IRModeEnabled
  0x1d9388 RColorize::SetBlastStrength
  0x1d9390 RColorize::Reset
  0x1d93c8 RColorize::fgThis_RColorize_global_ctors

Sheet rows:
  RColorize::RColorize(void)
  RColorize::~RColorize(void)
  RColorize::SetEnabled(int)
  RColorize::SetAreaBrightness(float)
  RColorize::GetAreaBrightness(void) const
  RColorize::Draw(void)
  RColorize::EnableMotionBlur(float)
  RColorize::DisableMotionBlur(void)
  RColorize type_info function
  RColorize::Get(void)
  RColorize::Init(void)
  RColorize::Kill(void)
  RColorize::Enabled(void)
  RColorize::IRModeEnabled(void)
  RColorize::SetBlastStrength(float)
  RColorize::Reset(void)
  RColorize::fgThis_RColorize
  RColorize virtual table
  RColorize type_info node

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): DisableMotionBlur, Draw, EnableMotionBlur, Init, RColorize, Reset, SetAreaBrightness, SetEnabled

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: RColorize@9a420]
  +0x010  w- LEA addr-taken [2: Draw@9a6e0, SetEnabled@9a500]
  +0x020  w- LEA addr-taken [2: Draw@9a6e0, SetEnabled@9a500]
  +0x030  w[4] R/W [4: Draw@9a6e0, RColorize@9a420, Reset@9ac00, SetEnabled@9a500]
  +0x034  w[4] R/W [3: DisableMotionBlur@9a4f0, Draw@9a6e0, EnableMotionBlur@9a4d0]
  +0x038  w[4] R/W [5: Draw@9a6e0, RColorize@9a420, Reset@9ac00, SetAreaBrightness@9a4a0, SetEnabled@9a500]
  +0x03c  w[4] R/W [2: Draw@9a6e0, SetEnabled@9a500]
  +0x03d  w[1] R [1: Draw@9a6e0]
  +0x03e  w[1] R [1: Draw@9a6e0]
  +0x03f  w[1] R [1: Draw@9a6e0]
  +0x040  w[4] R/W [2: Draw@9a6e0, SetEnabled@9a500]
  +0x044  w[4] R/W [2: Draw@9a6e0, SetEnabled@9a500]
  +0x048  w[4] R/W float [5: Draw@9a6e0, RColorize@9a420, Reset@9ac00, SetAreaBrightness@9a4a0, SetEnabled@9a500]
  +0x04c  w[4] W float [5: Draw@9a6e0, RColorize@9a420, Reset@9ac00, SetAreaBrightness@9a4a0, SetEnabled@9a500]
  +0x050  w[4] W float [4: DisableMotionBlur@9a4f0, Draw@9a6e0, EnableMotionBlur@9a4d0, RColorize@9a420]
  +0x054  w[4] W float [1: Draw@9a6e0]
  +0x058  w[4] R/W [2: Draw@9a6e0, RColorize@9a420]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RColorize@1d8418, ~RColorize@1d8440]
  +0x010  w[8] R/W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x018  w[8] R/W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x020  w[8] LEA/W addr-taken [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x028  w[8] W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x030  w[4] R/W [5: Draw@1d87b0, Enabled@1d9368, IRModeEnabled@1d9378, RColorize@1d8418, SetEnabled@1d8470]
  +0x034  w[4] R/W [3: DisableMotionBlur@1d9020, Draw@1d87b0, EnableMotionBlur@1d9010]
  +0x038  w[4] R/W [3: Draw@1d87b0, RColorize@1d8418, SetAreaBrightness@1d8740]
  +0x03c  w[4] R/W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x040  w[4] R/W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x044  w[4] R/W [2: Draw@1d87b0, SetEnabled@1d8470]
  +0x048  w[4] R/W float [5: Draw@1d87b0, GetAreaBrightness@1d8770, RColorize@1d8418, SetAreaBrightness@1d8740, SetEnabled@1d8470]
  +0x04c  w[4] R/W float [3: Draw@1d87b0, RColorize@1d8418, SetAreaBrightness@1d8740]
  +0x050  w[4] R/W float [4: DisableMotionBlur@1d9020, Draw@1d87b0, EnableMotionBlur@1d9010, RColorize@1d8418]
  +0x054  w[4] R/W float [2: Draw@1d87b0, SetBlastStrength@1d9388]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
