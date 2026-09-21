#!/usr/bin/env python3
"""CC0 software ONLY. No dedication/license claim for templates or external art.
Private native compositor: requires genuine supplied RGBA, never extracts it.
CLI writes only ignored Artist research, and needs a separately granted Build slot.
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,__version__ as PILLOW_VERSION
HERE=Path(__file__).resolve().parent
REVIEW=Path('build/new-horizons-linux/research/sorcery-art').resolve()

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)

def chunks(path):
 data=path.read_bytes();require(data[:8]==b'\x89PNG\r\n\x1a\n','not PNG');p=8;records=[]
 while p<len(data):
  require(p+12<=len(data),'truncated chunk header')
  n=struct.unpack_from('>I',data,p)[0];end=p+n+12;require(end<=len(data),'truncated chunk')
  kind=data[p+4:p+8];payload=data[p+8:p+8+n]
  require(zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',data,p+8+n)[0],'bad PNG CRC')
  records.append({'type':kind.decode('ascii'),'bytes':n});p=end
  if kind==b'IEND':break
 require(records and records[-1]['type']=='IEND' and p==len(data),'missing IEND or trailing data')
 return records

def clean_rgba(im):
 fresh=Image.new('RGBA',im.size);fresh.paste(im.convert('RGBA'));return fresh

def verify_foreground(im):
 require(im.mode=='RGBA','foreground must be actual RGBA, not RGB/checkerboard')
 a=im.getchannel('A');hist=a.histogram();require(hist[0]>0 and hist[255]>0,'foreground needs clear exterior and opaque subject')
 require(sum(hist[1:255])>0,'foreground needs genuine partial-alpha edge/wisp samples')
 w,h=im.size;require(w>=4 and h>=4,'foreground canvas too small')
 for box in [(0,0,w,1),(0,h-1,w,h),(0,0,1,h),(w-1,0,w,h)]:require(not a.crop(box).getbbox(),'foreground touches canvas rim; return to editor')
 return {'size':im.size,'alpha_bbox':a.getbbox(),'transparent_pixels':hist[0],'partial_pixels':sum(hist[1:255]),'opaque_pixels':hist[255]}

def load_template(path,slot):
 require(sha(path)==slot['sha256'],'template hash mismatch')
 data=path.read_bytes();blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
 require(blob==slot['git_blob'],'template Git blob mismatch')
 with Image.open(path) as im:
  require(list(im.size)==slot['size'],'template dimensions mismatch: never substitute scaled template')
  require(im.mode==slot['mode'],'template mode mismatch')
  return clean_rgba(im)

def validate_rect(rect,size):
 require(len(rect)==4 and all(type(v) is int for v in rect),'invalid safe rectangle')
 x0,y0,x1,y1=rect;w,h=size
 require(0<x0<x1<w and 0<y0<y1<h,'safe rectangle must protect every exterior border')

def composite(template,foreground,rect):
 """No crop or clipping. Whole foreground fits declared rect at uniform scale."""
 verify_foreground(foreground);validate_rect(rect,template.size)
 base=clean_rgba(template);fg=clean_rgba(foreground);x0,y0,x1,y1=rect
 scale=min((x1-x0)/fg.width,(y1-y0)/fg.height)
 size=(max(1,int(fg.width*scale)),max(1,int(fg.height*scale)))
 reduced=fg.convert('RGBa').resize(size,Image.Resampling.LANCZOS).convert('RGBA')
 x=x0+((x1-x0)-size[0])//2;y=y0+((y1-y0)-size[1])//2
 layer=Image.new('RGBA',base.size);layer.paste(reduced,(x,y))
 # Never silently erase conflicting pixels to make a border test green.
 alpha=layer.getchannel('A');before=list(base.getdata());lp=list(layer.getdata());w=base.width
 for i,(bp,fp) in enumerate(zip(before,lp)):
  px=i%w;py=i//w
  if fp[3]:
   require(x0<=px<x1 and y0<=py<y1,'foreground outside safe mask')
   require(bp[3]==255,'foreground overlaps protected template transparency')
 result=Image.alpha_composite(base,layer)
 for i,(bp,rp,fp) in enumerate(zip(before,result.getdata(),lp)):
  px=i%w;py=i//w
  if not fp[3] or not(x0<=px<x1 and y0<=py<y1):require(bp==rp,'template outside foreground mask changed')
 require(result.getchannel('A').tobytes()==base.getchannel('A').tobytes(),'template alpha changed')
 return result,layer,{'safe_rect':rect,'scale':scale,'resized_foreground':size,'offset':[x,y],'actual_alpha_bbox':alpha.getbbox(),'outside_foreground_mask_unchanged':True,'template_alpha_unchanged':True}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--foreground',type=Path,required=True);ap.add_argument('--foreground-sha256',required=True);ap.add_argument('--provenance',type=Path,required=True);ap.add_argument('--templates',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
 out=args.out.resolve();require(out.is_relative_to(REVIEW) and out!=REVIEW,'output must be an isolated ignored Artist review subdirectory')
 require(not out.exists(),'output revision already exists: preserve prior reviews')
 require(sha(args.foreground)==args.foreground_sha256,'foreground hash mismatch')
 require(args.provenance.is_file(),'missing supplied provenance sidecar')
 fg=Image.open(args.foreground);stats=verify_foreground(fg)
 specpath=HERE/'placements.json';spec=json.loads(specpath.read_text());paths=[args.foreground,args.provenance,specpath]+[args.templates/s['template'] for s in spec['slots']]
 baseline={p:sha(p) for p in paths};loaded=[(s,load_template(args.templates/s['template'],s)) for s in spec['slots']]
 # Complete all validation/composition in memory before writing reviewed output.
 prepared=[(s,*composite(t,fg,s['safe_rect'])) for s,t in loaded]
 out.mkdir(parents=True)
 report={'status':'PRIVATE REVIEW ONLY; no visual/rights/family/import approval','foreground':{'name':args.foreground.name,'sha256':baseline[args.foreground],'png_chunks':chunks(args.foreground),**stats},'provenance_sha256':baseline[args.provenance],'template_repository':spec['repository'],'template_revision':spec['revision'],'placements_sha256':baseline[specpath],'script_sha256':sha(Path(__file__)),'pillow':PILLOW_VERSION,'method':'Only full foreground resized: premultiplied RGBa Lanczos -> straight RGBA; alpha_composite over exact native template; no crop/recolor/clipping','rights':'Upstream CC BY-SA4 declaration + mixed origins; selected-file clearance pending. External raster rights not assumed.','slots':[]}
 sheet=Image.new('RGB',(430,185),'#766c5b');d=ImageDraw.Draw(sheet);d.text((8,8),'PRIVATE NATIVE TEMPLATE COMPOSITION / NOT APPROVED',fill='white')
 for index,(s,result,layer,details) in enumerate(prepared):
  p=out/(s['id']+'.png');result.save(p);layer.save(out/(s['id']+'-foreground-layer.png'))
  require(all(c['type'] in ('IHDR','IDAT','IEND') for c in chunks(p)),'output metadata present')
  report['slots'].append({'id':s['id'],'template':s['template'],'template_sha256':s['sha256'],'template_git_blob':s['git_blob'],'size':s['size'],'output_sha256':sha(p),'foreground_layer_sha256':sha(out/(s['id']+'-foreground-layer.png')),**details})
  x=12+index*130;sheet.paste(result,(x,52),result);d.text((x,30),s['id']+' '+str(tuple(s['size'])),fill='white')
 d.text((8,165),'Templates unscaled; proprietary/mixed-origin rights pending',fill='white');sheet.save(out/'native-review.png')
 require(all(sha(p)==h for p,h in baseline.items()),'input changed during operation')
 report['inputs_unchanged']=True;report['sheet_sha256']=sha(out/'native-review.png');(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PRIVATE native composition PASS; not artistic or rights acceptance:',out)
if __name__=='__main__':main()
