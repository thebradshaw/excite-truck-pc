# Map Mario Kart Wii SDK function addresses to Excite Truck addresses.
# Anchors: SDK functions named in both (MKWii MAP.txt and Dolphin signature matches on ET).
# Between anchors, align the two function-size sequences with dynamic programming.
import sys,re,os,bisect,json
W='/home/claude/work'
def norm(n):
    n=n.split('\t')[0].split('(')[0].strip()
    m=re.match(r'^(__)?(GX|OS|VI|DVD|NAND|WPAD|KPAD|PAD|SC|SI|EXI|IOS|AI|AX|DSP|IPC|MTX|PSMTX|ISFS|VEC|PSVEC|TPL|CARD|DB)::(.+)$',n)
    if m: n=(m.group(1) or '')+m.group(2)+m.group(3)
    return n
mk={}
for l in open(W+'/wiicompiled/projects/mkwii/MAP.txt'):
    p=l.split(None,1)
    if len(p)==2: mk[int(p[0],16)]=norm(p[1])
mka=sorted(a for a,n in mk.items() if not re.search(r'_(switch|caseD_[0-9A-Fa-f]+|default)$',n))
# ET function starts: the translator's metadata (entry points it emitted)
_meta=json.load(open(os.environ.get('ET_META',W+'/wc-et/generated/base_translation_output.json')))
et=sorted({f['entryPoint'] for f in _meta['functions'] if not f.get('interiorToOtherTranslation')})
etn={}
for l in open(W+'/sdk_names.txt'):
    a,n=l.split(' ',1); etn[int(a,16)]=norm(n)
def sizes(lst):
    return [b-a for a,b in zip(lst,lst[1:])]+[0]
# anchors: unique names on both sides
from collections import Counter
cm=Counter(mk.values()); ce=Counter(etn.values())
mkby={v:k for k,v in mk.items() if cm[v]==1}
anch=sorted((mkby[n],a) for a,n in etn.items() if ce[n]==1 and n in mkby and len(n)>3)
# Ambiguous ET names: keep the candidate surrounded by other functions from the same object file.
obj={}
for l in open(W+'/sdk_names.txt'):
    a,rest=l.split(' ',1); obj[int(a,16)]=rest.split('\t')[-1].strip()
etk=sorted(obj)
byname={}
for a,n in etn.items(): byname.setdefault(n,[]).append(a)
for n,cands in byname.items():
    if len(cands)<2 or n not in mkby or len(n)<=3: continue
    def support(a):
        lo=bisect.bisect_left(etk,a-0x600); hi=bisect.bisect_right(etk,a+0x600)
        return sum(1 for x in etk[lo:hi] if x!=a and obj[x]==obj[a])
    sc_=sorted(((support(a),a) for a in cands),reverse=True)
    if sc_[0][0]>=2 and sc_[0][0]>sc_[1][0]: anch.append((mkby[n],sc_[0][1]))
anch=sorted(set(anch))
# build locally consistent runs (libraries may be linked in a different order)
chain=[]
for m,e in anch:
    if chain and 0<e-chain[-1][1]<0x6000 and 0<m-chain[-1][0]<0x6000 and abs((e-chain[-1][1])-(m-chain[-1][0]))<0x1800:
        chain.append((m,e))
    else:
        chain.append(None); chain.append((m,e))
print('anchors',len(anch),'runs',chain.count(None),file=sys.stderr)
mi={a:i for i,a in enumerate(mka)}; ei={a:i for i,a in enumerate(et)}
ms=sizes(mka); es=sizes(et)
mapping={}
for p0,p1 in zip(chain,chain[1:]):
    if p0 is None: continue
    if p1 is None:
        mapping[p0[0]]=(p0[1],True); continue
    (m0,e0),(m1,e1)=p0,p1
    if m0 not in mi or e0 not in ei: continue
    A=list(range(mi[m0],mi[m1])); B=list(range(ei[e0],ei[e1]))
    if len(A)>400 or len(B)>400 or abs(len(A)-len(B))>60: 
        mapping[m0]=e0; continue
    n,mm=len(A),len(B)
    # DP: score match by size similarity
    def sc(i,j):
        a,b=ms[A[i]],es[B[j]]
        if a==b: return 3
        if abs(a-b)<=max(8,0.1*max(a,b)): return 1
        return -2
    G=-1
    D=[[0]*(mm+1) for _ in range(n+1)]; T=[[0]*(mm+1) for _ in range(n+1)]
    for i in range(1,n+1): D[i][0]=i*G; T[i][0]=1
    for j in range(1,mm+1): D[0][j]=j*G; T[0][j]=2
    for i in range(1,n+1):
        for j in range(1,mm+1):
            o=[D[i-1][j-1]+sc(i-1,j-1),D[i-1][j]+G,D[i][j-1]+G]
            t=max(range(3),key=lambda x:o[x]); D[i][j]=o[t]; T[i][j]=t
    i,j=n,mm
    while i>0 or j>0:
        t=T[i][j] if i>0 and j>0 else (1 if i>0 else 2)
        if t==0:
            if sc(i-1,j-1)>0: mapping[mka[A[i-1]]]=(et[B[j-1]],ms[A[i-1]]==es[B[j-1]])
            i-=1;j-=1
        elif t==1: i-=1
        else: j-=1
    mapping[m0]=(e0,True)
