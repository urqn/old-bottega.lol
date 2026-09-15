#include "shared.h"
#include "present.h"
#include "render.h"

#include <windows.h>

namespace nchams {
namespace {

HANDLE      g_map = nullptr;
ncfg::Config* s_cfg = nullptr;

DWORD WINAPI InitThread(LPVOID) {
    // Open (or create) the shared config mapping owned by the external app.
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               (DWORD)ncfg::kMapBytes, ncfg::kMapName);
    if (!g_map) return 1;

    s_cfg = (ncfg::Config*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, ncfg::kMapBytes);
    if (!s_cfg) {
        CloseHandle(g_map);
        g_map = nullptr;
        return 1;
    }

    if (s_cfg->magic != ncfg::kMagic) {
        // Fresh/externally-abandoned map: seed it so the app can pick it up.
        s_cfg->magic = ncfg::kMagic;
        s_cfg->version = ncfg::kVersion;
        s_cfg->enabled = 0;
        s_cfg->include_local = 0;
        s_cfg->color[0] = 1.f; s_cfg->color[1] = 0.3f;
        s_cfg->color[2] = 0.6f; s_cfg->color[3] = 1.f;
    }
    s_cfg->status = ncfg::kStatusOk;
    s_cfg->frames = 0;
    s_cfg->last_parts = 0;

    nchams::SetConfig(s_cfg);

    s_cfg->injected = nchams::InstallPresentHook() ? 1 : 0;
    s_cfg->status = s_cfg->injected ? ncfg::kStatusMapped : ncfg::kStatusHookFail;

    uint32_t tick = 0;
    for (;;) {
        Sleep(300);
        ++tick;
        (void)tick;
        s_cfg->heartbeat = (uint32_t)GetTickCount64();
    }
    return 0;
}

} // namespace
} // namespace nchams

BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, nchams::InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}