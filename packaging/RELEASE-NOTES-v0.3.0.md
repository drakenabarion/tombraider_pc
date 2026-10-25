**Tomb Raider: Definitive Edition (PS4) running natively on Linux.** Built on bbport → SuperMedo's Windows port → uncharted_pc → virtualon_pc, with shadPS4's video core. **No game files are included.**

### What you need
- Your own decrypted dump of CUSA00109 with the 1.01 update copied over it (the folder with `eboot.bin` and `sce_module`).
- Linux x86-64 with `python3`, and a Vulkan 1.3 GPU with a current driver.

### Running
Make `Tomb-Raider-DE-x86_64.AppImage` executable and start it, set the game folder on the Game page and press Play. The first start prepares the game executable. Saves, caches and settings go to `~/.local/share/tombraider`. Insert or L3+R3 opens the settings overlay.

### New in 0.3.0: display settings in the game's Options > Display
All of them are also on the launcher's Display page; a change in either place is kept for both (`bbport.ini`).
- **Fullscreen:** remembered between starts (also F11 and Alt+Enter).
- **Frame Rate:** 30, 40, 60 (PS4), and 120, 144, 240 (experimental). Changes apply at once.
- **Render Resolution:** 1280×720 up to 3840×2160. The game itself renders at that size (its targets, effects and hair), not an upscale. Applies on the next start; above 1440p it needs more memory.
- **Shadow Quality:** PS4, High (twice the shadow resolution), Highest (also softer shadow edges). Applies on the next start.
- **Hair Shadows:** On (PS4) or Off: Lara's hair no longer casts its flickering shadow on her face.
- **Upscaling and Sharpening:** FSR 1 or bilinear, with optional RCAS, when the rendered image is smaller than the window or screen.

### Fixed in 0.3.0
- **Motion blur above 90 FPS:** a bug in the game's own motion-blur code (it reads past the end of its camera history when frames come faster than 1/30 s, which never happened on PS4) smeared the image at high frame rates. Fixed in the game code; at 60 FPS and below it behaves exactly as before.

### Fixed in 0.2.x
- Quit Game on the main menu, with the game's own Yes/No popup.
- TressFX hair flicker and noise, cave lighting flicker, the lens flare, and the rain line at the top of the screen.

### Limitations
- **Frame rates above 60** are experimental: shadows may look slightly different at 240 FPS.
- **Shader compilation:** shaders compile on first use, so a new area can hitch briefly; changing Shadow Quality to or from Highest compiles them again.
- Linux only for now; a Windows build follows.
- Online features report offline.
