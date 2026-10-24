# Show the neighbourhood of an unmapped MKWii function and the matching ET gap.
import json,os,sys,bisect
from dol import *
m=json.load(open(os.environ.get('ET_WORK','work')+'/mk2et.json'))
mk=[]
for l in open(os.environ.get('ET_WORK','work')+'/wiicompiled/projects/mkwii/MAP.txt'):
    p=l.split(None,1)
    if len(p)==2: mk.append((int(p[0],16),p[1].strip()))
mk.sort(); mka=[a for a,_ in mk]
et=sorted(int(f[5:13],16) for f in os.listdir(os.environ.get('ET_WORK','work')+'/wiicompiled/build/excite-truck/functions'))
for t in sys.argv[2:]:
    t=int(t,16); i=mka.index(t)
    print('#####',mk[i][1])
    lo=hi=None
    for j in range(i-6,i+7):
        a,n=mk[j]; v=m.get(hex(a)); print('  MK',hex(a),mka[j+1]-a,n[:40],hex(v[0]) if v else '-')
        if v and j<i: lo=v[0]
        if v and j>i and hi is None: hi=v[0]
    if lo and hi:
        k=et.index(lo)
        while et[k]<=hi:
            a=et[k]; ins=list(md.disasm(rd(a,12),a)); print('  ET',hex(a),et[k+1]-a,'; '.join(x.mnemonic+' '+x.op_str for x in ins)); k+=1
