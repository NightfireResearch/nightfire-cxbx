# GLoadingScreen

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (14):
  0xe1f90 undefined InitLoadDots(undefined1 param_1)
  0xe2110 undefined __stdcall LoadAndDrawUnLoadingScreen(void)
  0xe2390 void __stdcall InitLoadScreen(char * param_1, char * param_2)
  0xe2610 undefined DrawBackground(void)
  0xe2710 undefined DrawDots(void)
  0xe2830 undefined ClearToBlack(void)
  0xe28a0 undefined Update(void)
  0xe2a70 undefined Shutdown(void)
  0xe2b60 undefined LoadScreenSystemTaskCallback(void)
  0xe2b70 undefined __stdcall AddLoadingScreenSyncTask(void)
  0xe2b90 undefined __stdcall DeleteLoadingScreenSyncTask(void)
  0xe2ba0 undefined __stdcall FreeUnloadingScreen(void)
  0xe2e30 undefined Print(undefined4 param_1)
  0xe2ff0 void __stdcall Status(char * fmt, ...)

PS2 methods (23):
  0x247e98 GLoadingScreen::LoadScreenSystemTaskCallback
  0x247eb8 GLoadingScreen::AddLoadingScreenSyncTask
  0x247ee0 GLoadingScreen::DeleteLoadingScreenSyncTask
  0x247f00 GLoadingScreen::InitLoadDots
  0x248110 GLoadingScreen::LoadAndDrawUnLoadingScreen
  0x2483f0 GLoadingScreen::FreeUnloadingScreen
  0x248480 GLoadingScreen::InitLoadScreen
  0x2487d8 GLoadingScreen::DrawBackground
  0x2488e8 GLoadingScreen::DrawDots
  0x248a40 GLoadingScreen::DrawHintText
  0x248ca0 GLoadingScreen::ClearToBlack
  0x248d40 GLoadingScreen::Update
  0x248fc8 GLoadingScreen::Shutdown
  0x249128 GLoadingScreen::FadeToBlack
  0x249260 GLoadingScreen::DrawDebugMessage
  0x249268 GLoadingScreen::Print
  0x249528 GLoadingScreen::PrintChannel
  0x249548 GLoadingScreen::PrintVAList
  0x2495a0 GLoadingScreen::Printf
  0x2495e8 GLoadingScreen::Status
  0x249720 GLoadingScreen::SaveToMemCard
  0x2498f0 GLoadingScreen::VSync
  0x249930 GLoadingScreen::ClearFrameBuffers

Sheet rows:
  GLoadingScreen::LoadScreenSystemTaskCallback(int, int)
  GLoadingScreen::AddLoadingScreenSyncTask(void)
  GLoadingScreen::DeleteLoadingScreenSyncTask(void)
  GLoadingScreen::InitLoadDots(bool)
  GLoadingScreen::LoadAndDrawUnLoadingScreen(void)
  GLoadingScreen::FreeUnloadingScreen(void)
  GLoadingScreen::InitLoadScreen(char *, char *)
  GLoadingScreen::DrawBackground(void)
  GLoadingScreen::DrawDots(void)
  GLoadingScreen::DrawHintText(void)
  GLoadingScreen::ClearToBlack(void)
  GLoadingScreen::Update(void)
  GLoadingScreen::Shutdown(void)
  GLoadingScreen::FadeToBlack(int)
  GLoadingScreen::DrawDebugMessage(void)
  GLoadingScreen::Print(char *)
  GLoadingScreen::PrintChannel(PRINTCHANNEL, char *)
  GLoadingScreen::PrintVAList(char *, char *)
  GLoadingScreen::Printf(char *,...)
  GLoadingScreen::Status(char *,...)
  GLoadingScreen::SaveToMemCard(void)
  GLoadingScreen::VSync(void)
  GLoadingScreen::ClearFrameBuffers(void)
  GLoadingScreen::fStatusBuffer
  GLoadingScreen::fBGShapeFile
  GLoadingScreen::fLoadingStage
  GLoadingScreen::fStartOfGame
  GLoadingScreen::fFont
  GLoadingScreen::fFontFile
  GLoadingScreen::fTitleID
  GLoadingScreen::fDescID
  GLoadingScreen::fDescNumLines
  GLoadingScreen::fHintID
  GLoadingScreen::fScaleY
  GLoadingScreen::fDotShapeFile
  GLoadingScreen::fCurrentCycleTime
  GLoadingScreen::fCurrentDirection
  GLoadingScreen::fTimer
  GLoadingScreen::fNumActiveDots
  GLoadingScreen::fNumDots
  GLoadingScreen::fNumPieces
  GLoadingScreen::fUnLoadingIcon
  GLoadingScreen::fDotTAR
  GLoadingScreen::fBackPiece
  GLoadingScreen::fUnloadingIconTar
  GLoadingScreen::fUnLoadFontFile
  GLoadingScreen::fUnLoadFont

Xbox methods treated as members (1 of 14; untyped ones count when ECX is read before it is written): ClearToBlack

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[1] R [1: Print@249268]
  +0x001  w- LEA addr-taken [1: Print@249268]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
