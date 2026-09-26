# Manual fixes

Things the matching can't do through its tools (splitting functions, renaming globals, removing labels) or that
need a human decision. 26 Sept 2026.

## 1. Split the merged unwind funclets (Driving.xbe) - script

Ghidra kept disassembling past the last `JMP` of the compiler-generated exception frame handlers and unwind
funclets at the end of `.text` (0x1503b0-0x15d36f), so 728 funclets are part of the function before them.
`results/function-splits.json` lists them (address, the function that currently swallows it, the owner and
unwind state, and the name it will get).

- Open Driving.xbe, run `ghidra/NightfireSplit.py` (it now reads the program per entry; all entries are
  Driving.xbe). It shrinks each containing function and creates the funclet.
- Then tell me: `xbox_eh.py` names them `<Owner>_Unwind_<state>` (521 now; the other 207 belong to owners
  that are still unnamed and get named when their owners do).

## 2. Swap two globals (Driving.xbe)

The render-context getters were swapped and have been fixed (batch 012); their globals still carry each
other's names:

| Address | Now | Should be |
|---|---|---|
| 0x23fb64 | gpCurrentTextureRenderContext | EAGLInternal::DevicePrivate::gpCurrentRenderContext |
| 0x23fb68 | gpCurrentRenderContext | EAGLInternal::DevicePrivate::gpCurrentTextureRenderContext |

Evidence: 0x23fb64 is written by NewRenderContext, DeleteRenderContext and SetCurrentRenderContext (0xe8a00),
as PS2's render-context global 0x3485b0; 0x23fb68 only by SetCurrentTextureRenderContext (0xe8a10), reached
from the texture render-target begin/end (RShadowMap::Begin, GrabBackBuffer). Rename via a temporary name.

## 3. Delete stale secondary labels (Driving.xbe)

Version Tracking left the old names as secondary labels where it renamed a function. The function names are
right; the labels are wrong (Symbol Table, or the listing's label at the entry):

| Function | Stale label |
|---|---|
| 0xe4030 IniFiles::IniFiles | OpenIniFile |
| 0x144460 SFILTER_addtofilterlist | SFILTER_add (keep if you like the note on the plate) |
| 0x14a4f0 MUTEX_create | REALMUTEX_create |
| 0x14a520 MUTEX_lock | REALMUTEX_lock |
| 0x14a530 MUTEX_unlock | REALMUTEX_unlock |

0x132a7b `atexit` also carries `_atexit` (source ANALYSIS, from Ghidra's library matching) - harmless.

## 4. Please look at (need a judgement)

- **Driving.xbe 0xf4310** - not inside any function. XbSymbolDatabase identifies it as
  `D3D8::D3DDevice_SetVertexShaderConstant`, but it sits in the game's EAGL code, not the D3D section. If it
  disassembles as a function, create it and I'll name it; left alone for now.
- **DRIVING.ELF 0x212640** - a reviewer took it for a third `WCollider::InRegion` overload. The sheet lists two
  (0x212540, `COORD4 *, unsigned int`; 0x2126d0, `COORD3 &, float, unsigned int`) and 0x212640 lies inside the
  first one's sheet size, so it is more likely an unlisted static or part of 0x212540. Left alone.
- **SRuleSpeed::CheckRule** - still untouched, as asked (candidates Xbox 0xb9eb0 / 0xba620; a windowed
  reviewer favoured 0xba620).

## 5. Found by the windowed review (26 Sept 2026, evening)

- **Merge two fragments (Driving.xbe):** 0xe384a is a split-off piece of `GSubtitles::LoadSubtitles` (0xe3840)
  and 0x898ee of `RPlayerCamera::TriggerAIPathAnimationCamera` (0x898d0): odd entries, no callers, running on
  the parent's registers. Delete the fragment function and let the parent's body extend over it.
- **PARTICLE_Add looks wrong on both platforms:** Xbox 0xd43a0 and PS2 0x2363b0 carry `PARTICLE_Add` but their
  bodies are AMix "Giotto" sound code (probably `SOUND_Play` or similar). The real PARTICLE_Add
  (`GFXGallery::PARTICLE_Add(UGroup *, Particles *)`, sheet row 5229) looks like Xbox 0xd42a0 / PS2
  FUN_00235ed0: a Particles lookup falling back to `RParticleLibrary::AddSystem`. Worth untangling by hand
  before anything else is named from these.
- **RigidBody::ResolveMassScaledTorque4 (Xbox 0xad470)** may be a linker-folded body shared with
  `ResolveMassScaledTorque`, like 0xad440 (which serves both `ResolveMassScaledForce` and `...Force4` and is left
  unnamed). If so the name only tells half the story; a plate note would do.
- **GFXGallery::~GFXGallery:** Xbox 0xd5170 was identified, but the PS2 address given (0x2337e0) isn't a
  function start in PS2 Ghidra - check whether PS2 needs a split there; the Xbox name is held back until then.
- **Held back, weak evidence** (named by a reviewer on call position alone): `AddCurtain` 0xd0d60,
  `RDebris::DrawShellCasings` 0xa9680 (PS2 body is an empty stub), `ExtractAlphaInfo` 0xa4ee0 (PS2 stub), and
  0xb2810, a thunk to `ResetRigidBodySP` sitting where PS2 calls `Simulation::CopyRigidBodiesToScratchPad`
  (linker folding). Name them if you're happy with that evidence.

- **Round 2 held back** (call position only): `EAGL::RenderContext::SetFrontBufferDepth` 0xe6aa0 (an empty `RET 4`),
  `SetBackBufferDepth` 0xe6ac0, `SetZBufferDepth` 0xe6b10 (the call order after SetSize in the RRenderer
  constructor), and `EAGL::GeoPrimState::GetPrimitiveType` 0xeec80 (right after SetPrimitiveType, reads its field).
- **Five EAGLAnim channel functions not yet created** (0xfdf20, 0xfdf60, 0xfdf90, 0xfdfd0, 0xfe060 - code
  pointers in the RawPoseChannel tables; likely QuatF4 / TranF3 and the *Interp variants). Added to
  `function-splits.json`; after creation they can go through review.

- **Round 3 held back:** `RShadowMap::Lock` 0xa5930 (PS2 body empty; three call sites match `SetupFrameBuffers`
  positions), `EAGL::DynamicLoader::GetElfData` 0xe5ae0 (`return this+0x10`, could be folded),
  `WCollider::PrepareRegion` 0xbe2d0 (position only), `RigidBody::ScaleObjObjForces` 0xaea40 (body matches only
  at the start), `EAGL::DrawTextured::SetTexture` 0xf5c00 (PS2 0x29d0a0 unnamed; sheet row 6186 placement loose).

## 6. Namespace moves

`python apply.py --pending-namespaces` lists the pending moves (including P022's ECameraLockOn::~ECameraLockOn and
EAGLAnim::FnTurnBlender::~FnTurnBlender on DRIVING.ELF): run `NightfireNamespaces.py` then
`NightfireClasses.py` on each program as usual.
