# memref.py <low16hex>...: loads/stores with that displacement, and the function holding them
import os,struct,json,bisect,sys
from dol import *
meta=json.load(open(os.environ.get('WS','work/wc-eb')+'/generated/base_translation_output.json'))
f=sorted({x['entryPoint'] for x in meta['functions']})
m=json.load(open(os.environ.get('EB_WORK','work/excitebots')+'/mk2eb.json')); inv={v[0]:k for k,v in m.items()}
s,o,z,t=[x for x in secs if x[0]==0x800045e0][0]
W=struct.unpack('>%dI'%(z//4),D[o:o+z])
want={int(x,16) for x in sys.argv[1:]}
for k,w in enumerate(W):
    if (w&0xFFFF) in want and 32<=w>>26<=55:
        a=s+4*k; fn=f[bisect.bisect_right(f,a)-1]; print(hex(a),'fn',hex(fn),'mk',inv.get(fn))
