# EAGL::Transform

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (28):
  0x160e0 undefined TransformPoint(undefined4 param_1, undefined4 param_2)
  0xf1500 undefined PostMult(undefined4 param_1)
  0xf1660 undefined BuildRotTrans(undefined4 param_1, undefined4 param_2)
  0xf16d0 undefined BuildRotTrans(undefined4 param_1, undefined4 param_2)
  0xf1740 undefined BuildAimedTrans(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf19a0 undefined BuildMatrix(undefined4 param_1)
  0xf19f0 undefined BuildQuatTrans(undefined4 param_1, undefined4 param_2)
  0xf2270 undefined Transpose(void)
  0xf2330 undefined Invert(undefined4 param_1, undefined4 param_2)
  0xf2790 undefined Determinant(undefined4 param_1, undefined4 param_2)
  0xf2960 undefined AppendScale(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf2a20 undefined AppendTranslate(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xf2ae0 undefined AppendRotTrans(undefined4 param_1, undefined4 param_2)
  0xf2b30 undefined AppendRotTrans(undefined4 param_1, undefined4 param_2)
  0xf2b80 undefined AppendMatrix(undefined4 param_1)
  0xf2bd0 undefined AppendQuatTrans(undefined4 param_1, undefined4 param_2)
  0xf2c30 undefined AppendAimedTrans(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf2c80 undefined PrependScale(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf2d40 undefined PrependTranslate(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xf2e00 undefined PrependRotTrans(undefined4 param_1, undefined4 param_2)
  0xf2e50 undefined PrependMatrix(undefined4 param_1)
  0xf2ea0 undefined PrependQuatTrans(undefined4 param_1, undefined4 param_2)
  0xf2f20 undefined Inverse(void)
  0xf2f50 undefined ElementMinor(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf2ff0 undefined AppendRotate(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf3040 undefined PrependRotate(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf32e0 undefined BuildRotate(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xf8780 undefined BuildSQT(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 

PS2 methods (50):
  0x292500 EAGL::Transform::BuildQuatTrans
  0x292770 EAGL::Transform::TransformPoints
  0x292930 EAGL::Transform::TransformPoints
  0x292ad0 EAGL::Transform::TransformPoints
  0x292d28 EAGL::Transform::TransformPoints
  0x292f68 EAGL::Transform::Invert
  0x293170 EAGL::Transform::TransformPoint
  0x2932d8 EAGL::Transform::TransformPoint
  0x2934e0 EAGL::Transform::PostMult
  0x293500 EAGL::Transform::PostMult
  0x293520 EAGL::Transform::BuildScale
  0x2935b8 EAGL::Transform::BuildIdentity
  0x293608 EAGL::Transform::BuildZero
  0x293658 EAGL::Transform::BuildRotTrans
  0x2936d0 EAGL::Transform::BuildRotTrans
  0x293750 EAGL::Transform::BuildAimedTrans
  0x293890 EAGL::Transform::BuildSRT
  0x293a38 EAGL::Transform::ExtractRotTrans
  0x293aa0 EAGL::Transform::BuildMatrix
  0x293c20 EAGL::Transform::TransformPoints
  0x293d40 EAGL::Transform::TransformPoints
  0x293e48 EAGL::Transform::TransformPoints
  0x293f48 EAGL::Transform::TransformPoints
  0x294030 EAGL::Transform::Inverse
  0x294188 EAGL::Transform::ElementMinor
  0x294480 EAGL::Transform::AppendScale
  0x2944e0 EAGL::Transform::AppendRotate
  0x294518 EAGL::Transform::AppendTranslate
  0x294580 EAGL::Transform::AppendRotTrans
  0x294620 EAGL::Transform::AppendRotTrans
  0x2946c0 EAGL::Transform::AppendMatrix
  0x2946e0 EAGL::Transform::AppendQuatTrans
  0x2947f0 EAGL::Transform::AppendAimedTrans
  0x294940 EAGL::Transform::PrependScale
  0x2949a0 EAGL::Transform::PrependRotate
  0x2949d8 EAGL::Transform::PrependTranslate
  0x294a40 EAGL::Transform::PrependRotTrans
  0x294ad8 EAGL::Transform::PrependRotTrans
  0x294b70 EAGL::Transform::PrependMatrix
  0x294b98 EAGL::Transform::PrependQuatTrans
  0x294ca0 EAGL::Transform::PrependAimedTrans
  0x294df0 EAGL::Transform::TransformVector
  0x294f28 EAGL::Transform::OrthoInverse
  0x295010 EAGL::Transform::OrthoInverse
  0x2950b0 EAGL::Transform::BuildSQT
  0x295188 EAGL::Transform::BuildQT
  0x295238 EAGL::Transform::ReplaceRotate
  0x2953c0 EAGL::Transform::PreMult
  0x2953e8 EAGL::Transform::PreMult
  0x295410 EAGL::Transform::BuildRotate

Sheet rows:
  EAGL::Transform::BuildQuatTrans(COORD4 *, COORD4 *)
  EAGL::Transform::ExtractQuatTrans(COORD4 *, COORD4 *) const
  EAGL::Transform::TransformPoints(unsigned int, COORD3 *, unsign
  EAGL::Transform::TransformPoints(unsigned int, COORD3 *, COORD3
  EAGL::Transform::TransformPoints(unsigned int, COORD4 *, unsign
  EAGL::Transform::TransformPoints(unsigned int, COORD4 *, COORD4
  EAGL::Transform::Invert(EAGL::Transform &, EAGL::Transform &)
  EAGL::Transform::TransformPoint(COORD3 &, COORD3 &) const
  EAGL::Transform::TransformPoint(COORD4 &, COORD4 &) const
  EAGL::Transform::BuildRotate(float, float, float, float)
  EAGL::Transform::PostMult(EAGL::Transform &, EAGL::Transform *)
  EAGL::Transform::PostMult(EAGL::Transform &)
  EAGL::Transform::BuildScale(float, float, float, float)
  EAGL::Transform::BuildTranslate(float, float, float)
  EAGL::Transform::BuildIdentity(void)
  EAGL::Transform::BuildZero(void)
  EAGL::Transform::BuildRotTrans(MATRIX3 *, COORD4 *)
  EAGL::Transform::BuildRotTrans(MATRIX3 *, COORD3 *)
  EAGL::Transform::BuildAimedTrans(COORD3 *, COORD3 *, EAGL::Tran
  EAGL::Transform::BuildSRT(float, float, float, float, float, fl
  EAGL::Transform::ExtractRotTrans(MATRIX3 *, COORD3 *) const
  EAGL::Transform::BuildMatrix(MATRIX4 *)
  EAGL::Transform::TransformPoints(unsigned int, COORD3 *, unsign
  EAGL::Transform::TransformPoints(unsigned int, COORD3 *, COORD4
  EAGL::Transform::TransformPoints(unsigned int, COORD4 *, unsign
  EAGL::Transform::TransformPoints(unsigned int, COORD4 *, COORD3
  EAGL::Transform::Inverse(EAGL::Transform *) const
  EAGL::Transform::Inverse(void)
  EAGL::Transform::Transpose(void)
  EAGL::Transform::Transpose(EAGL::Transform &, EAGL::Transform &
  EAGL::Transform::ElementMinor(EAGL::Transform &, int, int, int)
  EAGL::Transform::Determinant(EAGL::Transform &, int)
  EAGL::Transform::AddWeightedSRT(float, EAGL::Transform &)
  EAGL::Transform::AppendScale(float, float, float, float)
  EAGL::Transform::AppendRotate(float, float, float, float)
  EAGL::Transform::AppendTranslate(float, float, float)
  EAGL::Transform::AppendRotTrans(MATRIX3 *, COORD4 *)
  EAGL::Transform::AppendRotTrans(MATRIX3 *, COORD3 *)
  EAGL::Transform::AppendMatrix(MATRIX4 *)
  EAGL::Transform::AppendQuatTrans(COORD4 *, COORD4 *)
  EAGL::Transform::AppendAimedTrans(COORD3 *, COORD3 *, EAGL::Tra
  EAGL::Transform::PrependScale(float, float, float, float)
  EAGL::Transform::PrependRotate(float, float, float, float)
  EAGL::Transform::PrependTranslate(float, float, float)
  EAGL::Transform::PrependRotTrans(MATRIX3 *, COORD4 *)
  EAGL::Transform::PrependRotTrans(MATRIX3 *, COORD3 *)
  EAGL::Transform::PrependMatrix(MATRIX4 *)
  EAGL::Transform::PrependQuatTrans(COORD4 *, COORD4 *)
  EAGL::Transform::PrependAimedTrans(COORD3 *, COORD3 *, EAGL::Tr
  EAGL::Transform::TransformVector(COORD3 &, COORD3 &) const
  EAGL::Transform::OrthoInverse(EAGL::Transform *) const
  EAGL::Transform::OrthoInverse(void)
  EAGL::Transform::BuildSQT(float, float, float, float, float, fl
  EAGL::Transform::BuildQT(float, float, float, float, float, flo
  EAGL::Transform::ReplaceRotate(float, float, float, float)
  EAGL::Transform::PreMult(EAGL::Transform &, EAGL::Transform *)
  EAGL::Transform::PreMult(EAGL::Transform &)

Xbox methods treated as members (25 of 28; untyped ones count when ECX is read before it is written): AppendAimedTrans, AppendMatrix, AppendQuatTrans, AppendRotTrans, AppendRotate, AppendScale, AppendTranslate, BuildAimedTrans, BuildMatrix, BuildQuatTrans, BuildRotTrans, BuildRotate, BuildSQT, Inverse, PostMult, PrependMatrix, PrependQuatTrans, PrependRotTrans, PrependRotate, PrependScale, PrependTranslate, TransformPoint, Transpose

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W float [6: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0]
  +0x004  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x008  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x00c  w[4] W float [6: BuildAimedTrans@f1740, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, Transpose@f2270]
  +0x010  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x014  w[4] R/W float [6: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0]
  +0x018  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x01c  w[4] W float [6: BuildAimedTrans@f1740, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, Transpose@f2270]
  +0x020  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x024  w[4] R/W float [7: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0…]
  +0x028  w[4] R/W float [6: BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, TransformPoint@160e0]
  +0x02c  w[4] W float [6: BuildAimedTrans@f1740, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780, Transpose@f2270]
  +0x030  w[4] R/W float [8: BuildAimedTrans@f1740, BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780…]
  +0x034  w[4] R/W float [8: BuildAimedTrans@f1740, BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780…]
  +0x038  w[4] R/W float [8: BuildAimedTrans@f1740, BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780…]
  +0x03c  w[4] R/W [6: BuildAimedTrans@f1740, BuildQuatTrans@f19f0, BuildRotTrans@f1660, BuildRotTrans@f16d0, BuildRotate@f32e0, BuildSQT@f8780]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x004  w[4] R/W float [26: BuildIdentity@2935b8, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x008  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x00c  w[4] R/W float [16: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildQT@295188, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x010  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x014  w[4] R/W float [26: BuildIdentity@2935b8, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x018  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x01c  w[4] R/W float [16: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildQT@295188, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x020  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x024  w[4] R/W float [26: BuildIdentity@2935b8, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x028  w[4, 8] R/W float [27: BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x02c  w[4] R/W float [16: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildQT@295188, BuildRotTrans@293658, BuildRotTrans@2936d0, BuildRotate@295410…]
  +0x030  w[4, 8] R/W float [26: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658…]
  +0x034  w[4] R/W float [25: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]
  +0x038  w[4, 8] R/W float [26: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildMatrix@293aa0, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658…]
  +0x03c  w[4] R/W float [17: BuildAimedTrans@293750, BuildIdentity@2935b8, BuildQT@295188, BuildQuatTrans@292500, BuildRotTrans@293658, BuildRotTrans@2936d0…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
