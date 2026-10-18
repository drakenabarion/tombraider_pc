# Local changes to gpu/third_party/fsr-vulkan

The submodule points at upstream FireBurn/FSR-Vulkan (`c64f093`). `build.sh` applies the
patches here to its working tree when they are not applied yet:

- `0001-...`: `BB_FSR4_PROFILE` (GPU time per FSR 4 pass) and `BB_FSR4_STATS` (driver
  statistics of each pass) in the FSR 4 v07 provider.
- `0002-...`: `#include <mutex>` in the FFX Vulkan backend; libc++ (the Windows MSYS2 CLANG64
  build) does not include it transitively.
- `0003-...`: on MinGW (MSYS2 CLANG64) the FFX 1.1.4 host and Vulkan backend sources get the
  portability header too, which then includes the CRT and C++ headers they use (`_countof`,
  `swprintf_s`, `log2`, placement new) that libc++ does not pull in transitively.
