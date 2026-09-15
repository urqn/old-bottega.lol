#include <windows.h>
#include <cstdio>
#include "src/core/app/app.h"
#include "src/render/render.h"

int main() {
    SetConsoleTitleW(L"bottega.lol");
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    std::printf("[bottega.lol] starting...\n");

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