# Handoff: Tomb Raider: Definitive Edition port

Read this before changing anything. Written October 2026 for whoever (person or AI) continues.

## Ground rules from the maintainer

- The maintainer runs every build and game run. Give **one command block** to run, never several.
- Ask for paths instead of guessing. Don't install anything without asking.
- The GitHub repository stays **private**.
- Keep the pipeline cache off (`BB_PIPELINE_CACHE=0`) until shader stutter is the task.
- Only shadPS4 changes (`gpu/`) need `bash build.sh`; Python script changes do not.

## Paths (the maintainer's machine)

- Repo: `/run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Projects/tombraider_pc`
- Game: `/run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Games/Tomb Raider/Game`
  (CUSA00109 base game with the 1.01 update copied over it)

## Build and run (the standard command)

```
cd /run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Projects/tombraider_pc && bash build.sh && BB_GAME_DIR="/run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Games/Tomb Raider/Game" BB_PIPELINE_CACHE=0 python3 run.py 2>&1 | tee out/play.log
```

- Use `python3 run.py`. **Not `run.sh`**: it still calls `scripts/link_libc.py`, which fails on this
  game's libc ("unsupported symbol encoding 'compilerrt_abort_impl'"). `run.py` uses
  `scripts/link_modules.py`, which works.
- `build.sh` exits non-zero on a failed build; errors are in `out/gpu-build.log`.
- The log of a run is `out/play.log`.
- `BB_READBACKS=2` (precise readbacks) hangs on a black screen while loading. Don't use it.

## Architecture in one paragraph

The PS4 game code runs natively. `scripts/prepare.py` and `scripts/link_modules.py` turn the dump
into a loadable image in `out/`. The C runtime in `src/` replaces the PS4 system libraries; missing
imports are listed by `tools/missing.py`, and names come from `src/import_names.inc`.
`gpu/shadps4` is shadPS4's video core (PM4 command processing in `video_core/amdgpu/liverpool.cpp`,
draws and bindings in `video_core/renderer_vulkan/vk_rasterizer.cpp`, shader translation in
`shader_recompiler/`), built into `out/gpu/libbbgpu.so`. **If you change the layout of
`Shader::Info` or `RuntimeInfo`, bump the versions in `vk_pipeline_serialization.cpp`.**

### Fixed in October 2026: TressFX hair, cave lighting, lens flare

TressFX runs as compute passes (simulation), then fill pass 2 (vs `f871bb9d932e79ab`,
ps `aa7cd3c6214fb035`), which builds a per-pixel linked list (12-byte nodes: tangent, depth, next;
counter in GDS[0]; heads table 1920x1080x4 bytes), then resolve pass 3 (ps `42d601c4e9788311`,
low 32 bits `e9788311`), which walks each list, keeps the 8 nearest fragments and blends them
into the scene target. Four separate bugs were behind the hair and cave lighting problems,
and a fifth behind the lens flare outside:

1. **Hair flicker (shader translator).** The resolve shader narrows EXEC with a plain
   `s_and_b64 exec, s0, vcc` before each node load. shadPS4's `CFG::SplitDivergenceScopes`
   only opened a divergence scope for `S_AND_SAVEEXEC_B64`, so lanes that EXEC had switched off
   still loaded and overwrote their registers; their `next` became 0 and they walked node 0, a
   random fragment from the current frame. Fix: `S_AND_B64` into EXEC opens a scope
   (`shader_recompiler/frontend/control_flow_graph.cpp`).
2. **Hair noise in some scenes (skipped clear).** The game clears the heads table with a small
   compute shader every frame. In the scene after the beach that memory had earlier held a render
   target's CMASK, and `Rasterizer::IsComputeMetaClear` treats a compute write to a known
   metadata address as a metadata clear and skips the dispatch, so the lists piled up across
   frames. Fix: each registered CMASK/FMASK/HTILE now records an upper bound of its size
   (`TextureCache::RegisterMeta`), and a write larger than that is not treated as a metadata
   clear (`texture_cache.h`, `vk_rasterizer.cpp`).
3. **Shadow atlas tiles (partial depth clear).** A register depth/stencil clear limited by the
   scissor to one shadow-atlas tile cleared the whole image. It is now a rectangle clear after
   the pass begins (`RecordPartialClear`; `BB_PARTIAL_DEPTH_CLEAR=0` turns it off). Linear
   depth exports (`PA_CL_VTE_CNTL.VTX_Z_FMT`) are honoured too (`BB_VTX_Z_FMT=0` turns it off).
4. **Light volumes (masked stencil clear).** Stencil clears with a partial write mask (the game
   resets bits 0x7 and keeps the 0x80 tag) wiped all 8 bits. They now go through the draw's own
   stencil ops (`MaskedStencilClear`; `BB_MASKED_STENCIL_CLEAR=0` turns it off).

5. **Lens flare flicker outside (occlusion queries).** The sun flare's strength comes from
   occlusion queries (two depth-only box draws at the sun). shadPS4 answered them with a
   synthetic counter on the command thread, ahead of the DMA clear of the result block that runs
   in order on the recording thread, so most results were wiped again; the game reads them a
   frame later without waiting (`0x727130` in the eboot, `Flush` thread) and the flare switched
   on and off. Now `Rasterizer::OcclusionSample` runs in order with the draws, counts the draws
   between begin and end sample with precise Vulkan occlusion queries, and writes the visible
   count into the result block when the GPU is done. The flare also fades correctly behind
   objects now (`BB_OCCLUSION_QUERIES=0`: the old synthetic counter).

