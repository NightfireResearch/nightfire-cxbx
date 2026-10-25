# USymbolTable

FastAlloc/constructed sizes under its tag: {'allocated': [8], 'constructed': []}
deleting destructor 0x11b650 frees/deletes with size 0xc (call to UMemory::FastFree)
deleting destructor 0x11b650 frees/deletes with size 0x8 (call to UMemory::FastFree)
Xbox vtable 0x001a2240 (1 slots) stored by its constructor
PS2 sheet virtual table row: ['USymbolTable virtual table']
constructor 0x11b580 first calls: ['UMemory::FastAlloc', 'WSoundMap::WSoundMap']

Xbox methods (5):
  0x11a950 int __thiscall NameLookup(USymbolTable * this, int param_1_00, int * param_2, undefined4 param_3)
  0x11b110 int __thiscall RemoveNamespace(USymbolTable * this, int param_1_00, char * param_2)
  0x11b510 void __thiscall AddNamespace(USymbolTable * this, char * param_1, undefined4 param_2)
  0x11b580 undefined USymbolTable(void)
  0x11b650 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (7):
  0x1952d0 USymbolTable::Namespace_type_info_function
  0x2ccf18 USymbolTable::USymbolTable
  0x2ccfa8 USymbolTable::~USymbolTable
  0x2cd050 USymbolTable::AddNamespace
  0x2cd088 USymbolTable::RemoveNamespace
  0x2cd5e0 USymbolTable::NameLookup
  0x2ce968 USymbolTable::operator_delete

Sheet rows:
  USymbolTable::Namespace type_info function
  USymbolTable::USymbolTable(void)
  USymbolTable::~USymbolTable(void)
  USymbolTable::AddNamespace(char *, USymbolTable::Namespace *)
  USymbolTable::RemoveNamespace(char *)
  USymbolTable::NameLookup(char *, unsigned int &) const
  USymbolTable type_info function
  USymbolTable::operator new(unsigned int)
  USymbolTable::operator delete(void *, unsigned int)
  USymbolTable virtual table
  USymbolTable::Namespace type_info node
  USymbolTable type_info node

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): AddNamespace, NameLookup, RemoveNamespace, USymbolTable, scalar_deleting_destructor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: USymbolTable@11b580, scalar_deleting_destructor@11b650]
  +0x004  w[4] R/W -> FUN_0011a880, FUN_0011aba0, FUN_0011b4d0 [4: AddNamespace@11b510, RemoveNamespace@11b110, USymbolTable@11b580, scalar_deleting_destructor@11b650]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [5: AddNamespace@2cd050, NameLookup@2cd5e0, RemoveNamespace@2cd088, USymbolTable@2ccf18, ~USymbolTable@2ccfa8]
  +0x004  w[4] W [2: USymbolTable@2ccf18, ~USymbolTable@2ccfa8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
