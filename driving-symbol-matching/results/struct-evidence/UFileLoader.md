# UFileLoader

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (18):
  0xe3c20 void * __stdcall FileLoad(char * param_1, int param_2)
  0x116e10 bool __cdecl StartUsingBigFile(char * param_1, char * param_2, bool param_3)
  0x116ed0 undefined StopUsingBigFile(void)
  0x116f00 undefined4 __cdecl LookupAbsolutePath(char * param_1, char * param_2)
  0x116f30 undefined __cdecl AttemptBigFileExists(char * param_1)
  0x116f70 undefined __cdecl AttemptBigFileSize(char * param_1)
  0x116fc0 undefined FileLoadAt(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x117010 undefined8 __cdecl FileExists(char * param_1)
  0x1170a0 uint __cdecl FileSize(char * param_1)
  0x117200 void __stdcall DumpFileRequestList(void)
  0x1173a0 undefined Startup(void)
  0x117530 int __stdcall AttemptBigFileLoad(char * param_1, int param_2)
  0x1175a0 undefined AttemptBigFileShapeLoad(undefined4 param_1, undefined4 param_2)
  0x117610 int __stdcall FileLoad(char * param_1, int param_2, bool param_3)
  0x1176b0 void * __stdcall FileLoad(char * param_1, int param_2)
  0x1176d0 void * __stdcall FileLoadz(char * param_1, int param_2)
  0x1176f0 undefined ShapeFileLoad(undefined4 param_1, undefined4 param_2, undefined1 param_3)
  0x117790 void __cdecl ShapeFileLoad(char * param_1, undefined4 param_2)

PS2 methods (41):
  0x2d6e98 UFileLoader::Startup
  0x2d6ed0 UFileLoader::Shutdown
  0x2d6ed8 UFileLoader::StartUsingBigFile
  0x2d6fa0 UFileLoader::DumpFileRequestList
  0x2d7078 UFileLoader::StopUsingBigFile
  0x2d70b0 UFileLoader::AddFileToRequestList
  0x2d70e8 UFileLoader::InitCDDir
  0x2d70f0 UFileLoader::LookupAbsolutePath
  0x2d7138 UFileLoader::AttemptBigFileLoad
  0x2d71c8 UFileLoader::AttemptBigFileShapeLoad
  0x2d7258 UFileLoader::AttemptBigFileExists
  0x2d7288 UFileLoader::AttemptBigFileSize
  0x2d72c8 UFileLoader::FileLoadDirectFromDisk
  0x2d7300 UFileLoader::FileLoadShapeDirectFromDisk
  0x2d7338 UFileLoader::FileLoad
  0x2d73c0 UFileLoader::FileLoad
  0x2d73e0 UFileLoader::FileLoadz
  0x2d7400 UFileLoader::FileLoadPackz
  0x2d7448 UFileLoader::FileLoadAt
  0x2d74a0 UFileLoader::FileLoadAtz
  0x2d74f8 UFileLoader::FileExists
  0x2d7578 UFileLoader::FileSize
  0x2d75c8 UFileLoader::FileSizez
  0x2d7618 UFileLoader::FileLoadBigHeader
  0x2d7660 UFileLoader::FileLoadBigHeaderz
  0x2d76a8 UFileLoader::fopen
  0x2d76f0 UFileLoader::ShapeFileLoad
  0x2d7778 UFileLoader::ShapeFileLoad
  0x2d7798 UFileLoader::ShapeFileLoadz
  0x2d77b8 UFileLoader::FileOpenSync
  0x2d7820 UFileLoader::FileSave
  0x2d7880 UFileLoader::FileSavez
  0x2d78e0 UFileLoader::SetFileAttribs
  0x2d78e8 UFileLoader::FileDeleteSync
  0x2d7930 UFileLoader::GetHDPath
  0x2d7950 UFileLoader::GetCDPath
  0x2d7970 UFileLoader::FindFiles
  0x2d7a30 UFileLoader::GetAbsoluteFileName
  0x2d7ac8 UFileLoader::FileFree
  0x2d8368 UFileLoader::m_bInitialized_global_ctors
  0x2d8388 UFileLoader::m_bInitialized_global_dtors

Sheet rows:
  UFileLoader::Startup(void)
  UFileLoader::Shutdown(void)
  UFileLoader::StartUsingBigFile(char *, char *, bool)
  UFileLoader::DumpFileRequestList(char *)
  UFileLoader::StopUsingBigFile(char *)
  UFileLoader::AddFileToRequestList(char *)
  UFileLoader::InitCDDir(char *)
  UFileLoader::LookupAbsolutePath(char *, char *)
  UFileLoader::AttemptBigFileLoad(char *, int)
  UFileLoader::AttemptBigFileShapeLoad(char *, int)
  UFileLoader::AttemptBigFileExists(char *)
  UFileLoader::AttemptBigFileSize(char *)
  UFileLoader::FileLoadDirectFromDisk(char *, int, bool)
  UFileLoader::FileLoadShapeDirectFromDisk(char *, int, bool)
  UFileLoader::FileLoad(char *, int, bool)
  UFileLoader::FileLoad(char *, int)
  UFileLoader::FileLoadz(char *, int)
  UFileLoader::FileLoadPackz(char *, int)
  UFileLoader::FileLoadAt(char *, void *, long)
  UFileLoader::FileLoadAtz(char *, void *, long)
  UFileLoader::FileExists(char *)
  UFileLoader::FileSize(char *)
  UFileLoader::FileSizez(char *)
  UFileLoader::FileLoadBigHeader(char *, int)
  UFileLoader::FileLoadBigHeaderz(char *, int)
  UFileLoader::fopen(char *, char *)
  UFileLoader::ShapeFileLoad(char *, int, bool)
  UFileLoader::ShapeFileLoad(char *, int)
  UFileLoader::ShapeFileLoadz(char *, int)
  UFileLoader::FileOpenSync(char *, unsigned int, int, void *)
  UFileLoader::FileSave(char *, void *, long)
  UFileLoader::FileSavez(char *, void *, long)
  UFileLoader::SetFileAttribs(char *, unsigned int)
  UFileLoader::FileDeleteSync(char *, int)
  UFileLoader::GetHDPath(char *)
  UFileLoader::GetCDPath(char *)
  UFileLoader::FindFiles(char *)
  UFileLoader::GetAbsoluteFileName(char *, basic_string<char, str
  UFileLoader::FileFree(void *)
  UFileLoader::m_bInitialized
  UFileLoader::m_bGameInstalled
  UFileLoader::m_bUsingBigFile
  UFileLoader::m_BigFileHandle
  UFileLoader::m_BigFileName
  UFileLoader::m_BigFileDirectory
  UFileLoader::m_InstallDir
  UFileLoader::m_CDDir

Xbox methods treated as members (3 of 18; untyped ones count when ECX is read before it is written): FileLoad, LookupAbsolutePath, Startup

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[1] W [1: LookupAbsolutePath@2d70f0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
