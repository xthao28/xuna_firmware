#!/usr/bin/env python3
"""Draw the Track A variants (2026-10-24) from the in-game sprite: look_around,
yawn_stretch, bounce_play, greet_nuzzle; 16 frames each at 100 ms.

The owner's sheet (assets/source/track_a_sheet_2026-10-24.png) is the motion
reference; the pixels come from assets/generated/pet_frames.png, so the
variants match the base animations exactly:
- the idle frame with its face painted over is the blank body (the same skin
  boxes tools/generate_pet_scene.py paints before drawing a face);
- faces are redrawn with the same eye/mouth geometry as the base emotions;
- motion is whole-pixel only: leaning shifts the head rows (a staircase down
  to fixed feet), stretching/squashing resamples the body above the feet with
  nearest neighbour, hopping lifts the whole sprite.
Every one-shot starts and ends on the idle frame byte for byte, and the feet
rows stay identical except while hopping.
Usage: python tools/generate_track_a.py
Outputs: assets/generated/track_a_frames.png, assets/generated/track_a_preview.gif,
src/assets/XunaTrackA.cpp, include/assets/XunaTrackA.h.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SW, SH, FRAMES = 96, 72, 16
FEET_ROW = 58          # rows >= 58 are the legs/feet (fixed in the base frames)
TOP = 13               # first row of the hair in the neutral pose
INK, WHITE, SKIN = '#161516', '#fff7e9', '#ee7837'
TONGUE = '#a94324'
EYES = (34, 54)        # left x of each eye box (generate_pet_scene.py)
TRANSPARENT = 0xF81F


def rgb565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def load_base():
    sheet = Image.open(ROOT / 'assets/generated/pet_frames.png').convert('RGBA')
    idle = sheet.crop((0, 0, SW, SH))  # Idle, step 0: no breath offset
    blank = idle.copy()
    d = ImageDraw.Draw(blank)
    for x in EYES:
        d.rectangle((x, 42, x + 15, 50), fill=SKIN)
    d.rectangle((43, 53, 59, 58), fill=SKIN)
    return idle, blank


def face(blank, eyes='open', gaze=0, mouth='flat', size=0):
    """eyes: open | closed | happy | squint | big; mouth: flat | smile | o | yawn."""
    im = blank.copy()
    d = ImageDraw.Draw(im)
    for x in EYES:
        if eyes == 'open':
            d.rectangle((x + 1, 43, x + 13, 50), fill=WHITE)
            d.rectangle((x + 8 + gaze, 43, x + 11 + gaze, 48), fill=INK)
        elif eyes == 'big':
            d.rectangle((x + 1, 41, x + 13, 50), fill=WHITE)
            d.rectangle((x + 6 + gaze, 42, x + 11 + gaze, 49), fill=INK)
            d.rectangle((x + 7 + gaze, 43, x + 8 + gaze, 44), fill=WHITE)  # sparkle
        elif eyes == 'closed':
            d.line((x + 1, 47, x + 12, 47), fill=INK, width=2)
        elif eyes == 'happy':
            d.line([(x + 2, 48), (x + 2, 45), (x + 5, 43), (x + 8, 43), (x + 11, 45), (x + 11, 48)],
                   fill=INK, width=2)
        elif eyes == 'squint':  # > <
            mid = 46
            if x == EYES[0]:
                d.line([(x + 3, mid - 3), (x + 11, mid), (x + 3, mid + 3)], fill=INK, width=2)
            else:
                d.line([(x + 11, mid - 3), (x + 3, mid), (x + 11, mid + 3)], fill=INK, width=2)
    if mouth == 'flat':
        d.rectangle((47, 54, 55, 55), fill=INK)
    elif mouth == 'smile':
        e = size
        d.line([(47 - e, 54), (49 - e, 56), (53 + e, 56), (55 + e, 54)], fill=INK, width=2)
    elif mouth == 'o':
        d.rectangle((50, 54, 53, 57), fill=INK)
    elif mouth == 'yawn':  # an open mouth growing with size 1..3 (row 57 at most)
        d.rectangle((50 - size, 53, 52 + size, 54 + size), fill=INK)
        if size >= 3:
            d.rectangle((50 - size + 2, 53 + size, 52 + size - 2, 54 + size), fill=TONGUE)
    return im


def lean(im, dx):
    """Shift the head by dx and the body by dx/2 above fixed feet."""
    if dx == 0:
        return im
    out = Image.new('RGBA', im.size)
    out.alpha_composite(im.crop((0, FEET_ROW, SW, SH)), (0, FEET_ROW))
    half = int(dx / 2)
    out.alpha_composite(im.crop((0, 40, SW, FEET_ROW)), (half, 40))
    out.alpha_composite(im.crop((0, 0, SW, 40)), (dx, 0))
    return out


def body_scale(im, dy=0, dx=0):
    """Stretch (dy > 0) or squash (dy < 0) the body above the feet; dx widens."""
    if dy == 0 and dx == 0:
        return im
    top = im.crop((0, TOP, SW, FEET_ROW))
    left, _, right, _ = top.getbbox()
    part = top.crop((left, 0, right, FEET_ROW - TOP))
    w, h = part.width + dx, part.height + dy
    part = part.resize((w, h), Image.Resampling.NEAREST)
    out = Image.new('RGBA', im.size)
    out.alpha_composite(im.crop((0, FEET_ROW, SW, SH)), (0, FEET_ROW))
    out.alpha_composite(part, (left - dx // 2, FEET_ROW - h))
    return out


def hop(im, up):
    if up == 0:
        return im
    out = Image.new('RGBA', im.size)
    out.alpha_composite(im, (0, -up))
    return out


def look_around(blank):
    # (gaze, lean, eyes, mouth): peek left, back with a blink, peek right.
    seq = [(0, 0, 'open', 'flat'), (-2, 0, 'open', 'flat'), (-4, 0, 'open', 'flat'),
           (-6, -1, 'open', 'o'), (-6, -2, 'open', 'o'), (-6, -2, 'open', 'o'),
           (-4, -1, 'open', 'flat'), (0, 0, 'closed', 'flat'), (0, 0, 'open', 'flat'),
           (1, 0, 'open', 'flat'), (2, 1, 'open', 'o'), (2, 2, 'open', 'o'),
           (2, 2, 'open', 'o'), (2, 1, 'open', 'flat'), (1, 0, 'open', 'flat'),
           (0, 0, 'open', 'flat')]
    return [lean(face(blank, e, g, m), l) for g, l, e, m in seq]


def yawn_stretch(blank):
    # (eyes, mouth, size, stretch)
    seq = [('open', 'flat', 0, 0), ('squint', 'flat', 0, 0), ('closed', 'o', 0, 0),
           ('closed', 'yawn', 1, 1), ('closed', 'yawn', 2, 3), ('closed', 'yawn', 3, 5),
           ('closed', 'yawn', 3, 6), ('closed', 'yawn', 3, 6), ('closed', 'yawn', 3, 5),
           ('closed', 'yawn', 2, 3), ('closed', 'o', 0, 1), ('closed', 'flat', 0, 0),
           ('open', 'flat', 0, 0), ('closed', 'flat', 0, 0), ('open', 'flat', 0, 0),
           ('open', 'flat', 0, 0)]
    return [body_scale(face(blank, e, 0, m, s), dy=st) for e, m, s, st in seq]


def bounce_play(blank):
    happy = face(blank, 'happy', 0, 'smile', 2)
    # (squash rows, widen, hop): two hops, squash on take-off and landing.
    seq = [(-2, 2, 0), (-1, 1, 0), (1, 0, 2), (2, 0, 5), (1, 0, 7), (0, 0, 5),
           (0, 0, 2), (-3, 3, 0), (-1, 1, 0), (1, 0, 3), (1, 0, 6), (0, 0, 7),
           (0, 0, 4), (0, 0, 1), (-3, 3, 0), (-1, 1, 0)]
    return [hop(body_scale(happy, dy, dx), up) for dy, dx, up in seq]


def greet_nuzzle(blank):
    # (eyes, mouth, size, lean, hop): bright eyes, a wiggle, a small happy hop.
    seq = [('open', 'flat', 0, 0, 0), ('big', 'flat', 0, 0, 0), ('big', 'smile', 1, 0, 0),
           ('big', 'smile', 2, -1, 0), ('big', 'smile', 2, -2, 0), ('big', 'smile', 2, -1, 0),
           ('big', 'smile', 2, 1, 0), ('big', 'smile', 2, 2, 0), ('big', 'smile', 2, 1, 0),
           ('big', 'smile', 2, -1, 0), ('big', 'smile', 2, -2, 0), ('happy', 'smile', 3, 0, 2),
           ('happy', 'smile', 3, 0, 3), ('happy', 'smile', 2, 0, 0), ('open', 'smile', 1, 0, 0),
           ('open', 'flat', 0, 0, 0)]
    return [hop(lean(face(blank, e, 0, m, s), l), up) for e, m, s, l, up in seq]


def main():
    idle, blank = load_base()
    rows = [look_around(blank), yawn_stretch(blank), bounce_play(blank), greet_nuzzle(blank)]
    neutral = idle.tobytes()
    feet = idle.crop((0, FEET_ROW + 1, SW, SH)).tobytes()  # as generate_pet_scene.py checks
    assert face(blank).tobytes() == neutral, 'redrawn neutral face must equal the idle frame'
    for r in (1, 3):  # one-shots start and end on idle
        assert rows[r][0].tobytes() == neutral and rows[r][-1].tobytes() == neutral
    for r, row in enumerate(rows):
        assert len(row) == FRAMES
        for i, f in enumerate(row):
            box = f.getbbox()
            assert box[0] >= 1 and box[1] >= 1 and box[2] <= SW - 1 and box[3] <= SH, (r, i, box)
            if r != 2 and not (r == 3 and i in (11, 12)):
                assert f.crop((0, FEET_ROW + 1, SW, SH)).tobytes() == feet, (r, i)
    frames = [f for row in rows for f in row]
    out = ROOT / 'assets/generated'
    strip = Image.new('RGBA', (SW * FRAMES, SH * 4))
    for k, f in enumerate(frames):
        strip.alpha_composite(f, ((k % FRAMES) * SW, (k // FRAMES) * SH))
    strip.save(out / 'track_a_frames.png')
    gif = []
    for f in frames:
        g = Image.new('RGB', (SW, SH), (0x39, 0x33, 0x42))
        g.paste(f, mask=f.getchannel('A'))
        gif.append(g.resize((SW * 4, SH * 4), Image.Resampling.NEAREST))
    gif[0].save(out / 'track_a_preview.gif', save_all=True, append_images=gif[1:], duration=100, loop=0)
    values = [rgb565(p) if p[3] else TRANSPARENT for f in frames for p in f.get_flattened_data()]
    assert all(rgb565(p) != TRANSPARENT for f in frames for p in f.get_flattened_data() if p[3])
    header = ('// Generated by tools/generate_track_a.py.\n#pragma once\n#include <cstdint>\n'
              'namespace xuna { namespace scene {\n'
              '// Track A variants (VisualVariant order after None): look_around,\n'
              '// yawn_stretch, bounce_play, greet_nuzzle; same canvas/anchor as kFrames.\n'
              'constexpr uint16_t kVariantCount=4,kVariantFrames=%d;\n'
              'extern const uint16_t kVariantSprites[kVariantCount*kVariantFrames*96*72];\n}}\n' % FRAMES)
    (ROOT / 'include/assets/XunaTrackA.h').write_text(header)
    body = ('#include <pgmspace.h>\n#include "assets/XunaTrackA.h"\nnamespace xuna { namespace scene {\n'
            'alignas(4) const uint16_t kVariantSprites[] PROGMEM = {\n' +
            '\n'.join('  ' + ', '.join('0x%04x' % v for v in values[i:i + 12]) + ','
                      for i in range(0, len(values), 12)) + '\n};\n}}\n')
    (ROOT / 'src/assets/XunaTrackA.cpp').write_text(body)
    print('frames=%d flash_bytes=%d' % (len(frames), len(values) * 2))


if __name__ == '__main__':
    main()
