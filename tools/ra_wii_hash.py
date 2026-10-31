#!/usr/bin/env python3
# Usage: tools/ra_wii_hash.py <disc.iso>  (prints the RetroAchievements hash; ISO only, not WBFS/RVZ)
# Re-implementation of rcheevos' Wii disc hash (rc_hash_wii_disc, encrypted path) for one ISO.
import hashlib, struct, sys
f = open(sys.argv[1], 'rb')
def rd(off, n): f.seek(off); return f.read(n)
u32 = lambda b: struct.unpack('>I', b)[0]
md5 = hashlib.md5()
print('encrypted', rd(0x61,1)[0] == 0, 'id', rd(0,6))
md5.update(rd(0, 0x80))
md5.update(rd(0x4E000, 4))
info = [u32(rd(0x40000 + 4*i, 4)) for i in range(8)]
parts = []
for j in range(0, 8, 2):
    base = info[j+1] << 2
    for i in range(info[j]):
        parts.append((u32(rd(base + 8*i, 4)), u32(rd(base + 8*i + 4, 4))))
print('partitions', [(hex(o<<2), t) for o, t in parts])
for off, typ in parts:
    if typ == 1: continue
    p = off << 2
    tmd_size = min(u32(rd(p + 0x2A4, 4)), 0x7C00)
    tmd_off = u32(rd(p + 0x2A8, 4)) << 2
    md5.update(rd(p + tmd_off, tmd_size))
    part_off = u32(rd(p + 0x2B8, 4)) << 2
    part_size = u32(rd(p + 0x2BC, 4)) << 2
    count = min(part_size // 0x8000, 1024)
    for ix in range(count):
        md5.update(rd(part_off + ix*0x8000 + 0x400, 0x7C00))
print(md5.hexdigest())
