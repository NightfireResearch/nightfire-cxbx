# PS2 infill conflicts

Sheet rows the retail layout places at a PS2 address whose Ghidra name disagrees. For the sheet project to settle; none of these are used.

| row | sheet name | PS2 | rule | PS2 Ghidra name | why |
|---|---|---|---|---|---|
| 247 | ActPoser::GetRootBonePosOri(MATRIX4 &) | 00112b78 | exact-run | ActSkeleton::GetNumBones | PS2 Ghidra name differs |
| 350 | ActWeapon::operator new(unsigned int) | 001184b8 | exact-run | ActWeapon::New | PS2 Ghidra name differs |
| 383 | AICharacter::operator new(unsigned int) | 00119560 | exact | AICharacter::new | PS2 Ghidra name differs |
| 384 | AICharacter::operator delete(void *, unsigned int) | 00119580 | exact | AICharacter::delete | PS2 Ghidra name differs |
| 2078 | IFeedback::Update(void) | 00165bc8 | exact | ITimer::Update | PS2 Ghidra name differs |
| 2400 | vector<Schedule *, allocator<Schedule *> >::_M_insert_aux(Sched | 0017a7c0 | exact-run | Vector_Schedule::_M_insert_aux | PS2 Ghidra name differs |
| 2401 | _List_base<TaskRecord, allocator<TaskRecord> >::clear(void) | 0017a928 | exact-run | List_TaskRecord_Clear | PS2 Ghidra name differs |
| 2463 | Explosion::operator new(unsigned int) | 0017e4f0 | exact | Explosion::_ctor | PS2 Ghidra name differs |
| 4640 | SMissionManager::GetObjectiveByID(unsigned int) | 00208a00 | count | maybeFindObjective | PS2 Ghidra name differs |
| 5021 | WSoundGroup::~WSoundGroup(void) | 00228b70 | exact-run | wSoundGroup_dtor | PS2 Ghidra name differs |
| 5595 | __terminate | 0024edf0 | exact-run | __pure_virtual | PS2 Ghidra name differs |
| 6291 | EAGL::DynamicLoader::RegisterShapes(char *) | 0029fd60 | exact-run | EAGL::DynamicLoader::ModelType_global_ctors | PS2 Ghidra name differs |
| 6949 | FILESYS_setcurrentpath | 00253590 | exact-run | setBaseDirectory | PS2 Ghidra name differs |
| 7190 | PRINT_string | 00260818 | exact-run | someDebugPrint | PS2 Ghidra name differs |
| 7272 | SNDVOICEI_isreserved(int, int) | 0030a730 | exact-run | SNDover | address already has a sheet row |
| 7281 | SNDI_equalpower(int) | 0030b318 | exact-run | SNDI_parsetimbre | address already has a sheet row |
| 7286 | SNDMEMI_printf(void *, char *,...) | 0030cac0 | exact-run | SNDSYS_restore | PS2 Ghidra name differs |
| 7300 | SNDDRV_strcat(char *, char *) | 0030d938 | exact | strcat | PS2 Ghidra name differs |
| 7340 | SNDMEM_gethighwater | 003111b8 | exact-run | SNDI_validrendermode | address already has a sheet row |
| 7362 | SNDI_Delete(void *) | 00312b00 | exact | SNDMEMI_free | PS2 Ghidra name differs |
| 7743 | RFU005 | 002b6990 | exact | ResumeIntrDispatch | PS2 Ghidra name differs |
| 7746 | RFU008 | 002b69c0 | exact | ResumeT3IntrDispatch | PS2 Ghidra name differs |
| 7754 | AddIntcHandler | 002b6a40 | exact | AddIntcHandler2 | PS2 Ghidra name differs |
| 7758 | AddDmacHandler2 | 002b6a80 | exact | AddDmacHandler | PS2 Ghidra name differs |
| 7800 | RFU060 | 002b6d20 | exact | SetupThread | PS2 Ghidra name differs |
| 7801 | RFU061 | 002b6d30 | exact | SetupHeap | PS2 Ghidra name differs |
| 7860 | RFU116 | 002b70e0 | exact | SetSyscall | PS2 Ghidra name differs |
| 7876 | sceResetttyinit | 002b71e0 | exact | sceResettyinit | PS2 Ghidra name differs |
| 8256 | UMemory::ObjectMemClass(void) | 002cfd68 | exact-run | GetSomeGlobalFileRelatedObject | PS2 Ghidra name differs |
| 8794 | AStream::Get(char *) | 002e9688 | count | AStream::Event | PS2 Ghidra name differs |
| 9292 | Error(char *) | 003037f0 | exact-run | someLibraryAssert | PS2 Ghidra name differs |
| 9377 | DEBUG_trace | 00307108 | exact-run | SNDSYS_getopts | address already has a sheet row |
| 9382 | SNDPROFILE_outputlatency | 00307850 | exact-run | SNDSYSI_timerservice | address already has a sheet row |
| 9440 | SNDREAL_exithandler(void) | 0030c990 | exact-run | SNDI_randomseeed | address already has a sheet row |
