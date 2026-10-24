#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Menu patches: an edited Scaleform movie stored as its differences from the game's own, so the
port ships no game data. A movie is a header and a list of tags; the patch keeps the header and,
for every tag of the edited movie, either "the original's tag n" or the new tag's bytes.

  menu_patch.py make <original.gfx> <edited.gfx> <out.bbmenu>
  menu_patch.py apply <original.gfx> <patch.bbmenu> <out.gfx>

Patch: 'BBMENU1\\0', SHA-1 of the original, SHA-1 of the edited movie, then zlib of: u32 header
size, header, u32 op count, ops (u8 0 + u32 tag index | u8 1 + u32 size + tag bytes)."""
import hashlib
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b'BBMENU1\0'


def tags(movie):
    """(header, [tag bytes]) of an uncompressed GFX/SWF movie, up to and including its End tag."""
    if movie[:3] not in (b'GFX', b'FWS'):
        raise ValueError('not an uncompressed GFX/SWF movie')
    bits = movie[8] >> 3
    pos = 8 + (5 + 4 * bits + 7) // 8 + 4
    header, out = movie[:pos], []
    length = struct.unpack_from('<I', movie, 4)[0]
    while pos < min(length, len(movie)):
        start = pos
        word = struct.unpack_from('<H', movie, pos)[0]
        pos += 2
        size = word & 0x3f
        if size == 0x3f:
            size = struct.unpack_from('<I', movie, pos)[0]
            pos += 4
        pos += size
        out.append(movie[start:pos])
        if word >> 6 == 0:
            break
    return header, out


def make(original, edited):
    head, new_tags = tags(edited)
    _h, old_tags = tags(original)
    where = {}
    for i, t in enumerate(old_tags):
        where.setdefault(t, i)
    ops = bytearray(struct.pack('<I', len(head)) + head + struct.pack('<I', len(new_tags)))
    for t in new_tags:
        if t in where:
            ops += struct.pack('<BI', 0, where[t])
        else:
            ops += struct.pack('<BI', 1, len(t)) + t
    return (MAGIC + hashlib.sha1(original).digest() + hashlib.sha1(edited).digest()
            + zlib.compress(bytes(ops), 9))


def original_sha1(patch):
    if patch[:8] != MAGIC:
        raise ValueError('not a menu patch')
    return patch[8:28]


def apply(original, patch):
    if hashlib.sha1(original).digest() != original_sha1(patch):
        raise ValueError('the movie is not the one this patch was made for')
    ops = zlib.decompress(patch[48:])
    hsize = struct.unpack_from('<I', ops)[0]
    out = bytearray(ops[4:4 + hsize])
    pos = 4 + hsize
    count = struct.unpack_from('<I', ops, pos)[0]
    pos += 4
    _h, old_tags = tags(original)
    for _ in range(count):
        kind, value = struct.unpack_from('<BI', ops, pos)
        pos += 5
        if kind == 0:
            out += old_tags[value]
        else:
            out += ops[pos:pos + value]
            pos += value
    struct.pack_into('<I', out, 4, len(out))
    if hashlib.sha1(out).digest() != patch[28:48]:
        raise ValueError('patched movie does not match')
    return bytes(out)


def main(argv):
    if len(argv) != 4 or argv[0] not in ('make', 'apply'):
        raise SystemExit(__doc__)
    a, b = Path(argv[1]).read_bytes(), Path(argv[2]).read_bytes()
    result = make(a, b) if argv[0] == 'make' else apply(a, b)
    Path(argv[3]).write_bytes(result)
    print(f'{argv[3]}: {len(result)} bytes')


if __name__ == '__main__':
    main(sys.argv[1:])
