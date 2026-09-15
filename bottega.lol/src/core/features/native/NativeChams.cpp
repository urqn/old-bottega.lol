#include "NativeChams.h"
#include "../../variables/variables.h"
#include "../../../memory/memory.h"
#include "../../injector/ManualMapper.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace NativeChams {
namespace {

HANDLE                          g_map  = nullptr;
Config*                         g_cfg  = nullptr;
std::atomic<bool>               g_busy{false};
std::string                     g_status = "Not injected";
std::uint64_t                   g_last_error = 0;

bool g_console = false;
std::uint64_t g_last_auto = 0;
std::uint32_t g_auto_attempts = 0;

std::uint64_t TickMs() { return GetTickCount64(); }

void OpenConsole() {
    if (g_console) return;
    g_console = true;
    if (GetConsoleWindow()) return; // already attached to a console
    if (!AllocConsole()) return;
    {
        FILE* cf = nullptr;
        freopen_s(&cf, "CONOUT$", "w", stdout);
        freopen_s(&cf, "CONOUT$", "w", stderr);
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleTitleW(L"bottega.lol - native renderer log");
}

void Log(const char* fmt, ...) {
    OpenConsole();
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::printf("[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond,
                st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::fflush(stdout);
}

// After a successful map the DLL reports its progress through the Config
// status/frames fields. Poll for ~15s and print the diagnosis.
void Monitor(const ManualMapper::Result& r) {
    if (!g_cfg) return;

    int32_t  last_status = 0;
    uint32_t last_frames = 0;
    const std::uint64_t start = TickMs();
    bool heartbeat_seen = false, injected_seen = false, drawing_seen = false;

    while (TickMs() - start < 15000) {
        Sleep(200);

        const bool alive  = g_cfg->magic == kMagic &&
                            static_cast<uint32_t>(TickMs()) - g_cfg->heartbeat < 3000;
        const uint32_t frames = g_cfg->frames;
        const int32_t  st     = g_cfg->status;

        if (alive && !heartbeat_seen) {
            heartbeat_seen = true;
            Log("[native] DLL alive (heartbeat ok)\n");
        }
        if (g_cfg->injected && !injected_seen) {
            injected_seen = true;
            if (st >= kStatusMapped)
                Log("[native] Present hook installed; scanning swapchain classes done\n");
            else
                Log("[native] DLL loaded but hook not reported yet...\n");
        }
        if (st != last_status) {
            if (st == kStatusResolved)
                Log("[native] datamodel + visual engine resolved\n");
            else if (st == kStatusDrawing)
                Log("[native] renderer active (drawing)\n");
            last_status = st;
        }
        if (frames > last_frames) {
            if (!drawing_seen) {
                drawing_seen = true;
                Log("[native] SUCCESS: rendering frames - last frame %u verts\n",
                    g_cfg->last_parts);
            } else if (frames % 60 == 0) {
                Log("[native] rendering: frame %u (last %u verts)\n",
                    frames, g_cfg->last_parts);
            }
            last_frames = frames;
        }
        if (drawing_seen && frames >= 5) {
            Log("[native] all good - native chams are live (frames=%u, last=%u verts)\n",
                frames, g_cfg->last_parts);
            return;
        }
    }

    // Never got rendering: print the best explanation we can.
    if (!heartbeat_seen) {
        Log("[native] no heartbeat from the game - the DLL never started "
            "(mapping or import block failed, see [map] lines above)\n");
    } else if (!injected_seen) {
        Log("[native] hook was never reported - Present install failed inside the game\n");
    } else if (g_cfg->status == kStatusHookFail) {
        Log("[native] hook install failed (-2) - no usable D3D11 swapchain class found\n");
    } else if (g_cfg->status == kStatusOpenFail) {
        Log("[native] config map failed to open inside the game (-1)\n");
    } else if (!g_cfg->enabled) {
        Log("[native] config flag enabled=0 - turn ON \"Native Chams (Internal)\" "
            "in the menu\n");
    } else if (g_cfg->status >= kStatusResolved) {
        Log("[native] engine resolved but nothing drawn - likely no players on "
            "screen, or the view never resolved; frames=%u\n", g_cfg->frames);
    } else {
        Log("[native] injection done, but the renderer never reported status; "
            "status=%d enabled=%d\n", (int)g_cfg->status, (int)g_cfg->enabled);
    }
    Log("[native] log end (map at 0x%08X%08X)\n",
        (uint32_t)(r.module_base >> 32), (uint32_t)r.module_base);
}

} // namespace

const char* StatusCodeText(int status) {
    switch (status) {
        case kStatusMapped:   return "hook installed";
        case kStatusResolved: return "engine resolved";
        case kStatusDrawing:  return "drawing";
        case kStatusOpenFail: return "config map failed";
        case kStatusHookFail: return "hook install failed";
        default:              return "starting";
    }
}

void Ensure() {
    if (g_cfg) return;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               (DWORD)kMapBytes, kMapName);
    if (!g_map) {
        g_status = "Config map failed";
        return;
    }
    g_cfg = (Config*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, kMapBytes);
    if (!g_cfg) {
        CloseHandle(g_map);
        g_map = nullptr;
        g_status = "Config map failed";
        return;
    }
    if (g_cfg->magic != kMagic) {
        g_cfg->magic = kMagic;
        g_cfg->version = kVersion;
        g_cfg->enabled = 0;
        g_cfg->include_local = 0;
        g_cfg->color[0] = 1.0f; g_cfg->color[1] = 0.3f;
        g_cfg->color[2] = 0.6f; g_cfg->color[3] = 1.0f;
        g_cfg->heartbeat = 0;
        g_cfg->injected = 0;
        g_cfg->status = 0;
        g_cfg->frames = 0;
        g_cfg->last_parts = 0;
    }
}

bool Injected() {
    Ensure();
    if (!g_cfg || g_cfg->magic != kMagic || !g_cfg->injected || !g_cfg->heartbeat) return false;
    // heartbeat carries the DLL's last GetTickCount64() in ms; consider it
    // dead after ~3s without an update (game closed / hook crashed).
    const uint32_t age = (uint32_t)TickMs() - g_cfg->heartbeat;
    return age < 3000;
}

bool Enabled() { return Injected() && g_cfg->enabled; }

bool InjectAsync() {
    const bool selftest = ManualMapper::mode == ManualMapper::Mode::SelfTest;
    if (selftest) {
        Log("[native] SELFTEST: mapping bottega.lol.internal.dll into our OWN "
            "process (no Roblox code involved)\n");
    } else {
        Ensure();
    }
    if (!selftest && !memory->IsConnected()) {
        g_status = "Game not attached";
        Log("[native] canceled - no game attached\n");
        return false;
    }
    if (!selftest && Injected()) {
        g_status = "Injected";
        Log("[native] already injected into pid %lu\n", memory->get_process_id());
        return true;
    }
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) {
        Log("[native] injection already in progress\n");
        return false;
    }

