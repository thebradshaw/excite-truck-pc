# findref.py <hexaddr>...: data words equal to addr, and code lis/addi(ori) pairs building it
import struct,sys
from dol import D,secs
def find_bytes(b):
    out=[]
    for s,o,z,t in secs:
        i=D.find(b,o,o+z)
        while i!=-1: out.append(s+i-o); i=D.find(b,i+1,o+z)
    return out
def words():
    for s,o,z,t in secs:
        for i in range(0,z-3,4): yield s+i,struct.unpack('>I',D[o+i:o+i+4])[0],t
def refs(a):
    hi=(a+0x8000)>>16&0xFFFF; lo=a&0xFFFF
    dat=[];code=[]
    lis={}
    for addr,w,t in words():
        if not t:
            if w==a: dat.append(addr)
            continue
        op=w>>26
        if op==15 and ((w>>16)&31)==0 and (w&0xFFFF)==hi: lis[(w>>21)&31]=addr
        elif op in (14,24) or 32<=op<=55:
            ra=(w>>16)&31
            if ra in lis and addr-lis[ra]<0x80 and (w&0xFFFF)==(lo if op!=24 else a&0xFFFF): code.append(addr)
    return dat,code
if __name__=='__main__':
    for x in sys.argv[1:]:
        if x.startswith('s:'):
            print(x,['%08x'%v for v in find_bytes(x[2:].encode()+b'\0')]); continue
        d,c=refs(int(x,16)); print(x,'data',['%08x'%v for v in d],'code',['%08x'%v for v in c])
