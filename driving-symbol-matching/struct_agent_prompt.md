You are deriving Xbox (MSVC) struct layouts for classes of Nightfire's Driving engine (Driving.xbe) from pre-gathered evidence, plus return types for their methods. Work mainly from the evidence files, keep Ghidra spot checks minimal (aim for under ~20 calls in total, several functions batched per python run), and don't re-read files. Report the number of Ghidra calls at the end. If one class is very large, prioritise its size, base, and the fields its evidence shows clearly (accessors, pointer hints, sheet-named members), and leave the rest undefined.

## This packet: {KEY}
Classes: {CLASSES}
{NOTES}

Evidence (read these): Q:\nightfire-cxbx\driving-symbol-matching\results\struct-evidence\<Class>.md for each class above.
Finished layouts for context and as the OUTPUT FORMAT (skim only what's relevant): Q:\nightfire-cxbx\driving-symbol-matching\results\struct-pilot-*.json (cluster3: WWorldPos, WCollider, Simulation; cluster4: RigidBody, SimpleRigidBody, SimRandom, PVehicle, PHelicopter; cluster5: PBondCar, CarControl, Missile, Sentry, Human, AttributeSet, AttributeSystem; physicsobject: PhysicsObject; rsceneobj: RSceneObj, RAutonomousObj, RSkeletalObj; targetable: WTargetable and CARP types; and any later clusterN).

How to read an evidence file: each "+0xNNN" row is one this-relative offset seen in disassembly. w[..] = widths; R/W/RW; "float" = FPU (Xbox F*) or lwc1/swc1 (PS2); "addr-taken" = address this+N computed (LEA/addiu): an embedded member, sub-object or array; "-> X" = the loaded pointer (or taken address) was passed as `this` to X (type hint); brackets list the methods. Xbox and PS2 tables are both given (PS2 offsets = PS2 layout; the PS2 symbol sheet rows give names and signatures). Only methods in the class's own namespace were scanned (indexed accessors and other classes' code are not included; check a couple of indexed accessors by hand if arrays matter).

## Layout rules learned so far (important)
- Vptr: GCC 2.95 (PS2) usually puts it LAST in the class that introduces it, MSVC (Xbox) FIRST, giving +4 on Xbox for that class's own fields only; derived classes' fields then sit at the SAME offset on both builds. Not universal: PS2 AttributeSystem keeps its vptr at +0. Check the PS2 constructor's vtable store (`sw v0,N(sN)` with v0 = a lui/addiu vtable address) before assuming.
- bool: 4 bytes PS2, 1 byte Xbox; adjacent bools pack on Xbox (later fields shift negatively, or stay if padded). Bool arrays too (PBondCar glare: int[16] on PS2, bool[16] on Xbox).
- Vectors: Xbox COORD3 is 12 bytes; PS2 sometimes pads vectors to 16 (and matrices), sometimes not; PS2 also pads before a COORD3 for alignment. Never blanket-apply a shift; follow it field by field and trust the Xbox table.
- MSVC std::vector on this build is 16 bytes (allocator, first, last, end), GCC's 12; MSVC tree 12 bytes.
- Array lengths can differ between builds (Simulation: 64 rigid bodies on Xbox, 48 on PS2).
- Size: deleting destructor FastFree size, allocation size, or pool stride. "constructed" sizes can include derived classes; several allocation sizes under one tag usually mean subclasses.
- A vtable store after a constructor's body can be exception-handling cleanup of an embedded member (Missile/RMissileStreak).
- PS2 methods returning a struct by value take `this` in a1 (result buffer in a0).
- Accessors can mislead (some read a global pool); Xbox 64-bit fields show as dword pairs; decompiler text hides/garbles offsets: prefer disassembly.
- Represent a base as "base": "<Base>" (the tool adds super_<Base> at 0) and list only the class's own fields.

## Types you may use
Builtins (byte, bool, char, short, ushort, int, uint, float, double, void, undefined4…), `X *`, `T[n]` (decimal n). Types already in Ghidra include every class in the finished layouts above plus COORD3 (12), COORD4 (16), MATRIX4, _VECTOR, IFeedback, ABaseSound, UGroup, WSimpleZone, RigidBodyInfo, ActionQueue, DamageZone, RMissileStreak, WHEEL_INFO1. For a class that may not exist, add a placeholder {"name": "...", "size": N, "if_missing": true, "fields": []} (only when you know its size; otherwise use `void *` and name the intended type in the evidence). Invented field names in lowerCamelCase, with "name invented" in the evidence.

## Optional Ghidra spot checks (read-only)
From Q:\nightfire-cxbx\driving-symbol-matching:
  from lib import ghidra_ro as g
  print(g.get("disassemble_function", program=g.XBOX, address="0x6d350"))   # or program=g.PS2
Do NOT write to Ghidra; create no files in the repo except the output; scratch goes in C:\Users\Charlie\AppData\Local\Temp\claude\Q--nightfire-cxbx\3928696e-f09a-5adc-838d-c984e1b6f83c\scratchpad.

## Output
Write Q:\nightfire-cxbx\driving-symbol-matching\results\struct-pilot-{KEY}.json:
{"classes": [{"name","size","base","vtable","comment","fields":[{"offset":"0x..","size":N,"type","name","confidence":"high|medium|low","evidence":"short, cite offsets/methods on both builds"}]}, ...placeholders],
 "vtable_slots": [],
 "returns": {"0x...": "type", ...},   // Xbox address -> return type, only with evidence; "void" when clearly nothing
 "skip_prototypes": [],
 "notes": ["ambiguities, PS2-vs-Xbox shifts, suspected misnamed functions (addresses, what they really are, the evidence)"]}
Only fields with evidence; hex-string offsets; within class size, non-overlapping, type sizes matching (check with a short script); valid JSON.

Final message: a short summary per class (size, confidence, key shifts), the number of Ghidra calls, and anything surprising (misnamed functions especially).
