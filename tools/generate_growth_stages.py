#!/usr/bin/env python3
"""Draw the growth stage sprites (M06): egg, newborn, child; no AI generation.

Today's XUNA is the mature stage: the in-game idle frame
(assets/generated/pet_frames.png). The younger stages are cut from its pixels,
so every stage is the same XUNA:
- The frame is reduced to its flat palette (ink, crest, body, shade, brow,
  eye white).
- A square head replaces the golden crest: the body is extended up to a flat
  top, keeping XUNA's eyes, brow, mouth and feet.
- Newborn: the square head with no gold, made smaller by removing plain rows
  and columns (thinner brow, shorter body, eyes closer together).
- Child: the full-size square head with a first golden fringe and three short
  tufts on top.
- Egg: drawn in the scene palette, a cream shell with orange marks and a
  small crack, without any crest.
The 3 px ink outline is redrawn after each change, and every stage is
bottom-anchored on the idle frame's ground row and centred, so sizes compare
honestly on the same 96x72 canvas.
Usage: python tools/generate_growth_stages.py
Outputs: assets/stages/xuna_stage_{egg,newborn,child}.png,
assets/generated/growth_stages_preview.png.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SW, SH = 96, 72
GROUND = 65            # last opaque row of the idle frame
OUTLINE = 3            # ink thickness of the idle frame
PALETTE = {
    '#': '#161516',    # ink (tools/generate_pet_scene.py)
    'Y': '#fdb156',    # crest
    'o': '#ec6e38',    # body
    's': '#c95126',    # body shade
    'b': '#7e2f1e',    # brow
    'w': '#fff7e9',    # eye white (tools/generate_pet_scene.py)
    'c': '#f1e4c8',    # egg shell
    'k': '#d2be98',    # egg shell shade
}
RGB = {k: tuple(int(v[i:i + 2], 16) for i in (1, 3, 5)) for k, v in PALETTE.items()}
FRAME_KEYS = '#Yosbw'  # colours present in the idle frame
# Idle frame geometry (frame coordinates).
BODY_ROW = 46          # rows >= 46 are body and feet with no crest
BODY_X = (22, 74)      # outer ink columns of the body
FACE_X = (26, 68)      # columns whose brow and eye pixels are kept
HEAD_TOP = 31          # top row of the square head
# Work on a roomier canvas so no stage clips; the idle frame sits at PAD.
WORK_W, WORK_H = 140, 100
PAD = ((WORK_W - SW) // 2, WORK_H - SH - 4)


def blank():
    return [['.'] * WORK_W for _ in range(WORK_H)]


def load_mature():
    sheet = Image.open(ROOT / 'assets/generated/pet_frames.png').convert('RGBA')
    idle = sheet.crop((0, 0, SW, SH))  # Idle, step 0: no breath offset
    grid = blank()
    for y in range(SH):
        for x in range(SW):
            pixel = idle.getpixel((x, y))
            if pixel[3] >= 128:
                grid[y + PAD[1]][x + PAD[0]] = min(
                    FRAME_KEYS,
                    key=lambda k: sum((a - b) ** 2 for a, b in zip(pixel, RGB[k])))
    return grid


def delete(grid, ys=(), xs=()):
    """Remove frame rows `ys` and columns `xs`."""
    ys = {PAD[1] + y for y in ys}
    xs = {PAD[0] + x for x in xs}
    return [[c for x, c in enumerate(row) if x not in xs]
            for y, row in enumerate(grid) if y not in ys]


def bbox(grid):
    ys = [y for y, row in enumerate(grid) if any(c != '.' for c in row)]
    xs = [x for row in grid for x, c in enumerate(row) if c != '.']
    return min(xs), min(ys), max(xs), max(ys)


def outline(grid):
    """Every opaque pixel within OUTLINE - 1 of transparency becomes ink."""
    h, w = len(grid), len(grid[0])
    out = [list(row) for row in grid]
    reach = range(-OUTLINE + 1, OUTLINE)
    for y in range(h):
        for x in range(w):
            if grid[y][x] != '.' and any(
                    not (0 <= y + dy < h and 0 <= x + dx < w)
                    or grid[y + dy][x + dx] == '.'
                    for dy in reach for dx in reach):
                out[y][x] = '#'
    return out


def fill_polygon(grid, points, key):
    mask = Image.new('L', (len(grid[0]), len(grid)))
    ImageDraw.Draw(mask).polygon(
        [(x + PAD[0], y + PAD[1]) for x, y in points], fill=255)
    for y in range(len(grid)):
        for x in range(len(grid[0])):
            if mask.getpixel((x, y)):
                grid[y][x] = key


def square_head(mature):
    """XUNA without the crest: the body rises to a flat top."""
    grid = blank()
    for fy in range(HEAD_TOP, SH):
        for fx in range(SW):
            y, x = fy + PAD[1], fx + PAD[0]
            source = mature[y][x]
            if fy >= BODY_ROW:
                grid[y][x] = source
            elif BODY_X[0] <= fx <= BODY_X[1]:
                face = FACE_X[0] <= fx <= FACE_X[1]
                if face and (source == 'b' or (source in 'w#' and fy >= 43)):
                    grid[y][x] = source       # brow, eye whites and pupils
                else:
                    grid[y][x] = 'o'
    return outline(grid)


def newborn(mature):
    grid = delete(square_head(mature), ys=[39, 40, 52, 56],
                  xs=[30, 31, 32, 49, 50, 51, 52, 66, 67, 68])
    return outline(grid)


def child(mature):
    grid = square_head(mature)
    # Stepped blocks like a small copy of the mature crest (low left, tall
    # centre, medium right), then a golden fringe under them.
    for left, right, rise in ((29, 41, 4), (40, 54, 8), (53, 66, 5)):
        fill_polygon(grid, [(left, HEAD_TOP + 3), (left, HEAD_TOP - rise),
                            (right, HEAD_TOP - rise), (right, HEAD_TOP + 3)], 'Y')
    grid = outline(grid)
    for fy in range(HEAD_TOP - 8, HEAD_TOP + 7):
        for fx in range(SW):
            y, x = fy + PAD[1], fx + PAD[0]
            ragged = fy == HEAD_TOP + 6 and (fx // 5) % 2
            if grid[y][x] == 'o' and not ragged:
                grid[y][x] = 'Y'
    return grid


def egg():
    centre, top, bottom = 48, 32, 65
    grid = blank()
    for fy in range(top, bottom + 1):
        u = 2 * (fy - top) / (bottom - top) - 1.15   # widest below the middle
        if abs(u) >= 1:
            continue
        half = round(13 * (1 - u * u) ** 0.5)
        for fx in range(centre - half, centre + half + 1):
            shaded = fx > centre + half * 0.45 or fy > bottom - 6
            grid[fy + PAD[1]][fx + PAD[0]] = 'k' if shaded else 'c'
    for x0, y0, w, h in ((39, 47, 5, 4), (51, 43, 5, 3), (44, 56, 7, 4), (55, 54, 3, 3)):
        for fy in range(y0, y0 + h):
            for fx in range(x0, x0 + w):
                if grid[fy + PAD[1]][fx + PAD[0]] != '.':
                    grid[fy + PAD[1]][fx + PAD[0]] = 's' if fy == y0 + h - 1 else 'o'
    grid = outline(grid)
    # A small zigzag crack near the top: XUNA is about to hatch.
    for fx in range(41, 56):
        step = (fx - 41) % 6
        fy = 40 + (step if step < 3 else 6 - step)
        grid[fy + PAD[1]][fx + PAD[0]] = '#'
        grid[fy + 1 + PAD[1]][fx + PAD[0]] = '#'
    return grid


def to_sprite(grid):
    """Bottom-anchor on GROUND and centre on the 96x72 canvas."""
    x0, y0, x1, y1 = bbox(grid)
    dx = (SW - (x1 - x0 + 1)) // 2 - x0
    dy = GROUND - y1
    assert y0 + dy >= 0 and x0 + dx >= 0 and x1 + dx < SW, 'stage does not fit'
    image = Image.new('RGBA', (SW, SH))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if grid[y][x] != '.':
                image.putpixel((x + dx, y + dy), RGB[grid[y][x]] + (255,))
    return image


def main():
    mature = load_mature()
    stages = {'egg': egg(), 'newborn': newborn(mature), 'child': child(mature),
              'mature': mature}
    sprites = {name: to_sprite(grid) for name, grid in stages.items()}
    heights = [bbox(grid)[3] - bbox(grid)[1] + 1 for grid in stages.values()]
    assert heights[1:] == sorted(heights[1:]), 'XUNA must grow after hatching'
    for name in ('egg', 'newborn', 'child'):
        sprites[name].save(ROOT / f'assets/stages/xuna_stage_{name}.png')
    scale, gap = 5, 10
    preview = Image.new('RGBA', (4 * SW * scale + 3 * gap, SH * scale), '#393342')
    for i, sprite in enumerate(sprites.values()):
        big = sprite.resize((SW * scale, SH * scale), Image.Resampling.NEAREST)
        preview.alpha_composite(big, (i * (SW * scale + gap), 0))
    preview.save(ROOT / 'assets/generated/growth_stages_preview.png')
    print('Growth stages:', ', '.join(f'{n} {h}px' for n, h in zip(stages, heights)))


if __name__ == '__main__':
    main()
