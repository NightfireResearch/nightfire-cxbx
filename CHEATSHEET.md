# Cheatsheet

## Defining in-memory addresses

Whilst developing an injected function which refers to some global state/data, you will need to access complex data structures in the game's existing memory rather than creating it in C.

There are some helper macros which exist:

```
U8_AT
U16_AT
U32_AT
I8_AT
I16_AT
BOOL_AT
FLOAT_AT
```

For more complex things, you might need to write your own. Eg:

```
// GameStateStack is an array of 64 uints, located at 0x0017bff0
// This definition behaves identically to a real array, ie sizeof(GameStateStack) == 64 * sizeof(uint) and accessing GameStateStack[1] reads from 0x0x0017bff4
#define GameStateStack (*(uint (*)[64])0x0017bff0)
```

or

```
// A pointer to memory address 0x0029dc00 which contains 64 SPRITE_DRAW instances (not pointers)
#define SprBuffList (*(SPRITE_DRAW(*)[64])0x0029dc00)
```


## Injection

To mark a function as injectable, prefix the function declaration with `// AUTOINJECT` - this will add an entry to the injection list. The function must have a prototype in a header that autofunc.cpp can see.


## Common mistakes

* Using PS2 memory addresses by mistake - make sure you're using the Xbox as primary reference, only go to PS2 when the logic has really been mangled.
* Only replace "in-memory" stuff with local variables when you're sure it's completely replaced and you know (have confirmed) the sizes of arrays. Otherwise, subtle bugs can come up. Eg if you identify some multiplayer-related stuff and reimplement it, but it actually turns out to be a sub-element of a larger array and so is no longer cleared as part of MP_Init.





## Common patterns

* Developers seemed to use shorts a bunch, which results in a bunch of useless `& 0xffff` in loops. Ignore that and use ints.
* Certain constants/checks show up when doing long->int
* Sometimes Xbox code will take pointers to an element/member within an array, which mangles the meaning somewhat. It's annoying and needs fixing manually.
* memcpy, strcpy etc always get inlined on Xbox. Check PS2 or recognise the patterns.
* Float/double `<=`/`>` comparisons show up as an XOR (or XNOR) of two separate comparisons, eg. `(a<b) != (a==b)` instead of `a<=b`. This isn't the original source - it's a decompiler artifact. Pre-SSE2 x86 can't use a normal `CMP`+`Jcc` for FPU comparisons (the result lands in the FPU status word, not `EFLAGS`), so the compiler emits `FCOMP` + `FNSTSW AX` + `TEST AH,mask` + `JP`/`JNP`, using the parity flag as a trick to test a combination of condition-code bits in one go. Ghidra doesn't recognise this idiom and reconstructs it as two separate relational comparisons XOR'd/XNOR'd together instead of the single operator that was actually compiled. The two forms are **not** symmetric when NaN is in play, since `a<b` and `a==b` are mutually exclusive (both false) for a NaN operand:
  * `(a<b) != (a==b)` is exactly `a<=b`, including the NaN case (both sides false) - always safe to simplify.
  * `(a<b) == (a==b)` is exactly `!(a<=b)`, **not** `a>b` - it disagrees with plain `>` specifically when NaN is involved (`!(a<=b)` is true for NaN, `a>b` is false). Only simplify this one to `>`/`<` if you're confident the operands can't be NaN (usually fine for ordinary game data like health or frame counts, but worth a moment's thought first).