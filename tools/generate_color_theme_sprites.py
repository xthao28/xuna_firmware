#!/usr/bin/env python3
"""Generate 7-Theme XUNA Color Exploration sprites, mobile assets, and showcase sheet.

Reads configurable palette definitions from `firmware/assets/color_themes.json`
and maps every character pixel into the 5 canonical roles:
  - highlight: Brightest color and highlights (spiky crown)
  - hair:      Primary accent color for the upper/head & face area
  - body:      Main body mid-tone shading (sideburn, leg crease, back leg)
  - shadow:    Darker shading (eyebrow bar, bottom feet tips, yawn tongue)
  - outline:   Character outline, eye pupils, and mouth
Plus crisp `eye_white` (#FFF7E9).
"""
from pathlib import Path
import json
import math
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parent
THEMES_CFG = json.loads((ROOT / 'assets/color_themes.json').read_text(encoding='utf-8'))
SCENE_CFG = json.loads((ROOT / 'assets/scene_config.json').read_text(encoding='utf-8'))
W, H = SCENE_CFG['lcd']
SW, SH = SCENE_CFG['sprite']
AX, AY = SCENE_CFG['anchor']
EYE_WHITE = '#FFF7E9'
FRAMES_PER_STATE = 32
STEP_MS = 100


def hex_to_rgb(h: str) -> tuple[int, int, int]:
    s = h.strip().lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def rgb565(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb[:3]
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def extract_clean_role_grid() -> list[list[str]]:
    """Extracts a clean 62x53 role matrix from xuna-emotion-sheet-v2.png."""
    source = Image.open(ROOT / 'assets/source/xuna-emotion-sheet-v2.png').convert('RGBA')
    cell = source.crop((0, 0, source.width // 4, source.height // 4))
    cell.putalpha(cell.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
    cut = cell.crop(cell.getbbox()).resize((62, 53), Image.Resampling.NEAREST)

    grid: list[list[str]] = [['transparent'] * cut.width for _ in range(cut.height)]
    for y in range(cut.height):
        for x in range(cut.width):
            r, g, b, a = cut.getpixel((x, y))
            if a < 128:
                grid[y][x] = 'transparent'
            elif r < 65 and g < 45 and b < 45:
                grid[y][x] = 'outline'
            elif y == 52:
                # Crisp bottom sole outline for all 3 feet
                grid[y][x] = 'outline'
            elif r > 235 and g > 235 and b > 215:
                grid[y][x] = 'eye_white'
            elif y < 18:
                grid[y][x] = 'highlight'
            elif r > 220 and g > 140:
                grid[y][x] = 'highlight'
            elif y in (50, 51):
                # Distinct shadow band on the bottom of all 3 feet
                grid[y][x] = 'shadow' if (22 <= x <= 30 and y == 51) or not (22 <= x <= 30) else 'body'
            elif y == 49 and 22 <= x <= 30:
                # Middle back leg mid-tone body color
                grid[y][x] = 'body'
            elif r < 160:
                grid[y][x] = 'shadow'
            elif (x < 14 and 14 <= y <= 35 and r < 234) or (y in (47, 48)):
                grid[y][x] = 'body'
            else:
                grid[y][x] = 'hair'
    return grid


def build_base_sprite_for_roles(role_grid: list[list[str]], roles: dict[str, str]) -> Image.Image:
    """Renders the 96x72 neutral base sprite using the given 5-role HEX dict."""
    cmap = {
        'highlight': hex_to_rgb(roles['highlight']) + (255,),
        'hair': hex_to_rgb(roles['hair']) + (255,),
        'body': hex_to_rgb(roles['body']) + (255,),
        'shadow': hex_to_rgb(roles['shadow']) + (255,),
        'outline': hex_to_rgb(roles['outline']) + (255,),
        'eye_white': hex_to_rgb(EYE_WHITE) + (255,),
        'transparent': (0, 0, 0, 0),
    }
    cut = Image.new('RGBA', (62, 53))
    for y, row in enumerate(role_grid):
        for x, role in enumerate(row):
            cut.putpixel((x, y), cmap[role])
    base = Image.new('RGBA', (SW, SH))
    base.alpha_composite(cut, (17, 13))
    return base


def build_emotion_frames_for_roles(role_grid: list[list[str]], roles: dict[str, str]) -> list[Image.Image]:
    """Builds all 128 frames (Idle, Happy, Curious, Sleep x 32) for a theme."""
    base = build_base_sprite_for_roles(role_grid, roles)
    ink = roles['outline']
    skin = roles['hair']
    white = EYE_WHITE
    frames: list[Image.Image] = []
    for emotion in range(4):
        for step in range(FRAMES_PER_STATE):
            phase = step / FRAMES_PER_STATE
            breath = round(1 - math.cos(2 * math.pi * phase))
            if emotion == 1:
                breath = round(1 - math.cos(4 * math.pi * phase))
            expression = round(1.5 * (1 - math.cos(2 * math.pi * phase)))
            face = base.copy()
            d = ImageDraw.Draw(face)
            for x in (34, 54):
                d.rectangle((x, 42, x + 15, 50), fill=skin)
                if emotion == 3 or (emotion == 0 and step == 27):
                    d.line((x + 1, 47, x + 12, 47), fill=ink, width=2)
                elif emotion == 0 and step in (26, 28):
                    d.rectangle((x + 1, 46, x + 13, 50), fill=white)
                    d.rectangle((x + 9, 46, x + 12, 49), fill=ink)
                elif emotion == 1:
                    d.line(
                        [(x + 2, 48), (x + 2, 45), (x + 5, 43), (x + 8, 43), (x + 11, 45), (x + 11, 48)],
                        fill=ink,
                        width=2,
                    )
                else:
                    d.rectangle((x + 1, 43, x + 13, 50), fill=white)
                    gaze = round(2 * math.sin(2 * math.pi * phase)) if emotion == 2 else 0
                    d.rectangle((x + 8 + gaze, 43, x + 11 + gaze, 48), fill=ink)
            d.rectangle((43, 53, 59, 58), fill=skin)
            if emotion == 1:
                d.line(
                    [(47 - expression, 54), (49 - expression, 56), (53 + expression, 56), (55 + expression, 54)],
                    fill=ink,
                    width=2,
                )
            elif emotion == 2:
                d.rectangle((50, 54, 53, 57), fill=ink)
            elif emotion == 3:
                d.rectangle((49 - expression // 2, 54, 53 + expression // 2, 55), fill=ink)
            else:
                d.rectangle((47, 54, 55, 55), fill=ink)
            frame = Image.new('RGBA', (SW, SH))
            frame.alpha_composite(face.crop((0, 58, SW, SH)), (0, 58))
            frame.alpha_composite(face.crop((0, 0, SW, 60)), (0, -breath))
            frames.append(frame)
    return frames


def draw_accent_particle(d: ImageDraw.ImageDraw, kind: str, color: str, ox: int, oy: int) -> None:
    """Draws the signature theme particle (rays, sparkle, heart) above XUNA's head."""
    if kind == 'heart':
        rows = [
            '.XX.XX.',
            'XXXXXXX',
            'XXXXXXX',
            '.XXXXX.',
            '..XXX..',
            '...X...',
        ]
        for r_idx, row in enumerate(rows):
            for c_idx, ch in enumerate(row):
                if ch == 'X':
                    d.rectangle((ox + c_idx * 3, oy + r_idx * 3, ox + c_idx * 3 + 2, oy + r_idx * 3 + 2), fill=color)
    elif kind == 'sparkle':
        for dx, dy, sz in [(0, 0, 3), (16, 12, 2)]:
            cx, cy = ox + dx, oy + dy
            d.rectangle((cx + sz, cy, cx + sz * 2 - 1, cy + sz * 3 - 1), fill=color)
            d.rectangle((cx, cy + sz, cx + sz * 3 - 1, cy + sz * 2 - 1), fill=color)
    else:
        d.line([(ox, oy + 12), (ox + 5, oy)], fill=color, width=3)
        d.line([(ox + 9, oy + 15), (ox + 18, oy + 7)], fill=color, width=3)


def main() -> None:
    role_grid = extract_clean_role_grid()
    themes = THEMES_CFG['themes']

    out_themes_dir = ROOT / 'assets/generated/themes'
    out_themes_dir.mkdir(parents=True, exist_ok=True)
    mobile_images_dir = REPO_ROOT / 'mobile/assets/images'

    room_bg_path = ROOT / 'assets/generated/room_background.png'
    room_bg = Image.open(room_bg_path).convert('RGBA') if room_bg_path.exists() else None

    emotions = ['idle', 'happy', 'curious', 'sleep']
    showcase_cards: list[Image.Image] = []

    for t in themes:
        tid = t['id']
        roles = t['roles']
        frames = build_emotion_frames_for_roles(role_grid, roles)
        t_dir = out_themes_dir / tid
        t_dir.mkdir(parents=True, exist_ok=True)

        for e_idx, e_name in enumerate(emotions):
            e_frames = frames[e_idx * FRAMES_PER_STATE : (e_idx + 1) * FRAMES_PER_STATE]
            still = e_frames[0]
            still.save(t_dir / f'xuna_pet_{e_name}.png')
            # Save all 7 themes x 4 expressions directly in mobile/assets/images/
            still.save(mobile_images_dir / f'xuna_pet_{tid}_{e_name}.png')

            if t.get('isDefault'):
                still.save(mobile_images_dir / f'xuna_pet_{e_name}.png')
                e_frames[0].save(
                    mobile_images_dir / f'xuna_pet_{e_name}.gif',
                    save_all=True,
                    append_images=e_frames[1:],
                    duration=STEP_MS,
                    loop=0,
                    disposal=2,
                )
                if room_bg is not None:
                    scene_views = []
                    for f in e_frames:
                        v = room_bg.copy()
                        v.alpha_composite(f, (AX, AY))
                        scene_views.append(v.convert('RGB'))
                    scene_views[0].save(mobile_images_dir / f'xuna_{e_name}.png')
                    scene_views[0].save(
                        mobile_images_dir / f'xuna_{e_name}.gif',
                        save_all=True,
                        append_images=scene_views[1:],
                        duration=STEP_MS,
                        loop=0,
                    )

        card = Image.new('RGBA', (250, 225), (0xC6, 0xC4, 0xCA, 255))
        cd = ImageDraw.Draw(card)
        sprite_2x = frames[0].resize((SW * 2, SH * 2), Image.Resampling.NEAREST)
        card.alpha_composite(sprite_2x, (8, 18))
        draw_accent_particle(cd, t.get('accentParticle', 'rays'), roles['shadow'], 155, 22)
        role_order = ['highlight', 'hair', 'body', 'shadow', 'outline']
        for s_idx, r_key in enumerate(role_order):
            sy = 46 + s_idx * 23
            cd.rectangle((198, sy, 224, sy + 18), fill=roles[r_key], outline=roles['outline'], width=2)
        title = f"{t['index']:02d}. {t['name'].upper()}"
        cd.text((38, 172), title, fill='#1E1E1E')
        cd.text((38, 190), t['tagline'], fill='#4A484E')
        showcase_cards.append(card)

    cols, rows_cnt = 4, 2
    showcase = Image.new('RGB', (cols * 250, rows_cnt * 225), (0xC6, 0xC4, 0xCA))
    for idx, card in enumerate(showcase_cards):
        showcase.paste(card.convert('RGB'), ((idx % cols) * 250, (idx // cols) * 225))
    showcase_path = ROOT / 'assets/generated/xuna_color_themes_showcase.png'
    showcase.save(showcase_path)
    print(f'Saved 7-theme showcase to {showcase_path}')

    # Also update generate_pet_scene.py and generate_track_a.py if needed
    p1 = ROOT / 'tools/generate_pet_scene.py'
    s1 = p1.read_text(encoding='utf-8').replace('\r\n', '\n')
    old1 = (
        "source=Image.open(ROOT/'assets/source/xuna-emotion-sheet-v2.png').convert('RGBA')\n"
        "cell=source.crop((0,0,source.width//4,source.height//4))\n"
        "cell.putalpha(cell.getchannel('A').point(lambda a:255 if a>=128 else 0))\n"
        "cut=cell.crop(cell.getbbox())\n"
        "cut=cut.resize((62,53),Image.Resampling.NEAREST)\n"
        "base=Image.new('RGBA',(SW,SH));base.alpha_composite(cut,(17,13))\n"
        "# Fixed feet and lower body. Only the upper body breathes, by at most two pixels.\n"
        "FRAMES_PER_STATE=32\n"
        "STEP_MS=100\n"
        "frames=[]\n"
        "ink='#161516';white='#fff7e9';skin='#ee7837'"
    )
    new1 = (
        "from generate_color_theme_sprites import (\n"
        "    THEMES_CFG,\n"
        "    EYE_WHITE,\n"
        "    extract_clean_role_grid,\n"
        "    build_base_sprite_for_roles,\n"
        ")\n"
        "DEFAULT_ROLES = next(t['roles'] for t in THEMES_CFG['themes'] if t.get('isDefault'))\n"
        "base = build_base_sprite_for_roles(extract_clean_role_grid(), DEFAULT_ROLES)\n"
        "# Fixed feet and lower body. Only the upper body breathes, by at most two pixels.\n"
        "FRAMES_PER_STATE=32\n"
        "STEP_MS=100\n"
        "frames=[]\n"
        "ink=DEFAULT_ROLES['outline'];white=EYE_WHITE.lower();skin=DEFAULT_ROLES['hair']"
    )
    if old1 in s1:
        p1.write_text(s1.replace(old1, new1), encoding='utf-8')
        print('Updated generate_pet_scene.py to use Theme 01 — Green Edition default roles.')

    p2 = ROOT / 'tools/generate_track_a.py'
    s2 = p2.read_text(encoding='utf-8').replace('\r\n', '\n')
    old2 = "INK, WHITE, SKIN = '#161516', '#fff7e9', '#ee7837'\nTONGUE = '#a94324'"
    new2 = "INK, WHITE, SKIN = '#1E1E1E', '#fff7e9', '#B7E17D'\nTONGUE = '#5F7F52'"
    if old2 in s2:
        p2.write_text(s2.replace(old2, new2), encoding='utf-8')
        print('Updated generate_track_a.py to use Theme 01 — Green Edition default roles.')


if __name__ == '__main__':
    main()
