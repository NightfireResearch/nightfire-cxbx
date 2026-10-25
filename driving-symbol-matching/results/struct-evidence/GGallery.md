# GGallery

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xd5d20 first calls: ['GSystem::FILE_FullPathname', 'UFileLoader::FileLoad', 'GSystem::FILE_FullPathname']

Xbox methods (31):
  0xd3470 undefined CULL_SetCanvas(void)
  0xd3840 undefined __thiscall GET_EffectsGallery(GGallery * this)
  0xd3850 int __stdcall GET_Canvas(int param_1, char * param_2)
  0xd57a0 undefined MGMT_MainShapeIndex(undefined4 param_1)
  0xd5800 undefined GET_Gallery(void)
  0xd5810 undefined GET_Puppet(undefined4 param_1, undefined4 param_2)
  0xd58f0 undefined GET_Key(undefined4 param_1, undefined4 param_2)
  0xd5910 undefined Get_InterpolatedKey(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xd59c0 undefined GET_SomeThing(undefined4 param_1)
  0xd5a00 void __thiscall SPRITE_SetState(GGallery * this, undefined4 * param_1)
  0xd5a80 undefined FONT_Load(undefined4 param_1)
  0xd5ad0 undefined FONT_Width(undefined4 param_1)
  0xd5b00 undefined FONT_Height(undefined4 param_1)
  0xd5b30 undefined QUAD_SetState(undefined4 param_1)
  0xd5b70 undefined QUAD_Interpolate(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xd5c60 undefined TRANSFORM_Vertices(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, u
  0xd5d20 undefined GGallery(undefined4 param_1)
  0xd5f50 undefined BITMAP_Draw2D(undefined4 param_1)
  0xd61e0 undefined FONT_SetScale(undefined4 param_1, undefined4 param_2)
  0xd6200 undefined FONT_Draw(undefined4 param_1, undefined4 param_2)
  0xd63a0 undefined FONT_Draw(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xd6520 undefined FONT_Draw(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd66a0 undefined LINE_Draw2DStrip(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd6750 undefined QUAD_Draw(undefined4 param_1, undefined4 param_2)
  0xd68b0 undefined QUAD_Draw(undefined4 param_1)
  0xd68c0 undefined QUAD_Draw2D(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0xd69a0 undefined SPRITE_Draw2D(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xd6d70 undefined SPRITE_Draw2D(undefined4 param_1)
  0xd6da0 undefined SPRITE_Flush(void)
  0xd6e30 undefined ~GGallery(void)
  0xd6eb0 undefined FONT_SetState(undefined4 param_1)

PS2 methods (50):
  0x236738 GGallery::GGallery
  0x236a00 GGallery::~GGallery
  0x236ac8 GGallery::MGMT_MainShapeIndex
  0x236b48 GGallery::DataIndex
  0x236b58 GGallery::GET_Gallery
  0x236b60 GGallery::GET_Canvas
  0x236c20 GGallery::GET_Name
  0x236cc0 GGallery::GET_Puppet
  0x236de8 GGallery::GET_PuppetAndGroup
  0x236f20 GGallery::GET_Texture
  0x236f30 GGallery::GET_Key
  0x236f58 GGallery::GET_Keys
  0x237078 GGallery::GET_FirstIdleKey
  0x2370e8 GGallery::Get_InterpolatedKey
  0x237210 GGallery::GET_SomeThing
  0x237278 GGallery::STATE_Reset
  0x237308 GGallery::STATE_SetDefaultRenderState
  0x237350 GGallery::BITMAP_SetState
  0x237410 GGallery::BITMAP_Draw2D
  0x2376e8 GGallery::BITMAP_Interpolate
  0x2377d8 GGallery::FONT_SetState
  0x2378a0 GGallery::FONT_SetScale
  0x2378b8 GGallery::FONT_Load
  0x237928 GGallery::FONT_Draw
  0x237a50 GGallery::FONT_Draw
  0x237b80 GGallery::FONT_Draw
  0x237ca8 GGallery::FONT_Width
  0x237ce0 GGallery::FONT_Height
  0x237d18 GGallery::LINE_SetState
  0x237d90 GGallery::LINE_Draw2D
  0x237e88 GGallery::LINE_Draw2D
  0x238028 GGallery::LINE_Draw2DStrip
  0x2380e8 GGallery::LINE_Interpolate
  0x238188 GGallery::QUAD_SetState
  0x2381c8 GGallery::QUAD_Draw
  0x2383e8 GGallery::QUAD_Draw
  0x238408 GGallery::QUAD_Draw2D
  0x238518 GGallery::QUAD_Interpolate
  0x2386c8 GGallery::SPRITE_SetState
  0x238790 GGallery::SPRITE_Draw2D
  0x238aa8 GGallery::SPRITE_Draw2D
  0x238d40 GGallery::SPRITE_Draw2D
  0x2391e8 GGallery::SPRITE_Draw2D
  0x239220 GGallery::SPRITE_Draw2D
  0x2392c8 GGallery::SPRITE_Interpolate
  0x2393b8 GGallery::SPRITE_Flush
  0x239438 GGallery::STENCIL_Begin
  0x239458 GGallery::STENCIL_Apply
  0x239478 GGallery::STENCIL_End
  0x239498 GGallery::TRANSFORM_Vertices

Sheet rows:
  GGallery::GGallery(char *)
  GGallery::~GGallery(void)
  GGallery::MGMT_MainShapeIndex(char *)
  GGallery::DataIndex(int, int)
  GGallery::GET_Gallery(void)
  GGallery::GET_Canvas(UGroup *, char *)
  GGallery::GET_Name(UGroup *, UGroup *)
  GGallery::GET_Puppet(UGroup *, char *, int, int)
  GGallery::GET_PuppetAndGroup(UGroup *, char *, UGroup **, int,
  GGallery::GET_Texture(int)
  GGallery::GET_Key(void *, int)
  GGallery::GET_Keys(void *, int, int, void **, int *, void **, i
  GGallery::GET_FirstIdleKey(UGroup *)
  GGallery::Get_InterpolatedKey(GGallery::Keys *, GGallery::Keys
  GGallery::GET_File(int)
  GGallery::STATE_Reset(void)
  GGallery::STATE_SetDefaultRenderState(void)
  GGallery::BITMAP_SetState(GGallery::Keys *)
  GGallery::BITMAP_Draw2D(GGallery::Keys *)
  GGallery::BITMAP_Interpolate(GGallery::Bitmaps *, GGallery::Bit
  GGallery::FONT_SetState(GGallery::Keys *)
  GGallery::FONT_SetScale(float, float)
  GGallery::FONT_Load(char *)
  GGallery::FONT_Draw(GGallery::Text *, char *)
  GGallery::FONT_Draw(char *, float, float, unsigned int)
  GGallery::FONT_Draw(char *, float, float)
  GGallery::FONT_Width(char *)
  GGallery::FONT_Height(char *)
  GGallery::LINE_SetState(GGallery::Keys *)
  GGallery::LINE_Draw2D(float, float, GGallery::Lines *)
  GGallery::LINE_Draw2D(GGallery::Keys *, GGallery::Lines *)
  GGallery::LINE_Draw2DStrip(COORD4 *, EAGL::Colour *, int)
  GGallery::LINE_Interpolate(GGallery::Lines *, GGallery::Lines *
  GGallery::QUAD_SetState(GGallery::Keys *)
  GGallery::QUAD_Draw(GGallery::Quads *, float)
  GGallery::QUAD_Draw(GGallery::Keys *, float)
  GGallery::QUAD_Draw2D(float, float, float, float, unsigned int)
  GGallery::QUAD_Interpolate(GGallery::Quads *, GGallery::Quads *
  GGallery::SPRITE_SetState(GGallery::Keys *)
  GGallery::SPRITE_Draw2D(float, float, float, float, float, floa
  GGallery::SPRITE_Draw2D(float, float, float, float, float, floa
  GGallery::SPRITE_Draw2D(int, int, GGallery::Keys *, GGallery::B
  GGallery::SPRITE_Draw2D(GGallery::Keys *)
  GGallery::SPRITE_Draw2D(GGallery::Keys *, GGallery::Keys *, flo
  GGallery::SPRITE_Interpolate(GGallery::Bitmaps *, GGallery::Bit
  GGallery::SPRITE_Flush(void)
  GGallery::STENCIL_Begin(void)
  GGallery::STENCIL_Apply(void)
  GGallery::STENCIL_End(void)
  GGallery::TRANSFORM_Vertices(COORD4 *, int, COORD4 *, COORD4 *,

Xbox methods treated as members (22 of 31; untyped ones count when ECX is read before it is written): BITMAP_Draw2D, FONT_Draw, FONT_Height, FONT_Load, FONT_SetScale, FONT_SetState, FONT_Width, GET_EffectsGallery, GET_Gallery, GET_SomeThing, GGallery, QUAD_Draw, QUAD_Draw2D, QUAD_SetState, SPRITE_Draw2D, SPRITE_Flush, SPRITE_SetState, ~GGallery

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> UGroup::GetArray [3: GET_Gallery@d5800, GET_SomeThing@d59c0, GGallery@d5d20]
  +0x004  w[4] R/W [2: GGallery@d5d20, ~GGallery@d6e30]
  +0x010  w- LEA addr-taken [1: ~GGallery@d6e30]
  +0x04c  w[4] R/W [2: GGallery@d5d20, ~GGallery@d6e30]
  +0x050  w[4] R/W [9: BITMAP_Draw2D@d5f50, FONT_Draw@d6200, FONT_Draw@d6520, FONT_SetState@d6eb0, GGallery@d5d20, QUAD_Draw@d6750…]
  +0x060  w[4] LEA/R/W addr-taken [7: BITMAP_Draw2D@d5f50, FONT_Draw@d6200, FONT_SetState@d6eb0, GGallery@d5d20, QUAD_Draw@d6750, QUAD_SetState@d5b30…]
  +0x064  w[4] R/W float [5: BITMAP_Draw2D@d5f50, FONT_Draw@d6200, FONT_SetState@d6eb0, QUAD_SetState@d5b30, SPRITE_SetState@d5a00]
  +0x068  w[4] R/W float [4: BITMAP_Draw2D@d5f50, FONT_SetState@d6eb0, QUAD_SetState@d5b30, SPRITE_SetState@d5a00]
  +0x070  w- LEA addr-taken [6: BITMAP_Draw2D@d5f50, FONT_SetState@d6eb0, GGallery@d5d20, QUAD_Draw@d6750, QUAD_SetState@d5b30, SPRITE_SetState@d5a00]
  +0x080  w[4] LEA/W float addr-taken [4: BITMAP_Draw2D@d5f50, GGallery@d5d20, SPRITE_Draw2D@d69a0, SPRITE_SetState@d5a00]
  +0x084  w[4] W float [3: BITMAP_Draw2D@d5f50, SPRITE_Draw2D@d69a0, SPRITE_SetState@d5a00]
  +0x088  w[4] W [1: SPRITE_SetState@d5a00]
  +0x090  w[4] W [3: FONT_SetState@d6eb0, GGallery@d5d20, SPRITE_SetState@d5a00]
  +0x0a0  w[4] W [1: GGallery@d5d20]
  +0x0a4  w[4] W [1: GGallery@d5d20]
  +0x0a8  w[4] R/W [10: FONT_Draw@d6200, FONT_Draw@d63a0, FONT_Draw@d6520, FONT_Height@d5b00, FONT_Load@d5a80, FONT_SetScale@d61e0…]
  +0x0ac  w[4] R/W [3: FONT_Load@d5a80, GGallery@d5d20, ~GGallery@d6e30]
  +0x0b0  w[1] R/W [9: BITMAP_Draw2D@d5f50, FONT_Draw@d6200, FONT_Draw@d63a0, FONT_Draw@d6520, GGallery@d5d20, QUAD_Draw2D@d68c0…]
  +0x1408  w[4] R [1: GET_EffectsGallery@d3840]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W [3: GET_Gallery@236b58, GGallery@236738, Get_InterpolatedKey@2370e8]
  +0x004  w[4] R/W -> MEM_free [2: GGallery@236738, ~GGallery@236a00]
  +0x008  w[8] W [1: Get_InterpolatedKey@2370e8]
  +0x010  w[8] LEA/W addr-taken [3: GGallery@236738, Get_InterpolatedKey@2370e8, ~GGallery@236a00]
  +0x018  w[8] W [1: Get_InterpolatedKey@2370e8]
  +0x020  w[8] W [1: Get_InterpolatedKey@2370e8]
  +0x028  w[8] W [1: Get_InterpolatedKey@2370e8]
  +0x030  w[4] W [1: Get_InterpolatedKey@2370e8]
  +0x04c  w[4] R/W [2: GGallery@236738, ~GGallery@236a00]
  +0x050  w[4] R/W [14: BITMAP_Draw2D@237410, BITMAP_SetState@237350, FONT_Draw@237928, FONT_Draw@237b80, FONT_SetState@2377d8, GGallery@236738…]
  +0x060  w[4, 8] LEA/R/W float addr-taken [11: BITMAP_Draw2D@237410, BITMAP_SetState@237350, FONT_Draw@237928, FONT_SetState@2377d8, GGallery@236738, LINE_Draw2D@237d90…]
  +0x064  w[4] R/W float [7: BITMAP_Draw2D@237410, BITMAP_SetState@237350, FONT_Draw@237928, FONT_SetState@2377d8, LINE_SetState@237d18, QUAD_SetState@238188…]
  +0x068  w[4, 8] R/W float [8: BITMAP_Draw2D@237410, BITMAP_SetState@237350, FONT_SetState@2377d8, GGallery@236738, LINE_SetState@237d18, QUAD_SetState@238188…]
  +0x070  w[8] LEA/W addr-taken [10: BITMAP_Draw2D@237410, BITMAP_SetState@237350, FONT_SetState@2377d8, GGallery@236738, LINE_Draw2D@237d90, LINE_SetState@237d18…]
  +0x078  w[8] W [7: BITMAP_SetState@237350, FONT_SetState@2377d8, GGallery@236738, LINE_SetState@237d18, QUAD_SetState@238188, SPRITE_SetState@2386c8…]
  +0x080  w[4, 8] R/W float [6: BITMAP_Draw2D@237410, BITMAP_SetState@237350, GGallery@236738, SPRITE_Draw2D@238d40, SPRITE_SetState@2386c8, STATE_Reset@237278]
  +0x084  w[4] R/W float [4: BITMAP_Draw2D@237410, BITMAP_SetState@237350, SPRITE_Draw2D@238d40, SPRITE_SetState@2386c8]
  +0x088  w[4, 8] W float [4: BITMAP_SetState@237350, GGallery@236738, SPRITE_SetState@2386c8, STATE_Reset@237278]
  +0x090  w[4] W [4: BITMAP_SetState@237350, FONT_SetState@2377d8, GGallery@236738, SPRITE_SetState@2386c8]
  +0x0a0  w[8] W [1: GGallery@236738]
  +0x0a8  w[4] R/W -> FONT_destroy, FONT_getrecta [9: FONT_Draw@237928, FONT_Draw@237a50, FONT_Draw@237b80, FONT_Height@237ce0, FONT_Load@2378b8, FONT_SetScale@2378a0…]
  +0x0ac  w[4] R/W -> FONT_destroy, UFileLoader::FileFree [3: FONT_Load@2378b8, GGallery@236738, ~GGallery@236a00]
  +0x0b0  w[4] R/W [11: BITMAP_Draw2D@237410, FONT_Draw@237928, FONT_Draw@237a50, FONT_Draw@237b80, GGallery@236738, QUAD_Draw2D@238408…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GET_EffectsGallery: R +0x1408 w4
  GET_Gallery: R +0x0 w4
  FONT_SetScale: R +0xa8 w4
