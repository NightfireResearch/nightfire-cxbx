# Batch P015: not applied

| row | sheet name | PS2 | why |
|---|---|---|---|
| 5839 | global destructors keyed to EAGLInternal::ProfileDMDraw | 00286c88 | reject: 0x286c88 is an empty jr ra called directly (twice) from EAGL::Model::Call, not a destructor stub; no dtors stub follows the ctor |
| 6051 | EAGL::Transform::BuildRotate(float, float, float, float) | 002934e0 | reject: 0x2934e0 is a MultMatrix(a,b,c) wrapper = PostMult(Transform&, Transform*); the real BuildRotate is 0x295410 (sin/cos of degree angle, normalised axis, matches AUF), see extras |
| 6131 | EAGL::VU0_quatstoangvel(COORD3 &, COORD4 &, COORD4 &, float) | 00297480 | reject: 0x297480 just returns this+0x60 (a matrix getter); VU0_quatstoangvel is already named at 0x109ad8 |
| 6172 | EAGLInternal::InternalProfiler::~InternalProfiler(void) | 00299d68 | reject: 0x299d68 is the ctors-table stub (DATA 0x363024) calling static init with (1,0xffff) = global constructors keyed to gBufferHead; no InternalProfiler dtor found |
| 6186 | EAGL::DrawTextured::SetTexture(EAGL::TAR *) | 0029b6b8 | reject: 0x29b6b8 is a copy of DrawTextured::Init (allocs SingleDraw Coord/Colour/STQ bufs, looks up Parameters/Coordinates/Colours) = DrawClipTextured::Init, not SetTexture |
| 6407 | EAGLAnim::FnCompoundChannel::~FnCompoundChannel(void) | 002659a8 | reject: 0x2659a8 sits in the EvalEvent vtable slot and evaluates child anims; the real dtor is 0x266410 (vtable slot 1, frees children, ~FnAnimMemoryMap), see extras |
| 6522 | EAGLAnim::FnDeltaQFast::~FnDeltaQFast(void) | 0026b9d0 | reject: Body is a this-adjusting thunk forwarding a vtable call (EvalSQT slot of another class's vtable at 0x3afb1c), not a destructor; real ~FnDeltaQFast is 0x26d468 (see extras). |
| 6952 | FILESYS_seteventcb | 00253ba0 | defer: Empty 8-byte stub placed by order only. |
| 7152 | CLIP_setflags2 | 0025f8c0 | reject: Candidate is a SHAPE_type/SHAPE_depth helper; the real CLIP_setflags2 (x,y only) is unanalysed code at 0x25f5a8 (see extras). |
| 7162 | RMPASM_xformi | 0025fa20 | reject: Candidate maps bit depth 4/8 to palette size 16/256; unrelated to RMPASM_xformi. |
| 7257 | SNDMEM_display | 00307c00 | reject: Candidate clamps *size so *addr+*size fits the sound heap: it is SNDMEMI_constrain(unsigned*,int*) (row 7259, see extras), not SNDMEM_display. |
| 7711 | sceDevVif1PutFifo | 002b5220 | reject: Candidate is a static byte-zeroing loop used by sceDmaReset; not VIF1 FIFO code. |
| 8113 | scePadStateIntToStr | 002c8ff0 | reject: body returns reqState byte +0x71 (that is scePadGetReqState, row 8114), not a state-to-string function; no IntToStr code in retail |
| 8215 | _Rb_tree<CStringKey, pair<CStringKey, USymbolTable::Namespace * | 002cde60 | accepted, but the _Rb_tree shorthand or the method is unknown |
| 8559 | _Rb_tree<unsigned int, pair<unsigned int, void (*)(UData *, UGr | 002dbff8 | accepted, but the _Rb_tree shorthand or the method is unknown |
| 9320 | TRP_restore | 00304260 | reject: body is the MPEG extension_and_user_data loop (0x1b5/0x1b2 start codes, extension dispatch); the TRP trap library is absent from retail |
| 9321 | TrpDisplayRegs(void) | 00304260 | reject: same MPEG extension parser as above, nothing to do with displaying trap registers |
| 9325 | TrpOpWriteReg(unsigned int, char, char) | 00304bf0 | reject: MPEG extra_bit_information (count Get_Bits1 flags, flush 8); TRP code absent from retail |
| 9326 | TrpOpWriteImmS(unsigned int) | 00304c38 | reject: this is marker_bit(char *) (Get_Bits(1), text ignored; row 9318), not TrpOpWriteImmS |
| 9327 | TrpOpWriteImmU(unsigned int) | 00304c58 | reject: MPEG user_data helper calling next_start_code; TRP code absent from retail |
| 9660 | ActManager::fCurrentActor | 00324538 | reject: row is a data variable (ActManager::fCurrentActor); candidate is IPU DMA channel-4 code (DIntr, DMAC enable, CHCR) |
