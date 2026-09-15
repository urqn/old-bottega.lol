#pragma once
#include "aimmath.h"

namespace Cheat {
namespace Features {
namespace MouseSilent {

void SetActive(bool on, const paidm::Vector3& world_target);
void Restore();
void Shutdown();
bool Aiming();

} // namespace MouseSilent
} // namespace Features
} // namespace Cheat