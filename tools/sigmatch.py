# Name Wii SDK functions in a DOL using Dolphin's totaldb.dsy hash signatures.
import struct, sys
sys.argv=[sys.argv[0]]+sys.argv[1:]
from dol import D, secs
DB=sys.argv[2]; MAP=sys.argv[3]
raw=open(DB,'rb').read()
n=struct.unpack('<I',raw[:4])[0]
db={}
for i in range(n):
    c,sz=struct.unpack('<II',raw[4+i*136:12+i*136]); nm=raw[12+i*136:140+i*136].split(b'\0')[0].decode('latin1')
    db[c]=(nm,sz)
s,o,z,t=[x for x in secs if x[0]==0x800049c0][0]
def word(a): return struct.unpack('>I',D[o+a-s:o+a-s+4])[0]
def h(a,size):
    sum_=0
    for off in range(a,a+size,4):
        opc=word(off); op=opc&0xFC000000; op2=op3=0; aux=op>>26
        if aux==4:
            op2=opc&0x3F
            if op2 in (0,8,16,21,22): op3=opc&0x7C0
        elif aux in (7,8,10,11,12,13,14,15): op2=opc&0x03FF0000
        elif aux in (19,31,63): op2=opc&0x7FF
        elif aux==59:
            op2=opc&0x3F
            if op2<16: op3=opc&0x7C0
        elif 32<=aux<56: op2=opc&0x03FF0000
        sum_=((sum_<<17)&0xFFFE0000)|((sum_>>15)&0x1FFFF); sum_^=(op|op2|op3)
    return sum_
starts=sorted(int(l.split()[0],16) for l in open(MAP))
starts=[a for a in starts if s<=a<s+z]+[s+z]
out={}
for a,b in zip(starts,starts[1:]):
    size=b-a
    while size>4 and word(a+size-4)==0: size-=4
    c=h(a,size)
    if c in db and size>=32 and db[c][1]==size: out[a]=db[c][0]
    else:
        # try size reported by the db for any entry: end at each blr
        pass
for a in sorted(out): print('%08x %s'%(a,out[a]))
print(len(db),'db entries;',len(out),'matched of',len(starts)-1,file=sys.stderr)
