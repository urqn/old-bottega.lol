#include "workspace.h"
#include "../globals/globals.h"
#include "../../memory/memory.h"
#include "../../sdk/offsets.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace WorkspaceCache {
namespace {
std::vector<CachedPart> parts;
std::mutex partsMutex;
constexpr std::size_t kMaxParts = 1500;
constexpr std::size_t kMaxNodes = 4000;
std::size_t nodesVisited = 0;

bool IsCollidable(uintptr_t instance, uintptr_t prim) {
    const float transparency = memory->read<float>(instance + Offsets::BasePart::Transparency);
    if (transparency >= 0.95f)
        return false;
    const uint8_t flags = memory->read<uint8_t>(prim + Offsets::Primitive::PrimitiveFlags);
    return (flags & (uint8_t)Offsets::PrimitiveFlags::CanCollide) != 0;
}

void Gather(uintptr_t instance, std::vector<CachedPart>& out) {
    if (!instance || nodesVisited >= kMaxNodes || out.size() >= kMaxParts)
        return;
    ++nodesVisited;
    if (Globals::localPlayer.Addr) {
        auto localChar = Globals::localPlayer.GetModelRef();
        if (localChar.Addr && instance == localChar.Addr)
            return;
    }
    if (RBX::RbxInstance(instance).FindChild("Humanoid").Addr != 0)
        return;
    const uintptr_t prim = memory->read<uintptr_t>(instance + Offsets::BasePart::Primitive);
    if (prim && prim != 0xFFFFFFFFFFFFFFFFull && prim > 0x10000 && prim < 0x7FFFFFFF0000ull && IsCollidable(instance, prim)) {
        const auto cls = RBX::RbxInstance(instance).GetClass();
        if (cls == "Part" || cls == "MeshPart" || cls == "WedgePart" || cls == "CornerWedgePart") {
            const RBX::Vec3 size = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Size);
            if (size.X < 150.0f && size.Z < 150.0f && size.X > 0.5f && size.Y > 0.5f) {
                CachedPart cp{};
                cp.prim = prim;
                cp.pos = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Position);
                cp.size = size;
                for (int i = 0; i < 9; ++i)
                    cp.rot[i] = memory->read<float>(prim + Offsets::Primitive::Rotation + (std::uint64_t)i * sizeof(float));
                out.push_back(cp);
                if (out.size() >= kMaxParts)
                    return;
            }
        }
    }
    const uintptr_t childStart = memory->read<uintptr_t>(instance + Offsets::Instance::ChildrenStart);
    if (!childStart)
        return;
    const uintptr_t childEnd = memory->read<uintptr_t>(childStart + Offsets::Instance::ChildrenEnd);
    const uintptr_t current = memory->read<uintptr_t>(childStart);
    if (!childEnd || !current || childEnd < current)
        return;
    constexpr std::size_t kMaxChildren = 512;
    std::size_t count = 0;
    for (uintptr_t ptr = current; ptr < childEnd && count < kMaxChildren; ptr += 0x10, ++count) {
        const uintptr_t child = memory->read<uintptr_t>(ptr);
        if (child)
            Gather(child, out);
        if (nodesVisited >= kMaxNodes || out.size() >= kMaxParts)
            return;
    }
}
}

bool IsVisible(const RBX::Vec3& camPos, const RBX::Vec3& targetPos) {
    std::vector<CachedPart> local;
    if (!TakeSnapshot(local))
        return true;
    return IsVisible(local, camPos, targetPos);
}

bool TakeSnapshot(std::vector<CachedPart>& out) {
    std::lock_guard<std::mutex> lock(partsMutex);
    if (parts.empty())
        return false;
    out = parts;
    return true;
}

bool IsVisible(const std::vector<CachedPart>& local, const RBX::Vec3& camPos, const RBX::Vec3& targetPos) {
    RBX::Vec3 dir{targetPos.X - camPos.X, targetPos.Y - camPos.Y, targetPos.Z - camPos.Z};
    const float rayLen = sqrtf(dir.X * dir.X + dir.Y * dir.Y + dir.Z * dir.Z);
    if (rayLen < 1e-6f)
        return true;
    dir.X /= rayLen;
    dir.Y /= rayLen;
    dir.Z /= rayLen;
    for (const auto& part : local) {
        const float px = part.pos.X - camPos.X;
        const float py = part.pos.Y - camPos.Y;
        const float pz = part.pos.Z - camPos.Z;
        const float distSq = px * px + py * py + pz * pz;
        const float maxExtent = (std::max)(part.size.X, (std::max)(part.size.Y, part.size.Z)) * 1.5f;
        if (distSq > (rayLen + maxExtent) * (rayLen + maxExtent))
            continue;
        if (distSq < 1.0f)
            continue;
        const float* r = part.rot;
        float e[3] = {
            px * r[0] + py * r[1] + pz * r[2],
            px * r[3] + py * r[4] + pz * r[5],
            px * r[6] + py * r[7] + pz * r[8]
        };
        float f[3] = {
            dir.X * r[0] + dir.Y * r[1] + dir.Z * r[2],
            dir.X * r[3] + dir.Y * r[4] + dir.Z * r[5],
            dir.X * r[6] + dir.Y * r[7] + dir.Z * r[8]
        };
        const float h[3] = {part.size.X * 0.5f, part.size.Y * 0.5f, part.size.Z * 0.5f};
        float tMin = 0.0f;
        float tMax = rayLen - 0.5f;
        bool hit = true;
        for (int i = 0; i < 3; ++i) {
            if (std::abs(f[i]) > 1e-6f) {
                float t1 = (e[i] - h[i]) / f[i];
                float t2 = (e[i] + h[i]) / f[i];
                if (t1 > t2)
                    std::swap(t1, t2);
                if (t1 > tMin)
                    tMin = t1;
                if (t2 < tMax)
                    tMax = t2;
                if (tMin > tMax) {
                    hit = false;
                    break;
                }
            } else if (e[i] < -h[i] || e[i] > h[i]) {
                hit = false;
                break;
            }
        }
        if (hit && tMax >= 0.0f)
            return false;
    }
    return true;
}

void Update() {
    if (!Globals::workspace.Addr)
        return;
    std::vector<CachedPart> next;
    next.reserve(512);
    nodesVisited = 0;
    Gather(Globals::workspace.Addr, next);
    std::lock_guard<std::mutex> lock(partsMutex);
    parts = std::move(next);
}

void Loop() {
    using namespace std::chrono_literals;
    while (Globals::running) {
        if (memory->IsConnected())
            Update();
        std::this_thread::sleep_for(2s);
    }
}
}
