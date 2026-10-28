# PowerPC helper for the Excite Truck DOL: 'd ADDR N' disassembles, 'x ADDR' finds lis/addi references,
# 'c ADDR...' lists bl callers. Set ET_DOL to main.dol.
import os,struct,sys,capstone
d=open(os.environ.get('ET_DOL','/home/claude/work/extracted/sys/main.dol'),'rb').read()
offs=struct.unpack('>18I',d[0:72]);addrs=struct.unpack('>18I',d[72:144]);sizes=struct.unpack('>18I',d[144:216])
segs=[(a,of,s) for of,a,s in zip(offs,addrs,sizes) if s]
def rd(a,n):
    for sa,of,s in segs:
        if sa<=a<sa+s: return d[of+a-sa:of+a-sa+n]
md=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN); md.skipdata=True
def dis(a,n):
    for i in md.disasm(rd(a,n*4),a): print(f"{i.address:08X}  {i.mnemonic} {i.op_str}")
def xref(target):
    hi=(target+0x8000)>>16; lo=target&0xffff
    for sa,of,s in segs[:7]:
        code=d[of:of+s]
        for k in range(0,s,4):
            w=struct.unpack('>I',code[k:k+4])[0]
            if (w>>26)==14 and (w&0xffff)==lo:
                # look back for lis hi
                for b in range(1,12):
                    if k-4*b<0: break
                    w2=struct.unpack('>I',code[k-4*b:k-4*b+4])[0]
                    if (w2>>26)==15 and (w2&0xffff)==hi: print(hex(sa+k)); break
if __name__=='__main__':
    if sys.argv[1]=='x': xref(int(sys.argv[2],16))
    elif sys.argv[1]=='d': dis(int(sys.argv[2],16),int(sys.argv[3]))
def calls(target):
    out=[]
    for sa,of,s in segs[:7]:
        for k in range(0,s,4):
            w=struct.unpack('>I',d[of+k:of+k+4])[0]
            if (w>>26)==18 and (w&3)==1:
                off=w&0x3fffffc
                if off&0x2000000: off-=0x4000000
                if sa+k+off==target: out.append(sa+k)
    return out
if __name__=='__main__' and sys.argv[1]=='c':
    for t in sys.argv[2:]: print(t,[hex(a) for a in calls(int(t,16))])
