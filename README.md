# Tomb Raider: Definitive Edition for Linux (work in progress)

**Tomb Raider: Definitive Edition (PS4, CUSA00109, update 1.01) running natively on Linux.**

The original PS4 game code runs directly on the PC and the graphics are translated to Vulkan.
It is not an emulator: there is no CPU emulation, and the PS4 system libraries are replaced by a
small native runtime. This is a proof of concept of the approach on a larger, more complex game.

**Lineage:** [bbport](https://github.com/deadinside28/bloodborne_pc) (Bloodborne on Linux, deadinside28)
→ [its Windows port](https://github.com/Supermedo/bloodborne_pc) (SuperMedo)
→ [uncharted_pc](https://github.com/Supermedo/uncharted_pc) (Uncharted collection, SuperMedo)
→ virtualon_pc (Virtual-On Masterpiece) → this port. The renderer is
[shadPS4](https://github.com/shadps4-emu/shadPS4)'s video core. Naming throughout reflects that
lineage: `BB_*` settings and `bbport_*` files are kept, and the packaging, launcher and some
comments still carry Virtual-On, Uncharted or Bloodborne names.

> **No game files are included.** You need your own decrypted dump of the game: the base game
> with the 1.01 update copied over it (the folder with `eboot.bin` and `sce_module`).
> This project is not affiliated with Square Enix, Crystal Dynamics or Sony Interactive
> Entertainment.

## Status

- Boots, plays the intro videos with sound, and plays well past the first base camp at 60 FPS.
- There are no binary releases yet; build from source (below) and start with `run.py`.

## Limitations

Known problems (as of October 2026):

- **Shader compilation stutter.** Each new shader compiles on first use; large parts of the game
  stutter until compiled. Run with `BB_PIPELINE_CACHE=0` for now (the cache is off while the port
  is being debugged).
- `run.sh` is still Virtual-On's and does not work for this game. Use `run.py` (or the AppImage).
- Windows: not released yet.
- Online features report offline.

Updates will be slow for a while: the maintainer has a busy few days ahead.

## Requirements

- Linux, x86-64, with a recent glibc (the builds so far use 2.43), or Windows 10 (1903 or
  later) or Windows 11, 64-bit.
- A GPU with Vulkan 1.3 and a current driver. Developed on an NVIDIA GeForce RTX 5080.
- About 8 GB of free memory (on Windows, RAM + page file).

## Building

```
bash build.sh
```

builds `out/bb-probe` and `out/gpu/libbbgpu.so` (needs clang, cmake, ninja, pkg-config with
Vulkan and SDL3, FFmpeg, protobuf, OpenSSL, zlib and miniupnpc). With `DLSS_SDK_ROOT` set to a
checkout of the [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS), it also builds the optional
DLSS bridge.

The Windows build path (MSYS2 CLANG64, `packaging/windows/`) is inherited from Virtual-On and
has not been adapted or tested for this game.

## Running

From the source tree:

```
BB_GAME_DIR=/path/to/CUSA00109 BB_PIPELINE_CACHE=0 python3 run.py
```

`BB_GAME_DIR` is the base game with the 1.01 update copied over it. Saves and caches go to
`user/`. Insert or L3+R3 opens the settings overlay. Do not use `run.sh` or the launcher yet
(see Limitations).

## Changes for Tomb Raider

- TLS relocations through named local TLS symbols (the game's 2014 `libc.prx`), in
  `scripts/link_modules.py`.
- About 100 more imports: invitation dialog, `sceKernelSleep`, `posix_mmap`/`msync`, PlayGo,
  camera, depth and pad-tracker stubs (no camera connected), sockets reporting offline
  (`src/runtime_extra.c` and others).
- shadPS4: 32 samplers per shader (the hair resolve uses more than 16); early fragment tests
  from the depth control registers.
- Diagnostics: `BB_TRACE_WAITS`, `BB_TRACE_SHADER`, `BB_TRACE_BUFFERS`, `BB_TRACE_BINDS`,
  `BB_TRACE_GDS`, `BB_TRACE_DMA`, `BB_TRACE_SMALL_TARGETS`, `BB_UNDEF_ZERO`, `BB_PATCH_SHADERS`
  (see `docs/HANDOFF.md`).
- The settings overlay is in English.

The changes made for Virtual-On are listed in its own repository.

## Credits

Built on the [Uncharted: The Nathan Drake Collection PC port](https://github.com/Supermedo/uncharted_pc)
by SuperMedo, which is built on [bbport](https://github.com/deadinside28/bloodborne_pc), the
native Linux port of Bloodborne by deadinside28, and SuperMedo's
[Windows port of it](https://github.com/Supermedo/bloodborne_pc), with the
[shadPS4](https://github.com/shadps4-emu/shadPS4) renderer. Video decoding (Videodec2) and the
DLSS bridge come from
[IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR).

Also used: [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) and the AMD FidelityFX SDK (FSR),
FSR 4 assets from [FireBurn/Q2RTX](https://github.com/FireBurn/Q2RTX), the
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) (NVIDIA's DLSS libraries under NVIDIA's license),
the NGS2 implementation of [AnyPS5](https://github.com/boykopovar/AnyPS5) (GPL-2.0),
[LibAtrac9](https://github.com/Thealexbarney/LibAtrac9), [SDL3](https://github.com/libsdl-org/SDL),
[FFmpeg](https://ffmpeg.org), [Dear ImGui](https://github.com/ocornut/imgui),
[sirit](https://github.com/shadps4-emu/sirit), [magic_enum](https://github.com/Neargye/magic_enum),
[xbyak](https://github.com/herumi/xbyak),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[LLVM](https://llvm.org), [PyInstaller](https://pyinstaller.org),
[Pillow](https://python-pillow.org) and the Cinzel font (SIL Open Font License).

NVIDIA, GeForce RTX and DLSS are trademarks of NVIDIA Corporation. No official art is included.

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Third-party components keep their own licenses.
