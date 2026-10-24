#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tomb Raider (2013) / Definitive Edition .tiger archives: list entries, unpack them, and pull out
the Scaleform (GFx) movies they contain.

Layout (TAFS version 3): a header (magic 'TAFS', version, number of archive parts, number of
entries, flags, 32-byte platform name), then 16-byte entries: name hash, locale (0xffffffff: all),
size, offset (low 11 bits: flags; the low 4 the archive part, .000/.001/...; 0x80 in PATCH0:
stored in the patch archive itself). An entry is usually a CDRM container: magic
'CDRM', version, block count, padding, then per block (uncompressed size << 8 | type, compressed
size); type 1 is stored, type 2 zlib; block data starts 16-byte aligned after the table and each
block is 16-byte aligned.

  tiger.py list <game dir> <archive>            e.g. PRIORITY0
  tiger.py gfx <game dir> <archive> <out dir>   every GFX/CFX movie, as <hash>_<n>.gfx
  tiger.py patch <game dir> PRIORITY0 <out dir> <original.gfx> <edited.gfx> <hash>...
      PATCH0.000.tiger and PATCH0.manifest.bin in <out dir>: the game's PATCH0 plus those
      PRIORITY0 entries, with every copy of the original movie replaced by the edited one
"""
import struct
import sys
import zlib
from pathlib import Path


def read_entries(game, archive):
    head = (Path(game) / f'{archive}.000.tiger').open('rb').read(0x34)
    magic, version, parts, count, flags = struct.unpack_from('<4sIIII', head)
    if magic != b'TAFS' or version != 3:
        raise SystemExit(f'{archive}: not a TAFS v3 archive')
    platform = head[0x14:0x34].split(b'\0')[0].decode()
    with (Path(game) / f'{archive}.000.tiger').open('rb') as f:
        f.seek(0x34)
        table = f.read(count * 16)
    entries = [struct.unpack_from('<IIII', table, i * 16) for i in range(count)]
    return platform, parts, entries


def read_entry(game, archive, entry, handles):
    name_hash, locale, size, offset = entry
    part = offset & 0xf
    if part not in handles:
        handles[part] = (Path(game) / f'{archive}.{part:03d}.tiger').open('rb')
    f = handles[part]
    f.seek(offset & ~0x7ff)
    return f.read(size)


def uncdrm(data):
    if data[:4] != b'CDRM':
        return data
    _magic, _version, count, _pad = struct.unpack_from('<4sIII', data)
    blocks = [struct.unpack_from('<II', data, 16 + i * 8) for i in range(count)]
    pos = (16 + count * 8 + 15) & ~15
    out = bytearray()
    for packed, csize in blocks:
        usize, kind = packed >> 8, packed & 0xff
        chunk = data[pos:pos + csize]
        if kind == 2:
            out += zlib.decompress(chunk)
        else:
            out += chunk[:usize]
        pos = (pos + csize + 15) & ~15
    return bytes(out)


def drm_sections(game, archive, entry, handles):
    """[(type, data)] of a DRM resource: a header (version 0x16, two string lengths, section count)
    with 20-byte section descriptors and two strings, then one 16-byte location per section:
    (type << 24 | size, offset with archive part in the low 4 bits, stored size, hash). The data
    there is a CDRM container (or stored as is)."""
    head = read_entry(game, archive, entry, handles)
    version, len1, len2 = struct.unpack_from('<III', head)
    if version != 0x16 or len(head) < 0x20:
        return [(None, uncdrm(head))]
    count = struct.unpack_from('<I', head, 0x18)[0]
    pos = 0x20 + count * 20 + len1 + len2
    sections = []
    for i in range(count):
        if pos + 16 > len(head):
            break
        packed, offset, stored, _hash = struct.unpack_from('<IIII', head, pos)
        pos += 16
        kind, size = packed >> 24, packed & 0xffffff
        part = offset & 0xf
        if part not in handles:
            handles[part] = (Path(game) / f'{archive}.{part:03d}.tiger').open('rb')
        f = handles[part]
        f.seek(offset & ~0x7ff)
        sections.append((kind, uncdrm(f.read(stored))))
    return sections


def movies(blob):
    """(offset, bytes) of the GFX/CFX movies in a resource: signature, version 7..15, length."""
    found = []
    for sig in (b'GFX', b'CFX'):
        start = 0
        while (i := blob.find(sig, start)) >= 0:
            start = i + 1
            if i + 8 > len(blob) or not 7 <= blob[i + 3] <= 15:
                continue
            length = struct.unpack_from('<I', blob, i + 4)[0]
            if length < 32 or (sig == b'GFX' and i + length > len(blob)) or length > 64 << 20:
                continue
            if sig == b'CFX':
                try:
                    body = zlib.decompressobj().decompress(blob[i + 8:], length)
                except zlib.error:
                    continue
                movie = b'GFX' + blob[i + 3:i + 8] + body
            else:
                movie = blob[i:i + length]
            found.append((i, movie))
    return found


def cdrm(data, block=0x40000):
    """A CDRM container of zlib blocks (the game's own layout)."""
    chunks = [zlib.compress(data[i:i + block], 9) for i in range(0, len(data), block)] or [b'']
    head = struct.pack('<4sIII', b'CDRM', 0, len(chunks), 0)
    head += b''.join(struct.pack('<II', min(block, len(data) - i * block) << 8 | 2, len(c))
                     for i, c in enumerate(chunks))
    out = bytearray(head)
    out += bytes(-len(out) % 16)
    for c in chunks:
        out += c
        out += bytes(-len(out) % 16)
    return bytes(out)


def replace_movie(section, header_word2, old_movie, new_movie):
    """A Scaleform section with its movie replaced: relocation table (header_word2 >> 8 bytes),
    then the data: movie length, movie, padding to 4, the texture table. The table must stay where
    it is (other sections refer to it), so the edited movie may not be larger than the original:
    it keeps the original length, and the relocations stay as they are."""
    rsize = header_word2 >> 8
    relocs, data = section[:rsize], section[rsize:]
    old_len = struct.unpack_from('<I', data)[0]
    if data[4:4 + old_len] != old_movie:
        return None
    if len(new_movie) > old_len:
        raise SystemExit(f'edited movie is {len(new_movie) - old_len} bytes larger than the original; '
                         'it must fit in the original space')
    # The engine finds the texture table from the movie length: the edited movie keeps the
    # original length (header length field and zeros after its End tag, which Scaleform never
    # reads past), so the section's layout is exactly the original's.
    padded = bytearray(new_movie) + bytes(old_len - len(new_movie))
    struct.pack_into('<I', padded, 4, old_len)
    new_data = data[:4] + bytes(padded) + data[4 + old_len:]
    assert len(new_data) == len(data)
    return bytes(relocs), new_data


def build_patch(game, out, edits, source='PRIORITY0'):
    """PATCH0 with the given entries of `source` added the way the game's own patch does it: the
    entry is the original DRM header; sections that change are stored in PATCH0 (their location
    offset carries flag 0x80: "in the patch archive"), the others keep pointing at `source`.
    Entry offsets in PATCH0's table carry the same flag. edits: [(original movie, edited movie,
    [entry hashes])]."""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    base = (Path(game) / 'PATCH0.000.tiger').read_bytes()
    _p, _parts, patch_entries = read_entries(game, 'PATCH0')
    _p, _parts, entries = read_entries(game, source)
    handles = {}
    blob = bytearray(base)
    added = []
    for old_movie, new_movie, want in ((o, n, h) for o, n, hashes in edits for h in hashes):
        e = next(x for x in entries if x[0] == want)
        head = bytearray(read_entry(game, source, e, handles))
        count = struct.unpack_from('<I', head, 0x18)[0]
        len1, len2 = struct.unpack_from('<II', head, 4)
        loc = 0x20 + count * 20 + len1 + len2
        replaced = 0
        for i in range(count):
            packed, offset, stored, _h = struct.unpack_from('<IIII', head, loc + 16 * i)
            if packed >> 24 != 7:
                continue
            part = offset & 0xf
            if part not in handles:
                handles[part] = (Path(game) / f'{source}.{part:03d}.tiger').open('rb')
            f = handles[part]
            f.seek(offset & ~0x7ff)
            section = uncdrm(f.read(stored))
            if section.find(old_movie[:64]) < 0:
                continue
            word2 = struct.unpack_from('<I', head, 0x20 + i * 20 + 8)[0]
            result = replace_movie(section, word2, old_movie, new_movie)
            if not result:
                continue
            relocs, data = result
            raw = cdrm(relocs + data)
            blob += bytes(-len(blob) % 0x800)
            struct.pack_into('<I', head, 0x20 + i * 20, len(data))
            struct.pack_into('<II', head, loc + 16 * i + 4, len(blob) | 0x80, len(raw))
            blob += raw
            replaced += 1
        if not replaced:
            raise SystemExit(f'{source} {want:08x}: the movie is not in this entry')
        blob += bytes(-len(blob) % 0x800)
        added.append((e[0], e[1], len(head), len(blob) | 0x80))
        blob += head
        print(f'{source} {want:08x}: {count} sections, {replaced} replaced in PATCH0')
    blob += bytes(-len(blob) % 0x800)
    table = sorted(list(patch_entries) + added, key=lambda x: x[0])  # stable: keeps the game's order
    header = struct.pack('<4sIII', b'TAFS', 3, 1, len(table)) + base[0x10:0x34]
    header += b''.join(struct.pack('<IIII', *t) for t in table)
    if len(header) > min(t[3] & ~0x7ff for t in patch_entries):
        raise SystemExit('entry table overlaps the original data')
    blob[:len(header)] = header
    (out / 'PATCH0.000.tiger').write_bytes(bytes(blob))
    (out / 'PATCH0.manifest.bin').write_bytes(header)
    print(f'PATCH0: {len(table)} entries, {len(blob)} bytes -> {out}')


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    command, game, archive = argv[0], argv[1], argv[2]
    platform, parts, entries = read_entries(game, archive)
    handles = {}
    if command == 'list':
        print(f'{archive}: {platform}, {parts} parts, {len(entries)} entries')
        for e in entries:
            print(f'{e[0]:08x} locale {e[1]:08x} size {e[2]:9d} part {e[3] & 0xf} at {e[3] & ~0xf:#x}')
    elif command == 'gfx':
        out = Path(argv[3])
        out.mkdir(parents=True, exist_ok=True)
        total = 0
        for e in entries:
            try:
                sections = drm_sections(game, archive, e, handles)
            except (zlib.error, struct.error) as error:
                print(f'{e[0]:08x}: {error}', file=sys.stderr)
                continue
            found = [(kind, blob, offset, movie) for kind, blob in sections
                     for offset, movie in movies(blob)]
            for n, (kind, blob, offset, movie) in enumerate(found):
                # The movie's own path, when the resource names it (e.g. Menus\\MainMenu\\...).
                (out / f'{archive}_{e[0]:08x}_{n}.gfx').write_bytes(movie)
                total += 1
                names = {s.decode(errors='replace') for s in
                         __import__('re').findall(rb'[A-Za-z0-9_]+\\[A-Za-z0-9_\\]+', blob[:offset])[-3:]}
                print(f'{archive} {e[0]:08x} #{n} (section type {kind}): {len(movie)} bytes at {offset:#x} {sorted(names)}')
        print(f'{total} movies')
    elif command == 'patch':
        # tiger.py patch <game dir> PRIORITY0 <out dir> <original.gfx> <edited.gfx> <hash>...
        build_patch(game, argv[3], [(Path(argv[4]).read_bytes(), Path(argv[5]).read_bytes(),
                                     [int(h, 16) for h in argv[6:]])], archive)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
