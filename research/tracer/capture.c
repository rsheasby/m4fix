/*
 * capture.c - grab presented frames to C:\Myst 4\frames\NNNNNNms.bmp (quarter size)
 * while capture_active is set. Hooks Direct3DCreate9 via aresdx_rd.dll's IAT,
 * then IDirect3D9::CreateDevice and IDirect3DDevice9::Present via their vtables.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include "../../fix/memfix.h"

volatile int capture_active;
extern long long capture_now_ms(void);

static IDirect3DSurface9 *sysmem;
static UINT sw, sh;
static D3DFORMAT sfmt;
static HRESULT (WINAPI *real_Present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT (WINAPI *real_CreateDevice)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static IDirect3D9 *(WINAPI *real_Direct3DCreate9)(UINT);

static void save_frame(IDirect3DDevice9 *dev)
{
    IDirect3DSurface9 *bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC d; IDirect3DSurface9_GetDesc(bb, &d);
    if (!sysmem || d.Width != sw || d.Height != sh || d.Format != sfmt) {
        if (sysmem) IDirect3DSurface9_Release(sysmem);
        sysmem = NULL;
        if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(dev, d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sysmem, NULL))) { IDirect3DSurface9_Release(bb); return; }
        sw = d.Width; sh = d.Height; sfmt = d.Format;
    }
    if (SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(dev, bb, sysmem))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(IDirect3DSurface9_LockRect(sysmem, &lr, NULL, D3DLOCK_READONLY))) {
            UINT ow = sw / 4, oh = sh / 4, stride = (ow * 3 + 3) & ~3;
            BYTE *px = HeapAlloc(GetProcessHeap(), 0, stride * oh);
            for (UINT y = 0; y < oh; y++)
                for (UINT x = 0; x < ow; x++) {
                    BYTE *s = (BYTE *)lr.pBits + (y * 4) * lr.Pitch + (x * 4) * 4;
                    BYTE *o = px + (oh - 1 - y) * stride + x * 3;   /* BMP is bottom-up */
                    o[0] = s[0]; o[1] = s[1]; o[2] = s[2];
                }
            IDirect3DSurface9_UnlockRect(sysmem);
            char name[MAX_PATH];
            sprintf(name, "C:\\Myst 4\\frames\\%08lldms.bmp", capture_now_ms());
            HANDLE f = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            BITMAPFILEHEADER fh = { 0x4d42, sizeof fh + sizeof(BITMAPINFOHEADER) + stride * oh, 0, 0, sizeof fh + sizeof(BITMAPINFOHEADER) };
            BITMAPINFOHEADER ih = { sizeof ih, ow, oh, 1, 24, BI_RGB, stride * oh };
            DWORD w;
            WriteFile(f, &fh, sizeof fh, &w, NULL); WriteFile(f, &ih, sizeof ih, &w, NULL); WriteFile(f, px, stride * oh, &w, NULL);
            CloseHandle(f);
            HeapFree(GetProcessHeap(), 0, px);
        }
    }
    IDirect3DSurface9_Release(bb);
}

static HRESULT WINAPI my_Present(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    if (capture_active) save_frame(dev);
    return real_Present(dev, a, b, w, r);
}

static void patch_vtbl(void **vtbl, int idx, void *fn, void **orig)
{
    DWORD old;
    if (vtbl[idx] == fn) return;
    *orig = vtbl[idx];
    VirtualProtect(&vtbl[idx], 4, PAGE_READWRITE, &old);
    vtbl[idx] = fn;
    VirtualProtect(&vtbl[idx], 4, old, &old);
}

static HRESULT WINAPI my_CreateDevice(IDirect3D9 *d3d, UINT ad, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr = real_CreateDevice(d3d, ad, t, w, fl, pp, out);
    if (SUCCEEDED(hr) && *out) patch_vtbl(*(void ***)*out, 17, my_Present, (void **)&real_Present);
    return hr;
}

static IDirect3D9 *WINAPI my_Direct3DCreate9(UINT v)
{
    IDirect3D9 *d = real_Direct3DCreate9(v);
    if (d) patch_vtbl(*(void ***)d, 16, my_CreateDevice, (void **)&real_CreateDevice);
    return d;
}

int capture_install(void)
{
    CreateDirectoryA("C:\\Myst 4\\frames", NULL);
    HMODULE a = GetModuleHandleA("aresdx_rd.dll");
    real_Direct3DCreate9 = (void *)GetProcAddress(LoadLibraryA("d3d9.dll"), "Direct3DCreate9");
    return a && memfix_patch_iat(a, "d3d9.dll", "Direct3DCreate9", my_Direct3DCreate9) != NULL;
}