The hair looks somewhat bright in dark scenes (the candle-lit cave after the opening). PS4 footage
shows the same, so this is the original look. A darker hair would be an optional mod.

The diagnostics used to find these (hair buffer dumps, CPU write watcher, hardware breakpoints,
pixel interlock, CFG trace) were removed; the full diff is in `out/hair-diagnostics/` on the
maintainer's machine. Shader patches: put a modified SPIR-V at
`user/shader/patch/fs_0x<hash>_0.spv` and run with `BB_PATCH_SHADERS=1` (spirv-tools:
`spirv-dis`, `spirv-as`, check with `spirv-val --target-env vulkan1.3`). The `.dbg-*` and
`.cur-*` files there are inactive test variants of the resolve shader (no fog, no shadow, no
cube, no specular, tangent colours); rename one to `fs_0x42d601c4e9788311_0.spv` to use it.

## Port additions: menu items

The main menu has a Quit Game item (October 2026). Cross over it opens the game's own Yes/No
popup (the pause menu's quit warning, with the platform's button prompts); Yes closes the game.

- Movie side: `tools/menu/` (see its README). The edited movie ships as a patch against the
  player's own (`scripts/menus/mainmenu-quit.bbmenu`); `run.py` runs `scripts/game_overrides.py`,
  which builds `out/game-overrides/PATCH0.*` from the game's archives, and the runtime reads
  those in place of the game's (`BB_GAME_OVERRIDES`).
- Native side: `src/runtime_menu.c`, hooked through `runtime_pad.c`'s guest callbacks: the
  movies' `ExternalInterface.call` handler (eboot 0xd1ba80) receives the item's index and the
  highlight; the pad sampling sees Cross; the popup is `0x818fc0(0xd08350(), user, listener,
  1, text)` with the listener on the game's delegate vtable (0x17018a0). The overlay's Yes/No
  box is the fallback.
- `bbgpu_toggle_fullscreen` (also F11 and Alt+Enter) is there for an Options entry that is not
  added yet; it would go in the options movie the same way.

## Open problems, in priority order

### 1. Other visual bugs

None known (October 2026). The grey line along the top edge in rain is gone since the fixes
above (heavy rain section checked); no separate cause was found.

### 2. Shader compilation stutter

Shaders compile on first use. Later: async compilation and/or enabling the pipeline cache.

### 3. Packaging

The Linux AppImage is released (v0.2.0: `packaging/appimage-tombraider.sh`, launcher
`launcher/tombraider_launcher.py`). Still to do: a Windows build with the October fixes
(`packaging/windows/`), and `run.sh`, which is still Virtual-On's (it looks for `../CUSA03173`;
use `run.py`).

### 4. Clean-up

- Quiet the repeated `SetQueueReg` warnings in `liverpool.cpp` (`ProcessCompute`).
- The diagnostics (`BB_TRACE_*`) can stay; they are off unless their variable is set.

## Diagnostics reference

| Variable | What it does |
|---|---|
| `BB_TRACE_SHADER=<hash>` | Textures, samplers and user data of a shader's first draws |
| `BB_TRACE_BUFFERS=<h32>,...` | Buffers (with `BB_TRACE_WORDS` floats), render target, blend state |
| `BB_TRACE_BINDS=<h32>` | Which cached image each texture binding resolves to |
| `BB_TRACE_COUNT=<n>` | How many draws the traces cover |
| `BB_TRACE_WAITS=1` | GPU waits on memory labels, with timestamps |
| `BB_TRACE_GDS`, `BB_TRACE_DMA`, `BB_TRACE_SMALL_TARGETS` | GDS / DMA writes, small render targets |
| `BB_DUMP_SHADERS=1` | Shader IR dumps to `user/shader/dumps` (cache must be off) |
| `BB_PATCH_SHADERS=1` | Load SPIR-V replacements from `user/shader/patch` |
| `BB_VTX_Z_FMT=0`, `BB_PARTIAL_DEPTH_CLEAR=0`, `BB_MASKED_STENCIL_CLEAR=0` | Turn off the October 2026 depth/stencil fixes (for comparison) |
| `BB_OCCLUSION_QUERIES=0` | Synthetic occlusion results instead of real queries (for comparison) |
| `BB_TRACE_MENU=1` | Methods the game invokes on its menu movies and their ExternalInterface calls |
| `BB_GAME_OVERRIDES=<folder>\|off` | Game files read in place of the game's (default: `out/game-overrides`, built by `run.py`) |
| `BB_EBOOT_BYTES=<vaddr>=<hex>,...` | Byte writes into the executable after the patches |
| `BB_SKIP_SHADERS=<hashes>` | Skip draws with those shaders |
| `BB_TOGGLE_FILE=<file>` | Live renderer toggles (bits in `gpu/shim/bbport_toggles.h`) |
| `BB_CAPTURE_TRIGGER/BY_FLIP/DIR` | Frame captures |

Hashes in `BB_TRACE_BUFFERS` and `BB_TRACE_BINDS` are the low 32 bits (e.g. `e9788311`).
