# Build a function-start seed map for Excite Truck's main.dol:
# pointers into .text found in data sections + stack-frame prologues.
from dol import *
import struct,sys
s,o,z,t=[x for x in secs if x[0]==0x800049c0][0]
words=struct.unpack('>%dI'%(z//4),D[o:o+z])
def w(a): return words[(a-s)//4]
BLR=0x4E800020
BCTR=0x4E800420
def ends(q):
    # blr, bctr (tail call through a pointer), b (tail call), rfi, or zero padding
    return q in (BLR,BCTR,0,0x4C000064) or (q>>26)==18
starts=set()
# Switch jump tables: find "lis/addi rT; ... lwzx; mtctr; bctr" and the table bound from
# the "cmplwi rX, N" before it, then exclude those table slots (they point at case labels).
jt=set()
for i,x in enumerate(words):
    if x==0x4E800420:  # bctr
        hi=lo=None; reg=None; n=None
        for k in range(i-1,max(0,i-24),-1):
            q=words[k]; op=q>>26
            if op==14 and lo is None: lo=q&0xFFFF; lo=lo-0x10000 if lo&0x8000 else lo; reg=(q>>21)&31
            elif op==15 and lo is not None and hi is None and ((q>>21)&31)==reg: hi=q&0xFFFF
            elif op==10 and n is None: n=(q&0xFFFF)+1
        if hi is not None and lo is not None and n and n<512:
            t=((hi<<16)+lo)&0xFFFFFFFF
            for j in range(n): jt.add(t+4*j)
for ds,do,dz,dt in secs:
    if dt: continue
    vals=list(struct.unpack('>%dI'%(dz//4),D[do:do+dz//4*4]))
    for i,v in enumerate(vals):
        if s<=v<s+z and v%4==0 and (ds+4*i) not in jt:
            p=w(v-4) if v>s else BLR
            # previous insn ends a function (blr / b / rfi) or padding
            if ends(p): starts.add(v)
for i,x in enumerate(words):
    if (x>>16)==0x9421:
        a=s+4*i
        # walk back over leading non-branch setup insns to the previous blr/b
        b=a
        for _ in range(4):
            q=w(b-4)
            if ends(q): break
            if (q>>26)==16 or (q>>26)==19: break
            b-=4
        if ends(w(b-4)): starts.add(b)
# Every static constructor in .ctors is a function start.
for ds,do,dz,dt in secs:
    if ds==0x802ED060:
        for i in range(dz//4):
            v=struct.unpack('>I',D[do+4*i:do+4*i+4])[0]
            if s<=v<s+z: starts.add(v)
for a in sorted(starts): print('%08x func_%08X'%(a,a))
print(len(starts),'starts;',len(jt),'jump-table slots skipped',file=sys.stderr)
