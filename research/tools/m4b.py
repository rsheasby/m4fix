"""Parser for Ubisoft UBI_BF_SIG bigfiles (.m4b) used by Myst IV."""
import struct, sys

class Reader:
    def __init__(s, buf, pos=0): s.b, s.p = buf, pos
    def u8(s): v = s.b[s.p]; s.p += 1; return v
    def u32(s): v = struct.unpack_from('<I', s.b, s.p)[0]; s.p += 4; return v
    def str(s):
        n = s.u32(); v = s.b[s.p:s.p+n].rstrip(b'\0').decode('latin1'); s.p += n; return v

def parse_fat(buf):
    """Returns (entries, fat_end). entries = list of (path, size, offset)."""
    r = Reader(buf)
    assert r.str() == 'UBI_BF_SIG'
    hdr = (r.u32(), r.u32())
    out = []
    def walk(prefix):
        nd = r.u8()
        for _ in range(nd):
            name = r.str()
            walk(prefix + name + '/')
        nf = r.u32()
        for _ in range(nf):
            name = r.str(); size = r.u32(); off = r.u32()
            out.append((prefix + name, size, off))
    walk('')
    return out, r.p, hdr

if __name__ == '__main__':
    path = sys.argv[1]
    with open(path, 'rb') as f: head = f.read(64 << 20)
    ents, end, hdr = parse_fat(head)
    print(f'# hdr={hdr} fat_end={end} entries={len(ents)}')
    for p, s, o in ents: print(f'{o:10d} {s:10d} {p}')
