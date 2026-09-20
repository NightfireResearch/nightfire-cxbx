#!/usr/bin/env python3
# ---------------------------------------------------------------------------------------------------------------
# Checks that the standalone loader was linked the way it has to be linked.
#
# action.exe only works because it *is* the image at the XBE's base address: it is linked at 0x10000, with an
# array in .text big enough to span the XBE, ASLR off so it actually lands there, and its own code placed
# above the XBE's end so that copying the XBE in does not overwrite the thing doing the copying.
#
# All four fail at runtime rather than at build time, and the runtime failure is a clear message from
# Xbe_Map rather than a crash - but a build server would happily publish the broken executable first. This
# reads the fields straight out of the PE header, so it needs no toolchain at all.
#
#   python3 tools/check_loader_image.py build/macos/action.exe
#
# Python rather than PowerShell, which this replaced, because the cross build runs on macOS and Linux where
# pwsh is not present - and preprocess.py already makes Python a build dependency on every host.
# ---------------------------------------------------------------------------------------------------------------

import argparse
import struct
import sys

# IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE. With ASLR on, the loader is relocated away from 0x10000 and the
# whole arrangement collapses.
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040


def main():
    parser = argparse.ArgumentParser(description="Verify the standalone loader's link options.")
    parser.add_argument("exe", help="path to action.exe")
    # Defaults to the action engine's size of image; pass a larger value if the loader is ever asked to
    # carry a bigger XBE.
    parser.add_argument("--required-size-of-image", type=lambda s: int(s, 0), default=0x2FB660)
    parser.add_argument("--required-image-base", type=lambda s: int(s, 0), default=0x10000)
    args = parser.parse_args()

    try:
        data = open(args.exe, "rb").read()
    except OSError as e:
        print(f"FAIL: {args.exe}: {e}")
        return 1

    # IMAGE_DOS_HEADER.e_lfanew, then past Signature (4) and IMAGE_FILE_HEADER (20) to the optional header.
    optional_header = struct.unpack_from("<i", data, 0x3C)[0] + 24

    entry_point,        = struct.unpack_from("<I", data, optional_header + 16)
    image_base,         = struct.unpack_from("<I", data, optional_header + 28)
    size_of_image,      = struct.unpack_from("<I", data, optional_header + 56)
    dll_characteristics, = struct.unpack_from("<H", data, optional_header + 70)

    print(args.exe)
    print(f"  image base          0x{image_base:08x}   (need 0x{args.required_image_base:x})")
    print(f"  size of image       0x{size_of_image:08x}   (need at least 0x{args.required_size_of_image:x})")
    print(f"  entry point         0x{entry_point:08x}   (need at or above 0x{args.required_size_of_image:x})")
    print(f"  DLL characteristics 0x{dll_characteristics:04x}")

    failed = False

    if image_base != args.required_image_base:
        print("FAIL: not linked at the XBE's base address - /BASE:0x10000 /FIXED has been lost.")
        failed = True

    if size_of_image < args.required_size_of_image:
        print("FAIL: the image does not span the XBE - the reservation array in src/loader/reserve.cpp is")
        print("      too small, or has been dropped from the target's sources.")
        failed = True

    if dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE:
        print("FAIL: ASLR is enabled - /DYNAMICBASE:NO has been lost.")
        failed = True

    # The reservation array has to come first in .text, so that everything else - the C runtime's startup
    # code as much as the loader's own - sits above the XBE's end and survives the copy. The entry point is
    # the part of that which the PE header states outright: it is the first thing the CRT contributes, so if
    # the array is not in front of it the entry point lands inside the range the XBE is copied over.
    #
    # This is the check the PowerShell version lacked, and the MinGW build needs it: that toolchain links
    # its startup objects ahead of the target's own unless told otherwise. The loader would still run, then
    # corrupt its own return path on the way out. See the -nostartfiles handling in CMakeLists.txt.
    #
    # Compared as an RVA, which is sound only because the loader's base is the XBE's base - checked above.
    if image_base == args.required_image_base and entry_point < args.required_size_of_image:
        print("FAIL: the entry point is inside the range the XBE is copied over, so the reservation array")
        print("      in src/loader/reserve.cpp is not first in .text. Under MinGW that means the C runtime's")
        print("      startup objects were linked ahead of it; under MSVC, that reserve.cpp is no longer the")
        print("      first source listed for the target.")
        failed = True

    if failed:
        return 1
    print("  OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
