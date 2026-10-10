"""Generate tracer/hooks.h: resolve exported functions to real bodies and compute stolen prologue lengths."""
import pefile, capstone, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from paths import GAME_BIN as BIN
# (dll, export name or 'rva:0x...', flags, argfmt)   flags: V=verbose log line, S=stats only
# argfmt: 0 none, 1 = [esp+4] is char*, 2 = [esp+4] is ContextIdentifier*, 3 = [esp+8] size (bytes) , 4 = [esp+4] char* and [esp+8]...
HOOKS = [
 ('m4_thor_rd.dll', '?DoFrame@Engine@thor@@QAEXXZ', 'S', 0),
# ('m4_thor_rd.dll', '?DoFrame@ResourceManager@thor@@QAEXXZ', 'S', 0),
 ('m4_aisdk_rd.dll', '?ContextChange@ai@@YAXKKK@Z', 'V', 4),
 ('m4_aisdk_rd.dll', '?ContextPrefetch@ai@@YAXKKK@Z', 'V', 4),
 ('m4_thor_rd.dll', '?OnRequestContext@NodePrefetchStrategy@prefetch@thor@@AAEXKKK@Z', 'V', 4),
# ('m4_thor_rd.dll', '?AllocDynamicSlice@TextureBox@thor@@QAEXKKK@Z', 'S', 0),
# ('m4_thor_rd.dll', '?FreeDynamicSlice@TextureBox@thor@@QAEXKKK@Z', 'S', 0),
# ('m4_thor_rd.dll', '?SetSliceVisibility@TextureBox@thor@@QAEXKKK_N@Z', 'S', 0),
 ('m4_thor_rd.dll', '?SetState@TextureBox@thor@@QAEXPBD_N@Z', 'S', 1),
 ('m4_thor_rd.dll', '?ContextChange@ResourceManager@thor@@QAEXABVContextIdentifier@2@@Z', 'V', 2),
 ('m4_thor_rd.dll', '?ContextLoad@ResourceManager@thor@@QAEXABVContextIdentifier@2@@Z', 'V', 2),
 ('m4_thor_rd.dll', '?ActivateContext@LoaderManager@thor@@QAEXABVContextIdentifier@2@@Z', 'V', 2),
 ('m4_thor_rd.dll', '?InitContext@LoaderManager@thor@@QAEXABVContextIdentifier@2@@Z', 'V', 2),
 ('m4_thor_rd.dll', '?DeactivateContext@LoaderManager@thor@@QAEXABVContextIdentifier@2@@Z', 'V', 2),
 ('m4_thor_rd.dll', '?DeactivateContext@LoaderManager@thor@@QAEXABVContextIdentifier@2@0@Z', 'V', 2),
 ('m4_thor_rd.dll', '?Load@LoadingRequest@thor@@QAEXPAVContext@2@@Z', 'V', 0),
 ('m4_thor_rd.dll', '?Activate@Context@thor@@UAEXXZ', 'V', 0),
 ('m4_thor_rd.dll', '?Deactivate@Context@thor@@UAEXXZ', 'V', 0),
 ('m4_thor_rd.dll', '?Load@Node@thor@@QAEXPBD@Z', 'V', 1),
 ('m4_thor_rd.dll', '?Activate@Node@thor@@QAEXXZ', 'V', 0),
 ('m4_thor_rd.dll', '?Deactivate@Node@thor@@QAEXXZ', 'V', 0),
 ('m4_thor_rd.dll', '?Load@TextureBox@thor@@UAEPAVResource@2@PAVParsedEntry@ubi@@PAX@Z', 'S', 0),
 ('m4_thor_rd.dll', '?LoadTextures@TextureBox@thor@@QAEXPBDAAV?$Map@USliceAddress@TextureBox@thor@@PAVTextureResource@3@ULessAddress@23@V?$ContainerAllocatorGeneralAlloc@U?$pair@$$CBUSliceAddress@TextureBox@thor@@PAVTextureResource@3@@std@@@ubi@@@ubi@@@Z', 'S', 1),
 ('m4_thor_rd.dll', '?Activate@TextureBox@thor@@UAEXXZ', 'S', 0),
 ('m4_thor_rd.dll', '?AddLoadingRequest@ShareManager@thor@@AAEXPAVLoadingRequest@2@ABVContextIdentifier@2@@Z', 'S', 0),
 ('base_rd.dll', '?Load@Loader@ubi@@SAPAXPBDPAX_N@Z', 'V', 1),
# ('base_rd.dll', '?Load@Loader@ubi@@SAPAXPAVParsedSection@2@PAX@Z', 'S', 0),
 ('base_rd.dll', '?GetFile@Loader@ubi@@SAPAUParsedFile@2@PBD@Z', 'V', 1),
# ('base_rd.dll', '?OpenFile@FileManager@ubi@@QAEPAVFile@2@PBDK@Z', 'S', 1),
# ('base_rd.dll', '?Read@File@ubi@@QAEKPAXK@Z', 'S', 3),
 ('base_rd.dll', '?Wait@ThreadManager@ubi@@SAXJ@Z', 'S', 0),
 ('base_rd.dll', '?Sleep@ThreadManager@ubi@@SAXXZ', 'S', 0),
# ('bigfile_rd.dll', '?OpenFile@DiskDrive_BigFile@ubi@@UAEPAVFile@2@ABVPathObject@2@K@Z', 'S', 0),
# ('bigfile_rd.dll', '?DoRead@File_BigFile@ubi@@MAEKPAXK@Z', 'S', 3),
# ('gfx_tools_rd.dll', '?CreateAndFillLoaderForFile@ImageCreator@gfx_tools@@SAPAVImageLoader@2@PAD@Z', 'S', 0),
# ('gfx_tools_rd.dll', '?CreateLoader@ImageCreator@gfx_tools@@SAPAVImageLoader@2@PAD@Z', 'S', 0),
 ('ltfil13n.dll', 'L_LoadBitmapMemory', 'S', 0),
# ('ltfil13n.dll', 'L_FileInfoMemory', 'S', 0),
# ('ltkrn13n.dll', 'L_GetBitmapRow', 'S', 0),
# ('ltkrn13n.dll', 'L_CreateBitmap', 'S', 0),
# ('ltkrn13n.dll', 'L_SizeBitmap', 'S', 0),
# ('ltkrn13n.dll', 'L_FreeBitmap', 'S', 0),
# ('aresdx_rd.dll', '?Create@Texture@ares@@SAPAV12@W4EnumPixelFormat@gfx_tools@@GGGEW4AccessPermissions@2@PAVResourceUpdater@2@PAK_N@Z', 'S', 0),
]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
REL = {'call', 'jmp', 'loop', 'jecxz'}
out = []
for dll, name, flags, argfmt in HOOKS:
    pe = pefile.PE(os.path.join(BIN, dll)); img = pe.get_memory_mapped_image()
    rva = None
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.name and e.name.decode() == name: rva = e.address
    assert rva is not None, (dll, name)
    # follow jmp rel32 thunks
    while img[rva] == 0xe9:
        rva = rva + 5 + int.from_bytes(img[rva+1:rva+5], 'little', signed=True)
    n = 0; insns = []; rel = 0
    for ins in md.disasm(img[rva:rva+32], rva):
        if ins.bytes[0] in (0xe8, 0xe9) and ins.size == 5:
            assert not rel, 'only one rel32 supported'; rel = n + 1   # offset of rel32 field
        elif ins.mnemonic in REL or ins.mnemonic.startswith('j') or ins.mnemonic.startswith('ret'):
            print(f'SKIP {name}: {ins.mnemonic} in prologue'); n = -1; break
        insns.append(f'{ins.mnemonic} {ins.op_str}'); n += ins.size
        if n >= 5: break
    if n < 0: continue
    out.append((dll, name, rva, n, flags, argfmt, '; '.join(insns), rel))
    print(f'{dll:18s} {rva:#08x} steal={n} rel={rel} {flags} [{"; ".join(insns)}] {name[:70]}')
with open(os.path.join(os.path.dirname(__file__), '..', 'tracer', 'hooks.h'), 'w') as f:
    f.write('/* generated by tools/genhooks.py */\n')
    for dll, name, rva, n, flags, argfmt, ins, rel in out:
        short = name.split('@@')[0].lstrip('?')
        cls = name.split('@')[1] if name.startswith('?') else ''
        label = f'{cls}::{short.split("@")[0]}' if cls else name
        f.write(f'HOOK("{dll}", {rva:#x}, {n}, {rel}, {1 if flags=="V" else 0}, {argfmt}, "{label}")  /* {ins} */\n')
