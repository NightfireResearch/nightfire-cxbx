# WCollider

FastAlloc/constructed sizes under its tag: {'allocated': [104], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xbe540 first calls: ['WCollider::Refresh']
constructor 0xbe620 first calls: ['WCollider::Refresh']

Xbox methods (11):
  0xbd750 undefined Validate(void)
  0xbd7b0 undefined InRegion(undefined4 param_1, undefined4 param_2)
  0xbd820 undefined InRegion(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xbd890 undefined GetWorldNormal(undefined4 param_1, undefined4 param_2)
  0xbdd40 undefined Clear(void)
  0xbe180 undefined ~WCollider(void)
  0xbe2d0 undefined PrepareRegion(void)
  0xbe420 undefined Refresh(undefined4 param_1)
  0xbe4b0 undefined Refresh(undefined4 param_1, undefined4 param_2)
  0xbe540 undefined WCollider(undefined4 param_1, undefined4 param_2, undefined1 param_3, undefined4 param_4)
  0xbe620 undefined WCollider(undefined4 param_1, undefined4 param_2)

PS2 methods (11):
  0x2117e8 WCollider::WCollider
  0x2118b8 WCollider::WCollider
  0x211988 WCollider::~WCollider
  0x211e18 WCollider::Refresh
  0x211ed0 WCollider::PrepareRegion
  0x212368 WCollider::Clear
  0x2124b8 WCollider::Validate
  0x212540 WCollider::InRegion
  0x2126d0 WCollider::InRegion
  0x2127f0 WCollider::GetWorldNormal
  0x212bf0 WCollider::WCollider_global_ctors

Sheet rows:
  WCollider::WCollider(COORD3 &, float, bool, unsigned int)
  WCollider::WCollider(COORD4 *, unsigned int)
  WCollider::~WCollider(void)
  WCollider::Refresh(COORD4 *)
  WCollider::Refresh(COORD3 &, float)
  WCollider::PrepareRegion(void)
  WCollider::Clear(void)
  WCollider::Validate(void) const
  WCollider::InRegion(COORD4 *, unsigned int) const
  WCollider::InRegion(COORD3 &, float, unsigned int) const
  WCollider::GetWorldNormal(COORD4 *, WCollisionMgr::WorldCollisi
  WCollider::InstValidationRec * __uninitialized_copy_aux<WCollid

Xbox methods treated as members (11 of 11; untyped ones count when ECX is read before it is written): Clear, GetWorldNormal, InRegion, PrepareRegion, Refresh, Validate, WCollider, ~WCollider

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x004  w[4] R/W [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x008  w[4] R/W [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x00c  w[4] W float [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x010  w- LEA addr-taken [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x01c  w[4] R/W float [4: Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x020  w- LEA addr-taken [5: PrepareRegion@be2d0, Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x02c  w[4] LEA/R/W float addr-taken [6: InRegion@bd820, PrepareRegion@be2d0, Refresh@be420, Refresh@be4b0, WCollider@be540, WCollider@be620]
  +0x030  w- LEA addr-taken [2: GetWorldNormal@bd890, PrepareRegion@be2d0]
  +0x034  w[4] R/W [6: Clear@bdd40, GetWorldNormal@bd890, Validate@bd750, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x038  w[4] R/W [5: Clear@bdd40, Validate@bd750, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x03c  w[4] R/W [4: Clear@bdd40, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x040  w- LEA addr-taken [1: PrepareRegion@be2d0]
  +0x044  w[4] R/W [6: Clear@bdd40, GetWorldNormal@bd890, PrepareRegion@be2d0, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x048  w[4] R/W [6: Clear@bdd40, GetWorldNormal@bd890, PrepareRegion@be2d0, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x04c  w[4] R/W [4: Clear@bdd40, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x050  w- LEA addr-taken [1: PrepareRegion@be2d0]
  +0x054  w[4] R/W [5: Clear@bdd40, Validate@bd750, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x058  w[4] R/W [5: Clear@bdd40, Validate@bd750, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x05c  w[4] R/W [4: Clear@bdd40, WCollider@be540, WCollider@be620, ~WCollider@be180]
  +0x060  w[1] R/W [6: Clear@bdd40, PrepareRegion@be2d0, Refresh@be4b0, Validate@bd750, WCollider@be540, WCollider@be620]
  +0x061  w[1] R/W [3: PrepareRegion@be2d0, WCollider@be540, WCollider@be620]
  +0x064  w[1, 4] R/W [8: GetWorldNormal@bd890, InRegion@bd7b0, InRegion@bd820, PrepareRegion@be2d0, Refresh@be420, Refresh@be4b0…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x008  w[4] W [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x00c  w[4] W float [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x010  w[8] LEA/W addr-taken [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x018  w[4] W [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x01c  w[4] R/W float [3: Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x020  w[4, 8] LEA/R/W float addr-taken [6: InRegion@212540, InRegion@2126d0, PrepareRegion@211ed0, Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x028  w[4] W [2: WCollider@2117e8, WCollider@2118b8]
  +0x02c  w[4] LEA/R/W float addr-taken [6: InRegion@212540, InRegion@2126d0, PrepareRegion@211ed0, Refresh@211e18, WCollider@2117e8, WCollider@2118b8]
  +0x030  w[4] LEA/R/W addr-taken [7: Clear@212368, GetWorldNormal@2127f0, PrepareRegion@211ed0, Validate@2124b8, WCollider@2117e8, WCollider@2118b8…]
  +0x034  w[4] R [1: Validate@2124b8]
  +0x03c  w[4] LEA/R/W addr-taken [6: Clear@212368, GetWorldNormal@2127f0, PrepareRegion@211ed0, WCollider@2117e8, WCollider@2118b8, ~WCollider@211988]
  +0x040  w[4] R [1: GetWorldNormal@2127f0]
  +0x048  w[4] LEA/R/W addr-taken [6: Clear@212368, PrepareRegion@211ed0, Validate@2124b8, WCollider@2117e8, WCollider@2118b8, ~WCollider@211988]
  +0x04c  w[4] R [1: Validate@2124b8]
  +0x054  w[4] R/W [6: Clear@212368, PrepareRegion@211ed0, Refresh@211e18, Validate@2124b8, WCollider@2117e8, WCollider@2118b8]
  +0x058  w[4] R/W [3: PrepareRegion@211ed0, WCollider@2117e8, WCollider@2118b8]
  +0x05c  w[4] R/W [7: GetWorldNormal@2127f0, InRegion@212540, InRegion@2126d0, PrepareRegion@211ed0, Refresh@211e18, WCollider@2117e8…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
