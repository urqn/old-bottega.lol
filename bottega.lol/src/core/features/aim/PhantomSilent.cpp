// PhantomSilent.cpp - ported from phantomX paid. Spins the camera part's
// rotation (LookAt matrix) every tick until the raycaster returns the target.
#include "PhantomSilent.h"
#include "paidmem.h"
#include "sdk/sdk.h"
#include "sdk/offsets.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace Cheat {
namespace Features {
namespace PhantomSilent {
namespace {

// --- source, do not change ---
paidm::Vector3 CrossProduct(const paidm::Vector3& a, const paidm::Vector3& b)
{
    return a.Cross(b);
}

// ===== source — do not change =====
struct Matrix3x3 {
    float data[9]{};
};

static std::atomic<bool> g_writer_run{ false };
static std::atomic<bool> g_active{ false };
static std::atomic<float> g_tx{ 0.f }, g_ty{ 0.f }, g_tz{ 0.f };
static std::atomic<bool> g_writer_started{ false };
static bool g_running = false;

Matrix3x3 LookAtToMatrix(const paidm::Vector3& cameraPosition, const paidm::Vector3& targetPosition)
{
    paidm::Vector3 forward = (targetPosition - cameraPosition);
    forward.Normalize();
    paidm::Vector3 right = CrossProduct(paidm::Vector3(0.f, 1.f, 0.f), forward);
    right.Normalize();
    paidm::Vector3 up = CrossProduct(forward, right);
    Matrix3x3 lookAtMatrix{};
    lookAtMatrix.data[0] = -right.x;
    lookAtMatrix.data[1] = up.x;
    lookAtMatrix.data[2] = -forward.x;
    lookAtMatrix.data[3] = right.y;
    lookAtMatrix.data[4] = up.y;
    lookAtMatrix.data[5] = -forward.y;
    lookAtMatrix.data[6] = -right.z;
    lookAtMatrix.data[7] = up.z;
    lookAtMatrix.data[8] = -forward.z;
    return lookAtMatrix;
}

std::uint64_t PartPrimitive(std::uint64_t part)
{
    if (!paidm::IsValid(part))
        return 0;
    return paidm::Read<std::uint64_t>(part + Offsets::BasePart::Primitive);
}

paidm::Vector3 GetPartPos(std::uint64_t part)
{
    const std::uint64_t prim = PartPrimitive(part);
    if (!paidm::IsValid(prim))
        return {};
    return paidm::Read<paidm::Vector3>(prim + Offsets::Primitive::Position);
}

void SetRotation(std::uint64_t part_or_cam, const Matrix3x3& m, bool is_part)
{
    if (!paidm::IsValid(part_or_cam))
        return;

    if (is_part) {
        const std::uint64_t prim = PartPrimitive(part_or_cam);
        if (!paidm::IsValid(prim))
            return;
        paidm::WriteRaw(prim + Offsets::Primitive::Rotation, m.data, sizeof(m.data));
    } else {
        paidm::WriteRaw(part_or_cam + Offsets::Camera::Rotation, m.data, sizeof(m.data));
    }
}

// re-resolve the camera every tick, no stale address held
bool ResolveCamera(std::uint64_t& camera, bool& camera_is_part)
{
    static uintptr_t s_base = 0;
    if (!s_base)
        s_base = paidm::BaseAddr();
    if (!s_base)
        return false;

    if (!Globals::workspace || !paidm::IsValid(Globals::workspace.Addr))
        return false;

    // FindFirstChildOfClass("Camera")
    std::uint64_t cameraparent = 0;
    for (const auto& ch : RBX::RbxInstance(Globals::workspace.Addr).GetChildList()) {
        if (ch.GetClass() == "Camera") {
            cameraparent = ch.Addr;
            break;
        }
    }
    if (!paidm::IsValid(cameraparent)) {
        cameraparent = paidm::Read<std::uint64_t>(
            Globals::workspace.Addr + Offsets::Workspace::CurrentCamera);
    }
    if (!paidm::IsValid(cameraparent))
        return false;

    // camera = cameraparent.FindFirstChild("Part"); if 0 -> cameraparent
    camera = 0;
    camera_is_part = false;
    for (const auto& ch : RBX::RbxInstance(cameraparent).GetChildList()) {
        if (ch.GetName() == "Part") {
            camera = ch.Addr;
            camera_is_part = true;
            break;
        }
    }
    if (!paidm::IsValid(camera)) {
        camera = cameraparent;
        camera_is_part = false;
    }
    return paidm::IsValid(camera);
}

// camera.position -> lookAt matrix to target
Matrix3x3 ComputeMatrixFromCamera(std::uint64_t camera, bool camera_is_part, const paidm::Vector3& targetpos)
{
    paidm::Vector3 pos = camera_is_part
        ? GetPartPos(camera)
        : paidm::Read<paidm::Vector3>(camera + Offsets::Camera::Position);

    Matrix3x3 m = LookAtToMatrix(pos, targetpos);
    return Matrix3x3{ {
        m.data[0],
        m.data[1],
        m.data[2],
        m.data[3],
        0.01f,
        m.data[5],
        m.data[6],
        m.data[7],
        m.data[8],
    } };
}

void ApplyOnce(const paidm::Vector3& targetpos)
{
    std::uint64_t camera = 0;
    bool is_part = false;
    if (!ResolveCamera(camera, is_part))
        return;
    SetRotation(camera, ComputeMatrixFromCamera(camera, is_part, targetpos), is_part);
}

void EnsureWriter()
{
    bool expected = false;
    if (!g_writer_started.compare_exchange_strong(expected, true))
        return;

    g_writer_run = true;
    std::thread([] {
        while (g_writer_run) {
            if (g_active) {
                const paidm::Vector3 target(
                    g_tx.load(std::memory_order_relaxed),
                    g_ty.load(std::memory_order_relaxed),
                    g_tz.load(std::memory_order_relaxed));

                std::uint64_t camera = 0;
                bool is_part = false;
                if (ResolveCamera(camera, is_part)) {
                    SetRotation(camera, ComputeMatrixFromCamera(camera, is_part, target), is_part);
                    // on click the game clears rotation more often — push again
                    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
                        SetRotation(camera, ComputeMatrixFromCamera(camera, is_part, target), is_part);
                }
            }
            Sleep((g_active && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) ? 1 : 10);
        }
    }).detach();
    g_running = true;
}

void run(const paidm::Vector3& targetpos)
{
    g_tx.store(targetpos.x, std::memory_order_relaxed);
    g_ty.store(targetpos.y, std::memory_order_relaxed);
    g_tz.store(targetpos.z, std::memory_order_relaxed);
    ApplyOnce(targetpos);
}
// ===== end source =====

} // namespace

void SetActive(bool on, const paidm::Vector3& world_target)
{
    if (!on) {
        g_active = false;
        return;
    }
    if (world_target.LengthSquared() < 1e-6f)
        return;

    EnsureWriter();
    g_active = true;
    run(world_target);
}

void Shutdown()
{
    g_active = false;
    g_writer_run = false;
    g_writer_started = false;
}

} // namespace PhantomSilent
} // namespace Features
} // namespace Cheat
