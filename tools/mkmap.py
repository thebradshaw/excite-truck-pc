# Build a function-start seed map for Excite Truck's main.dol:
# pointers into .text found in data sections + stack-frame prologues.
from dol import *
import struct,sys
s,o,z,t=[x for x in secs if x[0]==0x800049c0][0]
words=struct.unpack('>%dI'%(z//4),D[o:o+z])
def w(a): return words[(a-s)//4]
BLR=0x4E800020
starts=set()
for ds,do,dz,dt in secs:
    if dt: continue
    for i in range(dz//4):
        v=struct.unpack('>I',D[do+4*i:do+4*i+4])[0]
        if s<=v<s+z and v%4==0:
            p=w(v-4) if v>s else BLR
            # previous insn ends a function (blr / b / rfi) or padding
            if p==BLR or (p>>26)==18 or p==0 or p==0x4C000064: starts.add(v)
for i,x in enumerate(words):
    if (x>>16)==0x9421:
        a=s+4*i
        # walk back over leading non-branch setup insns to the previous blr/b
        b=a
        for _ in range(4):
            q=w(b-4)
            if q==BLR or (q>>26)==18 or q==0: break
            if (q>>26)==16 or (q>>26)==19: break
            b-=4
        if w(b-4)==BLR or (w(b-4)>>26)==18 or w(b-4)==0: starts.add(b)
for a in sorted(starts): print('%08x func_%08X'%(a,a))
print(len(starts),file=sys.stderr)
