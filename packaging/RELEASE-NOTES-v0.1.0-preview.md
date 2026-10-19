**Preview build: Tomb Raider: Definitive Edition (PS4) running natively on Linux.** A proof of concept on a larger game, built on bbport → SuperMedo's Windows port → uncharted_pc → virtualon_pc, with shadPS4's video core. **No game files are included.**

### What you need
- Your own decrypted dump of CUSA00109 with the 1.01 update copied over it (the folder with `eboot.bin` and `sce_module`).
- Linux x86-64 with `python3`, and a Vulkan 1.3 GPU with a current driver.

### Running
Make `Tomb-Raider-DE-x86_64.AppImage` executable and start it, set the game folder on the Game page and press Play. The first start prepares the game executable. Saves, caches and settings go to `~/.local/share/tombraider`. Insert or L3+R3 opens the settings overlay.

### What works
Boots, plays the intro videos with sound, and plays to the first base camp at 60 FPS.

### Limitations
- **TressFX hair flickers under dynamic lighting:** normally dark, but in some lighting the whole head of hair flickers to a silvery tone. Under investigation.
- **Shader compilation stutter:** shaders compile on first use, so new areas stutter heavily. The pipeline cache is off in this build.
- Lighting flicker in caves where sunlight comes in.
- Rain draws a grey distortion line along the top edge of the screen.
- No Windows build yet. The launcher is adapted from the Virtual-On one (Uncharted lineage); some options there may not apply.
- Online features report offline.

Updates will be slow for the next while: the maintainer has a busy few days ahead.
