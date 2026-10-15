# A native arm64 macOS build with Apple's clang (docs/driving/macos.md 5): the standalone driving engine on SDL3, its
# GPU API on Metal. One ABI on arm64, so the game's sources compile as ordinary macOS code.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "")   # Metal's math_mode pragma

# No fused multiply-adds: arm64 always has them, and clang fuses a * b + c by default (macos.md 4)
set(CMAKE_C_FLAGS_INIT "-ffp-contract=off")
set(CMAKE_CXX_FLAGS_INIT "-ffp-contract=off -fms-extensions -fno-exceptions -Wno-invalid-offsetof -Wno-ignored-attributes")
