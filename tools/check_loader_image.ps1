# ---------------------------------------------------------------------------------------------------------------
# Checks that the standalone loader was linked the way it has to be linked.
#
# action.exe only works because it *is* the image at the XBE's base address: it is linked at 0x10000, with an
# array in .text big enough to span the XBE, and with ASLR off so it actually lands there. Nothing else can
# claim that address at runtime, so if any of those three link options is lost the loader cannot work at all.
#
# All three fail at runtime rather than at build time, and the runtime failure is a clear message from
# Xbe_Map rather than a crash - but a build server would happily publish the broken executable first. This
# reads the three fields straight out of the PE header, so it is cheap enough to run on every build.
#
#   tools/check_loader_image.ps1 -Exe Release/action.exe
#
# Reading the header by hand rather than calling dumpbin, because dumpbin's location varies between machines
# and build agents and this needs no toolchain at all.
# ---------------------------------------------------------------------------------------------------------------

param(
    [Parameter(Mandatory = $true)][string]$Exe,

    # The XBE the loader has to be able to host. Defaults to the action engine's size of image; pass a larger
    # value if the loader is ever asked to carry a bigger one.
    [uint32]$RequiredSizeOfImage = 0x2FB660,
    [uint32]$RequiredImageBase = 0x10000
)

if (-not (Test-Path $Exe)) {
    Write-Output "FAIL: $Exe does not exist"
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($Exe)

# IMAGE_DOS_HEADER.e_lfanew, then past Signature (4) and IMAGE_FILE_HEADER (20) to the optional header.
$optionalHeader = [BitConverter]::ToInt32($bytes, 0x3c) + 24

$imageBase          = [BitConverter]::ToUInt32($bytes, $optionalHeader + 28)
$sizeOfImage        = [BitConverter]::ToUInt32($bytes, $optionalHeader + 56)
$dllCharacteristics = [BitConverter]::ToUInt16($bytes, $optionalHeader + 70)

Write-Output ("{0}" -f $Exe)
Write-Output ("  image base          0x{0:x8}   (need 0x{1:x})" -f $imageBase, $RequiredImageBase)
Write-Output ("  size of image       0x{0:x8}   (need at least 0x{1:x})" -f $sizeOfImage, $RequiredSizeOfImage)
Write-Output ("  DLL characteristics 0x{0:x4}" -f $dllCharacteristics)

$failed = $false

if ($imageBase -ne $RequiredImageBase) {
    Write-Output "FAIL: not linked at the XBE's base address - /BASE:0x10000 /FIXED has been lost."
    $failed = $true
}

if ($sizeOfImage -lt $RequiredSizeOfImage) {
    Write-Output "FAIL: the image does not span the XBE - the reservation array in src/loader/reserve.cpp is"
    Write-Output "      too small, or has been dropped from the target's sources."
    $failed = $true
}

# IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE. With ASLR on, the loader is relocated away from 0x10000 and the
# whole arrangement collapses.
if (($dllCharacteristics -band 0x40) -ne 0) {
    Write-Output "FAIL: ASLR is enabled - /DYNAMICBASE:NO has been lost."
    $failed = $true
}

if ($failed) { exit 1 }
Write-Output "  OK"
