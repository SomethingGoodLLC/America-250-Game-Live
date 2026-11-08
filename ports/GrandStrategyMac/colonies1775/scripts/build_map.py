"""Build deterministic colony lookup textures from archived OHM and Natural Earth data."""
import json, math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageFilter
ROOT=Path(__file__).resolve().parents[1]; OUT=ROOT/'output'; OUT.mkdir(exist_ok=True)
W=H=2048
bounds=(-85,29,-66,48)
def xy(p):
 lon,lat=p; return ((lon+85)/19*(W-1),(48-lat)/19*(H-1))
# Draw coastline with polygon holes, clipping to the requested East Coast extent.
land=Image.new('1',(W,H)); d=ImageDraw.Draw(land)
for f in json.load(open(ROOT/'source/land.geojson'))['features']:
 polys=f['geometry']['coordinates']
 if f['geometry']['type']=='Polygon':polys=[polys]
 for poly in polys:
  xs=[v[0] for v in poly[0]];ys=[v[1] for v in poly[0]]
  if max(xs)<-85 or min(xs)>-66 or max(ys)<29 or min(ys)>48:continue
  d.polygon([xy(p) for p in poly[0]],fill=1)
  for hole in poly[1:]:d.polygon([xy(p) for p in hole],fill=0)
# Fixed IDs are independent of source relation ordering.
colonies=[(2861371,'New Hampshire'),(2861369,'Massachusetts Bay'),(2861379,'Rhode Island'),(2861377,'Connecticut'),(2847811,'New York'),(2795396,'New Jersey'),(2861342,'Pennsylvania'),(2685030,'Delaware'),(2919890,'Maryland'),(2919251,'Virginia'),(2919249,'North Carolina'),(2919895,'South Carolina'),(2919897,'Georgia')]
colors=['D7A64B','AC694F','CE8993','8CA876','718CB4','B29AC7','D2C17D','D49065','A7B4C0','B88859','7DAD9B','7098A1','9A9F6D']
source={e['id']:e for e in json.load(open(ROOT/'source/ohm-boundaries.json'))['elements']}
ids=Image.new('L',(W,H),0); ids.paste(14,(0,0,W,H),land)
def rings(members):
 ways=[[ (p['lon'],p['lat']) for p in m['geometry']] for m in members if m['type']=='way' and m['role']=='outer']
 while ways:
  ring=ways.pop()
  while ring[0]!=ring[-1]:
   for n,w in enumerate(ways):
    if w[0]==ring[-1]:ring.extend(w[1:]);ways.pop(n);break
    if w[-1]==ring[-1]:ring.extend(w[-2::-1]);ways.pop(n);break
   else:raise RuntimeError('Unclosed boundary')
  yield ring
# Broad polygons first; Delaware is split from Pennsylvania explicitly.
order=[13,12,11,10,5,7,2,1,4,6,9,3,8]
records=[]
for i in order:
 rid,name=colonies[i-1]; mask=Image.new('1',(W,H));md=ImageDraw.Draw(mask)
 for ring in rings(source[rid]['members']):md.polygon([xy(p) for p in ring],fill=1)
 mask=ImageChops.logical_and(mask,land);ids.paste(i,(0,0,W,H),mask)
for i,(rid,name) in enumerate(colonies,1):
 records.append({'id':i,'name':name,'color':colors[i-1],'relation':rid,'source_tags':source[rid]['tags']})
records=[{'id':0,'name':'Atlantic and inland waters','color':'193D52'}]+records+[{'id':14,'name':'Surrounding lands','color':'686E61'}]
palette={r['id']:tuple(bytes.fromhex(r['color'])) for r in records}
lookup=Image.new('RGB',(W,H));lookup.putdata([palette[i] for i in ids.getdata()]);lookup.save(OUT/'T_Colonies1775_Lookup.png');ids.save(OUT/'province_ids.png')
preview=lookup.copy();pd=ImageDraw.Draw(preview)
# Borders only on the display texture, never in the lookup texture.
pix=ids.load();edge=Image.new('L',(W,H),0);ep=edge.load()
for y in range(1,H-1):
 for x in range(1,W-1):
  if pix[x,y]!=pix[x+1,y] or pix[x,y]!=pix[x,y+1]:ep[x,y]=255
# 3 px borders so they stay visible after mip filtering at the full-map zoom.
preview.paste((48,61,59),(0,0,W,H),edge.filter(ImageFilter.MaxFilter(3)))
preview.save(OUT/'T_Colonies1775_Display.png')
counts=ids.getcolors(W*H);counts={i:n for n,i in counts}
assert all(counts.get(i,0)>0 for i in range(15)),counts
for r in records:r['pixels']=counts[r['id']]
json.dump({'date':'1775-04-19','bounds':bounds,'width':W,'height':H,'regions':records},open(OUT/'manifest.json','w'),indent=2)
json.dump([{'Color':'#'+r['color']+'FF','Name':str(r['id'])} for r in records],open(OUT/'Lookup.json','w'),indent=2)
json.dump([{'ID':r['id'],'Name':r['name'],'Area':'East Coast','Region':'Thirteen Colonies' if 1<=r['id']<=13 else 'Context','StructType':'BaseMapStruct'} for r in records],open(OUT/'MapData.json','w'),indent=2)
# A labeled review sheet separate from the pixel-exact runtime textures.
sheet=Image.new('RGB',(2048,2400),'#ECE7D8');sheet.paste(preview,(0,0));pd=ImageDraw.Draw(sheet)
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',28)
pd.text((40,2070),'THIRTEEN COLONIES | 1775 | EAST COAST',fill='#263C43',font=font)
for i,r in enumerate(records[1:14]):
 x=40+(i%4)*500;y=2130+(i//4)*52;pd.rectangle((x,y,x+26,y+26),fill='#'+r['color']);pd.text((x+40,y),r['name'],fill='#263C43',font=font)
pd.text((40,2350),'Data: OpenHistoricalMap (CC0), Natural Earth (public domain). See README for scope.',fill='#263C43',font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',21))
sheet.save(OUT/'Colonies1775_preview.png')
print('Built 13 colonies + water + surrounding lands:',counts)
