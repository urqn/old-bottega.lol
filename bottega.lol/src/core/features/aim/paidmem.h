#pragma once
// Memory adapter that mirrors the paid g_Memory API surface so the paid
// silent-aim modules paste 1:1. Backed by bottega's memory_t.
#include "memory/memory.h"
#include "core/globals/globals.h"
#include "sdk/offsets.h"
#include "aimmath.h"
#include <windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace paidm {

inline HANDLE Handle() { return memory->get_process_handle(); }
inline bool Connected() { return memory->IsConnected(); }
inline DWORD GetPID() { return memory->get_process_id(); }
inline uintptr_t BaseAddr() { return memory->get_module_address(); }

// Game window (bottega's g_target) — set by render.cpp, used by mouse/input math.
HWND GameHwnd();
void SetGameHwnd(HWND h);

inline bool IsValid(uintptr_t a) {
    if (!a || a < 0x10000ull) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(Handle(), reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi))) return false;
    return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

inline bool IsWritable(uintptr_t a, size_t n) {
    if (!a || !n) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(Handle(), reinterpret_cast<LPCVOID>(a), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD p = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE ||
           p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
}

template <typename T>
T Read(uintptr_t a) { return memory->read<T>(a); }

template <typename T>
void Write(uintptr_t a, const T& v) { memory->write<T>(a, v); }

inline SIZE_T ReadRaw(uintptr_t a, void* buf, SIZE_T n) {
    SIZE_T done = 0;
    if (ReadProcessMemory(Handle(), reinterpret_cast<LPCVOID>(a), buf, n, &done)) return done;
    return 0;
}

inline SIZE_T WriteRaw(uintptr_t a, const void* buf, SIZE_T n) {
    SIZE_T done = 0;
    if (WriteProcessMemory(Handle(), reinterpret_cast<LPVOID>(a), buf, n, &done)) return done;
    return 0;
}

inline uintptr_t Alloc(SIZE_T size, DWORD protect) {
    return reinterpret_cast<uintptr_t>(VirtualAllocEx(Handle(), nullptr, size, MEM_RESERVE | MEM_COMMIT, protect));
}

inline bool Free(uintptr_t p) {
    return VirtualFreeEx(Handle(), reinterpret_cast<LPVOID>(p), 0, MEM_RELEASE) != 0;
}

inline uintptr_t ModuleBase(const wchar_t* name) {
    if (!name || _wcsicmp(name, L"RobloxPlayerBeta.exe") == 0)
        return memory->get_module_address();
    const DWORD pid = GetPID();
    if (!pid) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m{ sizeof(m) };
    uintptr_t base = 0;
    if (Module32FirstW(snap, &m)) {
        do {
            if (_wcsicmp(m.szModule, name) == 0) { base = reinterpret_cast<uintptr_t>(m.modBaseAddr); break; }
        } while (Module32NextW(snap, &m));
    }
    CloseHandle(snap);
    return base;
}

// Camera instance address (current camera already resolved by the overlay).
inline uintptr_t CameraAddr() {
    return Globals::camera.Addr;
}

// Camera world position from the current camera CFrame (paid Camera::GetPosition()).
inline Vector3 CamPosition() {
    if (!Globals::camera.Addr) return Vector3();
    const RBX::CFrame c = memory->read<RBX::CFrame>(Globals::camera.Addr + Offsets::Camera::CFrame);
    const RBX::Vec3 p = c.GetPosition();
    return Vector3(p.X, p.Y, p.Z);
}

// VisualEngine view matrix (full 4x4, same bytes paid ViewportSilent/MouseSilent read).
inline Matrix4x4 ReadView() {
    Matrix4x4 vm;
    const uintptr_t ve = memory->read<uint64_t>(BaseAddr() + Offsets::VisualEngine::Pointer);
    if (ve && paidm::IsValid(ve))
        ReadRaw(ve + Offsets::VisualEngine::ViewMatrix, &vm, sizeof(vm));
    return vm;
}

inline Vector2 Dimensions() {
    const uintptr_t ve = memory->read<uint64_t>(BaseAddr() + Offsets::VisualEngine::Pointer);
    if (!ve) return Vector2();
    return memory->read<Vector2>(ve + Offsets::VisualEngine::Dimensions);
}

// world -> screen using the VisualEngine view matrix (paid MouseSilent w2s).
inline Vector2 WorldToScreen(const Vector3& world) {
    const Matrix4x4 m = ReadView();
    const Vector2 dims = Dimensions();
    const float w = world.x * m.m[3][0] + world.y * m.m[3][1] + world.z * m.m[3][2] + m.m[3][3];
    if (w < 0.01f) return Vector2(-1.f, -1.f);
    const float inv = 1.0f / w;
    const float x = world.x * m.m[0][0] + world.y * m.m[0][1] + world.z * m.m[0][2] + m.m[0][3];
    const float y = world.x * m.m[1][0] + world.y * m.m[1][1] + world.z * m.m[1][2] + m.m[1][3];
    return Vector2((dims.x * 0.5f) + (x * inv * dims.x * 0.5f),
                   (dims.y * 0.5f) - (y * inv * dims.y * 0.5f));
}

} // namespace paidm