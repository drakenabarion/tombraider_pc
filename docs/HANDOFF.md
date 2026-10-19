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

## Open problems, in priority order

### 1. TressFX hair is silvery (the main open bug)

TressFX runs as compute passes (simulation), then fill pass 2 (vs `f871bb9d932e79ab`,
ps `aa7cd3c6214fb035`), which builds a per-pixel linked list (12-byte nodes: tangent, depth, next;
counter in GDS[0]), then resolve pass 3 (ps `42d601c4e9788311`, low 32 bits `e9788311`), which
blends into the RGBA8 scene target.

Already ruled out: tangents and node decode (a debug patch showed correct colourful tangents),
the GDS node counter, cube addressing, early depth tests, undefined values, texture
upload paths, and the ambient cube (image 0: filled correctly in gameplay; it only scales
brightness between 0.1 and 0.25).

What the resolve shader does (from `user/shader/dumps/fs_0x42d601c4e9788311.irprogram.txt`, made
with `BB_DUMP_SHADERS=1` and the cache off): hair base colour is a constant in the shader
(0.145, 0.106, 0.071 = brown). Colour = base x per-light diffuse (lights loop with 4-tap
shadow-map compares on image 2, the 8192x4096 shadow map at `0x1040008000`, and falloff LUTs
on images 3-9) + a white Kajiya-Kay specular that is NOT shadowed and fades as diffuse brightens.
The symptom (one flat shade, white highlight changing with the view) fits **diffuse = 0**, so
the leading suspect is the shadow-map binding: every compare fails, or the binding resolves to
the wrong image (bbport's scene proxies or upscaler redirects can swap bindings).

Next step, already built into the code but not yet run:

```
cd /run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Projects/tombraider_pc && bash build.sh && BB_GAME_DIR="/run/media/draken/e2b89716-ca77-4bf6-8f14-0fe93582fd70/Games/Tomb Raider/Game" BB_PIPELINE_CACHE=0 BB_TRACE_BINDS=e9788311 BB_TRACE_COUNT=60 python3 run.py 2>&1 | tee out/play.log
```

Then `grep "^Bind e9788311 #2:" out/play.log`. Binding 2 should be the real depth image
(`depth 1`, `usage ... dt 1`, the shadow map rendered as a depth target). If it is a colour image,
an image with `dt 0`, or `scene_proxy 1`, that is the bug: the hair samples empty memory or a
stand-in instead of the rendered shadow map. If it is the correct depth image, check the compare:
the sampler words are in the `BB_TRACE_SHADER=42d601c4e9788311` output (depth compare func 3,
less-or-equal), and a shader patch that forces the shadow factor to 1 would confirm it.

Shader patches: put a modified SPIR-V at `user/shader/patch/fs_0x<hash>_0.spv` and run with
`BB_PATCH_SHADERS=1` (spirv-tools: `spirv-dis`, `spirv-as`, check with
`spirv-val --target-env vulkan1.3`). `fs_0x42d601c4e9788311_0.spv` there now is a **debug patch
that colours the hair by tangent**. The maintainer wants it as an optional mod later. It is only
active with `BB_PATCH_SHADERS=1`.

### 2. Other visual bugs

- Hair flicker (may share the cause above).
- Lighting flicker in caves with sunlight coming in.
- Rain: grey distortion line along the top edge of the screen.

### 3. Shader compilation stutter

Shaders compile on first use. Later: async compilation and/or enabling the pipeline cache.

### 4. Packaging (needed before any binary release)

Everything in `packaging/`, `launcher/` and `run.sh` is still Virtual-On's: the launcher
(`launcher/virtualon_launcher.py`), the AppImage (`dist/Virtual-On-x86_64.AppImage`, data in
`~/.local/share/virtualon`, which would clash with the installed Virtual-On), the Windows scripts
(`packaging/windows/`, `VO_KIT`), and `run.sh` (calls `link_libc.py`; make it use what `run.py`
does). Rename to Tomb Raider, give it its own data folder, then build `appimage-linux.sh` on
Linux and `packaging/windows/build-windows.bat` on Windows. The Virtual-On repo
(`Projects/virtualon_pc`) shows how its releases were made.

### 5. Clean-up

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
| `BB_SKIP_SHADERS=<hashes>` | Skip draws with those shaders |
| `BB_TOGGLE_FILE=<file>` | Live renderer toggles (bits in `gpu/shim/bbport_toggles.h`) |
| `BB_CAPTURE_TRIGGER/BY_FLIP/DIR` | Frame captures |

Hashes in `BB_TRACE_BUFFERS` and `BB_TRACE_BINDS` are the low 32 bits (e.g. `e9788311`).
