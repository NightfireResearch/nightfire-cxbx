# Functions to create by hand in DRIVING.ELF

Ghidra's create_function refused these through the MCP server, or they sit inside another function's body.
Create each (or shrink the enclosing function first), then say so: a small batch will name them.

| address | name | why it needs you |
|---|---|---|
| 0x00167c78 | InputDevice::DeadZoneScale | create_function refused |
| 0x002d3dd8 | CalcPlaneY | create_function refused |
| 0x0026ba08 | QuatMultXxYxZ | create_function refused |
| 0x0025f5a8 | CLIP_setflags2 | create_function refused |
| 0x002beb20 | sceDeci2ExReqSend | create_function refused |
| 0x0024fd78 | set_terminate | inside __default_unexpected's body |
| 0x00317dc8 | SNDMIXI_initfx | inside SNDMIXI_initfx2's body (restorefx2 is 0x190 bytes, then initfx) |
