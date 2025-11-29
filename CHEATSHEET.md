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