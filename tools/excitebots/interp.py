# For MKW functions the aligner left unmapped: if the mapped neighbours on both sides keep the
# same distance in Excitebots (same SDK build, same object layout), the function sits at the
# same offset. Only accepted when the target is a known function start.
import os
EB_WORK=os.environ.get('EB_WORK','work/excitebots')   # scratch: sdk_names.txt, mk_overrides.txt, outputs
WC=os.environ.get('WC','work/wiicompiled')                # pristine WiiCompiled checkout (MKWii MAP.txt)
WS=os.environ.get('WS','work/wc-eb')                      # patched workspace with the Excitebots translation
import json,re,bisect,sys
mk={}
for l in open(WC+'/projects/mkwii/MAP.txt'):
    p=l.split(None,1)
    if len(p)==2: mk[int(p[0],16)]=p[1].strip()
mka=sorted(a for a,n in mk.items() if not re.search(r'_(switch|caseD_[0-9A-Fa-f]+|default)$',n))
m=json.load(open(EB_WORK+'/mk2eb.json')); mp={int(k,16):(v[0] if isinstance(v,list) else v) for k,v in m.items()}
meta=json.load(open(WS+'/generated/base_translation_output.json'))
ebf={f['entryPoint'] for f in meta['functions']}
ks=sorted(mp)
want=[int(l.split()[0],16) for l in open(EB_WORK+'/mk_overrides.txt')] if len(sys.argv)<2 else mka
add={}
for a in want:
    if a in mp or a>=0x8E000000: continue
    i=bisect.bisect_left(ks,a)
    if i==0 or i>=len(ks): continue
    lo,hi=ks[i-1],ks[i]
    if hi-lo>0x3000: continue
    if mp[hi]-mp[lo]!=hi-lo: continue
    t=mp[lo]+(a-lo)
    if a in mk and a in mka and t in ebf: add[a]=t
for a,t in sorted(add.items()): print('%08x %08x %s'%(a,t,mk[a]))
print(len(add),file=sys.stderr)
