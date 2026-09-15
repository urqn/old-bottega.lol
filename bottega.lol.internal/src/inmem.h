#pragma once
#include <excpt.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include "offsets_native.h"

// Raw (in-process) memory readers with the exact semantics used by the
// external SDK (src/sdk/sdk.h / memory.cpp). Everything is guarded with
// structured exceptions so a stale/cleaned instance never crashes the game.
namespace inmem {

inline bool TryRead(uintptr_t addr, void* dst, size_t n) {
    if (!addr) return false;
    __try {
        memcpy(dst, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

inline uint64_t Rd(uintptr_t addr) {
    uint64_t v = 0;
    TryRead(addr, &v, sizeof(v));
    return v;
}

template <typename T>
inline T ReadT(uintptr_t addr) {
    T v{};
    TryRead(addr, &v, sizeof(v));
    return v;
}

// Roblox String instance layout: [inline data ... 16 bytes][len i32 @ +0x10].
// len >= 16 -> pointer stored at +0x00, otherwise the data is in the object.
inline std::string Str(uintptr_t addr) {
    std::string out;
    if (!addr) return out;
    const int32_t len = ReadT<int32_t>(addr + 0x10);
    if (len <= 0 || len > 255) return out;
    uintptr_t p = addr;
    if (len >= 16) p = Rd(addr);
    if (!p) return out;
    char buf[256];
    if (!TryRead(p, buf, static_cast<size_t>(len))) return out;
    out.assign(buf, static_cast<size_t>(len));
    return out;
}

inline std::string GetName(uintptr_t inst) {
    const uintptr_t container = Rd(inst + 0x70);
    return container ? Str(container + 0x8) : std::string{};
}

inline std::string GetClass(uintptr_t inst) {
    if (!inst) return {};
    const uintptr_t desc = Rd(inst + 0x18);
    if (!desc) return {};
    const uintptr_t info = Rd(desc + 0x8);
    return info ? Str(info) : std::string{};
}

// Instance child list: list ptr @ +0x78 -> { begin @ +0x00, end @ +0x08 }.
// Entries are 0x10 bytes; first field is the child instance pointer.
inline std::vector<uintptr_t> Children(uintptr_t inst) {
    std::vector<uintptr_t> out;
    if (!inst) return out;
    const uintptr_t list = Rd(inst + 0x78);
    if (!list) return out;
    const uintptr_t begin = Rd(list);
    const uintptr_t end   = Rd(list + 0x8);
    if (!begin || !end || end < begin) return out;
    const size_t n = std::min<size_t>(4096, (end - begin) >> 4);
    for (size_t i = 0; i < n; ++i) {
        const uintptr_t child = Rd(begin + i * 0x10);
        if (child) out.push_back(child);
    }
    return out;
}

inline uintptr_t CharacterOf(uintptr_t player) { return Rd(player + 0x298); } // GetModelRef

inline uintptr_t PrimitiveOf(uintptr_t part) { return Rd(part + nofs::BasePart::Primitive); }

} // namespace inmem