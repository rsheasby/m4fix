"""callsites.py DLL importname_substring [ctx] - list call sites of an imported function with preceding instructions."""
import sys, pefile, struct, capstone, bisect
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from x86dis import load
path, sub = sys.argv[1], sys.argv[2]; ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 6
pe, names = load(path); base = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
exps = sorted((base + e.address, e.name.decode()) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name) if hasattr(pe,'DIRECTORY_ENTRY_EXPORT') else []
ea = [a for a, _ in exps]
def owner(va):
    i = bisect.bisect_right(ea, va) - 1
    return f'{exps[i][1][:90]}+{va-exps[i][0]:#x}' if i >= 0 else '?'
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
iat = [a for a, n in names.items() if sub in n and '!' in n]
targets = set(iat)
# also jmp-thunks to these IAT slots
for a in iat:
    p = b'\xff\x25' + struct.pack('<I', a); j = img.find(p)
    while j != -1: targets.add(('thunk', base + j)); j = img.find(p, j + 1)
for t in targets:
    if isinstance(t, tuple):
        va = t[1]
        for i in range(0x1000, len(img) - 5):
            if img[i] == 0xe8 and base + i + 5 + struct.unpack_from('<i', img, i + 1)[0] == va:
                print(f'--- call thunk {names.get(iat[0])} @ {base+i:#x} in {owner(base+i)}')
    else:
        p = b'\xff\x15' + struct.pack('<I', t); j = img.find(p)
        while j != -1:
            print(f'--- call [{names[t]}] @ {base+j:#x} in {owner(base+j)}')
            # show ctx instrs before
            st = max(0, j - 40); ins = list(md.disasm(img[st:j + 6], base + st))
            for x in ins[-ctx:]: print(f'    {x.address:08x}: {x.mnemonic} {x.op_str}')
            j = img.find(p, j + 1)
