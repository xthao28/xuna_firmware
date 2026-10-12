#!/usr/bin/env python3
"""Draw original simplified pixel geometry from xuna-pet reference, export RGB565/GIF.
Uses existing Pillow. Reference is retained for design provenance, not resized into frames.
Frame order: bob(2) x eyes(5: normal/half/closed/open/happy) x feet(2).
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFilter
ROOT=Path(__file__).resolve().parents[1]
cfg=json.loads((ROOT/'assets/scene_config.json').read_text())
P=cfg['palette'];W,H=cfg['lcd'];SW,SH=cfg['sprite'];AX,AY=cfg['anchor']
def polygon(d,points,fill,width=2):
 d.polygon(points,fill=P[fill]);d.line(points+[points[0]],fill=P['outline'],width=width)
def background():
 im=Image.new('RGB',(W,H),P['wall']);d=ImageDraw.Draw(im)
 d.rectangle((0,117,319,169),fill=P['floor']);d.rectangle((0,115,319,117),fill=P['outline'])
 # Window in old left-wall position, desaturated olive and flat skyline.
 d.rectangle((9,15,83,95),fill=P['outline']);d.rectangle((13,19,79,91),fill=P['olive'])
 d.polygon([(13,75),(23,75),(23,67),(31,67),(31,78),(43,78),(43,56),(51,56),(51,73),(66,73),(66,63),(79,63),(79,91),(13,91)],fill=P['olive_shadow'])
 d.rectangle((43,19,46,91),fill=P['outline'])
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
# Trace only the character's large semantic regions, discard original texture/shading.
# Re-author eyes, spots and moving feet on a fixed pixel canvas. Not a resized photo sprite.
reference=Image.open(ROOT/'assets/source/xuna-pet.png').convert('RGB').crop((500,210,1240,815))
orange=Image.new('L',reference.size);cream=Image.new('L',reference.size);dark=Image.new('L',reference.size)
orange.putdata([255 if r>65 and r>g*1.30 and r>b*1.15 else 0 for r,g,b in reference.getdata()])
cream.putdata([255 if r>175 and g>160 and b>110 and r>b*1.10 else 0 for r,g,b in reference.getdata()])
orange=orange.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.MinFilter(9))
cream=cream.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.MinFilter(7))
near=Image.new('L',reference.size);near.putdata([max(a,b) for a,b in zip(orange.getdata(),cream.getdata())]);near=near.filter(ImageFilter.MaxFilter(35))
dark.putdata([255 if n and r<65 and g<60 and b<70 else 0 for (r,g,b),n in zip(reference.getdata(),near.getdata())])
combined=Image.new('L',reference.size);combined.putdata([max(a,b,c) for a,b,c in zip(orange.getdata(),cream.getdata(),dark.getdata())]);box=combined.getbbox()
# The traced bounding box uses 80x64, keeping the reference's broad compact proportions.
masks=[m.crop(box).resize((80,64),Image.Resampling.NEAREST) for m in (orange,cream,dark)]
base=Image.new('RGBA',(SW,SH))
for mask,color in [(masks[2],P['outline']),(masks[0],P['pet']),(masks[1],P['cream'])]:
 layer=Image.new('RGBA',(80,64),color);layer.putalpha(mask);base.alpha_composite(layer,(8,3))
# Close small texture holes without retaining any photographic texture.
alpha=base.getchannel('A');outside=alpha.point(lambda x:0 if x else 255)
ImageDraw.floodfill(outside,(0,0),0)
for y in range(SH):
 for x in range(SW):
  if outside.getpixel((x,y)):base.putpixel((x,y),tuple(bytes.fromhex(P['pet'][1:]))+(255,))
base.save(ROOT/'assets/generated/pet_authored_base.png')
def pet(bob,eye,wiggle):
 im=Image.new('RGBA',(SW,SH))
 # Independent feet, sampled from an immutable base. Draw behind torso with overlap.
 for i,(x0,x1) in enumerate([(8,26),(26,43),(43,55),(55,71),(71,91)]):
  foot=base.crop((x0,50,x1,SH));bounds=foot.getbbox()
  if bounds:
   for y in range(max(0,bounds[3]-4),bounds[3]):
    for x in range(foot.width):
     if foot.getpixel((x,y))[3]:foot.putpixel((x,y),tuple(bytes.fromhex(P['outline'][1:]))+(255,))
  dx=(-2 if i%2 else 2) if wiggle else 0
  im.alpha_composite(foot,(x0+dx,50))
 # Overlap at the joint: a one-pixel breath cannot expose a full transparent scanline.
 im.alpha_composite(base.crop((0,0,SW,55)),(0,-bob))
 d=ImageDraw.Draw(im)
 # Five stable body-relative dark-orange spots.
 for x,y in [(27,34),(20,42),(38,48),(73,40),(65,48)]:
  d.rectangle((x-2,y-1-bob,x+2,y+1-bob),fill=P['pet_shadow']);d.rectangle((x-1,y-2-bob,x+1,y+2-bob),fill=P['pet_shadow'])
 if eye:
  # Preserve default sly face from traced regions; re-author the eyelids for animation.
  for rect in [(29,33-bob,49,45-bob),(59,30-bob,77,43-bob)]:d.rectangle(rect,fill=P['pet'])
  if eye==2:
   d.line((31,39-bob,46,39-bob),fill=P['outline'],width=2);d.line((61,37-bob,74,36-bob),fill=P['outline'],width=2)
  elif eye==4:
   d.line([(31,40-bob),(35,36-bob),(40,35-bob),(45,39-bob)],fill=P['outline'],width=2)
   d.line([(60,39-bob),(64,35-bob),(70,35-bob),(74,38-bob)],fill=P['outline'],width=2)
   d.rectangle((44,45-bob,59,53-bob),fill=P['outline']);d.rectangle((47,46-bob,56,48-bob),fill=P['cream'])
  else:
   depth=3 if eye==1 else 10
   polygon(d,[(30,34-bob),(48,34-bob),(45,34+depth-bob),(35,36+depth-bob)],'cream')
   polygon(d,[(61,33-bob),(74,31-bob),(75,32+depth-bob),(65,35+depth-bob)],'cream')
 return im
def rgb565(rgb):
 r,g,b=rgb[:3];return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)
def write_array(name,values):
 return 'alignas(4) const uint16_t '+name+'[] PROGMEM = {\n'+'\n'.join('  '+', '.join('0x%04x'%v for v in values[i:i+12])+',' for i in range(0,len(values),12))+'\n};\n'
bg=background();frames=[pet(b,e,w) for b in range(2) for e in range(5) for w in range(2)]
output=ROOT/'assets/generated';output.mkdir(exist_ok=True)
bg.save(output/'room_background.png');sheet=Image.new('RGBA',(SW*5,SH*4))
for i,frame in enumerate(frames):sheet.paste(frame,((i%5)*SW,(i//5)*SH))
sheet.save(output/'pet_frames.png')
preview=bg.convert('RGBA');preview.alpha_composite(frames[0],(AX,AY));preview.convert('RGB').save(output/'pet_lcd_preview.png')
sequence=[0,0,10,10,0,2,4,6,0,1,0,1,0,8,8,8,0,0]
views=[]
for i in sequence:
 view=bg.convert('RGBA');view.alpha_composite(frames[i],(AX,AY));views.append(view.convert('RGB'))
views[0].save(output/'pet_animation.gif',save_all=True,append_images=views[1:],duration=[700,700,700,700,700,700,90,110,120,700,150,150,150,700,700,700,700,700],loop=0,disposal=2)
header='''// Generated by tools/generate_pet_scene.py.
#pragma once
#include <cstdint>
namespace xuna { namespace scene {
constexpr uint16_t kWidth=%d,kHeight=%d,kSpriteWidth=%d,kSpriteHeight=%d;
constexpr uint16_t kAnchorX=%d,kAnchorY=%d,kFrameCount=20,kTransparent=0xf81f;
extern const uint16_t kBackground[kWidth*kHeight];
extern const uint16_t kFrames[kFrameCount*kSpriteWidth*kSpriteHeight];
}}
'''%(W,H,SW,SH,AX,AY)
(ROOT/'include/assets/XunaAnimatedScene.h').write_text(header)
values=[rgb565(p) if p[3] else 0xf81f for frame in frames for p in frame.getdata()]
assert 0xf81f not in [rgb565(tuple(bytes.fromhex(v[1:]))) for v in P.values()]
(ROOT/'src/assets/XunaAnimatedScene.cpp').write_text('#include <pgmspace.h>\n#include "assets/XunaAnimatedScene.h"\nnamespace xuna { namespace scene {\n'+write_array('kBackground',[rgb565(p) for p in bg.getdata()])+write_array('kFrames',values)+'}}\n')
(output/'animated_scene.json').write_text(json.dumps({'background_bytes':W*H*2,'frame_bytes':SW*SH*2,'frames':20,'total_flash_pixels_bytes':W*H*2+len(values)*2,'opaque_height':max(f.getbbox()[3] for f in frames)-min(f.getbbox()[1] for f in frames),'height_percent':(max(f.getbbox()[3] for f in frames)-min(f.getbbox()[1] for f in frames))/H*100,'anchor':[AX,AY]},indent=2)+'\n')
print('Generated background + 20 authored frames; Flash pixels:',W*H*2+len(values)*2)
