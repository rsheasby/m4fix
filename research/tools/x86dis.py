"""dis.py DLL RVA [count] - disassemble x86 at RVA, annotating imports/exports."""
import sys, pefile, capstone, functools
@functools.lru_cache(None)
def load(path):
    pe = pefile.PE(path)
    names = {}
    for e in getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None).symbols if hasattr(pe,'DIRECTORY_ENTRY_EXPORT') else []:
        if e.name: names.setdefault(pe.OPTIONAL_HEADER.ImageBase + e.address, e.name.decode())
    for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for i in d.imports:
            names[i.address] = d.dll.decode() + '!' + (i.name.decode() if i.name else f'#{i.ordinal}')
    return pe, names
def dis(path, rva, count=80, stop_ret=True):
    pe, names = load(path)
    base = pe.OPTIONAL_HEADER.ImageBase
    data = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out = []
    for n, ins in enumerate(md.disasm(data[rva:rva + count * 16], base + rva)):
        if n >= count: break
        ann = ''
        for op in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            try: v = int(op, 16)
            except ValueError: continue
            if v in names: ann += '  ; ' + names[v]
            elif base <= v < base + len(data) - 4 and 'call' in ins.mnemonic or 'ptr' in ins.op_str:
                if base <= v < base + len(data) - 4:
                    t = int.from_bytes(data[v - base:v - base + 4], 'little')
                    if t in names: ann += '  ; ->' + names[t]
        if ins.address in names: out.append(f'<{names[ins.address]}>')
        out.append(f'{ins.address:08x}: {ins.mnemonic:6s} {ins.op_str}{ann}')
        if stop_ret and ins.mnemonic == 'ret' and n > 2: break
    return '\n'.join(out)
if __name__ == '__main__':
    print(dis(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 200))
