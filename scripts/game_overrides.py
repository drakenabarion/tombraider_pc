#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Game files the port changes, built from the player's own: the menus/ patches applied to the
game's movies and packed into a PATCH0 archive the way the game's own update is (tiger.py). The
runtime reads the folder's files in place of the game's (BB_GAME_OVERRIDES).

  game_overrides.py <game dir> <out dir>

Kept until the game's archives or the patches change. When a movie is not the one a patch was
made for (another version of the game), the folder is left empty and the game runs unchanged."""
import hashlib
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_patch  # noqa: E402
import tiger  # noqa: E402

# Patch, the PRIORITY0 entries whose Scaleform sections hold the movie (the main menu's is in two
# resources; both get the edited copy).
PATCHES = [('mainmenu-quit.bbmenu', (0x0a1d4a56, 0xcd4d322e)),
           # Options > Display (the options movie): the port's settings rows
           ('options-display.bbmenu', (0xc694569a,))]
VERSION = 2


def find_movie(game, source, entry_hash, sha1):
    _p, _parts, entries = tiger.read_entries(game, source)
    entry = next((e for e in entries if e[0] == entry_hash), None)
    if not entry:
        return None
    handles = {}
    head = tiger.read_entry(game, source, entry, handles)
    count = struct.unpack_from('<I', head, 0x18)[0]
    len1, len2 = struct.unpack_from('<II', head, 4)
    loc = 0x20 + count * 20 + len1 + len2
    for i in range(count):
        packed, offset, stored, _h = struct.unpack_from('<IIII', head, loc + 16 * i)
        if packed >> 24 != 7:
            continue
        part = offset & 0xf
        if part not in handles:
            handles[part] = (Path(game) / f'{source}.{part:03d}.tiger').open('rb')
        handles[part].seek(offset & ~0x7ff)
        section = tiger.uncdrm(handles[part].read(stored))
        for _at, movie in tiger.movies(section):
            if hashlib.sha1(movie).digest() == sha1:
                return movie
    return None


def stamp(game, here):
    h = hashlib.sha1(f'v{VERSION}'.encode())
    for name in ('PATCH0.000.tiger', 'PATCH0.manifest.bin', 'PRIORITY0.manifest.bin'):
        st = (Path(game) / name).stat()
        h.update(f'{name} {st.st_size} {st.st_mtime_ns}'.encode())
    for f in (here / 'menu_patch.py', here / 'tiger.py', Path(__file__).resolve(),
              *(here / 'menus' / p for p, _e in PATCHES)):
        h.update(f.read_bytes())
    return h.hexdigest()


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    game, out = Path(argv[0]), Path(argv[1])
    here = Path(__file__).resolve().parent
    if not (game / 'PATCH0.000.tiger').is_file() or not (game / 'PRIORITY0.manifest.bin').is_file():
        print('Game overrides: no PATCH0/PRIORITY0 archives; the game runs unchanged')
        shutil.rmtree(out, ignore_errors=True)
        return 0
    want = stamp(game, here)
    if (out / 'stamp.txt').is_file() and (out / 'stamp.txt').read_text().strip() == want:
        return 0
    shutil.rmtree(out, ignore_errors=True)
    edits = []
    for name, hashes in PATCHES:
        patch = (here / 'menus' / name).read_bytes()
        original = find_movie(game, 'PRIORITY0', hashes[0], menu_patch.original_sha1(patch))
        if original is None:
            print(f'Game overrides: {name} does not fit this version of the game; the game runs unchanged')
            return 0
        edits.append((original, menu_patch.apply(original, patch), list(hashes)))
    tiger.build_patch(game, out, edits)
    (out / 'stamp.txt').write_text(want + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