# Extend past the end of each anchored run: align the next 40 functions on each
# side with free end gaps, keep pairs while the alignment stays strong.
def extend(m0,e0,W=40):
    if m0 not in mi or e0 not in ei: return
    A=list(range(mi[m0]+1,min(len(mka),mi[m0]+1+W))); B=list(range(ei[e0]+1,min(len(et),ei[e0]+1+W)))
    n,mm=len(A),len(B)
    def sc(i,j):
        a,b=ms[A[i]],es[B[j]]
        if a==b: return 3
        if abs(a-b)<=max(8,0.1*max(a,b)): return 1
        return -3
    G=-2
    D=[[0]*(mm+1) for _ in range(n+1)]; T=[[0]*(mm+1) for _ in range(n+1)]
    for i in range(1,n+1): D[i][0]=i*G; T[i][0]=1
    for j in range(1,mm+1): D[0][j]=j*G; T[0][j]=2
    best=(0,0,0)
    for i in range(1,n+1):
        for j in range(1,mm+1):
            o=[D[i-1][j-1]+sc(i-1,j-1),D[i-1][j]+G,D[i][j-1]+G]
            t=max(range(3),key=lambda x:o[x]); D[i][j]=o[t]; T[i][j]=t
            if D[i][j]>best[0]: best=(D[i][j],i,j)
    _,i,j=best
    while i>0 and j>0:
        t=T[i][j]
        if t==0:
            if sc(i-1,j-1)>0: mapping.setdefault(mka[A[i-1]],(et[B[j-1]],ms[A[i-1]]==es[B[j-1]]))
            i-=1;j-=1
        elif t==1: i-=1
        else: j-=1
for p0,p1 in zip(chain,chain[1:]+[None]):
    if p0 is not None and p1 is None:
        last=max((k for k in mapping if k>=p0[0] and k<p0[0]+0x6000), default=p0[0])
        v=mapping.get(last,(p0[1],True)); extend(last, v[0] if isinstance(v,tuple) else v)
# Keep extending from any mapped function whose MK successor is still unmapped.
for _ in range(6):
    before=len(mapping)
    for k in sorted(mapping):
        if k in mi and mi[k]+1<len(mka) and mka[mi[k]+1] not in mapping:
            v=mapping[k]; extend(k, v[0] if isinstance(v,tuple) else v, W=30)
    if len(mapping)==before: break
# Gap fill: between two mapped MK neighbours, if both sides have the same number of
# functions in the gap, pair them in order (sizes may differ between SDK versions).
mm=sorted((k,v[0] if isinstance(v,tuple) else v) for k,v in mapping.items())
for (ma,ea),(mb,eb) in zip(mm,mm[1:]):
    if ma not in mi or mb not in mi or ea not in ei or eb not in ei: continue
    ga=mka[mi[ma]+1:mi[mb]]; gb=et[ei[ea]+1:ei[eb]]
    if 0<len(ga)==len(gb)<=6 and 0<eb-ea<0x2000:
        for x,y in zip(ga,gb): mapping.setdefault(x,(y,False))
# Drop low-confidence pairs unless hand-checked; drop ET targets claimed twice.
KEEP_INEXACT={0x80172e00,0x80172eb4,0x801a7424,0x801269bc,0x8019e18c,0x8019c048}
for k in list(mapping):
    v=mapping[k]
    if isinstance(v,tuple) and not v[1] and k not in KEEP_INEXACT: del mapping[k]
from collections import Counter as _C
cnt=_C((v[0] if isinstance(v,tuple) else v) for v in mapping.values())
for k in list(mapping):
    v=mapping[k]; e=v[0] if isinstance(v,tuple) else v
    if cnt[e]>1: del mapping[k]
# Order check: a pair must sit between its mapped neighbours (same library, same order).
def _e(v): return v[0] if isinstance(v,tuple) else v
for _ in range(3):
    ks=sorted(mapping); bad=[]
    for i,k in enumerate(ks):
        e=_e(mapping[k])
        lo=ks[i-1] if i>0 else None; hi=ks[i+1] if i+1<len(ks) else None
        ok=0;chk=0
        if lo is not None and k-lo<0x400: chk+=1; ok+= _e(mapping[lo])<e and e-_e(mapping[lo])<0x2000
        if hi is not None and hi-k<0x400: chk+=1; ok+= e<_e(mapping[hi]) and _e(mapping[hi])-e<0x2000
        if chk==2 and ok==0: bad.append(k)
    for k in bad: del mapping[k]
    print('order check dropped',len(bad),file=sys.stderr)
for l in open(os.path.dirname(os.path.abspath(__file__))+'/manual_map.txt'):
    if l.startswith('#') or not l.strip(): continue
    a,b,_=l.split(None,2); a=int(a,16)
    if b=='none': mapping.pop(a,None)
    else: mapping[a]=(int(b,16),True)
json.dump({hex(k):v for k,v in mapping.items()},open(W+'/mk2et.json','w'))
ov=[l.split() for l in open(W+'/mk_overrides.txt')]
hit=exact=0;miss=[]
for a,n in ov:
    a=int(a,16)
    if a in mapping: hit+=1; exact+=mapping[a][1] if isinstance(mapping[a],tuple) else 1
    else: miss.append((hex(a),mk.get(a,'?'),n))
print('overrides',len(ov),'mapped',hit,'exact-size',exact,file=sys.stderr)
for m in miss: print('MISS',*m)
