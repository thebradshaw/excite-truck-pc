# Find Excite Truck functions in the Excitebots DOL by masked instruction patterns.
import struct,sys,re
def load(p):
    D=open(p,'rb').read()
    off=struct.unpack('>18I',D[0:72]);addr=struct.unpack('>18I',D[72:144]);sz=struct.unpack('>18I',D[144:216])
    t=[(addr[i],off[i],sz[i]) for i in range(7) if sz[i]]
    t.sort(key=lambda x:-x[2]); s,o,z=t[0]
    return s,list(struct.unpack('>%dI'%(z//4),D[o:o+z]))
def mask(w):
    op=w>>26
    if op==18: return w&0xFC000003         # b/bl
    if op==16: return w&0xFFFF0003         # bc
    if op in (14,15,24,25): # addi/lis/ori/oris: keep regs, drop imm
        return w&0xFFFF0000
    if 32<=op<=55: # loads/stores: drop displacement
        return w&0xFFFF0000
    return w
ets,etw=load(sys.argv[1]); ebs,ebw=load(sys.argv[2])
ebm=[mask(w) for w in ebw]
idx={}
for i in range(len(ebm)-3):
    idx.setdefault(tuple(ebm[i:i+4]),[]).append(i)
def find(a,n):
    i=(a-ets)//4; pat=[mask(w) for w in etw[i:i+n]]
    res=[]
    for j in idx.get(tuple(pat[:4]),[]):
        if ebm[j:j+n]==pat: res.append(ebs+4*j)
    return res
if __name__=='__main__':
    for l in sys.stdin:
        p=l.split()
        if not p or p[0].startswith('#'): continue
        a=int(p[0],16); n=int(p[1]) if len(p)>1 else 8
        r=find(a,n); print('%08x'%a, ' '.join('%08x'%x for x in r[:5]), '(%d)'%len(r))
