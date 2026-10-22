#!/usr/bin/env python3
"""Convert the approved XUNA sheet to 128 RGB565 frames and preserve the room.
Frame order: Idle / Happy / Curious / Sleep, 32 timed loop frames per state.
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
cfg=json.loads((ROOT/'assets/scene_config.json').read_text())
P=cfg['palette'];W,H=cfg['lcd'];SW,SH=cfg['sprite'];AX,AY=cfg['anchor']
def polygon(d,points,fill,width=2):
 d.polygon(points,fill=P[fill]);d.line(points+[points[0]],fill=P['outline'],width=width)
SKYLINE=[(13,75),(23,75),(23,67),(31,67),(31,78),(43,78),(43,56),(51,56),(51,73),(66,73),(66,63),(79,63),(79,91),(13,91)]
# Window backdrops (Backdrop enum order). Day is the approved original art.
WINDOW=(9,15,84,96)            # frame box, exclusive right/bottom
GLASS=(13,19,80,92)            # glass incl. mullion, exclusive right/bottom
MULLION=(43,46)
STARS=[(18,25),(30,30),(22,41),(37,23),(39,48),(50,27),(72,47),(27,57),(76,24),(53,44)]
LIT={'night':[(46,60),(48,66),(69,67),(73,72),(25,71),(35,82),(56,78),(16,80)],
     'evening':[(46,62),(70,68),(25,72)]}
SKY={'evening':[(19,'#4A3466'),(34,'#7E4470'),(48,'#C25E62'),(61,'#EE9A5C')],
     'night':[(19,'#1C1D44'),(50,'#262A57')],
     'rain':[(19,'#5D6880'),(51,'#6F7A92')]}
CITY={'evening':'#3A2840','night':'#17162B','rain':'#444B60'}
def draw_window(d,variant):
 d.rectangle((9,15,83,95),fill=P['outline'])
 if variant=='day':
  # Window in old left-wall position, desaturated olive and flat skyline.
  d.rectangle((13,19,79,91),fill=P['olive'])
  d.polygon(SKYLINE,fill=P['olive_shadow'])
 else:
  bands=SKY[variant]
  for i,(y,c) in enumerate(bands):
   d.rectangle((13,y,79,(bands[i+1][0]-1) if i+1<len(bands) else 91),fill=c)
  if variant=='evening':d.ellipse((53,58,67,72),fill='#FFD98A')
  if variant=='night':
   d.ellipse((56,25,68,37),fill='#F3E6B5');d.ellipse((60,22,72,34),fill=SKY['night'][0][1])
   for x,y in STARS:d.point((x,y),fill='#FFF4C8')
  d.polygon(SKYLINE,fill=CITY[variant])
  for x,y in LIT.get(variant,[]):d.rectangle((x,y,x+1,y+1),fill='#E8B85A')
 d.rectangle((43,19,46,91),fill=P['outline'])
# Effects above XUNA's head (EffectKind order after None): heart, puff, sparkle.
EFFECT=(154,42,16,30)          # x, y, w, h of the effect region (wall only)
FX_COLORS={'X':P['outline'],'H':'#E8506A','W':'#FFFFFF','G':'#B9B4C4','L':'#E6E2EC','Y':'#F6C445','V':'#FFF6D5'}
FX={'heart':['..XXX...XXX..','.XHHHX.XHHHX.','XHWHHHXHHHHHX','XHWHHHHHHHHHX','XHHHHHHHHHHHX','.XHHHHHHHHHX.',
             '..XHHHHHHHX..','...XHHHHHX...','....XHHHX....','.....XHX.....','......X......'],
    'puff':['.............','....XXX......','...XGGGX.XX..','..XGGLGGXGGX.','.XGGLLGGGGGX.','XGGGGGGGGGGGX',
            'XGGGGGGGGGGGX','.XGGGGGGGGGX.','..XXXXXXXXX..','.............','.............'],
    'sparkle':['......Y......','......Y......','.....YVY.....','....YVVVY....','YYYYVVVVVYYYY','....YVVVY....',
               '.....YVY.....','......Y......','......Y......','.............','.............']}
FXW,FXH=13,11
def background():
 im=Image.new('RGB',(W,H),P['wall']);d=ImageDraw.Draw(im)
 d.rectangle((0,117,319,169),fill=P['floor']);d.rectangle((0,115,319,117),fill=P['outline'])
 draw_window(d,'day')
 # Frame, hard shadow and simple plant on the right ledge; no decorative text.
 d.rectangle((273,26,297,51),fill=P['floor_shadow']);d.rectangle((269,22,293,47),fill=P['outline']);d.rectangle((272,25,290,44),fill=P['floor'])
 d.rectangle((265,35,268,44),fill=P['wall'])
 d.rectangle((270,110,319,120),fill=P['outline']);d.rectangle((273,113,319,119),fill=P['floor']);d.rectangle((277,120,319,127),fill=P['floor_shadow'])
 d.line((293,96,293,76),fill=P['olive_shadow'],width=3)
 polygon(d,[(292,79),(290,70),(294,66),(298,66),(298,74)],'olive',1)
 polygon(d,[(291,87),(281,82),(280,77),(285,77),(292,82)],'olive',1)
 polygon(d,[(295,91),(303,81),(308,81),(307,87),(298,94)],'olive',1)
 polygon(d,[(281,94),(304,94),(302,111),(283,111)],'cream')
 # Grounded shadow is independent of sprite deformation.
 d.polygon([(118,133),(128,130),(190,130),(203,136),(190,142),(129,142),(115,138)],fill=P['floor_shadow'])
 return im
# Use ONE upright reference for every frame; never interpolate mismatched AI poses.
import math
source=Image.open(ROOT/'assets/source/xuna-emotion-sheet-v2.png').convert('RGBA')
cell=source.crop((0,0,source.width//4,source.height//4))
cell.putalpha(cell.getchannel('A').point(lambda a:255 if a>=128 else 0))
cut=cell.crop(cell.getbbox())
cut=cut.resize((62,53),Image.Resampling.NEAREST)
base=Image.new('RGBA',(SW,SH));base.alpha_composite(cut,(17,13))
# Fixed feet and lower body. Only the upper body breathes, by at most two pixels.
FRAMES_PER_STATE=32
STEP_MS=100
frames=[]
ink='#161516';white='#fff7e9';skin='#ee7837'
for emotion in range(4):
 for step in range(FRAMES_PER_STATE):
  phase=step/FRAMES_PER_STATE
  breath=round(1-math.cos(2*math.pi*phase))
  if emotion==1: breath=round(1-math.cos(4*math.pi*phase))
  expression=round(1.5*(1-math.cos(2*math.pi*phase)))
  face=base.copy();d=ImageDraw.Draw(face)
  # Re-author the small face on the same immutable body, avoiding pose morphing.
  for x in (34,54):
   d.rectangle((x,42,x+15,50),fill=skin)
   if emotion==3 or (emotion==0 and step==27):
    d.line((x+1,47,x+12,47),fill=ink,width=2)
   elif emotion==0 and step in (26,28):
    d.rectangle((x+1,46,x+13,50),fill=white)
    d.rectangle((x+9,46,x+12,49),fill=ink)
   elif emotion==1:
    d.line([(x+2,48),(x+2,45),(x+5,43),(x+8,43),(x+11,45),(x+11,48)],fill=ink,width=2)
   else:
    d.rectangle((x+1,43,x+13,50),fill=white)
    gaze=0
    if emotion==2:
     gaze=round(2*math.sin(2*math.pi*phase))
    d.rectangle((x+8+gaze,43,x+11+gaze,48),fill=ink)
  d.rectangle((43,53,59,58),fill=skin)
  if emotion==1:
   d.line([(47-expression,54),(49-expression,56),(53+expression,56),(55+expression,54)],fill=ink,width=2)
  elif emotion==2:
   d.rectangle((50,54,53,57),fill=ink)
  elif emotion==3:
   d.rectangle((49-expression//2,54,53+expression//2,55),fill=ink)
  else:
   d.rectangle((47,54,55,55),fill=ink)
  frame=Image.new('RGBA',(SW,SH))
  frame.alpha_composite(face.crop((0,58,SW,SH)),(0,58))
  frame.alpha_composite(face.crop((0,0,SW,60)),(0,-breath))
  frames.append(frame)
# The feet and their anchor must remain byte-identical across every emotion/frame.
assert all(f.crop((0,59,SW,SH)).tobytes()==frames[0].crop((0,59,SW,SH)).tobytes() for f in frames)
def rgb565(rgb):
 r,g,b=rgb[:3];return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
def write_array(name,values):
 return 'alignas(4) const uint16_t '+name+'[] PROGMEM = {\n'+'\n'.join('  '+', '.join('0x%04x'%v for v in values[i:i+12])+',' for i in range(0,len(values),12))+'\n};\n'
bg=background()
def window_variant(variant):
 im=bg.copy();draw_window(ImageDraw.Draw(im),variant);return im
variants=['day','evening','night','rain']
windows=[window_variant(v).crop(WINDOW) for v in variants]
assert windows[0].tobytes()==bg.crop(WINDOW).tobytes()
fx_images=[]
for name in ('heart','puff','sparkle'):
 rows=FX[name];assert len(rows)==FXH and all(len(r)==FXW for r in rows)
 im=Image.new('RGBA',(FXW,FXH))
 for y,row in enumerate(rows):
  for x,ch in enumerate(row):
   if ch!='.':im.putpixel((x,y),Image.new('RGB',(1,1),FX_COLORS[ch]).getpixel((0,0))+(255,))
 fx_images.append(im)
output=ROOT/'assets/generated';output.mkdir(exist_ok=True)
bg.save(output/'room_background.png');sheet=Image.new('RGBA',(SW*32,SH*4))
for i,frame in enumerate(frames):sheet.paste(frame,((i%32)*SW,(i//32)*SH))
sheet.save(output/'pet_frames.png')
preview=bg.convert('RGBA');preview.alpha_composite(frames[0],(AX,AY));preview.convert('RGB').save(output/'pet_lcd_preview.png')
sequence=list(range(len(frames)))
views=[]
for i in sequence:
 view=bg.convert('RGBA');view.alpha_composite(frames[i],(AX,AY));views.append(view.convert('RGB'))
sheet=Image.new('RGB',(W*2+8,H*2+8),'#000000')
for i,v in enumerate(variants):
 view=window_variant(v).convert('RGBA');view.alpha_composite(frames[0],(AX,AY))
 view.alpha_composite(fx_images[i%3],(EFFECT[0]+1,EFFECT[1]+EFFECT[3]-FXH-7))
 sheet.paste(view.convert('RGB'),((i%2)*(W+8),(i//2)*(H+8)))
sheet.save(output/'scene_backdrops.png')
views[0].save(output/'pet_animation.gif',save_all=True,append_images=views[1:],duration=STEP_MS,loop=0,disposal=2)
header='''// Generated by tools/generate_pet_scene.py.
#pragma once
#include <cstdint>
namespace xuna { namespace scene {
constexpr uint16_t kWidth=%d,kHeight=%d,kSpriteWidth=%d,kSpriteHeight=%d;
constexpr uint16_t kAnchorX=%d,kAnchorY=%d,kFrameCount=128,kTransparent=0xf81f;
extern const uint16_t kBackground[kWidth*kHeight];
extern const uint16_t kFrames[kFrameCount*kSpriteWidth*kSpriteHeight];
// Window backdrops (Backdrop order: Day, Evening, Night, Rain), frame included.
constexpr uint16_t kWindowX=%d,kWindowY=%d,kWindowW=%d,kWindowH=%d,kWindowCount=4;
// Glass (incl. mullion) where rain falls, relative to the screen; mullion columns are skipped.
constexpr uint16_t kGlassX=%d,kGlassY=%d,kGlassW=%d,kGlassH=%d,kMullionX0=%d,kMullionX1=%d;
constexpr uint16_t kRainColor=0x%04x,kStarBright=0x%04x,kStarDim=0x%04x,kStarCount=%d;
extern const uint16_t kWindows[kWindowCount*kWindowW*kWindowH];
extern const uint8_t kStarX[kStarCount],kStarY[kStarCount];
// Effect region above the head (wall only) and its sprites (Heart, Puff, Sparkle).
constexpr uint16_t kEffectX=%d,kEffectY=%d,kEffectW=%d,kEffectH=%d,kEffectSpriteW=%d,kEffectSpriteH=%d,kEffectCount=3;
extern const uint16_t kEffects[kEffectCount*kEffectSpriteW*kEffectSpriteH];
}}
'''%(W,H,SW,SH,AX,AY,WINDOW[0],WINDOW[1],WINDOW[2]-WINDOW[0],WINDOW[3]-WINDOW[1],
     GLASS[0],GLASS[1],GLASS[2]-GLASS[0],GLASS[3]-GLASS[1],MULLION[0],MULLION[1],
     rgb565((0xB8,0xC4,0xD8)),rgb565((0xFF,0xF4,0xC8)),rgb565((0x6E,0x6F,0xA0)),len(STARS),
     EFFECT[0],EFFECT[1],EFFECT[2],EFFECT[3],FXW,FXH)
(ROOT/'include/assets/XunaAnimatedScene.h').write_text(header)
values=[rgb565(p) if p[3] else 0xf81f for frame in frames for p in frame.getdata()]
assert all(rgb565(p)!=0xf81f for f in frames for p in f.getdata() if p[3])
(ROOT/'src/assets/XunaAnimatedScene.cpp').write_text('#include <pgmspace.h>\n#include "assets/XunaAnimatedScene.h"\nnamespace xuna { namespace scene {\n'+write_array('kBackground',[rgb565(p) for p in bg.getdata()])+write_array('kFrames',values)
 +write_array('kWindows',[rgb565(p) for w in windows for p in w.getdata()])
 +'const uint8_t kStarX[] PROGMEM = {'+', '.join(str(x) for x,_ in STARS)+'};\n'
 +'const uint8_t kStarY[] PROGMEM = {'+', '.join(str(y) for _,y in STARS)+'};\n'
 +write_array('kEffects',[rgb565(p) if p[3] else 0xf81f for im in fx_images for p in im.getdata()])+'}}\n')
(output/'animated_scene.json').write_text(json.dumps({'background_bytes':W*H*2,'frame_bytes':SW*SH*2,'frames':len(frames), 'frames_per_state':32, 'step_ms':STEP_MS,'total_flash_pixels_bytes':W*H*2+len(values)*2,'opaque_height':max(f.getbbox()[3] for f in frames)-min(f.getbbox()[1] for f in frames),'height_percent':(max(f.getbbox()[3] for f in frames)-min(f.getbbox()[1] for f in frames))/H*100,'anchor':[AX,AY]},indent=2)+'\n')
print('Generated background + 128 anchored frames; Flash pixels:',W*H*2+len(values)*2)
