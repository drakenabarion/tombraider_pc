# Menu additions

The port adds items to the game's Scaleform (GFx 10, ActionScript 2) menus. Nothing from the game
is shipped: `scripts/menus/*.bbmenu` holds only the differences, and `run.py` applies them to the
player's own files at start (`scripts/game_overrides.py`, kept in `out/game-overrides` until the
game's archives or the patches change). The runtime then reads that folder's `PATCH0.000.tiger`
and `PATCH0.manifest.bin` in place of the game's (`BB_GAME_OVERRIDES`; `off` disables it).

## Main menu: Quit Game

`edit_main3d.py` edits the P-code of `State_Main3D` in the main menu movie (PRIORITY0 entries
0a1d4a56 and cd4d322e, the same movie in both):

- shows the PS3 layout's hidden `nav_list_item_6_mc` as "$Menu_QuitLabel" after the last item,
  and keeps it there when `OnEnable` registers the items again;
- reports to the port with `ExternalInterface.call`: `BBPortQuitIndex<n>` on enter (`-1` on exit)
  and `BBPortHighlight<n>` on every highlight;
- removes the trace and debug-key code and the unused Infocast function, so the movie fits in the
  original's space.

The native side is `src/runtime_menu.c`: Cross over the item opens the game's own Yes/No popup.

## Options > Display: the port's settings

`edit_display.py` adds rows after Subtitles (now: Fullscreen; DLSS and frame rate later) to
`State_OptionsDisplay` in the options movie (PRIORITY0 entry c694569a). The game drives the page
and knows only its own four rows, so the port handles the added ones (`src/runtime_menu.c`,
`port_settings`):

- the movie reports the highlighted row: `BBPortFocus:<key>` (`BBPortFocus:` for the game's);
- while one is highlighted, it asks `BBPortGet:<key>` every frame and shows the answer (a `$`
  string is localized, e.g. `$Menu_On`); the port answers with the movie's
  SetExternalInterfaceRetVal, as the game's own handler does;
- the port changes the value on D-pad left/right or Cross.

A row is a line in `ROWS` (edit_display.py) and an entry in `port_settings`. The class is
recompiled from ActionScript source, which grows the movie; removing the `trace()` calls from
every script (`pcode.py`) makes the room.

## Rules the archive imposes

- The edited movie must not be larger than the original: the engine finds the section's texture
  table from the movie length, so `tiger.py` pads the movie to the original length and writes
  that length into the GFX header.
- Edit P-code, not ActionScript source: FFDec's P-code round trip is byte-identical; recompiling
  source grows the movie.
- PATCH0 stores only the changed sections (location offset flag 0x80); the rest keep pointing at
  PRIORITY0.

## Changing it

```
tools/menu/rebuild.sh "<game dir>" <path to ffdec.jar>
```

exports the movie, runs `edit_main3d.py`, imports the result with FFDec and writes
`scripts/menus/mainmenu-quit.bbmenu`. `scripts/game_overrides.py` rebuilds the archive on the
next run.
