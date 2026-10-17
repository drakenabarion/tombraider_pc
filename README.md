# Virtual-On Masterpiece for Linux

**Cyber Troopers Virtual-On Masterpiece 1995~2001 (PS4, CUSA16156) running natively on Linux.**

The original PS4 game code runs directly on the PC and the graphics are translated to Vulkan.
It is not an emulator: there is no CPU emulation, and the PS4 system libraries are replaced by a
small native runtime.

**Lineage:** [bbport](https://github.com/deadinside28/bloodborne_pc) (Bloodborne, deadinside28)
→ [uncharted_pc](https://github.com/Supermedo/uncharted_pc) (Uncharted collection, SuperMedo)
→ this port. The renderer is [shadPS4](https://github.com/shadps4-emu/shadPS4)'s video core.
The git history carries all three, and the code still uses bbport's names (`BB_*` settings,
`bbport_*` files) so fixes can move between the projects.

> **No game files are included.** You need your own decrypted dump of the game (the folder
> with `eboot.bin`, `pjv3.elf`, `sce_module`, `sce_sys` and the game data). Plain decrypted
> ELF dumps work as they are; no fake-SELF signing step is needed.
> This project is not affiliated with SEGA or Sony Interactive Entertainment.

## Status

Work in progress.

- **Cyber Troopers Virtual-On** (`omg`), **Virtual-On Oratorio Tangram** (`ot`) and
  **Virtual-On Force** (`force`): boot, menus and matches play and render correctly.
- **The collection menu** (`eboot.bin`): not supported yet (needs NGS2 audio and kernel event
  flags). Each game starts directly instead.
- Audio: the port outputs through PipeWire; not checked yet.
- Online play is not available: network and RUDP calls report offline.

## Requirements

- Linux, x86-64, with a recent glibc (the builds so far use 2.43).
- A GPU with Vulkan 1.3 and a current driver. Developed on an NVIDIA GeForce RTX 5080.
- About 8 GB of free memory.

## Building

```
bash build.sh
```

builds `out/bb-probe` and `out/gpu/libbbgpu.so` (needs clang, cmake, ninja, pkg-config with
Vulkan and SDL3, FFmpeg, protobuf, OpenSSL, zlib and miniupnpc). With `DLSS_SDK_ROOT` set to a
checkout of the [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS), it also builds the optional
DLSS bridge. `packaging/appimage-linux.sh` packs an AppImage.

## Running

```
BB_GAME_DIR=/path/to/CUSA16156 BB_EXE=omg python3 run.py
```

`BB_EXE` picks the game: `omg` (Virtual-On), `ot` (Oratorio Tangram) or `force` (Force).
Saves and caches go to `user/` unless `BB_USER_DIR` says otherwise.

## Changes from the ports it is built on

- Decrypted ELF dumps load directly (`scripts/prepare.py`).
- The runtime covers the newer SDK of Virtual-On's `libc.prx` (POSIX imports from libkernel,
  thread-exit hooks, heap trace info) and the libraries the game uses offline: USB, RUDP,
  dialogs, screenshots, more save data calls (`src/runtime_extra.c` and others).
- Prepared vertex inputs are checked against the game's current vertex tables before use;
  Virtual-On rewrites those tables between the preparation and the draw.
- A DMA copy of a memory range onto itself no longer reloads the textures in that range.
  Force issues one over each mech's shadow mask right after drawing it, and the reload replaced
  the mask with stale memory: square shadows. shadPS4 has the same bug.
- A draw that samples one of its own render targets gets a barrier between the write and the
  read (`BB_FEEDBACK_BARRIER=0` turns it off).
- The upscaler's jitter also applies to passes drawn on a second scene-sized depth buffer.
- `tools/toggle_bisect.py` finds which renderer optimization breaks a game's picture while it
  runs (with `BB_TOGGLE_FILE`).

## Credits

Built on the [Uncharted: The Nathan Drake Collection PC port](https://github.com/Supermedo/uncharted_pc)
by SuperMedo, which is built on [bbport](https://github.com/deadinside28/bloodborne_pc), the
native Linux port of Bloodborne by deadinside28, and on the
[shadPS4](https://github.com/shadps4-emu/shadPS4) renderer. Video decoding (Videodec2) and the
DLSS bridge come from
[IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR).

Also used: [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) and the AMD FidelityFX SDK (FSR),
FSR 4 assets from [FireBurn/Q2RTX](https://github.com/FireBurn/Q2RTX), the
[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) (NVIDIA's DLSS libraries under NVIDIA's license),
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
