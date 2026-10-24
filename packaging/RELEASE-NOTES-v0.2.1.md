**Tomb Raider: Definitive Edition (PS4) running natively on Linux.** Built on bbport → SuperMedo's Windows port → uncharted_pc → virtualon_pc, with shadPS4's video core. **No game files are included.**

### What you need
- Your own decrypted dump of CUSA00109 with the 1.01 update copied over it (the folder with `eboot.bin` and `sce_module`).
- Linux x86-64 with `python3`, and a Vulkan 1.3 GPU with a current driver.

### Running
Make `Tomb-Raider-DE-x86_64.AppImage` executable and start it, set the game folder on the Game page and press Play. The first start prepares the game executable. Saves, caches and settings go to `~/.local/share/tombraider`. Insert or L3+R3 opens the settings overlay.

### New in 0.2.1
- **Quit Game on the main menu.** It asks first with the game's own Yes/No popup (PS4 button prompts); Yes closes the game. The menu change is built from your own game files when the game starts (`out/game-overrides` in the data folder); no game files are shipped.
- **F11 or Alt+Enter** switches between window and fullscreen.

### Fixed in 0.2.0
- **TressFX hair:** no more flicker under dynamic lighting, and no more noise in some scenes.
- **Lighting:** shadows and sunlight in caves no longer flicker.
- **Lens flare:** the sun flare no longer flashes outdoors, and it fades behind objects as on PS4.
- **Rain:** the grey line along the top edge of the screen is gone.

### What works
The game plays normally at 60 FPS as far as it has been tested (well past the first base camp), with sound and videos.

### Limitations
- **Shader compilation:** shaders compile on first use, so a new area can hitch briefly. The pipeline cache is off in this build.
- Linux only for now: a Windows build with these fixes follows (0.1.0's Windows zip is on the previous release).
- The launcher is adapted from the Virtual-On one (Uncharted lineage); some options there may not apply.
- Online features report offline.
