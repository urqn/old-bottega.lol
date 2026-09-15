#include "app.h"
#include <cstdio>
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <chrono>
#include "../../memory/memory.h"
#include "../../sdk/offsets.h"
#include "../../sdk/sdk.h"
#include "../cache/cache.h"
#include "../globals/globals.h"
#include "../tp_handler/tp_handler.h"
#include "../cache/workspace.h"
#include "../net/ping.h"
#include "../features/mesh/EngineChams.h"
#include "../features/aim/SilentAim.h"

#pragma comment(lib, "winmm.lib")

namespace {
constexpr const char* kProc = "RobloxPlayerBeta.exe";
constexpr const wchar_t* kTitle = L"Roblox";
std::thread g_tp;
std::thread g_ws;
std::thread g_ping;
std::thread g_chams;
}

namespace App {

bool game_open() {
    return FindWindowW(nullptr, kTitle) != nullptr;
}

bool init() {
    if (!memory->find_process_id(kProc)) {
        std::printf("[bottega.lol] unable to get pid - make sure roblox is running.\n");
        return false;
    }
    if (!memory->attach_to_process(kProc)) {
        std::printf("[bottega.lol] unable to attach to roblox.\n");
        return false;
    }
    if (!memory->find_module_address(kProc)) {
        std::printf("[bottega.lol] unable to find main module address.\n");
        return false;
    }
    const auto base = memory->get_module_address();
    if (!base) {
        std::printf("[bottega.lol] base address is null.\n");
        return false;
    }
    const auto fake = memory->read<std::uintptr_t>(base + Offsets::FakeDataModel::Pointer);
    if (!fake) {
        std::printf("[bottega.lol] fake datamodel pointer is null.\n");
        return false;
    }
    const auto dm = memory->read<std::uintptr_t>(fake + Offsets::FakeDataModel::RealDataModel);
    if (!dm) {
        std::printf("[bottega.lol] datamodel pointer is null.\n");
        return false;
    }
    const auto ve = memory->read<std::uintptr_t>(base + Offsets::VisualEngine::Pointer);
    if (!ve) {
        std::printf("[bottega.lol] visualengine pointer is null.\n");
        return false;
    }

    Globals::dataModel   = RBX::RbxInstance{dm};
    Globals::renderEngine = RBX::RenderEngine{ve};
    Globals::workspace   = Globals::dataModel.FindChildByClass("Workspace");
    Globals::players     = Globals::dataModel.FindChildByClass("Players");
    Globals::camera      = Globals::workspace.Addr
                               ? Globals::workspace.FindChildByClass("Camera")
                               : RBX::RbxInstance{};
    const auto local = Globals::players.Addr
                           ? memory->read<std::uintptr_t>(Globals::players.Addr + Offsets::Players::LocalPlayer)
                           : 0;
    Globals::localPlayer = RBX::RbxInstance{local};

    std::printf("[bottega.lol] attached to roblox\n");
    std::printf("    base : 0x%llX\n", (unsigned long long)base);
    std::printf("    dm   : 0x%llX\n", (unsigned long long)Globals::dataModel.Addr);
    std::printf("    ve   : 0x%llX\n", (unsigned long long)Globals::renderEngine.Addr);
    std::printf("    lcl  : 0x%llX\n", (unsigned long long)Globals::localPlayer.Addr);
    return true;
}

bool start() {
    timeBeginPeriod(1);
    Globals::running = true;
    g_tp   = std::thread(Core::tp_handler::thread);
    g_ws   = std::thread(WorkspaceCache::Loop);
    g_ping = std::thread(Ping::Loop);
    EngineChams::Start();
    return true;
}

void stop() {
    Globals::running = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (g_tp.joinable())
        g_tp.join();
    if (g_ws.joinable())
        g_ws.join();
    if (g_ping.joinable())
        g_ping.join();
    EngineChams::Stop();
    SilentAim::Shutdown();
    timeEndPeriod(1);
}

} // namespace App
