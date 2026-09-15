#pragma once
#include "../../sdk/sdk.h"
#include <vector>
#include <cstdint>

namespace WorkspaceCache {
struct CachedPart {
    std::uintptr_t prim = 0;
    RBX::Vec3 pos{};
    RBX::Vec3 size{};
    float rot[9]{};
};

bool IsVisible(const RBX::Vec3& camPos, const RBX::Vec3& targetPos);
bool IsVisible(const std::vector<CachedPart>& parts, const RBX::Vec3& camPos, const RBX::Vec3& targetPos);
bool TakeSnapshot(std::vector<CachedPart>& out);
void Update();
void Loop();
}
