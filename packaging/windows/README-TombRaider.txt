Tomb Raider: Definitive Edition for Windows (preview, untested)
===============================================================

A native port: the original PS4 game code runs directly on Windows. No game files are
included. You need your own decrypted dump of CUSA00109 with the 1.01 update copied over it
(the folder with eboot.bin and sce_module).

Requirements
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A Vulkan 1.3 graphics card with a current driver.
- About 8 GB of free memory (RAM + page file).

Starting
- TombRaider.exe opens the launcher. On the Game page choose your game folder, then press PLAY.
- Play Tomb Raider.exe starts the game with the settings saved in the launcher, without
  opening it (for shortcuts and Steam).
- Insert or L3+R3 in game opens the settings overlay.

Known problems
- TressFX hair flickers to a silvery tone under some dynamic lighting.
- Heavy stutter while shaders compile on first use.
- Lighting flicker in caves with sunlight; a grey line at the top of the screen in rain.
- Some launcher options come from the Virtual-On launcher and may not apply.
- The Windows build is a preview: the port is developed and tested on Linux.

Data
- Saves and shader caches: user\ next to TombRaider.exe.
- Launcher options: %APPDATA%\tombraider-launcher. Generated files: out\.
- The log of the last run: user\last_run.log.

Port by drakenabarion. What it builds on is credited in CREDITS.txt; the license is in
LICENSE (GNU GPL v2 or later).
