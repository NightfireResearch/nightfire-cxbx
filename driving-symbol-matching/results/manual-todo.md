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

## 5. Namespace moves

`python apply.py --pending-namespaces` lists 37 (including P022's ECameraLockOn::~ECameraLockOn and
EAGLAnim::FnTurnBlender::~FnTurnBlender on DRIVING.ELF): run `NightfireNamespaces.py` then
`NightfireClasses.py` on each program as usual.
