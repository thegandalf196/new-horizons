#!/usr/bin/env python3
"""CC0 synthetic software tests; not art-generation or template rights evidence.
Run only with separately granted Build light-test slot. No purchaser art input.
"""
import hashlib,io,tempfile,unittest
from pathlib import Path
from PIL import Image
from compose import composite,verify_foreground,load_template,chunks,REVIEW

def foreground():
 im=Image.new('RGBA',(8,8),(255,0,255,0))
 for y in range(2,6):
  for x in range(2,6):im.putpixel((x,y),(255,0,0,255))
 im.putpixel((2,2),(255,0,0,128));return im

def template():return Image.new('RGBA',(12,12),(0,0,0,255))

class CompositorTests(unittest.TestCase):
 def test_true_alpha_stats(self):
  s=verify_foreground(foreground());self.assertEqual(s['partial_pixels'],1);self.assertGreater(s['transparent_pixels'],0)
 def test_rgb_or_checkerboard_rejected(self):
  with self.assertRaisesRegex(ValueError,'actual RGBA'):verify_foreground(foreground().convert('RGB'))
 def test_opaque_rgba_rejected(self):
  im=foreground();im.putalpha(255)
  with self.assertRaisesRegex(ValueError,'clear exterior'):verify_foreground(im)
 def test_empty_foreground_rejected(self):
  with self.assertRaises(ValueError):verify_foreground(Image.new('RGBA',(8,8)))
 def test_hard_cutout_only_rejected(self):
  im=foreground();im.putpixel((2,2),(255,0,0,255))
  with self.assertRaisesRegex(ValueError,'partial-alpha'):verify_foreground(im)
 def test_canvas_touch_rejected(self):
  im=foreground();im.putpixel((0,4),(255,0,0,255))
  with self.assertRaisesRegex(ValueError,'canvas rim'):verify_foreground(im)
 def test_correct_partial_alpha_no_double_application(self):
  result,layer,report=composite(template(),foreground(),[2,2,10,10])
  self.assertEqual(result.getpixel((4,4)),(128,0,0,255));self.assertEqual(layer.getpixel((4,4)),(255,0,0,128));self.assertEqual(report['offset'],[2,2])
 def test_transparent_rgb_cannot_create_magenta_fringe(self):
  result,_,_=composite(template(),foreground(),[4,4,8,8])
  self.assertTrue(all(p[1]==0 and p[2]==0 for p in result.getdata()))
 def test_reserved_border_and_zero_mask_preserved(self):
  t=template();t.putpixel((0,0),(8,22,39,17));before=t.tobytes();fg=foreground();fbytes=fg.tobytes()
  result,layer,report=composite(t,fg,[2,2,10,10])
  for old,new,a in zip(t.getdata(),result.getdata(),layer.getchannel('A').getdata()):
   if not a:self.assertEqual(old,new)
  self.assertEqual(result.getchannel('A').tobytes(),t.getchannel('A').tobytes());self.assertEqual(t.tobytes(),before);self.assertEqual(fg.tobytes(),fbytes)
 def test_template_transparency_conflict_rejected_not_erased(self):
  t=template();t.putpixel((4,4),(0,0,0,128))
  with self.assertRaisesRegex(ValueError,'protected template transparency'):composite(t,foreground(),[2,2,10,10])
 def test_unsafe_border_rect_rejected(self):
  with self.assertRaisesRegex(ValueError,'protect every'):composite(template(),foreground(),[0,0,12,12])
 def test_aspect_preserved_and_template_not_resized(self):
  t=Image.new('RGBA',(82,93),(19,20,21,255));result,_,r=composite(t,foreground(),[3,3,79,90])
  self.assertEqual(result.size,(82,93));self.assertEqual(r['resized_foreground'],(76,76));self.assertEqual(r['offset'],[3,8])
 def test_repeated_png_bytes_identical(self):
  def render():
   result,_,_=composite(template(),foreground(),[2,2,10,10]);b=io.BytesIO();result.save(b,format='PNG');return b.getvalue()
  self.assertEqual(render(),render())
 def test_template_hash_size_mode_and_blob_guards(self):
  REVIEW.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='template-test-',dir=REVIEW) as td:
   p=Path(td)/'synthetic.png';template().save(p);data=p.read_bytes()
   record={'sha256':hashlib.sha256(data).hexdigest(),'git_blob':hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(),'size':[12,12],'mode':'RGBA'}
   self.assertEqual(load_template(p,record).size,(12,12))
   self.assertEqual([c['type'] for c in chunks(p)],['IHDR','IDAT','IEND'])
   for key,value,message in [('sha256','0'*64,'hash'),('git_blob','0'*40,'blob'),('size',[32,32],'dimensions'),('mode','RGB','mode')]:
    with self.subTest(key=key),self.assertRaisesRegex(ValueError,message):load_template(p,{**record,key:value})
 def test_crc_corruption_rejects(self):
  REVIEW.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='template-test-',dir=REVIEW) as td:
   p=Path(td)/'synthetic.png';template().save(p);data=bytearray(p.read_bytes());data[-1]^=1;p.write_bytes(data)
   with self.assertRaisesRegex(ValueError,'CRC'):chunks(p)

if __name__=='__main__':unittest.main()
