# SPDX-License-Identifier: GPL-2.0-or-later
"""Pictures for the Virtual-On launcher (Pillow).

- card_scene(): a game's poster. Posters are the player's own pictures in launcher/art
  (menu.jpg, omg.jpg, ot.jpg, force.jpg; never distributed); without one, an original painted
  scene: a wireframe arena under a glowing sky, a different colour per game;
- poster_tile(): the launcher tile (rounded poster, caption, accent ring when selected);
- cover(), steam_artwork(): the collection's own box art from the player's game folder
  (sce_sys/icon0.png, pic1.png) for the Steam library pictures.
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# Launcher accent (Virtual-On cyan) and the poster files looked for in launcher/art.
ACCENT = (94, 200, 255)
POSTERS = {'menu': 'menu.jpg', 'omg': 'omg.jpg', 'ot': 'ot.jpg', 'force': 'force.jpg'}
# Painted scenes: sky top, horizon glow, floor grid colour, streak colour, random seed.
PALETTES = {
    'menu': ((6, 10, 28), (40, 120, 230), (70, 170, 255), (180, 230, 255), 11),
    'omg': ((20, 8, 6), (235, 120, 40), (255, 150, 70), (255, 220, 150), 23),
    'ot': ((4, 16, 22), (40, 200, 190), (80, 240, 220), (200, 255, 240), 37),
    'force': ((18, 4, 20), (210, 60, 170), (255, 90, 200), (255, 200, 240), 41),
}
_LOADED = {}


def _gradient(size, stops):
    """Vertical gradient; stops are (position 0..1, (r, g, b))."""
    w, h = size
    column = Image.new('RGB', (1, h))
    pixels = column.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                f = (t - p0) / max(p1 - p0, 1e-6)
                pixels[0, y] = tuple(int(a + (b - a) * f) for a, b in zip(c0, c1))
                break
    return column.resize((w, h))


def painted_scene(key, w, h):
    """An arena: dark sky fading to a glowing horizon, a perspective grid floor, light streaks."""
    sky, glow, grid, streak, seed = PALETTES.get(key, PALETTES['menu'])
    rng = random.Random(seed)
    horizon = int(h * 0.56)
    image = _gradient((w, h), [(0, sky), (0.50, tuple(int(s + (g - s) * 0.55) for s, g in zip(sky, glow))),
                               (0.56, glow), (0.60, tuple(c // 3 for c in glow)), (1, (2, 2, 4))])
    # Horizon light.
    light = Image.new('RGB', (w, h), (0, 0, 0))
    ImageDraw.Draw(light).ellipse((w * 0.1, horizon - h * 0.10, w * 0.9, horizon + h * 0.08), fill=glow)
    light = light.filter(ImageFilter.GaussianBlur(h * 0.06))
    image = Image.composite(light, image, light.convert('L').point(lambda v: int(v * 0.6)))
    d = ImageDraw.Draw(image, 'RGBA')
    # Distant towers on the horizon.
    x = 0
    while x < w:
        tw, th = rng.randint(w // 40, w // 16), rng.randint(h // 30, h // 7)
        d.rectangle((x, horizon - th, x + tw, horizon), fill=tuple(c // 5 for c in sky) + (255,))
        x += tw + rng.randint(0, w // 30)
    # Floor grid: lines to a vanishing point, and rows closing in towards the horizon.
    vx = w / 2
    for i in range(-14, 15):
        d.line((vx + i * w * 0.012, horizon, vx + i * w * 0.16, h), fill=grid + (150,), width=max(1, w // 400))
    for k in range(1, 18):
        y = horizon + (h - horizon) * (k / 17) ** 2.2
        d.line((0, y, w, y), fill=grid + (int(60 + 140 * k / 17),), width=max(1, w // 500))
    # Light streaks across the sky.
    for _i in range(5):
        y = rng.uniform(h * 0.08, horizon - h * 0.12)
        x0 = rng.uniform(-w * 0.2, w * 0.6)
        length = rng.uniform(w * 0.25, w * 0.6)
        tilt = rng.uniform(-0.12, 0.05) * length
        for width, alpha in ((max(3, w // 90), 40), (max(1, w // 300), 200)):
            d.line((x0, y, x0 + length, y + tilt), fill=streak + (alpha,), width=width)
    image = image.filter(ImageFilter.GaussianBlur(0.6))
    # Vignette.
    shade = Image.new('L', (w, h), 0)
    ImageDraw.Draw(shade).ellipse((-w * 0.2, -h * 0.25, w * 1.2, h * 1.2), fill=255)
    shade = shade.filter(ImageFilter.GaussianBlur(max(w, h) // 12))
    return Image.composite(image, Image.new('RGB', (w, h), (2, 2, 4)), shade)


def art_directories():
    """Where posters are looked for: the art folder next to the frozen launcher and launcher/art
    (the player's own pictures, never distributed), then pictures bundled as covers."""
    here = Path(__file__).resolve().parent
    bundle = Path(getattr(sys, '_MEIPASS', here))
    own = [Path(sys.executable).resolve().parent / 'art'] if getattr(sys, 'frozen', False) else []
    return own + [here / 'art', bundle / 'covers-virtualon']


def load_art(name):
    for directory in art_directories():
        try:
            with Image.open(directory / name) as image:
                return image.convert('RGB')
        except OSError:
            continue
    return None


def _load_poster(key):
    if key not in _LOADED:
        _LOADED[key] = load_art(POSTERS[key]) if key in POSTERS else None
    return _LOADED[key]


def _cover(image, size, focus=(0.5, 0.45)):
    w, h = size
    scale = max(w / image.width, h / image.height)
    scaled = image.resize((math.ceil(image.width * scale), math.ceil(image.height * scale)), Image.LANCZOS)
    fx, fy = focus
    left = max(0, min(scaled.width - w, round(fx * scaled.width - w / 2)))
    top = max(0, min(scaled.height - h, round(fy * scaled.height - h / 2)))
    return scaled.crop((left, top, left + w, top + h))


def card_scene(key, width, height):
    """The poster filling the card, or the painted scene when there is none."""
    width, height = max(width, 8), max(height, 8)
    image = _load_poster(key)
    if image is None:
        return painted_scene(key, width * 2, height * 2).resize((width, height), Image.LANCZOS)
    return _cover(image, (width, height))


def _font(bold):
    """A bold sans-serif face for captions (system fonts; Pillow's default as a last resort)."""
    names = (('DejaVuSans-Bold.ttf', 'NotoSans-Bold.ttf', 'LiberationSans-Bold.ttf', 'arialbd.ttf') if bold else
             ('DejaVuSans.ttf', 'NotoSans-Regular.ttf', 'LiberationSans-Regular.ttf', 'arial.ttf'))
    for name in names:
        try:
            ImageFont.truetype(name, 10)
            return name
        except OSError:
            continue
    return None


def _fit_font(name, text, max_width, size, minimum=8):
    if not name:
        return ImageFont.load_default()
    size = max(minimum, size)
    font = ImageFont.truetype(name, size)
    while size > minimum and font.getlength(text) > max_width:
        size -= 1
        font = ImageFont.truetype(name, size)
    return font


def _ink(font, text):
    box = font.getbbox(text or ' ')
    return box[2] - box[0], box[3] - box[1]


def _rounded_mask(size, radius):
    mask = Image.new('L', size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return mask


def poster_tile(key, width, height, title, subtitle, *, selected=False, hover=False, dimmed=False,
                radius=14, page=(15, 16, 18)):
    """A launcher tile: drop shadow, rounded poster, caption strip, accent ring when selected."""
    width, height = max(width, 48), max(height, 80)
    text_w = max(24, width - 24)
    title_font = _fit_font(_font(True), title.upper(), text_w, max(11, min(17, width // 15)))
    sub_font = _fit_font(_font(False), subtitle, text_w, max(9, min(12, width // 22)))
    _tw, title_h = _ink(title_font, title.upper())
    _sw, sub_h = _ink(sub_font, subtitle)
    gap = max(7, width // 38)
    caption = min(max(title_h + gap + sub_h + 18, 0), height // 3)
    poster_h = height - caption
    pad = max(10, width // 28)
    canvas = Image.new('RGBA', (width + pad * 2, height + pad * 2), (0, 0, 0, 0))
    poster = card_scene(key, width, poster_h)
    if dimmed:
        poster = Image.blend(poster, Image.new('RGB', poster.size, (10, 10, 12)), 0.55)
    framed = Image.new('RGBA', (width, poster_h), (0, 0, 0, 255))
    framed.paste(poster, (0, 0))
    framed.putalpha(_rounded_mask((width, poster_h), radius))
    shadow = Image.new('RGBA', (width + 8, poster_h + 8), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((0, 0, width + 7, poster_h + 7), radius=radius + 2, fill=(0, 0, 0, 110))
    shadow = shadow.filter(ImageFilter.GaussianBlur(max(6, width // 40)))
    canvas.alpha_composite(shadow, (pad + 4, pad + 6))
    canvas.alpha_composite(framed, (pad, pad))
    draw = ImageDraw.Draw(canvas)
    if selected:
        for offset, alpha in ((0, 255), (1, 140)):
            draw.rounded_rectangle((pad - 2 + offset, pad - 2 + offset, pad + width + 1 - offset,
                                    pad + poster_h + 1 - offset), radius=radius + 1, outline=ACCENT + (alpha,), width=2)
    elif hover:
        draw.rounded_rectangle((pad, pad, pad + width - 1, pad + poster_h - 1), radius=radius,
                               outline=tuple(c // 2 for c in ACCENT) + (200,), width=2)
    foot = Image.new('RGBA', (width, caption), (0, 0, 0, 0))
    ImageDraw.Draw(foot).rounded_rectangle((0, 0, width - 1, caption - 1), radius=max(8, radius - 4),
                                           fill=(page[0], page[1], page[2], 245))
    canvas.alpha_composite(foot, (pad, pad + poster_h))
    title_color = ACCENT if selected else (240, 244, 248)
    sub_color = ACCENT if selected else (140, 150, 162)
    text = title.upper()
    title_y = pad + poster_h + 8
    draw.text((pad + max(8, (width - title_font.getlength(text)) / 2), title_y), text, fill=title_color,
              font=title_font, anchor='lt')
    draw.text((pad + max(8, (width - sub_font.getlength(subtitle)) / 2), title_y + title_h + gap), subtitle,
              fill=sub_color, font=sub_font, anchor='lt')
    flat = Image.new('RGB', canvas.size, page)
    flat.paste(canvas, mask=canvas.split()[3])
    return flat


def cover(game_dir):
    """The collection's box art (sce_sys/icon0.png) of the game folder, or None."""
    try:
        with Image.open(Path(game_dir or '.') / 'sce_sys' / 'icon0.png') as image:
            return image.convert('RGB')
    except OSError:
        return None


def _fill(image, size):
    w, h = size
    scale = max(w / image.width, h / image.height)
    scaled = image.resize((math.ceil(image.width * scale), math.ceil(image.height * scale)), Image.LANCZOS)
    left, top = (scaled.width - w) // 2, (scaled.height - h) // 2
    return scaled.crop((left, top, left + w, top + h))


def _framed(art, size):
    """The square box art centred on a blurred, darkened copy of itself."""
    background = _fill(art, size).filter(ImageFilter.GaussianBlur(max(size) / 30))
    background = Image.blend(background, Image.new('RGB', size, (6, 8, 12)), 0.35)
    side = min(size)
    background.paste(art.resize((side, side), Image.LANCZOS), ((size[0] - side) // 2, (size[1] - side) // 2))
    return background


def steam_artwork(game_dir, grid_dir, appid, game=None):
    """Steam library pictures for the shortcut with this id: cover (p), header, hero.
    The game's own poster when the player has one in launcher/art; otherwise the collection's
    box art and background from the game folder; otherwise the painted scene."""
    poster = _load_poster(game) if game else None
    art = poster or cover(game_dir)
    grid_dir = Path(grid_dir)
    grid_dir.mkdir(parents=True, exist_ok=True)
    if art is None:
        pictures = {f'{appid}p.png': painted_scene(game, 600, 900), f'{appid}.png': painted_scene(game, 920, 430),
                    f'{appid}_hero.png': painted_scene(game, 1920, 620)}
    else:
        pictures = {f'{appid}p.png': _framed(art, (600, 900)), f'{appid}.png': _framed(art, (920, 430))}
        try:
            if poster is not None:
                raise OSError
            with Image.open(Path(game_dir) / 'sce_sys' / 'pic1.png') as wide:
                pictures[f'{appid}_hero.png'] = _fill(wide.convert('RGB'), (1920, 620))
        except OSError:
            pictures[f'{appid}_hero.png'] = _fill(poster, (1920, 620)) if poster else painted_scene(game, 1920, 620)
    written = []
    for name, picture in pictures.items():
        picture.save(grid_dir / name)
        written.append(grid_dir / name)
    return written


def icon(size=256):
    """The launcher icon: an original emblem (a V of light on a dark disc)."""
    scale = 4
    s = size * scale
    image = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    d.ellipse((s * 0.03, s * 0.03, s * 0.97, s * 0.97), fill=(10, 14, 24, 255), outline=ACCENT + (255,),
              width=int(s * 0.035))
    v = [(s * 0.24, s * 0.28), (s * 0.40, s * 0.28), (s * 0.50, s * 0.58), (s * 0.60, s * 0.28),
         (s * 0.76, s * 0.28), (s * 0.56, s * 0.76), (s * 0.44, s * 0.76)]
    d.polygon(v, fill=ACCENT + (255,))
    d.line((s * 0.18, s * 0.84, s * 0.82, s * 0.84), fill=(255, 140, 60, 255), width=int(s * 0.03))
    return image.resize((size, size), Image.LANCZOS)


if __name__ == '__main__':  # preview: python3 virtualon_art.py out.png [icon.png]
    sheet = Image.new('RGB', (4 * 300, 420), (15, 16, 18))
    titles = {'menu': ('Collection', 'Masterpiece 1995~2001'), 'omg': ('Virtual-On', 'Operation Moongate · 1995'),
              'ot': ('Oratorio Tangram', '1998'), 'force': ('Force', '2001')}
    for i, key in enumerate(PALETTES):
        sheet.paste(poster_tile(key, 260, 380, *titles[key], selected=(i == 1)), (i * 300, 0))
    sheet.save(sys.argv[1])
    if len(sys.argv) > 2:
        icon().save(sys.argv[2])
