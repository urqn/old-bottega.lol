// MouseSilent.cpp - ported verbatim from phantomX paid.
// Writes the cursor position into MouseService's mouse InputObject (engine-side).
#include "MouseSilent.h"
#include "paidmem.h"
#include "sdk/sdk.h"

#include <cstdint>

namespace Cheat {
namespace Features {
namespace MouseSilent {
namespace {

bool g_aiming = false;
std::uint64_t g_mouse_service = 0;

bool world_to_screen(const paidm::Matrix4x4& m, const paidm::Vector2& dim, const paidm::Vector3& p, paidm::Vector2& out)
{
    float w = p.x * m.m[3][0] + p.y * m.m[3][1] + p.z * m.m[3][2] + m.m[3][3];
    if (w < 0.01f)
    {
        return false;
    }

    float x = p.x * m.m[0][0] + p.y * m.m[0][1] + p.z * m.m[0][2] + m.m[0][3];
    float y = p.x * m.m[1][0] + p.y * m.m[1][1] + p.z * m.m[1][2] + m.m[1][3];
    float inv = 1.0f / w;

    out.x = (dim.x * 0.5f) + (x * inv * dim.x * 0.5f);
    out.y = (dim.y * 0.5f) - (y * inv * dim.y * 0.5f);
    return true;
}

std::uint64_t resolve_mouse()
{
    if (g_mouse_service && paidm::IsValid(g_mouse_service))
    {
        return g_mouse_service;
    }

    if (!Globals::dataModel.Addr ||
        !paidm::IsValid(Globals::dataModel.Addr))
    {
        return 0;
    }

    RBX::RbxInstance ms = RBX::RbxInstance(Globals::dataModel.Addr).FindFirstChild("MouseService");
    if (!ms || !paidm::IsValid(ms.Addr))
    {
        return 0;
    }

    g_mouse_service = ms.Addr;
    return g_mouse_service;
}

// InputObject then InputObject2, engine copy gets the write
bool write_mouse_pos(std::uint64_t mouse_service, float x, float y)
{
    auto try_input = [&](std::uintptr_t input_off) -> bool
    {
        std::uint64_t input = paidm::Read<std::uint64_t>(mouse_service + input_off);
        if (!input || input == (std::uint64_t)-1 || !paidm::IsValid(input))
        {
            return false;
        }

        float pos[2]{ x, y };
        return paidm::WriteRaw(
                   input + Offsets::MouseService::MousePosition, pos, sizeof(pos)) == sizeof(pos);
    };

    if (try_input(Offsets::MouseService::InputObject))
    {
        return true;
    }

    return try_input(Offsets::MouseService::InputObject2);
}

bool resolve_view(paidm::Matrix4x4& out_view, paidm::Vector2& out_dims)
{
    std::uint64_t cam = paidm::CameraAddr();
    if (!cam || !paidm::IsValid(cam))
    {
        return false;
    }

    out_dims = paidm::Read<paidm::Vector2>(cam + Offsets::Camera::ViewportSize);
    if (out_dims.x < 1.f || out_dims.y < 1.f)
    {
        return false;
    }

    out_view = paidm::ReadView();
    return true;
}

} // namespace

void Restore()
{
    g_aiming = false;
}

void Shutdown()
{
    Restore();
    g_mouse_service = 0;
}

void SetActive(bool on, const paidm::Vector3& world_target)
{
    if (!on)
    {
        Restore();
        return;
    }

    std::uint64_t ms = resolve_mouse();
    if (!ms)
    {
        return;
    }

    paidm::Matrix4x4 view{};
    paidm::Vector2 dims{};
    if (!resolve_view(view, dims))
    {
        return;
    }

    paidm::Vector2 target{};
    if (!world_to_screen(view, dims, world_target, target))
    {
        return;
    }

    if (!write_mouse_pos(ms, target.x, target.y))
    {
        g_mouse_service = 0; // reset, find again next frame
        return;
    }

    g_aiming = true;
}

bool Aiming()
{
    return g_aiming;
}

} // namespace MouseSilent
} // namespace Features
} // namespace Cheat