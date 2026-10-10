"""profile.py samples.bin - symbolize sampler output (EIP self time + inclusive via validated return addresses)."""
import sys, struct, os, bisect, collections, pefile, functools
from paths import CX, GAME_BIN
SEARCH = [GAME_BIN, os.path.join(os.path.dirname(__file__), '..', 'tracer'),
          CX + '/lib/wine/i386-windows', CX + '/lib/wine/dxvk']
NSTK = 48
b = open(sys.argv[1], 'rb').read(); p = 0
nm = struct.unpack_from('<I', b, p)[0]; p += 4
mods = []
for i in range(nm):
    lo, hi = struct.unpack_from('<II', b, p); name = b[p+8:p+72].split(b'\0')[0].decode(); p += 72
    mods.append((lo, hi, name))
mods.sort(); los = [m[0] for m in mods]
n = struct.unpack_from('<I', b, p)[0]; p += 4
@functools.lru_cache(None)
def pe_for(name):
    for d in SEARCH:
        for f in (name, name.lower()):
            fp = os.path.join(d, f)
            if os.path.exists(fp):
                pe = pefile.PE(fp); img = pe.get_memory_mapped_image()
                ex = sorted((e.address, e.name.decode() if e.name else f'#{e.ordinal}') for e in pe.DIRECTORY_ENTRY_EXPORT.symbols) if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT') else []
                return img, ex, [a for a, _ in ex]
    return None
def sym(a):
    i = bisect.bisect_right(los, a) - 1
    if i < 0 or a >= mods[i][1]: return None, None
    lo, hi, name = mods[i]; rva = a - lo
    info = pe_for(name)
    if not info or not info[1]: return name, f'{name}+{rva:#x}'
    img, ex, ea = info
    j = bisect.bisect_right(ea, rva) - 1
    if j < 0: return name, f'{name}+{rva:#x}'
    off = rva - ex[j][0]
    return name, f'{name}!{ex[j][1][:110]}+{off:#x}' if off < 0x4000 else f'{name}+{rva:#x}'
def is_ret(a):
    i = bisect.bisect_right(los, a) - 1
    if i < 0 or a >= mods[i][1]: return False
    info = pe_for(mods[i][2]); rva = a - mods[i][0]
    if not info: return True
    img = info[0]
    if rva < 6 or rva > len(img): return False
    return img[rva-5] == 0xe8 or (img[rva-6] == 0xff and img[rva-5] in (0x15, 0x90, 0x91, 0x92, 0x93, 0x95, 0x96, 0x97)) \
        or (img[rva-2] == 0xff and 0xd0 <= img[rva-1] <= 0xd7) or (img[rva-3] == 0xff and 0x50 <= img[rva-2] <= 0x57) \
        or (img[rva-7] == 0xff and img[rva-6] == 0x94)
selfc = collections.Counter(); incl = collections.Counter(); modc = collections.Counter(); stacks = collections.Counter()
for k in range(n):
    vals = struct.unpack_from(f'<{1+NSTK}I', b, p + k * 4 * (1 + NSTK))
    eip, stk = vals[0], [v for v in vals[1:] if v and is_ret(v)]
    m, s = sym(eip); selfc[s or f'{eip:#x}'] += 1; modc[m or '?'] += 1
    seen = set()
    chain = [s or hex(eip)] + [sym(v)[1] for v in stk]
    for c in chain:
        if c and c not in seen: incl[c] += 1; seen.add(c)
    stacks[' <- '.join(x.split('!')[-1][:60] for x in chain[:7] if x)] += 1
pct = lambda c: f'{100*c/n:5.1f}%'
print(f'{n} samples (~{n}ms in long frames)\n== by module (self)')
for k, c in modc.most_common(15): print(f'  {pct(c)} {k}')
print('== self'); [print(f'  {pct(c)} {k}') for k, c in selfc.most_common(30)]
print('== inclusive'); [print(f'  {pct(c)} {k}') for k, c in incl.most_common(60)]
if '-s' in sys.argv:
    print('== stacks'); [print(f'  {pct(c)} {k}') for k, c in stacks.most_common(25)]
if '-c' in sys.argv:
    key = sys.argv[sys.argv.index('-c') + 1]
    print(f'== stacks containing {key!r}')
    cc = collections.Counter(); tot = 0
    for k in range(n):
        vals = struct.unpack_from(f'<{1+NSTK}I', b, p + k * 4 * (1 + NSTK))
        chain = [sym(vals[0])[1] or hex(vals[0])] + [sym(v)[1] for v in vals[1:] if v and is_ret(v)]
        idx = [i for i, c in enumerate(chain) if c and key in c]
        if not idx: continue
        tot += 1
        i = idx[0]
        cc[' <- '.join((c or '?').split('!')[-1][:70] for c in chain[max(0, i-2):i+8])] += 1
    print(f'  {tot} samples ({100*tot/n:.1f}%)')
    for k, c in cc.most_common(12): print(f'  {100*c/n:5.1f}% {k}')
