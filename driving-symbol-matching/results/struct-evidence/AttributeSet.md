# AttributeSet

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x52020 first calls: []
constructor 0x58f00 first calls: ['AttributeSystem::GetCollection', 'AttributeSystem::LoadCollection']

Xbox methods (15):
  0x52020 undefined AttributeSet(undefined4 param_1)
  0x52030 undefined4 __fastcall Name(int * this)
  0x57a40 void __thiscall ~AttributeSet(AttributeSet * this, int * param_1)
  0x58020 undefined FindAttribute(undefined4 param_1, undefined4 param_2)
  0x580e0 undefined LookupBool(undefined4 param_1, undefined4 param_2)
  0x58180 undefined4 __stdcall LookupInt(char * param_1, undefined * param_2)
  0x58220 undefined LookupUInt(undefined4 param_1, undefined4 param_2)
  0x582c0 float __stdcall LookupFloat(char * param_1, undefined * param_2)
  0x58360 undefined LookupVector(undefined4 param_1, undefined4 param_2)
  0x583a0 undefined4 __stdcall LookupString(char * param_1, undefined * param_2)
  0x583e0 undefined LookupValidString(undefined4 param_1, undefined4 param_2)
  0x58420 undefined LookupStruct(undefined4 param_1, undefined4 param_2)
  0x58f00 int * __thiscall AttributeSet(AttributeSet * this, int * param_1_00, char * b, char * param_3)
  0x58f40 undefined SetName(undefined4 param_1, undefined1 param_2)
  0x752f0 void __thiscall ~AttributeSet(AttributeSet * this, int * param_1)

PS2 methods (44):
  0x1696d0 AttributeSet::AttributeSet
  0x1696f0 AttributeSet::AttributeSet
  0x169728 AttributeSet::~AttributeSet
  0x1697c0 AttributeSet::Name
  0x1697d0 AttributeSet::Class
  0x1697e0 AttributeSet::SetName
  0x1698f8 AttributeSet::SetParent
  0x1699f8 AttributeSet::FindAttribute
  0x169aa0 AttributeSet::Exists
  0x169ac0 AttributeSet::IsArray
  0x169af8 AttributeSet::Type
  0x169b28 AttributeSet::LookupBool
  0x169bf0 AttributeSet::LookupInt
  0x169cb8 AttributeSet::LookupUInt
  0x169d80 AttributeSet::LookupFloat
  0x169e48 AttributeSet::LookupVector
  0x169eb0 AttributeSet::LookupMatrix
  0x169f18 AttributeSet::LookupString
  0x169f78 AttributeSet::LookupValidString
  0x169fe0 AttributeSet::LookupSymbol
  0x16a040 AttributeSet::LookupStruct
  0x16a0a8 AttributeSet::LookupBoolArray
  0x16a118 AttributeSet::LookupIntArray
  0x16a188 AttributeSet::LookupUIntArray
  0x16a1f8 AttributeSet::LookupFloatArray
  0x16a268 AttributeSet::LookupVectorArray
  0x16a2c8 AttributeSet::LookupMatrixArray
  0x16a328 AttributeSet::AddBool
  0x16a448 AttributeSet::AddInt
  0x16a568 AttributeSet::AddUInt
  0x16a688 AttributeSet::AddFloat
  0x16a7a8 AttributeSet::AddVector
  0x16a930 AttributeSet::AddMatrix
  0x16aab8 AttributeSet::AddString
  0x16ac08 AttributeSet::AddSymbol
  0x16ad28 AttributeSet::AddStruct
  0x16aed8 AttributeSet::AddBoolArray
  0x16b068 AttributeSet::AddIntArray
  0x16b1f8 AttributeSet::AddUIntArray
  0x16b388 AttributeSet::AddFloatArray
  0x16b518 AttributeSet::AddVectorArray
  0x16b6a8 AttributeSet::AddMatrixArray
  0x16b838 AttributeSet::Remove
  0x173c20 AttributeSet::AttributeSet_global_ctors

Sheet rows:
  AttributeSet::AttributeSet(AttributeSet &)
  AttributeSet::AttributeSet(char *, char *)
  AttributeSet::~AttributeSet(void)
  AttributeSet::Name(void) const
  AttributeSet::Class(void) const
  AttributeSet::SetName(char *, bool)
  AttributeSet::SetParent(char *)
  AttributeSet::FindAttribute(char *, AttributeValue *&) const
  AttributeSet::Exists(char *) const
  AttributeSet::IsArray(char *) const
  AttributeSet::Type(char *) const
  AttributeSet::LookupBool(char *, bool *) const
  AttributeSet::LookupInt(char *, bool *) const
  AttributeSet::LookupUInt(char *, bool *) const
  AttributeSet::LookupFloat(char *, bool *) const
  AttributeSet::LookupVector(char *, bool *) const
  AttributeSet::LookupMatrix(char *, bool *) const
  AttributeSet::LookupString(char *, bool *) const
  AttributeSet::LookupValidString(char *, bool *) const
  AttributeSet::LookupSymbol(char *, bool *) const
  AttributeSet::LookupStruct(char *, AttributeType, bool *) const
  AttributeSet::LookupBoolArray(char *, unsigned int &, bool *) c
  AttributeSet::LookupIntArray(char *, unsigned int &, bool *) co
  AttributeSet::LookupUIntArray(char *, unsigned int &, bool *) c
  AttributeSet::LookupFloatArray(char *, unsigned int &, bool *)
  AttributeSet::LookupVectorArray(char *, unsigned int &, bool *)
  AttributeSet::LookupMatrixArray(char *, unsigned int &, bool *)
  AttributeSet::AddBool(char *, bool)
  AttributeSet::AddInt(char *, int)
  AttributeSet::AddUInt(char *, unsigned int)
  AttributeSet::AddFloat(char *, float)
  AttributeSet::AddVector(char *, COORD4 &, bool)
  AttributeSet::AddMatrix(char *, MATRIX4 &, bool)
  AttributeSet::AddString(char *, char *, bool)
  AttributeSet::AddSymbol(char *, void *)
  AttributeSet::AddStruct(char *, AttributeType, char *, void *,
  AttributeSet::AddBoolArray(char *, bool *, unsigned int, bool)
  AttributeSet::AddIntArray(char *, int *, unsigned int, bool)
  AttributeSet::AddUIntArray(char *, unsigned int *, unsigned int
  AttributeSet::AddFloatArray(char *, float *, unsigned int, bool
  AttributeSet::AddVectorArray(char *, COORD4 *, unsigned int, bo
  AttributeSet::AddMatrixArray(char *, MATRIX4 *, unsigned int, b
  AttributeSet::Remove(char *)

Xbox methods treated as members (8 of 15; untyped ones count when ECX is read before it is written): AttributeSet, FindAttribute, LookupStruct, Name, SetName, ~AttributeSet

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [7: AttributeSet@52020, AttributeSet@58f00, FindAttribute@58020, LookupStruct@58420, Name@52030, SetName@58f40…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [25: AddBool@16a328, AddBoolArray@16aed8, AddFloat@16a688, AddFloatArray@16b388, AddInt@16a448, AddIntArray@16b068…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  AttributeSet: W +0x0 w4
  Name: R +0x0 w4
