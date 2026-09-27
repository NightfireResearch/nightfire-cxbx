# PVehicle

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x0018fa00 (83 slots) stored by its constructor
PS2 sheet virtual table row: ['PVehicle virtual table']
constructor 0x6f900 first calls: ['PhysicsObject::PhysicsObject']

Xbox methods (12):
  0x6f810 undefined8 __cdecl GetNamedAttribs(PVehicle * this, char * param_2)
  0x6f840 undefined CarPhysicsAttrib(undefined4 param_1)
  0x6f860 undefined4 __stdcall RenderNameAttrib(int * param_1)
  0x6f8c0 undefined NumColoursAttrib(undefined4 param_1)
  0x6f900 undefined PVehicle(undefined4 param_1)
  0x6f930 undefined __stdcall InitializeGlobals(void)
  0x71000 undefined MissionEditorSwitch(undefined1 param_1, undefined4 param_2)
  0x71050 undefined ~PVehicle(undefined1 param_1)
  0x71880 int __stdcall GetNameCount(void)
  0x718a0 int __stdcall NameToIndex(char * param_1)
  0x718e0 undefined GetCarNames(void)
  0x71900 undefined Shutdown(void)

PS2 methods (14):
  0x1965b8 PVehicle::GetNameCount
  0x1965e8 PVehicle::NameToIndex
  0x1966b8 PVehicle::GetName
  0x196708 PVehicle::GetCarNames
  0x196740 PVehicle::GetNamedAttribs
  0x196778 PVehicle::CarPhysicsAttrib
  0x1967a8 PVehicle::RenderNameAttrib
  0x196818 PVehicle::NumColoursAttrib
  0x1968a8 PVehicle::PVehicle
  0x1968f8 PVehicle::InitializeGlobals
  0x198428 PVehicle::Shutdown
  0x1984b8 PVehicle::MissionEditorSwitch
  0x198dc8 PVehicle::~PVehicle
  0x198e60 PVehicle::GetNameCount_global_ctors

Sheet rows:
  PVehicle type_info function
  PVehicle::GetNameCount(void)
  PVehicle::NameToIndex(char *)
  PVehicle::GetName(unsigned int)
  PVehicle::GetCarNames(void)
  PVehicle::GetNamedAttribs(char *)
  PVehicle::CarPhysicsAttrib(AttributeSet &)
  PVehicle::RenderNameAttrib(AttributeSet &)
  PVehicle::NumColoursAttrib(AttributeSet &)
  PVehicle::PVehicle(char *)
  PVehicle::InitializeGlobals(void)
  PVehicle::Shutdown(void)
  PVehicle::MissionEditorSwitch(bool, int)
  PVehicle::~PVehicle(void)
  PVehicle ** find<PVehicle **, PVehicle *>(PVehicle **, PVehicle
  PVehicle ** remove_copy<PVehicle **, PVehicle **, PVehicle *>(P
  PVehicle ** remove<PVehicle **, PVehicle *>(PVehicle **, PVehic
  PVehicle virtual table
  PVehicle type_info node

Xbox methods treated as members (4 of 12; untyped ones count when ECX is read before it is written): GetNamedAttribs, MissionEditorSwitch, PVehicle, ~PVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: PVehicle@6f900]
  +0x06c  w[4] R/W [2: MissionEditorSwitch@71000, PVehicle@6f900]
  +0x070  w[4] R/W [2: MissionEditorSwitch@71000, PVehicle@6f900]

PS2 this-relative accesses (PS2 offsets):
  +0x068  w[4] W [1: PVehicle@1968a8]
  +0x06c  w[4] R/W [2: MissionEditorSwitch@1984b8, PVehicle@1968a8]
  +0x070  w[4] R/W [2: MissionEditorSwitch@1984b8, PVehicle@1968a8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
