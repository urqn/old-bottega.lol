#pragma once
#include <cstddef>
#include <cstdint>

// External-side controller for the internal (in-process) cham renderer.
// Talks to the injected DLL through a shared file mapping (same layout as
// bottega.lol.internal/src/shared.h — keep both in sync).
namespace NativeChams {

constexpr uint32_t  kMagic     = 0x4C544742u; // 'BTGL'
constexpr uint32_t  kVersion   = 1u;
inline constexpr wchar_t kMapName[] = L"Local\\bottega.lol.native";
constexpr size_t    kMapBytes  = 256;

#pragma pack(push, 1)
struct Config {
    uint32_t magic;         // +0x00
    uint32_t version;       // +0x04
    int32_t  enabled;       // +0x08
    int32_t  include_local; // +0x0C
    float    color[4];      // +0x10
    uint32_t heartbeat;     // +0x20
    int32_t  injected;      // +0x24
    int32_t  status;        // +0x28
    uint32_t frames;        // +0x2C
    uint32_t last_parts;    // +0x30
    uint8_t  _pad[256 - 0x34]; // +0x34
};
#pragma pack(pop)
static_assert(sizeof(Config) == 256, "NativeChams::Config must stay 256 bytes");

inline constexpr int kStatusOk       = 0;
inline constexpr int kStatusMapped   = 1;
inline constexpr int kStatusResolved = 2;
inline constexpr int kStatusDrawing  = 3;
inline constexpr int kStatusOpenFail = -1;
inline constexpr int kStatusHookFail = -2;

const char* StatusCodeText(int status);

// Opens/creates the mapping (called every frame; cheap no-op once mapped).
void Ensure();
// Hook live + heartbeat recently updated by the DLL.
bool Injected();
// Injected() && config enabled flag.
bool Enabled();
// Starts a background thread that manually maps the DLL into the game.
// Returns false if an injection is already in progress.
bool InjectAsync();
// Auto-mode: called every frame. Injects once when the game is attached and
// the DLL is not already live. Never triggers more than ~10 times.
void AutoInject();
// Short status string for the UI (e.g. "Injected", "Hook failed").
const char* Status();
// Pushes the current UI vars into the shared config (render thread, every frame).
void Update();

} // namespace NativeChams