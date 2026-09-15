#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/core/app/app.h"
#include "src/render/render.h"
#include "src/core/injector/ManualMapper.h"

int main() {
    SetConsoleTitleW(L"bottega.lol");
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    std::printf("[bottega.lol] starting...\n");

    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            std::vector<std::wstring> args(argv, argv + argc);
            LocalFree(argv);
            for (const auto& a : args) {
                if (a == L"--loadtest")
                    ManualMapper::mode = ManualMapper::Mode::LoadTest;
                else if (a == L"--threadtest")
                    ManualMapper::mode = ManualMapper::Mode::ThreadTest;
                else if (a == L"--selftest")
                    ManualMapper::mode = ManualMapper::Mode::SelfTest;
                else if (a == L"--apcinject")
                    ManualMapper::mode = ManualMapper::Mode::Apc;
            }
            if (ManualMapper::mode == ManualMapper::Mode::LoadTest)
                std::printf("[bottega.lol] injection mode: --loadtest (LoadLibraryExW, no manual map)\n");
            else if (ManualMapper::mode == ManualMapper::Mode::ThreadTest)
                std::printf("[bottega.lol] injection mode: --threadtest (map, bare thread, entry not called)\n");
            else if (ManualMapper::mode == ManualMapper::Mode::SelfTest)
                std::printf("[bottega.lol] injection mode: --selftest (manual map into OUR OWN process)\n");
            else if (ManualMapper::mode == ManualMapper::Mode::Apc)
                std::printf("[bottega.lol] injection mode: --apcinject (map + APC onto existing Roblox threads)\n");
        }
    }

    if (!App::init()) {
        ::MessageBoxW(nullptr, L"[bottega.lol] failed to initialize - make sure Roblox is running.", L"bottega.lol", MB_ICONERROR | MB_OK);
        return 1;
    }
    App::start();
    const bool ok = Render::run();
    App::stop();
    std::printf("[bottega.lol] session ended (%s)\n", ok ? "clean" : "unexpected exit");
    std::printf("[bottega.lol] press any key to close...\n");
    ::system("pause >nul");
    return ok ? 0 : 1;
}