// D3D8, D3DX and XGRAPHC entry points the game reaches that Ghidra has no name for, so that
// tools/xbe_entry_points.py, which works from the names, cannot put them in d3d8Entries.inc. Kept by hand;
// the names are ours (what each is, from its code and its callers - see the replacements in d3dSeam.cpp).
// tools/function_coverage.py reads this file as it reads the generated one.
//
// XBE_ENTRY(name, address, stackBytes)

XBE_ENTRY(D3DXLoadSurfaceFromMemory, 0x0015d3bdu, 40)         // D3DX section; EAGL's texture atlas update (0x000eb910)
XBE_ENTRY(D3D_ReturnsTrue, 0x00169450u, 4)                     // D3D section; EAGL::Device::Init (0x000e5065)
XBE_ENTRY(XGBytesPerPixelFromFormat, 0x00178fb8u, 4)           // XGRPH section; EAGL's buffer and texture setup
XBE_ENTRY(XGWriteSurfaceToFile, 0x0017a8eeu, 8)                // XGRPH section; EAGL's screenshot (0x000e7890)
