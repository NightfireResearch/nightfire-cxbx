# AEngine

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x001a3058 (2 slots) stored by its constructor
PS2 sheet virtual table row: ['AEngine virtual table']
constructor 0x12edd0 first calls: []

Xbox methods (5):
  0x12edd0 undefined AEngine(void)
  0x12ede0 undefined Play(void)
  0x12ef30 undefined scalar_deleting_destructor(void)
  0x12f940 long __cdecl Get(char * param_1, AVehicle.conflict * param_2, float param_3)
  0x12f9e0 undefined RemoveAll(void)

PS2 methods (7):
  0x2fa8d8 AEngine::AEngine
  0x2fa8f0 AEngine::Play
  0x2faa90 AEngine::~AEngine
  0x2faae8 AEngine::Get
  0x2fab90 AEngine::Remove
  0x2fabe8 AEngine::RemoveAll
  0x2fbde0 AEngine::AEngine_global_ctors

Sheet rows:
  AEngine::AEngine(void)
  AEngine::Play(APath &)
  AEngine::~AEngine(void)
  AEngine::Get(char *, AVehicle &, float)
  AEngine::Remove(void)
  AEngine::RemoveAll(void)
  AEngine type_info function
  AEngine::GetVehicle(void) const
  AEngine virtual table
  AEngine type_info node

Xbox methods treated as members (4 of 5; untyped ones count when ECX is read before it is written): AEngine, Get, Play, scalar_deleting_destructor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AEngine@12edd0, scalar_deleting_destructor@12ef30]
  +0x004  w[4] W float [1: Play@12ede0]
  +0x008  w[4] W float [1: Play@12ede0]
  +0x00c  w[4] W float [1: Play@12ede0]
  +0x014  w[4] W float [1: Play@12ede0]
  +0x018  w[4] W float [1: Play@12ede0]
  +0x01c  w[4] R/W float [1: Play@12ede0]
  +0x020  w[4] R [1: Play@12ede0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W float [1: Play@2fa8f0]
  +0x004  w[4] W float [1: Play@2fa8f0]
  +0x008  w[4] W float [1: Play@2fa8f0]
  +0x010  w[4] R/W float [1: Play@2fa8f0]
  +0x014  w[4] R float [1: Play@2fa8f0]
  +0x018  w[4] R/W float [1: Play@2fa8f0]
  +0x01c  w[4] R [1: Play@2fa8f0]
  +0x020  w[4] W [2: AEngine@2fa8d8, ~AEngine@2faa90]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  AEngine: W +0x0 w4
