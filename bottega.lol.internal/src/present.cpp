#include "present.h"
#include "render.h"

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <excpt.h>
#include <windows.h>

#include <cstdint>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace nchams {
namespace {

using PresentFn  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT,
                                               const DXGI_PRESENT_PARAMETERS*);

PresentFn  oPresent  = nullptr;
Present1Fn oPresent1 = nullptr;

// Per-class originals: the game may present through a swapchain class that is
// NOT the one our dummy swapchain produced. Every distinct vtable we hook gets
// its own stored original so forwarding is always exact.
struct ClassHook {
    void**    vtbl;
    PresentFn orig8;
    Present1Fn orig22;
};
ClassHook g_classes[16];
size_t    g_class_count = 0;

bool TryRead(uintptr_t addr, void* dst, size_t n) {
    __try {
        memcpy(dst, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    try {
        OnPresent(sc);
    } catch (...) {}
    return oPresent(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE hkPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                     const DXGI_PRESENT_PARAMETERS* pp) {
    try {
        OnPresent(static_cast<IDXGISwapChain*>(sc));
    } catch (...) {}
    return oPresent1(sc, sync, flags, pp);
}

PresentFn Lookup8(void* sc) {
    void** vtbl = *reinterpret_cast<void***>(sc);
    for (size_t i = 0; i < g_class_count; ++i)
        if (g_classes[i].vtbl == vtbl) return g_classes[i].orig8;
    return oPresent;
}

Present1Fn Lookup22(void* sc) {
    void** vtbl = *reinterpret_cast<void***>(sc);
    for (size_t i = 0; i < g_class_count; ++i)
        if (g_classes[i].vtbl == vtbl) return g_classes[i].orig22;
    return oPresent1;
}

HRESULT STDMETHODCALLTYPE hkPresentD(IDXGISwapChain* sc, UINT sync, UINT flags) {
    try {
        OnPresent(sc);
    } catch (...) {}
    PresentFn orig = Lookup8(sc);
    return orig ? orig(sc, sync, flags) : DXGI_STATUS_OCCLUDED;
}

HRESULT STDMETHODCALLTYPE hkPresent1D(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                      const DXGI_PRESENT_PARAMETERS* pp) {
    try {
        OnPresent(static_cast<IDXGISwapChain*>(sc));
    } catch (...) {}
    Present1Fn orig = Lookup22(sc);
    return orig ? orig(sc, sync, flags, pp) : DXGI_STATUS_OCCLUDED;
}

void PatchSlot(void** v, int index, void* target) {
    DWORD old = 0;
    if (!VirtualProtect(v + index, sizeof(void*), PAGE_READWRITE, &old)) return;
    v[index] = target;
    VirtualProtect(v + index, sizeof(void*), old, &old);
}

// Image base + size for the module containing `addr`.
bool ImageRange(uintptr_t addr, uintptr_t& base, uintptr_t& size) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi))) return false;
    const uintptr_t ib = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
    if (!ib) return false;
    const IMAGE_DOS_HEADER* dh = reinterpret_cast<const IMAGE_DOS_HEADER*>(ib);
    if (!dh || dh->e_magic != IMAGE_DOS_SIGNATURE || dh->e_lfanew <= 0) return false;
    const IMAGE_NT_HEADERS* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(ib + dh->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.SizeOfImage) return false;
    base = ib;
    size = nt->OptionalHeader.SizeOfImage;
    return true;
}

struct ModuleRange { uintptr_t lo, hi; };
ModuleRange g_modules[8];
size_t      g_module_count = 0;

void AddModule(const wchar_t* name) {
    if (g_module_count >= 8) return;
    HMODULE h = GetModuleHandleW(name);
    if (!h) h = LoadLibraryW(name);
    if (!h) return;
    uintptr_t lo = 0, size = 0;
    if (!ImageRange(reinterpret_cast<uintptr_t>(h), lo, size)) return;
    g_modules[g_module_count++] = { lo, lo + size };
}

bool InSwapchainRanges(uintptr_t a) {
    for (size_t i = 0; i < g_module_count; ++i)
        if (a >= g_modules[i].lo && a < g_modules[i].hi) return true;
    return false;
}

