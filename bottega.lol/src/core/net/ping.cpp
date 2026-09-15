#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include "ping.h"
#include "../globals/globals.h"
#include "../../memory/memory.h"
#include "../../sdk/offsets.h"
#include <windows.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace Ping {
namespace {
std::atomic<int> ms{-1};

bool PingOnce(const std::string& host) {
    IN_ADDR addr{};
    if (InetPtonA(AF_INET, host.c_str(), &addr) != 1)
        return false;
    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
        return false;
    std::vector<char> reply(sizeof(ICMP_ECHO_REPLY) + 32);
    const DWORD res = IcmpSendEcho(icmp, addr.S_un.S_addr, nullptr, 0, nullptr, reply.data(), (DWORD)reply.size(), 1000);
    int rtt = -1;
    if (res != 0)
        rtt = (int)((PICMP_ECHO_REPLY)reply.data())->RoundTripTime;
    IcmpCloseHandle(icmp);
    ms.store(rtt);
    return rtt >= 0;
}
}

int GetMs() {
    return ms.load();
}

void Loop() {
    using namespace std::chrono_literals;
    while (Globals::running) {
        if (memory->IsConnected() && Globals::dataModel.Addr) {
            const std::string host = memory->read_string(Globals::dataModel.Addr + Offsets::DataModel::ServerIP);
            if (!host.empty() && host != "Unknown")
                PingOnce(host);
            else
                ms.store(-1);
        } else {
            ms.store(-1);
        }
        std::this_thread::sleep_for(5s);
    }
}
}
