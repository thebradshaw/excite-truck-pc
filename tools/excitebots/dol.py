import sys
import struct, capstone, sys
import os; D=open(os.environ.get('DOL','Assets/main.dol'),'rb').read()
off=struct.unpack('>18I',D[0:72]);addr=struct.unpack('>18I',D[72:144]);sz=struct.unpack('>18I',D[144:216])
secs=[(addr[i],off[i],sz[i],i<7) for i in range(18) if sz[i]]
def rd(a,n):
    for s,o,z,t in secs:
        if s<=a<s+z: return D[o+a-s:o+a-s+n]
md=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN)
def dis(a,n=40):
    for i in md.disasm(rd(a,n*4),a): print(hex(i.address),i.mnemonic,i.op_str)
