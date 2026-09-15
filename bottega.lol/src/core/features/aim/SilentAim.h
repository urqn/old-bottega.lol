#pragma once
#include "aimmath.h"

namespace SilentAim {

// Manage the engine raycast hook (install when method = raycast/magic).
// Call every frame regardless of fire state (paid Aim::Render hook logic).
void TickMethod(int method);

// Dispatch to the ported paid silent modules. fire=false disarms all.
void Drive(int method, const paidm::Vector3& world, bool fire, bool force_mb);

// Stop writer threads + remove hooks (app shutdown).
void Shutdown();

} // namespace SilentAim