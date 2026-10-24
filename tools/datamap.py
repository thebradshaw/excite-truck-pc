# Find Excite Truck addresses for the data globals the runtime names by MKWii PAL address.
# Uses WiiCompiled's NTSC-U evidence (data_addresses.txt): each cites NTSC instructions that touch
# the global. NTSC address -> PAL address (NtscU ranges, inverted) -> ET address (ExciteTruck ranges),
# then the ET instruction at that spot is decoded to get the global's ET address. Votes decide.
import re,sys,struct,collections
sys.argv=[sys.argv[0],'/home/claude/work/extracted/sys/main.dol']
from dol import rd,md
VERS='/home/claude/work/wc-et/runtime/include/guest_abi/guest_versions.inc'
EV='/home/claude/work/wiicompiled/projects/mkwii-ntsc-u/data_addresses.txt'
R={'NtscU':[],'ExciteTruck':[]}
for l in open(VERS):
    m=re.match(r'MKW_REGION_RANGE\((\w+), (\w+), (\w+), ([+-])0x(\w+)\)',l)
    if m and m[1] in R: R[m[1]].append((int(m[2],16),int(m[3],16),(-1 if m[4]=='-' else 1)*int(m[5],16)))
def ntsc_to_pal(a):
    for f,l,d in R['NtscU']:
        if f+d<=a<=l+d: return a-d
def pal_to_et(a):
    for f,l,d in R['ExciteTruck']:
        if f<=a<=l: return a+d
NTSC_R13,NTSC_R2=0x80388880,0x8038AC20; ET_R13,ET_R2=0x80560900,0x80568100
def s16(x): return x-0x10000 if x&0x8000 else x
def ea_text(ins):
    m=re.search(r'(-?0x[0-9a-f]+|-?\d+)\((r13|r2)\)',ins)
    if m: return (NTSC_R13 if m[2]=='r13' else NTSC_R2)+int(m[1],0)
    return None
def et_ea(addr):
    raw=rd(addr,4)
    if raw is None: return None
    w=struct.unpack('>I',raw)[0]; op=w>>26; ra=(w>>16)&31; d=s16(w&0xFFFF)
    if op in (14,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55):
        if ra==13: return ET_R13+d,'sda'
        if ra==2: return ET_R2+d,'sda'
        # look back for lis ra,hi
        for k in range(1,16):
            q=struct.unpack('>I',rd(addr-4*k,4))[0]
            if q>>26==15 and ((q>>21)&31)==ra and ((q>>16)&31)==0: return ((((q&0xFFFF)<<16)+d)&0xFFFFFFFF),'lis'
    return None
def ntsc_lis_ea(ins,target):
    m=re.match(r'\s*(addi|lwz|stw|lbz|stb|lhz|sth|lfs|stfs|lfd|stfd|lha|lwzu|stwu)\s+r?\d+,\s*(r\d+),\s*(-?0x[0-9a-f]+|-?\d+)',ins) or re.match(r'\s*\w+\s+r?\d+,\s*(-?0x[0-9a-f]+|-?\d+)\((r\d+)\)',ins)
    if not m: return None
    g=m.groups()
    d=int(g[2],0) if len(g)==3 else int(g[0],0)
    # choose hi so EA is nearest the target
    hi=((target-d+0x8000)>>16)&0xFFFF
    return ((hi<<16)+d)&0xFFFFFFFF
out={}
for l in open(EV):
    if not re.match(r'^[0-9A-Fa-f]{8} [0-9A-Fa-f]{8} ',l): continue
    pal,ntsc=int(l[:8],16),int(l[9:17],16)
    votes=collections.Counter()
    for m in re.finditer(r'@([0-9a-fA-F]{8}) ([a-z][a-z0-9.]*\s+[^;()#]*(?:\([^)]*\))?)',l[18:]):
        na=int(m[1],16); ins=m[2]
        if ins.startswith('lis'): continue
        pa=ntsc_to_pal(na); ea=pal_to_et(pa) if pa else None
        if not ea: continue
        r=et_ea(ea)
        if not r: continue
        a_et,kind=r
        a_ntsc=ea_text(ins) if kind=='sda' else ntsc_lis_ea(ins,ntsc)
        if a_ntsc is None: continue
        k=a_ntsc-ntsc
        if abs(k)>0x10000: continue
        votes[a_et-k]+=1
    if votes:
        (best,n),=votes.most_common(1); total=sum(votes.values())
        out[pal]=(best,n,total)
for pal,(e,n,t) in sorted(out.items()):
    print('%08X %08X %d/%d'%(pal,e,n,t))
print(len(out),'globals mapped',file=sys.stderr)
