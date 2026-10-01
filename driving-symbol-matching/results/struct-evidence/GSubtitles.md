# GSubtitles

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x001a08b8 (1 slots) stored by its constructor
PS2 sheet virtual table row: ['GSubtitles virtual table']
constructor 0xe34e0 first calls: ['UFileLoader::FileLoad', 'FONT_create']

Xbox methods (12):
  0xe3180 undefined TheApp(void)
  0xe3190 undefined IsSubtitleDisplaying(void)
  0xe34e0 undefined GSubtitles(void)
  0xe3560 undefined SetupFMVSubtitles(undefined4 param_1)
  0xe3630 undefined ClearSubtitles(void)
  0xe3670 undefined DrawFont(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0xe36f0 undefined GetWidthHeight(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xe3730 undefined ~GSubtitles(void)
  0xe3790 undefined DisplaySubtitle(undefined4 param_1, undefined4 param_2)
  0xe3840 undefined LoadSubtitles(undefined4 param_1)
  0xe39b0 undefined DrawSubtitles(void)
  0xe3ad0 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (17):
  0x24a0d8 GSubtitles::GSubtitles
  0x24a168 GSubtitles::~GSubtitles
  0x24a1d0 GSubtitles::TheApp
  0x24a1e0 GSubtitles::IsSubtitleDisplaying
  0x24a1f0 GSubtitles::DisplaySubtitle
  0x24a290 GSubtitles::LoadFont
  0x24a300 GSubtitles::LoadSubtitles
  0x24a4d0 GSubtitles::UnloadFont
  0x24a518 GSubtitles::UnloadSubtitles
  0x24a550 GSubtitles::ClearFrames
  0x24a598 GSubtitles::SetupFMVSubtitles
  0x24a6d0 GSubtitles::DrawSubtitles
  0x24a870 GSubtitles::ClearSubtitles
  0x24a8b0 GSubtitles::DrawFont
  0x24a978 GSubtitles::DrawBackground
  0x24a980 GSubtitles::GetWidthHeight
  0x24afc0 GSubtitles::fTheApp_global_ctors

Sheet rows:
  GSubtitles::GSubtitles(void)
  GSubtitles::~GSubtitles(void)
  GSubtitles::TheApp(void)
  GSubtitles::IsSubtitleDisplaying(void)
  GSubtitles::DisplaySubtitle(unsigned int, float)
  GSubtitles::LoadFont(char *)
  GSubtitles::LoadSubtitles(char *)
  GSubtitles::UnloadFont(void)
  GSubtitles::UnloadSubtitles(void)
  GSubtitles::ClearFrames(void)
  GSubtitles::SetupFMVSubtitles(unsigned int)
  GSubtitles::DrawSubtitles(void)
  GSubtitles::ClearSubtitles(void)
  GSubtitles::DrawFont(char *, float, float, float, unsigned int)
  GSubtitles::DrawBackground(float, float, float, unsigned int)
  GSubtitles::GetWidthHeight(char *, float, float *, float *)
  GSubtitles type_info function
  GSubtitles::fTheApp
  GSubtitles virtual table
  GSubtitles type_info node

Xbox methods treated as members (12 of 12; untyped ones count when ECX is read before it is written): ClearSubtitles, DisplaySubtitle, DrawFont, DrawSubtitles, GSubtitles, GetWidthHeight, IsSubtitleDisplaying, LoadSubtitles, SetupFMVSubtitles, TheApp, scalar_deleting_destructor, ~GSubtitles

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: GSubtitles@e34e0, ~GSubtitles@e3730]
  +0x004  w[1] R/W [3: DisplaySubtitle@e3790, DrawSubtitles@e39b0, SetupFMVSubtitles@e3560]
  +0x008  w[4] W float [5: ClearSubtitles@e3630, DisplaySubtitle@e3790, DrawSubtitles@e39b0, GSubtitles@e34e0, SetupFMVSubtitles@e3560]
  +0x00c  w[4] R/W [5: ClearSubtitles@e3630, DisplaySubtitle@e3790, DrawSubtitles@e39b0, GSubtitles@e34e0, SetupFMVSubtitles@e3560]
  +0x010  w[4] R/W [4: DrawFont@e3670, GSubtitles@e34e0, GetWidthHeight@e36f0, ~GSubtitles@e3730]
  +0x014  w[4] R/W [2: GSubtitles@e34e0, ~GSubtitles@e3730]
  +0x018  w[4] W float [2: DrawSubtitles@e39b0, GSubtitles@e34e0]
  +0x01c  w[4] W float [2: DrawSubtitles@e39b0, GSubtitles@e34e0]
  +0x020  w[4] R/W [5: ClearSubtitles@e3630, DisplaySubtitle@e3790, GSubtitles@e34e0, SetupFMVSubtitles@e3560, ~GSubtitles@e3730]
  +0x024  w[4] R/W [5: ClearSubtitles@e3630, DisplaySubtitle@e3790, GSubtitles@e34e0, SetupFMVSubtitles@e3560, ~GSubtitles@e3730]
  +0x028  w[4] R/W [2: GSubtitles@e34e0, SetupFMVSubtitles@e3560]
  +0x02c  w[4] R/W [6: ClearSubtitles@e3630, DisplaySubtitle@e3790, DrawSubtitles@e39b0, GSubtitles@e34e0, IsSubtitleDisplaying@e3190, SetupFMVSubtitles@e3560]
  +0x030  w- LEA addr-taken -> GSubtitleSplitter::Reset [5: ClearSubtitles@e3630, DisplaySubtitle@e3790, DrawSubtitles@e39b0, SetupFMVSubtitles@e3560, ~GSubtitles@e3730]
  +0x080  w[4] W [1: GSubtitles@e34e0]
  +0x088  w[4] W [1: GSubtitles@e34e0]
  +0x08c  w[4] W [1: GSubtitles@e34e0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [2: DisplaySubtitle@24a1f0, SetupFMVSubtitles@24a598]
  +0x004  w[4] R/W float [6: ClearSubtitles@24a870, DisplaySubtitle@24a1f0, DrawSubtitles@24a6d0, GSubtitles@24a0d8, LoadSubtitles@24a300, SetupFMVSubtitles@24a598]
  +0x008  w[4] W [5: ClearSubtitles@24a870, DisplaySubtitle@24a1f0, GSubtitles@24a0d8, LoadSubtitles@24a300, SetupFMVSubtitles@24a598]
  +0x00c  w[4] R/W -> FONT_destroy [5: DrawFont@24a8b0, GSubtitles@24a0d8, GetWidthHeight@24a980, LoadFont@24a290, UnloadFont@24a4d0]
  +0x010  w[4] R/W -> FONT_destroy, UFileLoader::FileFree [3: GSubtitles@24a0d8, LoadFont@24a290, UnloadFont@24a4d0]
  +0x014  w[4] W float [1: GSubtitles@24a0d8]
  +0x018  w[4] W float [1: GSubtitles@24a0d8]
  +0x01c  w[4] R/W -> __builtin_vec_delete [5: ClearFrames@24a550, GSubtitles@24a0d8, LoadSubtitles@24a300, SetupFMVSubtitles@24a598, UnloadSubtitles@24a518]
  +0x020  w[4] R/W [4: ClearFrames@24a550, GSubtitles@24a0d8, LoadSubtitles@24a300, SetupFMVSubtitles@24a598]
  +0x024  w[4] R/W [3: GSubtitles@24a0d8, LoadSubtitles@24a300, SetupFMVSubtitles@24a598]
  +0x028  w[4] R/W -> GSubtitleSplitter::Init, GSystem::LOCALE_Symbol [7: ClearSubtitles@24a870, DisplaySubtitle@24a1f0, DrawSubtitles@24a6d0, GSubtitles@24a0d8, IsSubtitleDisplaying@24a1e0, LoadSubtitles@24a300…]
  +0x02c  w- LEA addr-taken [3: ClearFrames@24a550, DisplaySubtitle@24a1f0, SetupFMVSubtitles@24a598]
  +0x090  w[4] W [2: GSubtitles@24a0d8, ~GSubtitles@24a168]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  IsSubtitleDisplaying: R +0x2c w4
