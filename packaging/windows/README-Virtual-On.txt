Cyber Troopers Virtual-On Masterpiece 1995~2001 for Windows
===========================================================

A native port: the original PS4 game code runs directly on Windows. No game files are
included. You need your own decrypted dump of the collection (CUSA16156, the folder with
eboot.bin, sce_sys and the three games' pjv3.elf).

Requirements
- Windows 10 (1903 or later) or Windows 11, 64-bit.
- A Vulkan 1.3 graphics card with a current driver.
- About 8 GB of free memory (RAM + page file).
  Nothing else: Python and the libraries are inside this folder.

Starting
- Virtual-On.exe opens the launcher. On the Game page choose your game folder, then pick the
  Collection menu, Operation Moongate, Oratorio Tangram or Force on the Play page and press PLAY.
- Play Virtual-On.exe starts the game straight away with the settings saved in the launcher,
  without opening it (for shortcuts and Steam). Virtual-On.exe --play does the same, and
  Virtual-On.exe --play menu|omg|ot|force picks the game.
- Play page -> "Add to Steam" adds the games to your Steam library. "Desktop shortcut" puts
  the launcher on the desktop.
- Your own pictures: put menu.jpg, omg.jpg, ot.jpg and force.jpg in a folder named art next
  to Virtual-On.exe and they replace the launcher's painted posters.
- The games run at 60 FPS, as on PS4 (they count frames, so higher rates would run them too
  fast). Fullscreen is on the Display page.
- Controls page: keyboard and controller work together, all remappable. Bindings are saved
  in input.ini.

Data
- Saves and shader caches: user\ next to Virtual-On.exe (the launcher can pick another folder).
- Launcher options: %APPDATA%\virtualon-launcher. Generated files (prepared game image): out\.
- The log of the last run: user\last_run.log (the launcher's Log page shows it live).

Port by drakenabarion. What it builds on is credited in CREDITS.txt; the license is in
LICENSE (GNU GPL v2 or later).
