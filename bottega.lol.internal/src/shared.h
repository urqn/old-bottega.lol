#pragma once
#include <cstdint>

// Shared config layout for the Native Chams IPC. The external app maps this
// struct into "Local\bottega.lol.native" and the injected DLL renders from it.
// Both sides MUST keep this layout identical (0.9.0). Field order is fixed on
// purpose; do not reorder. `heartbeat` and `injected` are written by the DLL.
namespace ncfg {

constexpr uint32_t kMagic   = 0x4C544742u; // 'BTGL'
constexpr uint32_t kVersion = 1u;

inline constexpr wchar_t kMapName[] = L"Local\\bottega.lol.native";
constexpr size_t         kMapBytes  = 256;

#pragma pack(push, 1)
struct Config {
    uint32_t magic;         // +0x00  kMagic so both sides validate
    uint32_t version;       // +0x04  kVersion
    int32_t  enabled;       // +0x08  0/1  master switch
    int32_t  include_local; // +0x0C  0/1  also render the local character
    float    color[4];      // +0x10  rgba 0..1
    uint32_t heartbeat;     // +0x20  updated by the DLL every ~300ms
    int32_t  injected;      // +0x24  set to 1 once the Present hook is installed
    int32_t  status;        // +0x28  DLL->app status code (see kStatus_*)
    uint32_t frames;        // +0x2C  frames rendered by the in-process pass
    uint32_t last_parts;    // +0x30  parts drawn on the last frame
    uint8_t  _pad[256 - 0x34]; // +0x34  pad to a fixed 256-byte map
};
#pragma pack(pop)
static_assert(sizeof(Config) == 256, "ncfg::Config must stay 256 bytes");

// status codes written by the DLL (external app prints them to the log console)
inline constexpr int kStatusOk       = 0;    // not reached yet
inline constexpr int kStatusMapped   = 1;    // hook installed, Present live
inline constexpr int kStatusResolved = 2;    // datamodel + visual engine found
inline constexpr int kStatusDrawing  = 3;    // frames counter is ticking
inline constexpr int kStatusOpenFail = -1;   // config map open failed
inline constexpr int kStatusHookFail = -2;   // Present hook install failed

} // namespace ncfg