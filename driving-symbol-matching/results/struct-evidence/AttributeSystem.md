# AttributeSystem

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [104]}
Xbox vtable 0x0018dc28 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['AttributeSystem virtual table']
constructor 0x59090 first calls: ['UMemory::FastAlloc', 'FUN_000535c0', 'UMemory::FastAlloc']

Xbox methods (16):
  0x525d0 undefined SetCollectionSection(undefined4 param_1)
  0x53990 int __thiscall CountClassNames(AttributeSystem * this, int param_1_00, char * param_2)
  0x53a40 undefined GetClassNextName(undefined4 param_1, undefined4 param_2)
  0x55a90 undefined RegisterExtensionType(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4
  0x55da0 undefined4 __thiscall MakeString(AttributeSystem * this, char * param_1)
  0x570c0 undefined CreateExtensionAttribute(undefined4 param_1, undefined4 param_2)
  0x57240 undefined __stdcall RegisterExtensionField(undefined4 param_1, char * param_2, undefined4 param_3, undefined4 
  0x57a80 undefined ProcessDafiSection(undefined4 param_1, undefined4 param_2)
  0x57ee0 undefined LoadCollection(undefined4 param_1)
  0x58cc0 undefined GetCollection(undefined4 param_1, undefined4 param_2)
  0x58e30 undefined PrepareDatabase(void)
  0x59090 AttributeSystem * __thiscall AttributeSystem(AttributeSystem * this)
  0x592b0 undefined __stdcall Kill(void)
  0x592d0 undefined ~AttributeSystem(void)
  0x59530 undefined scalar_deleting_destructor(undefined1 param_1)
  0x59550 undefined __stdcall Init(void)

PS2 methods (29):
  0x16c668 AttributeSystem::AttributeSystem
  0x16c880 AttributeSystem::~AttributeSystem
  0x16cc20 AttributeSystem::Reset
  0x16cc28 AttributeSystem::SetCollectionSection
  0x16cc48 AttributeSystem::GetCollection
  0x16cf38 AttributeSystem::LoadCollection
  0x16d0d8 AttributeSystem::FindCollection
  0x16d130 AttributeSystem::ProcessDafiSection
  0x16dd20 AttributeSystem::CreateExtensionAttribute
  0x16df50 AttributeSystem::MakeString
  0x16e140 AttributeSystem::PrepareDatabase
  0x16e2d8 AttributeSystem::CountClassNames
  0x16e3e8 AttributeSystem::GetClassNextName
  0x16e508 AttributeSystem::RegisterExtensionType
  0x16e590 AttributeSystem::RegisterExtensionField
  0x16e868 AttributeSystem::GetExtensionTypeSize
  0x16e950 AttributeSystem::GetExtensionTypeClass
  0x16ea38 AttributeSystem::GetExtensionTypeName
  0x16eb20 AttributeSystem::InitializeExtensionType
  0x16ec40 AttributeSystem::GetMetrics
  0x16ec80 AttributeSystem::ConfigEditParameters
  0x16ec90 AttributeSystem::ConfigEditParameters
  0x1738d8 AttributeSystem::Get
  0x1738e8 AttributeSystem::Init
  0x173920 AttributeSystem::Kill
  0x173958 AttributeSystem::SetSymbolTable
  0x173960 AttributeSystem::GetSymbolTable
  0x173968 AttributeSystem::LoadingEnabled
  0x173970 AttributeSystem::LoadingDatabase

Sheet rows:
  AttributeSystem::AttributeSystem(void)
  AttributeSystem::~AttributeSystem(void)
  AttributeSystem::Reset(void)
  AttributeSystem::SetCollectionSection(char *)
  AttributeSystem::GetCollection(char *, char *)
  AttributeSystem::LoadCollection(AttributeCollection *)
  AttributeSystem::FindCollection(char *, char *)
  AttributeSystem::ProcessDafiSection(DAFIHandle &, AttributeColl
  AttributeSystem::CreateExtensionAttribute(AttributeType, Attrib
  AttributeSystem::MakeString(char *)
  AttributeSystem::PrepareDatabase(void)
  AttributeSystem::CountClassNames(char *)
  AttributeSystem::GetClassNextName(char *, char *)
  AttributeSystem::RegisterExtensionType(char *, char *, unsigned
  AttributeSystem::RegisterExtensionField(AttributeType, char *,
  AttributeSystem::GetExtensionTypeSize(AttributeType)
  AttributeSystem::GetExtensionTypeClass(AttributeType)
  AttributeSystem::GetExtensionTypeName(AttributeType)
  AttributeSystem::InitializeExtensionType(AttributeType, char *,
  AttributeSystem::GetMetrics(unsigned int &, unsigned int &, uns
  AttributeSystem::Debug(void)
  AttributeSystem::DebugAttributeSet(char *, AttributeSet &)
  AttributeSystem::ConfigEditParameters(char *, char *, Attribute
  AttributeSystem::ConfigEditParameters(char *, char *, int, int,
  AttributeSystem::ConfigEditParameters(char *, char *, unsigned
  AttributeSystem::ConfigEditParameters(char *, char *, float, fl
  AttributeSystem type_info function
  AttributeSystem::Get(void)
  AttributeSystem::Init(void)
  AttributeSystem::Kill(void)
  AttributeSystem::SetSymbolTable(USymbolTable *)
  AttributeSystem::GetSymbolTable(void) const
  AttributeSystem::LoadingEnabled(void) const
  AttributeSystem::LoadingDatabase(void) const
  AttributeSystem::AttributeExtensionParserMap::AttributeExtensio
  AttributeSystem::fgThis_AttributeSystem
  AttributeSystem virtual table
  AttributeSystem type_info node

Xbox methods treated as members (15 of 16; untyped ones count when ECX is read before it is written): AttributeSystem, CountClassNames, CreateExtensionAttribute, GetClassNextName, GetCollection, Init, LoadCollection, MakeString, PrepareDatabase, ProcessDafiSection, RegisterExtensionField, RegisterExtensionType, SetCollectionSection, scalar_deleting_destructor, ~AttributeSystem

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: AttributeSystem@59090, ~AttributeSystem@592d0]
  +0x004  w[4] R/W [5: AttributeSystem@59090, CreateExtensionAttribute@570c0, RegisterExtensionField@57240, RegisterExtensionType@55a90, ~AttributeSystem@592d0]
  +0x008  w[4] R/RW/W [2: AttributeSystem@59090, RegisterExtensionType@55a90]
  +0x00c  w[4] R/W -> FUN_00056f70 [4: AttributeSystem@59090, ProcessDafiSection@57a80, RegisterExtensionField@57240, ~AttributeSystem@592d0]
  +0x010  w[4] R/W [2: AttributeSystem@59090, ~AttributeSystem@592d0]
  +0x014  w[4] R/W -> FUN_00052cd0, FUN_00058880 [5: AttributeSystem@59090, CountClassNames@53990, GetClassNextName@53a40, GetCollection@58cc0, ~AttributeSystem@592d0]
  +0x018  w[4] R/W [2: AttributeSystem@59090, ~AttributeSystem@592d0]
  +0x01c  w[4] R/W [2: AttributeSystem@59090, ~AttributeSystem@592d0]
  +0x020  w[1] LEA/W addr-taken [3: AttributeSystem@59090, LoadCollection@57ee0, SetCollectionSection@525d0]
  +0x060  w[1] R/W [4: AttributeSystem@59090, LoadCollection@57ee0, PrepareDatabase@58e30, ~AttributeSystem@592d0]
  +0x061  w[1] W [2: AttributeSystem@59090, PrepareDatabase@58e30]
  +0x064  w[4] W [1: AttributeSystem@59090]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: AttributeSystem@16c668, ~AttributeSystem@16c880]
  +0x004  w[4] R/W [9: AttributeSystem@16c668, CreateExtensionAttribute@16dd20, GetExtensionTypeClass@16e950, GetExtensionTypeName@16ea38, GetExtensionTypeSize@16e868, InitializeExtensionType@16eb20…]
  +0x008  w[4] R/W [2: AttributeSystem@16c668, RegisterExtensionType@16e508]
  +0x00c  w[4] R/W [4: AttributeSystem@16c668, ProcessDafiSection@16d130, RegisterExtensionField@16e590, ~AttributeSystem@16c880]
  +0x010  w[4] R/W [2: AttributeSystem@16c668, ~AttributeSystem@16c880]
  +0x014  w[4] R/W [5: AttributeSystem@16c668, CountClassNames@16e2d8, GetClassNextName@16e3e8, GetCollection@16cc48, ~AttributeSystem@16c880]
  +0x018  w[4] R/W -> FUN_00172228 [3: AttributeSystem@16c668, MakeString@16df50, ~AttributeSystem@16c880]
  +0x01c  w[4] R/W [3: AttributeSystem@16c668, MakeString@16df50, ~AttributeSystem@16c880]
  +0x020  w[1] LEA/W addr-taken [2: AttributeSystem@16c668, LoadCollection@16cf38]
  +0x060  w[4] R/W [5: AttributeSystem@16c668, LoadCollection@16cf38, LoadingEnabled@173968, PrepareDatabase@16e140, ~AttributeSystem@16c880]
  +0x064  w[4] R/W [3: AttributeSystem@16c668, LoadingDatabase@173970, PrepareDatabase@16e140]
  +0x068  w[4] R/W [3: AttributeSystem@16c668, GetSymbolTable@173960, SetSymbolTable@173958]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
