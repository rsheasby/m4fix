"""xref.py DLL VA... - find call/jmp rel32 and absolute references to VAs; prints containing export if known."""
import sys, pefile, struct, bisect
pe = pefile.PE(sys.argv[1]); base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
exps = sorted((base + e.address, e.name.decode()) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name) if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT') else []
ea = [a for a, _ in exps]
def owner(va):
    i = bisect.bisect_right(ea, va) - 1
    return f'{exps[i][1]}+{va - exps[i][0]:#x}' if i >= 0 else '?'
for t in sys.argv[2:]:
    t = int(t, 16)
    print(f'== xrefs to {t:#x}')
    for sec in pe.sections:
        if not sec.Characteristics & 0x20000000: continue
        s, e = sec.VirtualAddress, sec.VirtualAddress + sec.Misc_VirtualSize
        for i in range(s, e - 5):
            op = img[i]
            if op in (0xe8, 0xe9):
                if base + i + 5 + struct.unpack_from('<i', img, i + 1)[0] == t: print(f'  {"call" if op==0xe8 else "jmp "} @ {base+i:#x} in {owner(base+i)}')
    pat = struct.pack('<I', t)
    i = img.find(pat)
    while i != -1:
        print(f'  abs @ {base+i:#x} in {owner(base+i)}'); i = img.find(pat, i + 1)