    g_status = "Injecting...";
    const std::uint32_t pid = selftest ? GetCurrentProcessId()
                                       : memory->get_process_id();
    OpenConsole();
    Log("[native] ===== injection start (pid %lu) =====\n", pid);
    std::thread([pid] {
        const DWORD t0 = GetTickCount();
        const ManualMapper::Result r = ManualMapper::mode == ManualMapper::Mode::LoadTest
            ? ManualMapper::LoadByLoadLibrary(pid)
            : ManualMapper::MapInto(pid);
        const DWORD ms = GetTickCount() - t0;
        if (!r.ok) {
            g_last_error = GetLastError();
            Log("[native] injection FAILED after %lu ms: %s (last error %lu)\n",
                ms, r.error.c_str(), g_last_error);
            g_status = std::string("Hook failed (") + r.error + ")";
            if (g_status.size() > 28) g_status.resize(28);
        } else {
            Log("[native] image mapped at 0x%08X%08X (+%lu ms), waiting for the "
                "DLL inside the game...\n", (uint32_t)(r.module_base >> 32),
                (uint32_t)r.module_base, ms);
            Monitor(r);
            g_status = Injected() ? "Injected" : "Not injected";
        }
        g_busy = false;
    }).detach();
    return true;
}

const char* Status() {
    if (g_busy) return "Injecting...";
    if (Injected()) return "Injected";
    return g_status.c_str();
}

void AutoInject() {
    if (Injected()) return;
    if (g_busy) return;
    if (!memory->IsConnected()) return;
    if (g_auto_attempts >= 10) return;
    const std::uint64_t now = TickMs();
    if (now - g_last_auto < 1500) return; // keep error spam off the console
    g_last_auto = now;
    ++g_auto_attempts;
    InjectAsync();
}

void Update() {
    Ensure();
    if (!g_cfg || g_cfg->magic != kMagic) return;
    g_cfg->enabled = variables::ESP::nativeChams ? 1 : 0;
    g_cfg->include_local = variables::ESP::nativeIncludeLocal ? 1 : 0;
    std::memcpy(g_cfg->color, variables::ESP::nativeChamsColor, sizeof(g_cfg->color));
    if (g_cfg->color[3] < 0.01f) g_cfg->color[3] = 0.012f;
}

} // namespace NativeChams