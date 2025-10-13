# Cheatsheet for defining in-memory addresses

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