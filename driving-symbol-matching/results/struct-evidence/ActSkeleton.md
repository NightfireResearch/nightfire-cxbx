# ActSkeleton

FastAlloc/constructed sizes under its tag: {'allocated': [36], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x198b0 first calls: ['UFileLoader::FileLoadz', 'dummyGetNullValue', None]

Xbox methods (9):
  0x197f0 undefined4 __thiscall GetBoneIndex(ActSkeleton * this, int param_1_00, undefined4 * param_2)
  0x19820 undefined GetStillPose(void)
  0x19830 undefined GetStillPose(undefined4 param_1)
  0x19850 undefined BlendQ(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x198b0 undefined ActSkeleton(undefined4 param_1)
  0x19ac0 undefined GetNumBones(void)
  0x19ad0 undefined BlendBones(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined
  0x19b80 undefined BlendQT(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 p
  0x19c60 undefined ~ActSkeleton(void)

PS2 methods (11):
  0x114700 ActSkeleton::StartUp
  0x114718 ActSkeleton::ShutDown
  0x114760 ActSkeleton::ActSkeleton
  0x114968 ActSkeleton::~ActSkeleton
  0x114a08 ActSkeleton::GetBoneIndex
  0x114a48 ActSkeleton::GetNumBones
  0x114a58 ActSkeleton::GetStillPose
  0x114a80 ActSkeleton::GetStillPose
  0x114aa8 ActSkeleton::BlendBones
  0x114bc8 ActSkeleton::BlendQT
  0x114c58 ActSkeleton::BlendQ

Sheet rows:
  ActSkeleton::StartUp(void)
  ActSkeleton::ShutDown(void)
  ActSkeleton::ActSkeleton(char *)
  ActSkeleton::~ActSkeleton(void)
  ActSkeleton::GetBoneIndex(char *)
  ActSkeleton::GetNumBones(void)
  ActSkeleton::GetStillPose(void)
  ActSkeleton::GetStillPose(float *)
  ActSkeleton::DumpSkeleton(EAGL::DynamicLoader *, EAGLAnim::Skel
  ActSkeleton::BlendBones(float, float *, float *, float *, bool)
  ActSkeleton::BlendQT(COORD4 &, COORD4 &, COORD4 &, COORD4 &, fl
  ActSkeleton::BlendQ(COORD4 &, COORD4 &, float)
  ActSkeleton::fSkinnedTransforms
  ActSkeleton::fSkinnedTransformsNumBones

Xbox methods treated as members (7 of 9; untyped ones count when ECX is read before it is written): ActSkeleton, BlendBones, GetBoneIndex, GetNumBones, GetStillPose, ~ActSkeleton

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: ActSkeleton@198b0, ~ActSkeleton@19c60]
  +0x004  w[4] R/W -> EAGL::DynamicLoader::GetAddr, EAGL::DynamicLoader::Release, EAGL::DynamicLoader::~DynamicLoader [3: ActSkeleton@198b0, GetBoneIndex@197f0, ~ActSkeleton@19c60]
  +0x008  w[4] R/W -> FUN_000fa340, __builtin_vec_new [5: ActSkeleton@198b0, BlendBones@19ad0, GetNumBones@19ac0, GetStillPose@19820, GetStillPose@19830]
  +0x00c  w[4] R/W [3: ActSkeleton@198b0, GetStillPose@19820, ~ActSkeleton@19c60]
  +0x010  w[4] R/W [2: ActSkeleton@198b0, ~ActSkeleton@19c60]
  +0x014  w[4] R/W [2: ActSkeleton@198b0, ~ActSkeleton@19c60]
  +0x018  w[4] W [1: ActSkeleton@198b0]
  +0x01c  w[4] R/W [1: ActSkeleton@198b0]
  +0x020  w[4] W [1: ActSkeleton@198b0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> UMemory::FastFree [2: ActSkeleton@114760, ~ActSkeleton@114968]
  +0x004  w[4] R/W -> EAGL::DynamicLoader::~DynamicLoader [2: ActSkeleton@114760, ~ActSkeleton@114968]
  +0x008  w[4] R/W [2: ActSkeleton@114760, GetNumBones@114a48]
  +0x00c  w[4] R/W -> __builtin_vec_delete [3: ActSkeleton@114760, GetStillPose@114a58, ~ActSkeleton@114968]
  +0x010  w[4] R/W -> __builtin_vec_delete [2: ActSkeleton@114760, ~ActSkeleton@114968]
  +0x014  w[4] R/W -> __builtin_vec_delete [2: ActSkeleton@114760, ~ActSkeleton@114968]
  +0x018  w[4] R/W [2: ActSkeleton@114760, BlendBones@114aa8]
  +0x01c  w[4] R/W [1: ActSkeleton@114760]
  +0x020  w[4] W [1: ActSkeleton@114760]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetStillPose: R +0x8 w4
  GetNumBones: R +0x8 w4
