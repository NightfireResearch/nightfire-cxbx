# ASoundManager

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (12):
  0x120d80 undefined __stdcall SetMissionOver(void)
  0x120db0 undefined NormalizedRandomNumber(void)
  0x120e60 void __thiscall BuildPaths(void * this, AListener * listener)
  0x120f90 undefined __stdcall Stop(void)
  0x121070 undefined __stdcall Pause(void)
  0x121150 undefined Resume(void)
  0x1211d0 undefined __thiscall StopSoundPos(void * this, COORD3 * param_1)
  0x121280 undefined ClearMission(void)
  0x121320 undefined Restart(void)
  0x121470 void __cdecl Init(char * param_1, bool param_2, char * param_3, char * param_4, int param_5)
  0x121d00 undefined __stdcall Shutdown(void)
  0x121e40 undefined ReportFailure(undefined1 param_1)

PS2 methods (14):
  0x2eb7a8 ASoundManager::Init
  0x2ec140 ASoundManager::Shutdown
  0x2ec238 ASoundManager::BuildPaths
  0x2ec408 ASoundManager::Stop
  0x2ec568 ASoundManager::Pause
  0x2ec6d8 ASoundManager::Resume
  0x2ec788 ASoundManager::Restart
  0x2ec968 ASoundManager::SetMissionOver
  0x2ec9a8 ASoundManager::StopSoundPos
  0x2ecb18 ASoundManager::ClearMission
  0x2ecbc0 ASoundManager::CheckDataAlignment
  0x2ece10 ASoundManager::NormalizedRandomNumber
  0x2ecf40 ASoundManager::SoundsArePlaying
  0x2ecf60 ASoundManager::ReportFailure

Sheet rows:
  ASoundManager::Init(char *, bool, char *, char *, int)
  ASoundManager::Shutdown(void)
  ASoundManager::BuildPaths(AListener &)
  ASoundManager::Stop(void)
  ASoundManager::Pause(void)
  ASoundManager::Resume(void)
  ASoundManager::Restart(void)
  ASoundManager::SetMissionOver(void)
  ASoundManager::StopSoundPos(COORD3 &)
  ASoundManager::ClearMission(void)
  ASoundManager::CheckDataAlignment(char *)
  ASoundManager::NormalizedRandomNumber(void)
  ASoundManager::SoundsArePlaying(void)
  ASoundManager::ReportFailure(char *)
  ASoundManager::fgSoundList
  ASoundManager::fgFailedStreams
  ASoundManager::fgMusicExt
  ASoundManager::fgSpeechExt
  ASoundManager::fgPath
  ASoundManager::fLoadIOP
  ASoundManager::fCinematicMode
  ASoundManager::fgCount
  ASoundManager::fgTime
  ASoundManager::fHUDFadeValue
  ASoundManager::fHUDFade
  ASoundManager::fStingerChannel
  ASoundManager::fMusicVolume
  ASoundManager::fSFXVolume
  ASoundManager::fInitialized
  ASoundManager::fMissionOver
  ASoundManager::fgIsPaused
  ASoundManager::fgMusicFile
  ASoundManager::fgSpeechFile

Xbox methods treated as members (4 of 12; untyped ones count when ECX is read before it is written): BuildPaths, Restart, Resume, StopSoundPos

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R float [1: StopSoundPos@2ec9a8]
  +0x004  w[4] R float [1: StopSoundPos@2ec9a8]
  +0x008  w[4] R float [1: StopSoundPos@2ec9a8]
  +0x05c  w[4] LEA/R addr-taken [1: BuildPaths@2ec238]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