void RegisterOrPatch(void** vt) {
    const uintptr_t pa = reinterpret_cast<uintptr_t>(vt[8]);
    const uintptr_t pb = vt[22] ? reinterpret_cast<uintptr_t>(vt[22]) : 0;

    if (pa == reinterpret_cast<uintptr_t>(&hkPresent) ||
        pa == reinterpret_cast<uintptr_t>(&hkPresentD))
        return; // already hooked

    if (reinterpret_cast<PresentFn>(pa) == oPresent) {
        // Same class as the dummy; forward through the shared original.
        PatchSlot(vt, 8, reinterpret_cast<void*>(&hkPresent));
        if (oPresent1 && pb == reinterpret_cast<uintptr_t>(oPresent1))
            PatchSlot(vt, 22, reinterpret_cast<void*>(&hkPresent1));
        return;
    }

    if (g_class_count >= 16) return;

    // Distinct class: save this class's originals, then patch its slots.
    PresentFn  a8  = reinterpret_cast<PresentFn>(pa);
    Present1Fn a22 = (pb && oPresent1) ? reinterpret_cast<Present1Fn>(pb) : nullptr;
    g_classes[g_class_count++] = { vt, a8, a22 };

    PatchSlot(vt, 8, reinterpret_cast<void*>(&hkPresentD));
    if (a22)
        PatchSlot(vt, 22, reinterpret_cast<void*>(&hkPresent1D));
}

// Scan committed/readable memory for IDXGISwapChain-family vtables (their
// Present/GetBuffer/ResizeBuffers slots all point inside a loaded DXGI module)
// and hook every class the game might present through.
void ScanAndHook() {
    AddModule(L"dxgi.dll");
    AddModule(L"dxgiwarp.dll");
    if (g_module_count == 0) return;

    uintptr_t addr = 0x10000;
    size_t hits = 0;
    while (addr < 0x00007FFFFFFFFFFFULL && hits < 256) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi))) break;
        const uintptr_t rbase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t rend  = rbase + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type != MEM_MAPPED &&
            mbi.Protect != PAGE_NOACCESS && !(mbi.Protect & PAGE_GUARD) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_WRITECOPY))) {
            const uintptr_t start = (rbase + 7) & ~7ull;
            for (uintptr_t p = start; p + 26 * sizeof(void*) <= rend; p += sizeof(void*)) {
                uintptr_t vals[26];
                if (!TryRead(p, vals, sizeof(vals))) break;
                if (!vals[8] || !vals[9] || !vals[13]) continue;
                if (vals[8] == vals[9] || vals[9] == vals[13]) continue;
                if (!InSwapchainRanges(vals[8])) continue;
                if (!InSwapchainRanges(vals[9]) || !InSwapchainRanges(vals[13])) continue;
                RegisterOrPatch(reinterpret_cast<void**>(p));
                if (++hits >= 256) break;
            }
        }

        const uintptr_t next = rbase + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
}

bool GetSwapchainVtables(PresentFn& outPresent, Present1Fn& outPresent1) {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcA;
    wc.lpszClassName = "bottega.nchams.dummy";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "d", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width = 1;
    sd.BufferDesc.Height = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    ID3D11Device* dev = nullptr;
    IDXGISwapChain* sc0 = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &sd, &sc0, &dev, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &sd, &sc0, &dev, nullptr, nullptr);
    if (FAILED(hr) || !sc0 || !dev) {
        if (sc0) sc0->Release();
        if (dev) dev->Release();
        DestroyWindow(hwnd);
        UnregisterClassA(wc.lpszClassName, nullptr);
        return false;
    }

    // Capture the base-class Present before anything is patched.
    void** v0 = *reinterpret_cast<void***>(sc0);
    outPresent = reinterpret_cast<PresentFn>(v0[8]);

    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc0->QueryInterface(IID_PPV_ARGS(&sc1))) && sc1) {
        void** v1 = *reinterpret_cast<void***>(sc1);
        outPresent1 = reinterpret_cast<Present1Fn>(v1[22]);
        sc1->Release();
    }

    dev->Release();
    sc0->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, nullptr);
    return outPresent != nullptr;
}

} // namespace

bool InstallPresentHook() {
    if (oPresent) return true;

    PresentFn  p8  = nullptr;
    Present1Fn p22 = nullptr;
    if (!GetSwapchainVtables(p8, p22)) return false;

    oPresent  = p8;
    oPresent1 = p22;

    ScanAndHook(); // anchors on the loaded DXGI modules, hooks every class

    return oPresent != nullptr;
}

} // namespace nchams