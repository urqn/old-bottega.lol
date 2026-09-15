#include "render.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "egui/egui.h"
#include "egui/egui_settings.h"
#include "egui/egui_variables.h"
#include "egui/egui_glass.h"
#include "egui/egui_spotify.h"
#include "egui/model3d.h"

#include "../memory/memory.h"
#include "../sdk/offsets.h"
#include "../sdk/sdk.h"
#include "../sdk/w2s.h"
#include "../core/globals/globals.h"
#include "../core/cache/cache.h"
#include "../core/cache/workspace.h"
#include "../core/net/ping.h"
#include "../core/variables/variables.h"
#include "../core/features/mesh/MeshChams.h"
#include "../core/features/mesh/MeshCache.h"
#include "../core/features/mesh/MeshDxShader.h"
#include "../core/features/mesh/ShaderChams.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "../core/features/aim/SilentAim.h"
#include "../core/features/aim/paidmem.h"

namespace paidm {
static HWND g_game_hwnd = nullptr;
HWND GameHwnd() { return g_game_hwnd; }
void SetGameHwnd(HWND h) { g_game_hwnd = h; }
} // namespace paidm

namespace {
using namespace egui;
using namespace Cheat::Visuals;

inline bool is_valid_ptr(std::uintptr_t p) {
    return p > 0x10000 && p < 0x7FFFFFFFFFFFull;
}

// ── ventana overlay ──────────────────────────────────────────────────────────
HWND g_overlay = nullptr;
HWND g_target  = nullptr;
float g_origin_x = 0.0f;
float g_origin_y = 0.0f;

LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SIZE) {
        g_ResizeWidth  = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    }
    if (msg == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

struct TargetCtx { HWND* out; DWORD pid; };

HWND find_target_window() {
    const HANDLE hp = memory->get_process_handle();
    if (!hp || hp == INVALID_HANDLE_VALUE)
        return nullptr;
    const DWORD pid = ::GetProcessId(hp);
    if (!pid)
        return nullptr;
    HWND found = nullptr;
    TargetCtx ctx{ &found, pid };
    ::EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        const TargetCtx& c = *reinterpret_cast<const TargetCtx*>(lp);
        DWORD wpid = 0;
        ::GetWindowThreadProcessId(h, &wpid);
        if (wpid == c.pid && ::IsWindowVisible(h)) {
            *c.out = h;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return found;
}

bool create_overlay() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"bottega_lol_overlay";
    if (!::RegisterClassExW(&wc))
        return false;
    g_overlay = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"bottega.lol", WS_POPUP,
        0, 0, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_overlay)
        return false;
    ::SetLayeredWindowAttributes(g_overlay, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS m{ -1, -1, -1, -1 }; // vidrio: la transparencia la hace el orpho negro inteligente
    ::DwmExtendFrameIntoClientArea(g_overlay, &m);
    return true;
}

// A la zona cliente de Roblox. false = no hay ventana utilizable (aunque no
// esté todavía / minimizada / cerrada del todo).
bool sync_overlay() {
    if (!IsWindow(g_target))
        g_target = find_target_window();
    paidm::SetGameHwnd(g_target);
    if (!IsWindow(g_target))
        return false;
    if (!::IsWindowVisible(g_target) || ::IsIconic(g_target))
        return false;
    RECT client{};
    ::GetClientRect(g_target, &client);
    POINT tl{ 0, 0 };
    ::ClientToScreen(g_target, &tl);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    if (w <= 0 || h <= 0)
        return false;
    RECT cur{};
    ::GetWindowRect(g_overlay, &cur);
    const bool moved = cur.left != tl.x || cur.top != tl.y ||
                       (cur.right - cur.left) != w || (cur.bottom - cur.top) != h;
    if (moved)
        ::SetWindowPos(g_overlay, HWND_TOPMOST, tl.x, tl.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    else
        ::ShowWindow(g_overlay, SW_SHOWNA);
    g_origin_x = static_cast<float>(tl.x);
    g_origin_y = static_cast<float>(tl.y);
    return true;
}

// Con el menu abierto la overlay captura el raton; si no, pasa a traves salvo
// que haya raton encima de la tarjeta de spotify.
void update_clickthrough(const ImVec2& client_mouse) {
    const LONG_PTR style = ::GetWindowLongPtrW(g_overlay, GWL_EXSTYLE);
    const bool locked = !egui::settings.state.menu_open && !egui::spotify_hovered(client_mouse);
    if (locked && !(style & WS_EX_TRANSPARENT))
        ::SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
    else if (!locked && (style & WS_EX_TRANSPARENT))
        ::SetWindowLongPtrW(g_overlay, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
}

// ── D3D11 (mismo patron que el ejemplo base) ────────────────────────────────
void create_render_target() {
    ID3D11Texture2D* back = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRenderTargetView);
        back->Release();
    }
}

void cleanup_render_target() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool create_device() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 1280;
    sd.BufferDesc.Height                  = 720;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = g_overlay;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    if (FAILED(hr))
        return false;
    create_render_target();
    return true;
}

void cleanup_device() {
    cleanup_render_target();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

// ── datos reales ─────────────────────────────────────────────────────────────
void refresh_players() {
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(10))
        return;
    last = now;
    PlayerCache::updateplayers();
}

std::string to_hex(std::uintptr_t v) {
    char b[32];
    snprintf(b, sizeof(b), "0x%llX", (unsigned long long)v);
    return std::string(b);
}

// ── helpers de dibujo de la ESP ─────────────────────────────────────────────
ImU32 esp_col(const float c[4], float a) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * a));
}

ImU32 mix_u32(ImU32 a, ImU32 b, float t) {
    const ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImLerp(av, bv, ImClamp(t, 0.0f, 1.0f)));
}

RBX::Vec3 cam_position() {
    if (Globals::camera.Addr) {
        const RBX::CFrame cam = memory->read<RBX::CFrame>(Globals::camera.Addr + Offsets::Camera::CFrame);
        if (ImFabs(cam.data[9]) + ImFabs(cam.data[10]) + ImFabs(cam.data[11]) > 0.0001f)
            return cam.GetPosition();
    }
    return PlayerCache::localPlayerPos;
}

bool to_client(const RBX::Mat4& view, const RBX::Vec3& w, ImVec2& out) {
    const ImGuiIO& io = ImGui::GetIO();
    // La overlay cubre exactamente el cliente del juego: proyectamos con su
    // tamano, no con el del monitor principal, o los puntos caen fuera de sitio
    // en ventana / monitor secundario.
    const RBX::Vec2 s = W2S::WorldToScreen(w, view, io.DisplaySize.x, io.DisplaySize.y);
    if (s.X <= 0.001f)
        return false;
    out = ImVec2(s.X, s.Y);
    return true;
}

struct VisCacheEntry { std::chrono::steady_clock::time_point at; bool visible; };
static std::unordered_map<std::uintptr_t, VisCacheEntry> g_vis_cache;

bool esp_visible(const std::vector<WorkspaceCache::CachedPart>& parts,
                 std::uintptr_t key, const RBX::Vec3& cam, const RBX::Vec3& tgt) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto it = g_vis_cache.find(key);
    if (it != g_vis_cache.end() && now - it->second.at < std::chrono::milliseconds(200))
        return it->second.visible;
    const bool v = WorkspaceCache::IsVisible(parts, cam, tgt);
    if (g_vis_cache.size() > 256)
        g_vis_cache.clear();
    g_vis_cache[key] = { now, v };
    return v;
}

// ── daño ────────────────────────────────────────────────────────────────────
struct DmgFloat { ImVec2 pos; float value; float born; };
static std::vector<DmgFloat> g_dmg_floats;

void draw_damage_floats(float now, ImDrawList* dl, const ImVec2& clip) {
    if (!vars.esp.damage_indicators || g_dmg_floats.empty())
        return;
    ImFont* f = fnt(efonts.inter.Bold13);
    g_dmg_floats.erase(std::remove_if(g_dmg_floats.begin(), g_dmg_floats.end(),
        [&](const DmgFloat& d) { return now - d.born >= vars.esp.damage_lifetime; }), g_dmg_floats.end());
    for (const auto& d : g_dmg_floats) {
        const float age = now - d.born;
        const float a = 1.0f - age / ImMax(vars.esp.damage_lifetime, 0.001f);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", d.value);
        const ImVec2 pos(ImClamp(d.pos.x, 0.0f, clip.x * 0.5f), ImClamp(d.pos.y - age * vars.esp.damage_float_speed, 0.0f, clip.y));
        const ImU32 col = esp_col(vars.esp.damage_color, a);
        if (vars.esp.damage_outline) {
            const ImU32 oc = esp_col(vars.esp.damage_outline_color, a);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dy)
                        dl->AddText(f, vars.esp.damage_font_size, ImVec2(pos.x + dx, pos.y + dy), oc, buf);
        }
        dl->AddText(f, vars.esp.damage_font_size, pos, col, buf);
    }
}

// ── cajas ───────────────────────────────────────────────────────────────────
void esp_box_corners(ImDrawList* dl, const ImVec2& tl, const ImVec2& br, ImU32 col, float thickness) {
    const float l = ImMin((br.x - tl.x) * 0.30f, (br.y - tl.y) * 0.18f);
    dl->AddLine(tl, ImVec2(tl.x + l, tl.y), col, thickness);
    dl->AddLine(tl, ImVec2(tl.x, tl.y + l), col, thickness);
    dl->AddLine(ImVec2(br.x - l, tl.y), ImVec2(br.x, tl.y), col, thickness);
    dl->AddLine(ImVec2(br.x, tl.y), ImVec2(br.x, tl.y + l), col, thickness);
    dl->AddLine(ImVec2(br.x - l, br.y), ImVec2(br.x, br.y), col, thickness);
    dl->AddLine(ImVec2(br.x, br.y - l), ImVec2(br.x, br.y), col, thickness);
    dl->AddLine(ImVec2(tl.x, br.y - l), ImVec2(tl.x, br.y), col, thickness);
    dl->AddLine(ImVec2(tl.x, br.y), ImVec2(tl.x + l, br.y), col, thickness);
}

void esp_box_outline(ImDrawList* dl, const ImVec2& tl, const ImVec2& br, float thickness, int flags, ImU32 col) {
    const float t = ImMax(1.0f, thickness) + 2.0f;
    if (flags & 1) dl->AddRectFilled(ImVec2(tl.x - t, tl.y - t), ImVec2(br.x + t, tl.y), col);
    if (flags & 2) dl->AddRectFilled(ImVec2(tl.x - t, br.y), ImVec2(br.x + t, br.y + t), col);
    if (flags & 4) dl->AddRectFilled(ImVec2(tl.x - t, tl.y), ImVec2(tl.x, br.y), col);
    if (flags & 8) dl->AddRectFilled(ImVec2(br.x, tl.y), ImVec2(br.x + t, br.y), col);
}

bool esp_box_3d(ImDrawList* dl, const RBX::Mat4& view, std::uintptr_t rootAddr,
                const RBX::Vec3& center, float height,
                ImU32 col, ImU32 col2, float thickness, bool gradient) {
    const RBX::CFrame cfr = RBX::RbxInstance(rootAddr).GetCFrame();
    const RBX::Vec3 pos = cfr.GetPosition();
    if (ImFabs(pos.X) + ImFabs(pos.Y) + ImFabs(pos.Z) < 0.0001f)
        return false;
    const RBX::Vec3 R = cfr.GetRightVector();
    const RBX::Vec3 U = cfr.GetUpVector();
    const RBX::Vec3 L = cfr.GetLookVector();
    const float hy = ImMax(8.0f, height) * 0.5f;
    const float hx = hy * 0.62f;
    const float hz = hy * 0.34f;
    ImVec2 c[8];
    for (int i = 0; i < 8; i++) {
        const float sx = (i & 4) ? hx : -hx;
        const float sy = (i & 2) ? hy : -hy;
        const float szz = (i & 1) ? hz : -hz;
        const RBX::Vec3 w{ center.X + R.X * sx + U.X * sy + L.X * szz,
                           center.Y + R.Y * sx + U.Y * sy + L.Y * szz,
                           center.Z + R.Z * sx + U.Z * sy + L.Z * szz };
        if (!to_client(view, w, c[i]))
            return false;
    }
    static const int edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7} };
    for (int e = 0; e < 12; e++) {
        const ImVec2& a = c[edges[e][0]];
        const ImVec2& b = c[edges[e][1]];
        ImU32 cc = col;
        if (gradient) {
            const float y0 = ImMin(a.y, b.y);
            const float y1 = ImMax(a.y, b.y);
            const float t = (y1 - y0) > 0.001f ? ((a.y + b.y) * 0.5f - y0) / (y1 - y0) : 0.0f;
            cc = mix_u32(col, col2, t);
        }
        dl->AddLine(a, b, cc, thickness);
    }
    return true;
}

bool screen_aabb(const RBX::Mat4& view, const RBX::Vec3& bmin, const RBX::Vec3& bmax,
                 ImVec2& tl, ImVec2& br) {
    ImVec2 smn(1e9f, 1e9f), smx(-1e9f, -1e9f);
    int n = 0;
    for (int i = 0; i < 8; i++) {
        const RBX::Vec3 w{ (i & 4) ? bmax.X : bmin.X, (i & 2) ? bmax.Y : bmin.Y, (i & 1) ? bmax.Z : bmin.Z };
        ImVec2 s;
        if (!to_client(view, w, s))
            continue;
        smn.x = ImMin(smn.x, s.x); smn.y = ImMin(smn.y, s.y);
        smx.x = ImMax(smx.x, s.x); smx.y = ImMax(smx.y, s.y);
        ++n;
    }
    if (n < 2)
        return false;
    tl = smn; br = smx;
    return true;
}

void chams_parts(ImDrawList* dl, const RBX::Mat4& view, const RBX::Vec3& bmin, const RBX::Vec3& bmax,
                 ImU32 fill_col, bool outline, ImU32 out_col) {
    ImVec2 tl, br;
    if (!screen_aabb(view, bmin, bmax, tl, br))
        return;
    if (br.x - tl.x < 1.0f || br.y - tl.y < 1.0f)
        return;
    dl->AddRectFilled(tl, br, fill_col);
    if (outline)
        dl->AddRect(tl, br, out_col, 0.0f, 0, 1.0f);
}

static bool part_corners(const RBX::Mat4& view, std::uintptr_t part, ImVec2 p[8]) {
    if (!part || !is_valid_ptr(part))
        return false;
    const RBX::CFrame cfr = RBX::RbxInstance(part).GetCFrame();
    const RBX::Vec3 pos = cfr.GetPosition();
    if (ImFabs(pos.X) + ImFabs(pos.Y) + ImFabs(pos.Z) < 0.0001f)
        return false;
    const auto prim = memory->read<std::uintptr_t>(part + Offsets::BasePart::Primitive);
    if (!prim || !is_valid_ptr(prim))
        return false;
    const RBX::Vec3 sz = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Size);
    if (sz.X <= 0.05f || sz.Y <= 0.05f || sz.Z <= 0.05f || sz.X > 50.0f || sz.Y > 50.0f || sz.Z > 50.0f)
        return false;
    const RBX::Vec3 R = cfr.GetRightVector();
    const RBX::Vec3 U = cfr.GetUpVector();
    const RBX::Vec3 La = cfr.GetLookVector();
    const float hx = sz.X * 0.5f, hy = sz.Y * 0.5f, hz = sz.Z * 0.5f;
    for (int i = 0; i < 8; i++) {
        const float sx = (i & 4) ? hx : -hx;
        const float sy = (i & 2) ? hy : -hy;
        const float sz2 = (i & 1) ? hz : -hz;
        const RBX::Vec3 w{ pos.X + R.X * sx + U.X * sy + La.X * sz2,
                           pos.Y + R.Y * sx + U.Y * sy + La.Y * sz2,
                           pos.Z + R.Z * sx + U.Z * sy + La.Z * sz2 };
        if (!to_client(view, w, p[i]))
            return false;
    }
    return true;
}

static void gather_part_addrs(const PlayerCache::LimbAddrs& L, std::uintptr_t out[22]) {
    out[0] = L.head; out[1] = L.hrp; out[2] = L.torso; out[3] = L.upperTorso; out[4] = L.lowerTorso;
    out[5] = L.lUpperArm; out[6] = L.lLowerArm; out[7] = L.lHand;
    out[8] = L.rUpperArm; out[9] = L.rLowerArm; out[10] = L.rHand;
    out[11] = L.lUpperLeg; out[12] = L.lLowerLeg; out[13] = L.lFoot;
    out[14] = L.rUpperLeg; out[15] = L.rLowerLeg; out[16] = L.rFoot;
    out[17] = L.lArm; out[18] = L.rArm; out[19] = L.lLeg; out[20] = L.rLeg;
    out[21] = 0;
}

static bool part_world_aabb(std::uintptr_t part, RBX::Vec3& wmin, RBX::Vec3& wmax) {
    if (!part || !is_valid_ptr(part))
        return false;
    const RBX::CFrame cfr = RBX::RbxInstance(part).GetCFrame();
    const RBX::Vec3 pos = cfr.GetPosition();
    if (ImFabs(pos.X) + ImFabs(pos.Y) + ImFabs(pos.Z) < 0.0001f)
        return false;
    const auto prim = memory->read<std::uintptr_t>(part + Offsets::BasePart::Primitive);
    if (!prim || !is_valid_ptr(prim))
        return false;
    const RBX::Vec3 sz = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Size);
    if (sz.X <= 0.05f || sz.Y <= 0.05f || sz.Z <= 0.05f || sz.X > 50.0f || sz.Y > 50.0f || sz.Z > 50.0f)
        return false;
    const RBX::Vec3 R = cfr.GetRightVector();
    const RBX::Vec3 U = cfr.GetUpVector();
    const RBX::Vec3 La = cfr.GetLookVector();
    const float hx = sz.X * 0.5f, hy = sz.Y * 0.5f, hz = sz.Z * 0.5f;
    for (int i = 0; i < 8; i++) {
        const float sx = (i & 4) ? hx : -hx;
        const float sy = (i & 2) ? hy : -hy;
        const float sz2 = (i & 1) ? hz : -hz;
        const RBX::Vec3 w{ pos.X + R.X * sx + U.X * sy + La.X * sz2,
                           pos.Y + R.Y * sx + U.Y * sy + La.Y * sz2,
                           pos.Z + R.Z * sx + U.Z * sy + La.Z * sz2 };
        wmin.X = (std::min)(wmin.X, w.X); wmin.Y = (std::min)(wmin.Y, w.Y); wmin.Z = (std::min)(wmin.Z, w.Z);
        wmax.X = (std::max)(wmax.X, w.X); wmax.Y = (std::max)(wmax.Y, w.Y); wmax.Z = (std::max)(wmax.Z, w.Z);
    }
    return true;
}

bool esp_box_bounds(const RBX::Mat4& view, std::uintptr_t character, bool parts_mode,
                    ImVec2& tl, ImVec2& br, RBX::Vec3& wmin, RBX::Vec3& wmax) {
    if (!character || !is_valid_ptr(character))
        return false;
    wmin = RBX::Vec3{ 1e9f, 1e9f, 1e9f };
    wmax = RBX::Vec3{ -1e9f, -1e9f, -1e9f };
    ImVec2 smn{ 1e9f, 1e9f }, smx{ -1e9f, -1e9f };
    bool any = false;

    if (parts_mode) {
        const auto& L = PlayerCache::GetLimbs(character);
        std::uintptr_t parts[22];
        gather_part_addrs(L, parts);
        for (const std::uintptr_t part : parts) {
            if (!part || part == L.hrp)
                continue;
            ImVec2 c[8];
            if (part_corners(view, part, c)) {
                for (int i = 0; i < 8; i++) {
                    smn.x = ImMin(smn.x, c[i].x);
                    smn.y = ImMin(smn.y, c[i].y);
                    smx.x = ImMax(smx.x, c[i].x);
                    smx.y = ImMax(smx.y, c[i].y);
                    any = true;
                }
                part_world_aabb(part, wmin, wmax);
            }
        }
    } else {
        const auto children = RBX::RbxInstance(character).GetChildList();
        for (const auto& ch : children) {
            if (!ch.Addr || !is_valid_ptr(ch.Addr))
                continue;
            const std::string cls = ch.GetClass();
            if (cls != "Part" && cls != "MeshPart" && cls != "WedgePart" && cls != "TrussPart")
                continue;
            if (ch.GetName() == "HumanoidRootPart")
                continue;
            ImVec2 c[8];
            if (part_corners(view, ch.Addr, c)) {
                for (int i = 0; i < 8; i++) {
                    smn.x = ImMin(smn.x, c[i].x);
                    smn.y = ImMin(smn.y, c[i].y);
                    smx.x = ImMax(smx.x, c[i].x);
                    smx.y = ImMax(smx.y, c[i].y);
                    any = true;
                }
                part_world_aabb(ch.Addr, wmin, wmax);
            }
        }
    }

    if (!any || smx.x <= smn.x || smx.y <= smn.y)
        return false;

    tl = smn;
    br = smx;
    return true;
}

void chams_part_boxes(ImDrawList* dl, const RBX::Mat4& view, std::uintptr_t character,
                      ImU32 fill_col, bool outline, ImU32 out_col) {
    const auto& L = PlayerCache::GetLimbs(character);
    std::uintptr_t parts[22];
    gather_part_addrs(L, parts);
    ImVec2 p[8];
    for (const std::uintptr_t part : parts) {
        if (!part)
            continue;
        if (!part_corners(view, part, p))
            continue;
        ImVec2 mn = p[0], mx = p[0];
        for (int i = 1; i < 8; i++) {
            mn.x = ImMin(mn.x, p[i].x); mn.y = ImMin(mn.y, p[i].y);
            mx.x = ImMax(mx.x, p[i].x); mx.y = ImMax(mx.y, p[i].y);
        }
        if (mx.x - mn.x < 1.0f || mx.y - mn.y < 1.0f)
            continue;
        dl->AddRectFilled(mn, mx, fill_col);
        if (outline)
            dl->AddRect(mn, mx, out_col, 0.0f, 0, 1.0f);
    }
}

// ── Bullet Tracers & Aim Structs ───────────────────────────────────────────
struct BulletTracer {
    ImVec2 start;
    ImVec2 end;
    float born;
    float lifetime;
    ImU32 col;
    int style;
    float thickness;
};
static std::vector<BulletTracer> g_bullet_tracers;

// ── aim asistido (cursor) ───────────────────────────────────────────────────
static ImVec2 g_aim_point{ -1.f, -1.f };
static bool   g_aim_have = false;

void run_aimbot() {
    SilentAim::TickMethod(vars.aimbot.pf_silent_method);
    g_aim_have = false;
    egui::aim_lock_active = false;
    if (!vars.aimbot.enabled && !vars.aimbot.pf_silent_enabled) {
        SilentAim::Drive(vars.aimbot.pf_silent_method, paidm::Vector3(), false, false);
        return;
    }
    if (egui::settings.state.menu_open) {
        SilentAim::Drive(vars.aimbot.pf_silent_method, paidm::Vector3(), false, false);
        return;
    }

    const bool aim_on = vars.aimbot.enabled && vars.aimbot.aim_key_active;

    // silent key: 0 = same as the aimbot key (paid silent_bind)
    bool silent_on = vars.aimbot.pf_silent_enabled;
    if (silent_on) {
        const int sk = vars.aimbot.pf_silent_key;
        const int sm = vars.aimbot.pf_silent_key_mode;
        if (sk == 0) {
            silent_on = aim_on;
        } else if (sm == 2) {
            static bool s_tog = false, s_prev = false;
            const bool s_dn = (GetAsyncKeyState(sk) & 0x8000) != 0;
            if (s_dn && !s_prev) s_tog = !s_tog;
            s_prev = s_dn;
            silent_on = s_tog;
        } else if (sm == 1) {
            silent_on = (GetAsyncKeyState(sk) & 0x8000) != 0;
        }
    }

    if (!aim_on && !silent_on) {
        SilentAim::Drive(vars.aimbot.pf_silent_method, paidm::Vector3(), false, false);
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const RBX::Mat4 view = Globals::renderEngine.GetViewMat();
    const float ccx = io.DisplaySize.x * 0.5f;
    const float ccy = io.DisplaySize.y * 0.5f;

    float bestScore = FLT_MAX;
    ImVec2 tgtScreen{};
    RBX::Vec3 aimWorld{};
    bool have = false;
    if (aim_on)
    for (const auto& p : PlayerCache::players) {
        if (!p.isValid || p.playerAddr == Globals::localPlayer.Addr)
            continue;
        if (vars.aimbot.max_distance > 0.0f && p.distance > vars.aimbot.max_distance)
            continue;
        if (vars.aimbot.teamcheck && p.teamAddr && PlayerCache::localPlayerTeam &&
            p.teamAddr == PlayerCache::localPlayerTeam)
            continue;
        RBX::Vec3 aim = p.position;
        if (vars.aimbot.aim_at_head && p.headAddr)
            aim = RBX::RbxInstance(p.headAddr).GetPos();
        ImVec2 s;
        if (!to_client(view, aim, s))
            continue;
        const float score = ImSqrt((s.x - ccx) * (s.x - ccx) + (s.y - ccy) * (s.y - ccy));
        if (vars.aimbot.fov_check && score > vars.aimbot.fov_radius)
            continue;
        if (score < bestScore) {
            bestScore = score;
            tgtScreen = s;
            aimWorld = aim;
            egui::aim_lock_name      = p.name;
            egui::aim_lock_health    = p.health;
            egui::aim_lock_max_health = p.maxHealth;
            egui::aim_lock_distance  = p.distance;
            have = true;
        }
    }
    if (aim_on && !have) {
        if (!silent_on) {
            SilentAim::Drive(vars.aimbot.pf_silent_method, paidm::Vector3(), false, false);
            return;
        }
    }

    float smooth = ImMax(vars.aimbot.smoothness, 0.5f);

    if (aim_on && have) {
    if (vars.aimbot.aim_method == 1) {
        if (!Globals::camera.Addr)
            return;
        const RBX::Vec3 cam = cam_position();
        RBX::Vec3 want{ aimWorld.X - cam.X, aimWorld.Y - cam.Y, aimWorld.Z - cam.Z };
        const float wl = ImSqrt(want.X * want.X + want.Y * want.Y + want.Z * want.Z);
        if (wl < 1e-4f)
            return;
        want.X /= wl; want.Y /= wl; want.Z /= wl;

        RBX::Vec3 curLook{ 0.0f, 0.0f, -1.0f };
        {
            const RBX::CFrame cc = memory->read<RBX::CFrame>(Globals::camera.Addr + Offsets::Camera::CFrame);
            const RBX::Vec3 cl = cc.GetLookVector();
            const float cll = ImSqrt(cl.X * cl.X + cl.Y * cl.Y + cl.Z * cl.Z);
            if (cll > 1e-4f)
                curLook = RBX::Vec3{ cl.X / cll, cl.Y / cll, cl.Z / cll };
        }

        const float tx = 1.0f / smooth;
        RBX::Vec3 look{ curLook.X + (want.X - curLook.X) * tx,
                        curLook.Y + (want.Y - curLook.Y) * tx,
                        curLook.Z + (want.Z - curLook.Z) * tx };
        const float ll = ImSqrt(look.X * look.X + look.Y * look.Y + look.Z * look.Z);
        if (ll < 1e-4f)
            return;
        look.X /= ll; look.Y /= ll; look.Z /= ll;

        const float lr = ImSqrt(look.X * look.X + look.Z * look.Z);
        const RBX::Vec3 right = (lr < 1e-4f) ? RBX::Vec3{ 1.0f, 0.0f, 0.0f }
                                             : RBX::Vec3{ -look.Z / lr, 0.0f, look.X / lr };
        const RBX::Vec3 up{
            right.Y * look.Z - right.Z * look.Y,
            right.Z * look.X - right.X * look.Z,
            right.X * look.Y - right.Y * look.X
        };
        float rot[9] = {
            right.X, up.X, -look.X,
            right.Y, up.Y, -look.Y,
            right.Z, up.Z, -look.Z
        };
        memory->write_raw(Globals::camera.Addr + Offsets::Camera::CFrame, rot, sizeof(rot));
        g_aim_point = tgtScreen;
        g_aim_have = true;
        egui::aim_lock_active = true;
    }
    }

    if (silent_on) {
        float silentBestScore = FLT_MAX;
        ImVec2 silentScreen{};
        RBX::Vec3 silentWorld{};
        bool haveSilent = false;
        for (const auto& p : PlayerCache::players) {
            if (!p.isValid || p.playerAddr == Globals::localPlayer.Addr)
                continue;
            if (vars.aimbot.max_distance > 0.0f && p.distance > vars.aimbot.max_distance)
                continue;
            if (vars.aimbot.teamcheck && p.teamAddr && PlayerCache::localPlayerTeam &&
                p.teamAddr == PlayerCache::localPlayerTeam)
                continue;
            RBX::Vec3 aim = p.position;
            if (vars.aimbot.aim_at_head && p.headAddr)
                aim = RBX::RbxInstance(p.headAddr).GetPos();
            if (vars.aimbot.pf_silent_prediction && p.rootPartAddr) {
                const auto prim = memory->read<std::uintptr_t>(p.rootPartAddr + Offsets::BasePart::Primitive);
                if (prim && is_valid_ptr(prim)) {
                    const RBX::Vec3 vel = memory->read<RBX::Vec3>(prim + Offsets::Primitive::AssemblyLinearVelocity);
                    const float pred = vars.aimbot.pf_silent_prediction_amount * 0.05f;
                    aim.X += vel.X * pred;
                    aim.Y += vel.Y * pred;
                    aim.Z += vel.Z * pred;
                }
            }
            ImVec2 s;
            if (!to_client(view, aim, s))
                continue;
            const float score = ImSqrt((s.x - ccx) * (s.x - ccx) + (s.y - ccy) * (s.y - ccy));
            if (vars.aimbot.fov_check && score > vars.aimbot.fov_radius)
                continue;
            if (score < silentBestScore) {
                silentBestScore = score;
                silentScreen = s;
                silentWorld = aim;
                haveSilent = true;
            }
        }
        if (haveSilent) {
            g_aim_point = silentScreen;
            g_aim_have = true;
            egui::aim_lock_active = true;

            if (vars.aimbot.pf_silent_auto_shoot) {
                static auto last_shoot = std::chrono::steady_clock::now();
                const auto now_shoot = std::chrono::steady_clock::now();
                if (now_shoot - last_shoot > std::chrono::milliseconds(120)) {
                    last_shoot = now_shoot;
                    ::mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    ::mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                }
            }

            const int method = vars.aimbot.pf_silent_method;
            bool force_mb = vars.aimbot.pf_silent_mb_force || method == 3;
            const int fk = vars.aimbot.pf_force_magic_key;
            const int fm = vars.aimbot.pf_force_magic_mode;
            if (fm == 0) {
                force_mb = true;
                vars.aimbot.pf_force_magic_active = true;
            } else if (fk != 0) {
                const bool fdown = (GetAsyncKeyState(fk) & 0x8000) != 0;
                if (fm == 2) {
                    static bool s_fmb = false, s_fmb_prev = false;
                    if (fdown && !s_fmb_prev) s_fmb = !s_fmb;
                    s_fmb_prev = fdown;
                    force_mb = s_fmb;
                } else {
                    force_mb = fdown;
                }
                vars.aimbot.pf_force_magic_active = force_mb;
            } else {
                vars.aimbot.pf_force_magic_active = vars.aimbot.pf_silent_mb_force || method == 3;
            }

            SilentAim::Drive(method,
                             paidm::Vector3(silentWorld.X, silentWorld.Y, silentWorld.Z),
                             true, force_mb);
        } else {
            SilentAim::Drive(vars.aimbot.pf_silent_method, paidm::Vector3(), false, false);
        }
    }

    if (aim_on && have && vars.aimbot.aim_method != 1) {
    POINT cur;
    ::GetCursorPos(&cur);
    const float cxx = (float)cur.x - g_origin_x;
    const float cyy = (float)cur.y - g_origin_y;
    float dx = (tgtScreen.x - cxx) / smooth;
    float dy = (tgtScreen.y - cyy) / smooth;
    dx = ImClamp(dx, -80.0f, 80.0f);
    dy = ImClamp(dy, -80.0f, 80.0f);
    if (dx != 0.0f || dy != 0.0f) {
        static INPUT mi{};
        mi.type = INPUT_MOUSE;
        mi.mi.dx = (LONG)(dx > 0.0f ? (int)(dx + 0.5f) : (int)(dx - 0.5f));
        mi.mi.dy = (LONG)(dy > 0.0f ? (int)(dy + 0.5f) : (int)(dy - 0.5f));
        mi.mi.dwFlags = MOUSEEVENTF_MOVE;
        ::SendInput(1, &mi, sizeof(INPUT));
    }
    g_aim_point = tgtScreen;
    g_aim_have = true;
    egui::aim_lock_active = true;

    if (vars.visuals.bullet_tracers_enabled) {
        static auto last_trc = std::chrono::steady_clock::now();
        const auto trc_now = std::chrono::steady_clock::now();
        if (trc_now - last_trc > std::chrono::milliseconds(90)) {
            last_trc = trc_now;
            g_bullet_tracers.push_back(BulletTracer{
                ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.9f),
                tgtScreen,
                (float)ImGui::GetTime(),
                vars.visuals.bullet_tracer_lifetime,
                esp_col(vars.visuals.bullet_tracer_color, 1.0f),
                vars.visuals.bullet_tracer_style,
                vars.visuals.bullet_tracer_thickness
            });
        }
    }
    }
}

void draw_aim_overlay() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    if (vars.aimbot.fov_circle_enabled) {
        const float r = vars.aimbot.fov_radius;
        const ImU32 col = esp_col(vars.aimbot.fov_color, 1.0f);
        const float th = 1.5f;
        if (vars.aimbot.fov_style == 0)
            dl->AddCircle(c, r, col, 64, th);
        else if (vars.aimbot.fov_style == 1) {
            const int segs = 48;
            for (int i = 0; i < segs; i += 2) {
                const float a0 = (float)i / (float)segs * IM_PI * 2.0f;
                const float a1 = (float)(i + 1) / (float)segs * IM_PI * 2.0f;
                dl->AddLine(ImVec2(c.x + cosf(a0) * r, c.y + sinf(a0) * r),
                            ImVec2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), col, th);
            }
        } else {
            for (int i = 0; i < 60; i++) {
                const float a = (float)i / 60.0f * IM_PI * 2.0f;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r), th * 0.8f, col, 8);
            }
        }
        if (vars.aimbot.fov_filled)
            dl->AddCircleFilled(c, r, esp_col(vars.aimbot.fov_color, 0.10f), 64);
    }

    if (vars.aimbot.crosshair_enabled) {
        const float s = vars.aimbot.crosshair_size;
        const float g = 2.0f;
        const ImU32 col = esp_col(vars.aimbot.crosshair_color, 1.0f);
        dl->AddLine(c + ImVec2(-g - s, 0), c + ImVec2(-g, 0), col, 1.5f);
        dl->AddLine(c + ImVec2(g, 0), c + ImVec2(g + s, 0), col, 1.5f);
        dl->AddLine(c + ImVec2(0, -g - s), c + ImVec2(0, -g), col, 1.5f);
        dl->AddLine(c + ImVec2(0, g), c + ImVec2(0, g + s), col, 1.5f);
    }

    if (g_aim_have) {
        if (vars.aimbot.show_target_indicator) {
            dl->AddCircleFilled(g_aim_point, 4.0f, esp_col(vars.aimbot.target_line_color, 1.0f), 16);
            dl->AddCircle(g_aim_point, 8.0f, esp_col(vars.aimbot.target_line_color, 1.0f), 24, 1.2f);
        }
        if (vars.aimbot.draw_line_to_target)
            dl->AddLine(c, g_aim_point, esp_col(vars.aimbot.target_line_color, 1.0f), 1.0f);
    }
}

// ── Visuals / Effects Structures & Handlers ─────────────────────────────────
struct HitmarkerEntry {
    ImVec2 pos;
    float born;
    float duration;
    float damage;
    bool headshot;
    int style;
};
static std::vector<HitmarkerEntry> g_hitmarkers;

struct WorldParticle {
    ImVec2 pos;
    ImVec2 vel;
    float size;
    float alpha;
    float born;
    float life;
};
static std::vector<WorldParticle> g_world_particles;

struct KillParticle {
    RBX::Vec3 pos;
    RBX::Vec3 vel;
    float born;
    float life;
    ImU32 col;
    float size;
};
static std::vector<KillParticle> g_kill_particles;

void trigger_hitsound() {
    if (!vars.visuals.hitsound_enabled)
        return;
    // Fast procedural click / tone sound
    ::MessageBeep(MB_OK);
}

void spawn_kill_effect(const RBX::Vec3& pos) {
    if (!vars.misc.kill_effects_enabled)
        return;
    const float now = (float)ImGui::GetTime();
    const int count = ImClamp(vars.misc.kill_effects_particle_count, 10, 300);
    const float lifetime = vars.misc.kill_effects_lifetime;

    for (int i = 0; i < count; ++i) {
        KillParticle kp{};
        kp.pos = pos;
        float vx = ((float)(rand() % 200) - 100.0f) * 0.15f;
        float vy = ((float)(rand() % 200) - 40.0f) * 0.15f;
        float vz = ((float)(rand() % 200) - 100.0f) * 0.15f;
        kp.vel = RBX::Vec3{ vx, vy, vz };
        kp.born = now;
        kp.life = lifetime * (0.5f + ((float)(rand() % 100) / 100.0f) * 0.5f);
        kp.size = 2.0f + (float)(rand() % 4);

        if (vars.misc.kill_effects_type == 1) { // Confetti
            const ImU32 cols[] = { IM_COL32(255, 60, 60, 255), IM_COL32(60, 255, 60, 255), IM_COL32(60, 160, 255, 255), IM_COL32(255, 220, 60, 255), IM_COL32(230, 60, 255, 255) };
            kp.col = cols[rand() % 5];
        } else if (vars.misc.kill_effects_type == 2) { // Sparks
            kp.col = IM_COL32(255, 200 + rand() % 55, 50, 255);
        } else if (vars.misc.kill_effects_type == 3) { // Smoke
            const int g = 180 + rand() % 60;
            kp.col = IM_COL32(g, g, g, 180);
            kp.size *= 2.5f;
        } else { // Explosion
            kp.col = (rand() % 2 == 0) ? IM_COL32(255, 100, 30, 255) : IM_COL32(255, 220, 50, 255);
        }
        g_kill_particles.push_back(kp);
    }
    if (g_kill_particles.size() > 1000)
        g_kill_particles.erase(g_kill_particles.begin(), g_kill_particles.begin() + 300);
}

void draw_visuals_overlays(const RBX::Mat4& view) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float now = (float)ImGui::GetTime();

    // ── Bullet Tracers ──
    if (vars.visuals.bullet_tracers_enabled && !g_bullet_tracers.empty()) {
        g_bullet_tracers.erase(std::remove_if(g_bullet_tracers.begin(), g_bullet_tracers.end(),
            [&](const BulletTracer& t) { return now - t.born >= t.lifetime; }), g_bullet_tracers.end());

        for (const auto& t : g_bullet_tracers) {
            const float age = now - t.born;
            const float alpha = 1.0f - (age / ImMax(t.lifetime, 0.001f));
            const ImVec4 base = ImGui::ColorConvertU32ToFloat4(t.col);
            const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * alpha));

            if (t.style == 1) { // Beam
                dl->AddLine(t.start, t.end, ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * alpha * 0.35f)), t.thickness * 2.5f);
                dl->AddLine(t.start, t.end, col, t.thickness);
            } else if (t.style == 2) { // Laser
                dl->AddLine(t.start, t.end, IM_COL32(255, 255, 255, (int)(255 * alpha)), 1.5f);
                dl->AddLine(t.start, t.end, col, t.thickness * 1.5f);
            } else { // Standard Line
                dl->AddLine(t.start, t.end, col, t.thickness);
            }
        }
    }

    // ── Hitmarkers ──
    if (vars.visuals.hitmarker_enabled && !g_hitmarkers.empty()) {
        g_hitmarkers.erase(std::remove_if(g_hitmarkers.begin(), g_hitmarkers.end(),
            [&](const HitmarkerEntry& h) { return now - h.born >= h.duration; }), g_hitmarkers.end());

        ImFont* f_small = fnt(efonts.inter.Text10);

        for (const auto& h : g_hitmarkers) {
            const float age = now - h.born;
            const float a = 1.0f - (age / ImMax(h.duration, 0.001f));
            const float* src_col = h.headshot ? vars.visuals.hitmarker_headshot_color : vars.visuals.hitmarker_color;
            const ImU32 col = esp_col(src_col, a);
            const float sz = vars.visuals.hitmarker_size;
            const float th = vars.visuals.hitmarker_thickness;
            const ImVec2 c = h.pos;

            if (h.style == 1) { // Circle
                dl->AddCircle(c, sz * 0.5f, col, 24, th);
                dl->AddLine(c + ImVec2(-sz * 0.7f, 0), c + ImVec2(-sz * 0.2f, 0), col, th);
                dl->AddLine(c + ImVec2(sz * 0.2f, 0), c + ImVec2(sz * 0.7f, 0), col, th);
                dl->AddLine(c + ImVec2(0, -sz * 0.7f), c + ImVec2(0, -sz * 0.2f), col, th);
                dl->AddLine(c + ImVec2(0, sz * 0.2f), c + ImVec2(0, sz * 0.7f), col, th);
            } else if (h.style == 2) { // Dots
                const float g = sz * 0.4f;
                dl->AddCircleFilled(c + ImVec2(-g, -g), th, col, 8);
                dl->AddCircleFilled(c + ImVec2(g, -g), th, col, 8);
                dl->AddCircleFilled(c + ImVec2(-g, g), th, col, 8);
                dl->AddCircleFilled(c + ImVec2(g, g), th, col, 8);
            } else { // Cross
                const float gap = sz * 0.25f;
                dl->AddLine(c + ImVec2(-sz, -sz), c + ImVec2(-gap, -gap), col, th);
                dl->AddLine(c + ImVec2(sz, -sz), c + ImVec2(gap, -gap), col, th);
                dl->AddLine(c + ImVec2(-sz, sz), c + ImVec2(-gap, gap), col, th);
                dl->AddLine(c + ImVec2(sz, sz), c + ImVec2(gap, gap), col, th);
            }

            if (vars.visuals.hitmarker_show_damage && h.damage > 0.0f) {
                char dbuf[16];
                snprintf(dbuf, sizeof(dbuf), "-%.0f", h.damage);
                dl->AddText(f_small, fsize(f_small), c + ImVec2(sz * 0.6f, -sz * 0.6f - age * 25.0f), col, dbuf);
            }
        }
    }

    // ── Kill Effects Particles ──
    if (vars.misc.kill_effects_enabled && !g_kill_particles.empty()) {
        const float dt = ImMax(io.DeltaTime, 0.001f);
        g_kill_particles.erase(std::remove_if(g_kill_particles.begin(), g_kill_particles.end(),
            [&](const KillParticle& kp) { return now - kp.born >= kp.life; }), g_kill_particles.end());

        for (auto& kp : g_kill_particles) {
            kp.pos.X += kp.vel.X * dt;
            kp.pos.Y += kp.vel.Y * dt;
            kp.pos.Z += kp.vel.Z * dt;
            kp.vel.Y -= 9.8f * dt; // gravity

            ImVec2 sp;
            if (to_client(view, kp.pos, sp)) {
                const float age = now - kp.born;
                const float a = 1.0f - (age / ImMax(kp.life, 0.001f));
                const ImVec4 base = ImGui::ColorConvertU32ToFloat4(kp.col);
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * a));
                dl->AddCircleFilled(sp, kp.size * a, col, 8);
                if (vars.misc.kill_effects_glow)
                    dl->AddCircle(sp, kp.size * a + 2.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * a * 0.35f)), 8, 1.0f);
            }
        }
    }

    // ── World Particles (Snow, Rain, Ash, Fireflies) ──
    if (vars.world.particles_enabled) {
        const int want = ImClamp(vars.world.particle_count, 10, 800);
        const float fw = io.DisplaySize.x;
        const float fh = io.DisplaySize.y;
        const float spd = vars.world.particle_speed;
        const float wind = vars.world.particle_wind;

        while ((int)g_world_particles.size() < want) {
            WorldParticle wp{};
            wp.pos = ImVec2((float)(rand() % (int)ImMax(fw, 1.0f)), (float)(rand() % (int)ImMax(fh, 1.0f)));
            wp.size = 1.5f + (float)(rand() % 3);
            wp.alpha = 0.3f + ((float)(rand() % 70) / 100.0f);
            wp.born = now;
            wp.life = 4.0f + (float)(rand() % 4);

            if (vars.world.particle_style == 1) { // Rain
                wp.vel = ImVec2(wind * 10.0f, 400.0f * spd);
                wp.size = 1.2f;
            } else if (vars.world.particle_style == 2) { // Ash
                wp.vel = ImVec2(((float)(rand() % 40) - 20.0f) + wind * 8.0f, 40.0f * spd);
            } else if (vars.world.particle_style == 3) { // Fireflies
                wp.vel = ImVec2(((float)(rand() % 60) - 30.0f) + wind * 4.0f, ((float)(rand() % 60) - 30.0f) * spd);
            } else { // Snow
                wp.vel = ImVec2(((float)(rand() % 30) - 15.0f) + wind * 15.0f, (30.0f + (float)(rand() % 40)) * spd);
            }
            g_world_particles.push_back(wp);
        }

        const float dt = ImMax(io.DeltaTime, 0.001f);
        for (auto& wp : g_world_particles) {
            wp.pos.x += wp.vel.x * dt;
            wp.pos.y += wp.vel.y * dt;

            if (wp.pos.x < 0.0f) wp.pos.x = fw;
            if (wp.pos.x > fw) wp.pos.x = 0.0f;
            if (wp.pos.y > fh) wp.pos.y = 0.0f;
            if (wp.pos.y < 0.0f) wp.pos.y = fh;

            if (vars.world.particle_style == 1) { // Rain streaks
                dl->AddLine(wp.pos, wp.pos + ImVec2(wp.vel.x * 0.03f, 8.0f * spd), IM_COL32(180, 210, 255, (int)(180 * wp.alpha)), 1.0f);
            } else if (vars.world.particle_style == 3) { // Fireflies
                const float pulse = 0.5f + 0.5f * sinf(now * 3.0f + wp.born);
                const ImU32 col = IM_COL32(230, 255, 120, (int)(220 * wp.alpha * pulse));
                dl->AddCircleFilled(wp.pos, wp.size * vars.world.particle_glow, col, 8);
            } else if (vars.world.particle_style == 2) { // Ash
                dl->AddCircleFilled(wp.pos, wp.size, IM_COL32(160, 160, 160, (int)(170 * wp.alpha)), 6);
            } else { // Snow
                dl->AddCircleFilled(wp.pos, wp.size, IM_COL32(255, 255, 255, (int)(200 * wp.alpha)), 8);
            }
        }
    } else {
        g_world_particles.clear();
    }
}

// ── Movement / World / Hitbox Feature Handlers ───────────────────────────────
void run_movement_features() {
    if (!Globals::localPlayer.Addr)
        return;
    const auto charRef = Globals::localPlayer.GetModelRef();
    if (!charRef.Addr)
        return;
    const auto& limbs = PlayerCache::GetLimbs(charRef.Addr);

    // ── Fly ──
    static bool s_fly_active = false;
    static bool s_gravity_active = false;
    static bool s_fly_noclip_active = false;
    static float s_gravity_backup = 196.2f;
    auto set_gravity = [](float g) {
        if (!Globals::workspace.Addr)
            return;
        const auto world = memory->read<std::uintptr_t>(Globals::workspace.Addr + Offsets::Workspace::World);
        if (world && is_valid_ptr(world))
            memory->write<float>(world + Offsets::World::Gravity, g);
    };
    auto apply_fly_noclip = [&](bool on) {
        if (!charRef.Addr)
            return;
        std::uintptr_t parts[22];
        gather_part_addrs(limbs, parts);
        const auto can_collide = Offsets::PrimitiveFlags::CanCollide;
        for (std::uintptr_t p : parts) {
            if (!p || !is_valid_ptr(p)) continue;
            const auto prim = memory->read<std::uintptr_t>(p + Offsets::BasePart::Primitive);
            if (prim && is_valid_ptr(prim)) {
                auto f = memory->read<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags);
                if (on) f &= ~can_collide; else f |= can_collide;
                memory->write<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags, f);
            }
        }
    };
    if (vars.misc.pf_fly && limbs.hrp) {
        const auto hrpPrim = memory->read<std::uintptr_t>(limbs.hrp + Offsets::BasePart::Primitive);
        if (hrpPrim && is_valid_ptr(hrpPrim)) {
            static RBX::Vec3 s_fly_smooth{ 0.0f, 0.0f, 0.0f };
            RBX::Vec3 vel{ 0.0f, 0.0f, 0.0f };
            const bool moving = !egui::settings.state.menu_open;
            if (moving) {
                const auto camCFrame = Globals::camera.Addr
                    ? memory->read<RBX::CFrame>(Globals::camera.Addr + Offsets::Camera::CFrame)
                    : RBX::CFrame{};
                const RBX::Vec3 forward{ -camCFrame.data[2], -camCFrame.data[5], -camCFrame.data[8] };
                const RBX::Vec3 right{ camCFrame.data[0], camCFrame.data[3], camCFrame.data[6] };
                const float speed = vars.misc.pf_fly_speed * 1.8f;

                if (GetAsyncKeyState('W') & 0x8000) { vel.X += forward.X * speed; vel.Y += forward.Y * speed; vel.Z += forward.Z * speed; }
                if (GetAsyncKeyState('S') & 0x8000) { vel.X -= forward.X * speed; vel.Y -= forward.Y * speed; vel.Z -= forward.Z * speed; }
                if (GetAsyncKeyState('D') & 0x8000) { vel.X += right.X * speed; vel.Z += right.Z * speed; }
                if (GetAsyncKeyState('A') & 0x8000) { vel.X -= right.X * speed; vel.Z -= right.Z * speed; }
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) { vel.Y += speed; }
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) { vel.Y -= speed; }
            }
            if (vars.misc.pf_fly_mode == 1) {
                static auto s_last = std::chrono::steady_clock::now();
                const auto now = std::chrono::steady_clock::now();
                float dt = std::chrono::duration<float>(now - s_last).count();
                s_last = now;
                if (dt < 0.0f) dt = 0.0f;
                if (dt > 0.1f) dt = 0.1f;
                RBX::CFrame cf = memory->read<RBX::CFrame>(hrpPrim + Offsets::Primitive::CFrame);
                cf.data[9] += vel.X * dt;
                cf.data[10] += vel.Y * dt;
                cf.data[11] += vel.Z * dt;
                memory->write<RBX::CFrame>(hrpPrim + Offsets::Primitive::CFrame, cf);
                memory->write<RBX::Vec3>(hrpPrim + Offsets::Primitive::AssemblyLinearVelocity, RBX::Vec3{ 0.0f, 0.0f, 0.0f });
            } else {
                const float k = ImClamp(ImGui::GetIO().DeltaTime * 7.0f, 0.05f, 1.0f);
                s_fly_smooth.X += (vel.X - s_fly_smooth.X) * k;
                s_fly_smooth.Y += (vel.Y - s_fly_smooth.Y) * k;
                s_fly_smooth.Z += (vel.Z - s_fly_smooth.Z) * k;
                memory->write<RBX::Vec3>(hrpPrim + Offsets::Primitive::AssemblyLinearVelocity, s_fly_smooth);
            }
            if (!s_gravity_active) {
                if (Globals::workspace.Addr) {
                    const auto world = memory->read<std::uintptr_t>(Globals::workspace.Addr + Offsets::Workspace::World);
                    if (world && is_valid_ptr(world))
                        s_gravity_backup = memory->read<float>(world + Offsets::World::Gravity);
                }
                s_gravity_active = true;
            }
            set_gravity(0.0f);
            if (vars.misc.pf_fly_mode == 2) {
                if (!s_fly_noclip_active) {
                    apply_fly_noclip(true);
                    s_fly_noclip_active = true;
                }
            } else if (s_fly_noclip_active) {
                apply_fly_noclip(false);
                s_fly_noclip_active = false;
            }
            s_fly_active = true;
        }
    } else if (s_fly_active) {
        if (s_gravity_active) {
            set_gravity(s_gravity_backup);
            s_gravity_active = false;
        }
        if (s_fly_noclip_active) {
            apply_fly_noclip(false);
            s_fly_noclip_active = false;
        }
        if (limbs.hrp) {
            const auto hrpPrim = memory->read<std::uintptr_t>(limbs.hrp + Offsets::BasePart::Primitive);
            if (hrpPrim && is_valid_ptr(hrpPrim))
                memory->write<RBX::Vec3>(hrpPrim + Offsets::Primitive::AssemblyLinearVelocity, RBX::Vec3{ 0.0f, 0.0f, 0.0f });
        }
        s_fly_active = false;
    }

    // ── WalkSpeed ──
    static bool s_walkspeed_active = false;
    if (limbs.humanoid) {
        if (vars.misc.walkspeed) {
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::WalkSpeed, vars.misc.walkspeed_value);
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::WalkSpeedCheck, vars.misc.walkspeed_value);
            s_walkspeed_active = true;
        } else if (s_walkspeed_active) {
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::WalkSpeed, 16.0f);
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::WalkSpeedCheck, 16.0f);
            s_walkspeed_active = false;
        }
    }

    // ── Noclip ──
    static bool s_noclip_active = false;
    if (vars.misc.noclip && charRef.Addr) {
        std::uintptr_t parts[22];
        gather_part_addrs(limbs, parts);
        for (std::uintptr_t p : parts) {
            if (!p || !is_valid_ptr(p)) continue;
            const auto prim = memory->read<std::uintptr_t>(p + Offsets::BasePart::Primitive);
            if (prim && is_valid_ptr(prim)) {
                auto flags = memory->read<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags);
                flags &= ~Offsets::PrimitiveFlags::CanCollide;
                memory->write<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags, flags);
            }
        }
        s_noclip_active = true;
    } else if (s_noclip_active && charRef.Addr) {
        std::uintptr_t parts[22];
        gather_part_addrs(limbs, parts);
        for (std::uintptr_t p : parts) {
            if (!p || !is_valid_ptr(p)) continue;
            const auto prim = memory->read<std::uintptr_t>(p + Offsets::BasePart::Primitive);
            if (prim && is_valid_ptr(prim)) {
                auto flags = memory->read<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags);
                flags |= Offsets::PrimitiveFlags::CanCollide;
                memory->write<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags, flags);
            }
        }
        s_noclip_active = false;
    }

    // ── Jump Power ──
    static bool s_jump_active = false;
    if (limbs.humanoid) {
        if (vars.misc.jump_power) {
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::JumpPower, vars.misc.jump_power_value);
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::JumpHeight, vars.misc.jump_power_value * 0.15f);
            memory->write<std::uint8_t>(limbs.humanoid + Offsets::Humanoid::UseJumpPower, 1);
            s_jump_active = true;
        } else if (s_jump_active) {
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::JumpPower, 50.0f);
            memory->write<float>(limbs.humanoid + Offsets::Humanoid::JumpHeight, 7.2f);
            s_jump_active = false;
        }
    }

    // ── Third Person ──
    static bool s_third_person_active = false;
    static bool s_tp_saved = false;
    static RBX::Vec3 s_tp_orig{ 0.0f, 0.0f, 0.0f };
    static std::uintptr_t s_tp_hum = 0;
    if (vars.misc.third_person) {
        if (limbs.humanoid) {
            if (!s_third_person_active || s_tp_hum != limbs.humanoid) {
                s_tp_saved = true;
                s_tp_orig = memory->read<RBX::Vec3>(limbs.humanoid + Offsets::Humanoid::CameraOffset);
                s_tp_hum = limbs.humanoid;
            }
            float dist = vars.misc.third_person_distance;
            if (dist < 0.5f) dist = 0.5f;
            if (dist > 120.f) dist = 120.f;
            const RBX::Vec3 camOff{ s_tp_orig.X, s_tp_orig.Y + dist, s_tp_orig.Z + dist };
            memory->write<RBX::Vec3>(limbs.humanoid + Offsets::Humanoid::CameraOffset, camOff);
            s_third_person_active = true;
        }
    } else if (s_third_person_active) {
        if (limbs.humanoid) {
            memory->write<RBX::Vec3>(limbs.humanoid + Offsets::Humanoid::CameraOffset,
                                     s_tp_saved ? s_tp_orig : RBX::Vec3{ 0.0f, 0.0f, 0.0f });
        }
        s_third_person_active = false;
        s_tp_saved = false;
        s_tp_hum = 0;
    }

    // ── Hitbox Expander ──
    static bool s_hitbox_active = false;
    static std::unordered_map<std::uintptr_t, RBX::Vec3> s_hb_orig;
    static std::unordered_set<std::uintptr_t> s_hb_active;

    auto hb_prim = [](std::uintptr_t part) -> std::uintptr_t {
        if (!part || !is_valid_ptr(part))
            return 0;
        const auto prim = memory->read<std::uintptr_t>(part + Offsets::BasePart::Primitive);
        return (prim && is_valid_ptr(prim)) ? prim : 0;
    };
    auto hb_apply = [&](const PlayerCache::CachedPlayer& p, float scale, bool no_collide,
                        bool transparent, float transp, std::unordered_set<std::uintptr_t>& touched) {
        const auto& lb = PlayerCache::GetLimbs(p.characterAddr);
        std::uintptr_t parts[22];
        gather_part_addrs(lb, parts);
        for (std::uintptr_t part : parts) {
            if (!part)
                continue;
            const auto prim = hb_prim(part);
            if (!prim)
                continue;
            const RBX::Vec3 cur = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Size);
            if (!std::isfinite(cur.X) || !std::isfinite(cur.Y) || !std::isfinite(cur.Z))
                continue;
            RBX::Vec3 orig;
            const auto it = s_hb_orig.find(part);
            if (it == s_hb_orig.end()) {
                s_hb_orig[part] = cur;
                orig = cur;
            } else {
                orig = it->second;
            }
            touched.insert(part);
            s_hb_active.insert(part);
            const RBX::Vec3 want{ orig.X * scale, orig.Y * scale, orig.Z * scale };
            memory->write<RBX::Vec3>(prim + Offsets::Primitive::Size, want);
            if (no_collide) {
                auto flags = memory->read<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags);
                flags &= ~Offsets::PrimitiveFlags::CanCollide;
                memory->write<std::uint32_t>(prim + Offsets::Primitive::PrimitiveFlags, flags);
            }
            if (transparent)
                memory->write<float>(part + Offsets::BasePart::Transparency, transp);
        }
    };
    auto hb_restore_all = [&]() {
        for (const auto& kv : s_hb_orig) {
            const auto prim = hb_prim(kv.first);
            if (prim)
                memory->write<RBX::Vec3>(prim + Offsets::Primitive::Size, kv.second);
        }
        s_hb_orig.clear();
        s_hb_active.clear();
    };

    if (vars.misc.hitbox_expander_enabled) {
        s_hitbox_active = true;
        const float scale = ImClamp(vars.misc.hitbox_expander_size, 0.1f, 50.0f);
        std::unordered_set<std::uintptr_t> touched;
        for (const auto& p : PlayerCache::players) {
            if (!p.isValid || !p.characterAddr || p.playerAddr == Globals::localPlayer.Addr)
                continue;
            if (vars.aimbot.teamcheck && p.teamAddr && PlayerCache::localPlayerTeam &&
                p.teamAddr == PlayerCache::localPlayerTeam)
                continue;
            hb_apply(p, scale, vars.misc.hitbox_expander_no_collide,
                     vars.misc.hitbox_expander_transparent, vars.misc.hitbox_expander_transparency, touched);
        }
        for (auto it = s_hb_active.begin(); it != s_hb_active.end();) {
            const std::uintptr_t part = *it;
            if (touched.count(part)) {
                ++it;
                continue;
            }
            it = s_hb_active.erase(it);
            const auto oit = s_hb_orig.find(part);
            if (oit != s_hb_orig.end()) {
                const auto prim = hb_prim(part);
                if (prim)
                    memory->write<RBX::Vec3>(prim + Offsets::Primitive::Size, oit->second);
                s_hb_orig.erase(oit);
            }
        }
    } else if (s_hitbox_active) {
        hb_restore_all();
        s_hitbox_active = false;
    }
}

void invalidate_world_lighting() {
    if (!Globals::renderEngine.Addr)
        return;
    const auto rv = memory->read<std::uintptr_t>(Globals::renderEngine.Addr + Offsets::VisualEngine::RenderView);
    if (!rv)
        return;
    memory->write<std::uint8_t>(rv + Offsets::RenderView::LightingValid, 0);
}

static bool clock_is_double(std::uintptr_t addr) {
    const float f = memory->read<float>(addr);
    if (f >= 0.f && f <= 24.f)
        return false;
    const double d = memory->read<double>(addr);
    return d >= 0.0 && d <= 24.0;
}

static float read_clock(std::uintptr_t addr, bool as_double) {
    return as_double ? static_cast<float>(memory->read<double>(addr)) : memory->read<float>(addr);
}

static void write_clock(std::uintptr_t addr, float t, bool as_double) {
    if (as_double)
        memory->write<double>(addr, static_cast<double>(t));
    else
        memory->write<float>(addr, t);
}

void run_world_features() {
    static auto lighting = Globals::dataModel.Addr ? Globals::dataModel.FindChildByClass("Lighting") : RBX::RbxInstance{};
    if (!lighting.Addr && Globals::dataModel.Addr)
        lighting = Globals::dataModel.FindChildByClass("Lighting");

    static bool s_ambient_active = false;
    static RBX::Vec3 s_amb_bak{};
    static RBX::Vec3 s_out_bak{};
    static bool s_brightness_active = false;
    static float s_bri_bak = 2.0f;
    static bool s_exposure_active = false;
    static float s_exp_bak = 0.0f;
    static bool s_clock_active = false;
    static bool s_clock_double = false;
    static float s_clk_bak = 12.0f;
    static bool s_shadows_active = false;
    static std::uint8_t s_shd_bak = 1;
    static bool s_fog_active = false;
    static RBX::Vec3 s_fog_bak{};
    static float s_fog_s_bak = 0.0f;
    static float s_fog_e_bak = 100000.0f;
    static bool s_atmos_active = false;
    static bool s_fov_active = false;
    static float s_fov_bak = 70.0f;

    bool wrote = false;

    if (lighting.Addr) {
        if (vars.world.world_ambient_enabled) {
            if (!s_ambient_active) {
                s_amb_bak = memory->read<RBX::Vec3>(lighting.Addr + Offsets::Lighting::Ambient);
                s_out_bak = memory->read<RBX::Vec3>(lighting.Addr + Offsets::Lighting::OutdoorAmbient);
                s_ambient_active = true;
            }
            const RBX::Vec3 amb{ vars.world.world_ambient_color[0], vars.world.world_ambient_color[1], vars.world.world_ambient_color[2] };
            const RBX::Vec3 outAmb{ vars.world.world_outdoor_ambient_color[0], vars.world.world_outdoor_ambient_color[1], vars.world.world_outdoor_ambient_color[2] };
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::Ambient, amb);
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::OutdoorAmbient, outAmb);
            wrote = true;
        } else if (s_ambient_active) {
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::Ambient, s_amb_bak);
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::OutdoorAmbient, s_out_bak);
            s_ambient_active = false;
            wrote = true;
        }

        if (vars.world.world_brightness_enabled) {
            if (!s_brightness_active) {
                s_bri_bak = memory->read<float>(lighting.Addr + Offsets::Lighting::Brightness);
                s_brightness_active = true;
            }
            memory->write<float>(lighting.Addr + Offsets::Lighting::Brightness, vars.world.world_brightness);
            wrote = true;
        } else if (s_brightness_active) {
            memory->write<float>(lighting.Addr + Offsets::Lighting::Brightness, s_bri_bak);
            s_brightness_active = false;
            wrote = true;
        }

        if (vars.world.world_exposure_enabled) {
            if (!s_exposure_active) {
                s_exp_bak = memory->read<float>(lighting.Addr + Offsets::Lighting::ExposureCompensation);
                s_exposure_active = true;
            }
            memory->write<float>(lighting.Addr + Offsets::Lighting::ExposureCompensation, vars.world.world_exposure);
            wrote = true;
        } else if (s_exposure_active) {
            memory->write<float>(lighting.Addr + Offsets::Lighting::ExposureCompensation, s_exp_bak);
            s_exposure_active = false;
            wrote = true;
        }

        if (vars.world.world_clocktime_enabled) {
            const auto caddr = lighting.Addr + Offsets::Lighting::ClockTime;
            if (!s_clock_active) {
                s_clock_double = clock_is_double(caddr);
                s_clk_bak = read_clock(caddr, s_clock_double);
                s_clock_active = true;
            }
            write_clock(caddr, vars.world.world_clocktime_value, s_clock_double);
            wrote = true;
        } else if (s_clock_active) {
            write_clock(lighting.Addr + Offsets::Lighting::ClockTime, s_clk_bak, s_clock_double);
            s_clock_active = false;
            wrote = true;
        }

        if (vars.world.world_shadows_disabled) {
            if (!s_shadows_active) {
                s_shd_bak = memory->read<std::uint8_t>(lighting.Addr + Offsets::Lighting::GlobalShadows);
                s_shadows_active = true;
            }
            memory->write<std::uint8_t>(lighting.Addr + Offsets::Lighting::GlobalShadows, 0);
            wrote = true;
        } else if (s_shadows_active) {
            memory->write<std::uint8_t>(lighting.Addr + Offsets::Lighting::GlobalShadows, s_shd_bak);
            s_shadows_active = false;
            wrote = true;
        }

        if (vars.world.world_fog_enabled) {
            if (!s_fog_active) {
                s_fog_bak = memory->read<RBX::Vec3>(lighting.Addr + Offsets::Lighting::FogColor);
                s_fog_s_bak = memory->read<float>(lighting.Addr + Offsets::Lighting::FogStart);
                s_fog_e_bak = memory->read<float>(lighting.Addr + Offsets::Lighting::FogEnd);
                s_fog_active = true;
            }
            const RBX::Vec3 fc{ vars.world.world_fog_color[0], vars.world.world_fog_color[1], vars.world.world_fog_color[2] };
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::FogColor, fc);
            memory->write<float>(lighting.Addr + Offsets::Lighting::FogStart, vars.world.world_fog_start);
            memory->write<float>(lighting.Addr + Offsets::Lighting::FogEnd, vars.world.world_fog_end);
            wrote = true;
        } else if (s_fog_active) {
            memory->write<RBX::Vec3>(lighting.Addr + Offsets::Lighting::FogColor, s_fog_bak);
            memory->write<float>(lighting.Addr + Offsets::Lighting::FogStart, s_fog_s_bak);
            memory->write<float>(lighting.Addr + Offsets::Lighting::FogEnd, s_fog_e_bak);
            s_fog_active = false;
            wrote = true;
        }

        if (vars.world.world_atmosphere_enabled) {
            const auto atmos = lighting.FindChildByClass("Atmosphere");
            if (atmos.Addr) {
                const RBX::Vec3 ac{ vars.world.world_atmos_color[0], vars.world.world_atmos_color[1], vars.world.world_atmos_color[2] };
                const RBX::Vec3 ad{ vars.world.world_atmos_decay[0], vars.world.world_atmos_decay[1], vars.world.world_atmos_decay[2] };
                memory->write<RBX::Vec3>(atmos.Addr + Offsets::Atmosphere::Color, ac);
                memory->write<RBX::Vec3>(atmos.Addr + Offsets::Atmosphere::Decay, ad);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Density, vars.world.world_atmos_density);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Glare, vars.world.world_atmos_glare);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Haze, vars.world.world_atmos_haze);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Offset, vars.world.world_atmos_offset);
                s_atmos_active = true;
            }
            wrote = true;
        } else if (s_atmos_active) {
            const auto atmos = lighting.FindChildByClass("Atmosphere");
            if (atmos.Addr) {
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Density, 0.3f);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Glare, 0.0f);
                memory->write<float>(atmos.Addr + Offsets::Atmosphere::Haze, 0.0f);
            }
            s_atmos_active = false;
            wrote = true;
        }

        if (wrote)
            invalidate_world_lighting();
    }

    if (Globals::camera.Addr) {
        if (vars.world.world_fov_enabled) {
            if (!s_fov_active) {
                s_fov_bak = memory->read<float>(Globals::camera.Addr + Offsets::Camera::FieldOfView);
                s_fov_active = true;
            }
            memory->write<float>(Globals::camera.Addr + Offsets::Camera::FieldOfView, vars.world.world_fov);
        } else if (s_fov_active) {
            memory->write<float>(Globals::camera.Addr + Offsets::Camera::FieldOfView, s_fov_bak);
            s_fov_active = false;
        }
    }
}

// ── radar ───────────────────────────────────────────────────────────────────
void draw_radar(ImDrawList* dl, ImGuiIO& io) {
    const float R = ImClamp(vars.esp.radar_size, 50.0f, 500.0f) * 0.5f;
    const ImVec2 rc(dpi(14.0f) + R, dpi(14.0f) + dpi(24.0f));
    const float a = vars.esp.radar_alpha;

    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.05f, 0.08f, a * 0.92f));
    const ImU32 bd = ImGui::ColorConvertFloat4ToU32(ImVec4(0.16f, 0.16f, 0.18f, a));
    const ImU32 ring = ImGui::ColorConvertFloat4ToU32(ImVec4(0.13f, 0.13f, 0.15f, a * 0.7f));

    dl->AddCircleFilled(rc, R, bg, 48);
    dl->AddCircle(rc, R, bd, 48, 1.5f);
    dl->AddCircle(rc, R * 0.5f, ring, 48, 1.0f);
    dl->AddLine(rc + ImVec2(-R, 0), rc + ImVec2(R, 0), ring, 1.0f);
    dl->AddLine(rc + ImVec2(0, -R), rc + ImVec2(0, R), ring, 1.0f);

    // El jugador local siempre mira hacia arriba: el radar rota con la camara.
    ImFont* f = fnt(efonts.inter.Text10);
    const ImU32 acc = esp_col(vars.esp.oov_arrow_color, a >= 0.99f ? 1.0f : a);
    const ImU32 name_c = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, a));
    const float scale = (R - 10.0f) / ImMax(vars.esp.radar_range, 1.0f);
    const float maxr = R - 12.0f;

    // eje de la camara en el plano XZ (forward = arriba del radar)
    const RBX::Mat4 view = Globals::renderEngine.GetViewMat();
    float lx = -view.data[2];
    float lz = -view.data[10];
    const float ll = ImMax(1e-6f, ImSqrt(lx * lx + lz * lz));
    lx /= ll; lz /= ll;
    const float rx = -lz, rz = lx;

    for (const auto& p : PlayerCache::players) {
        if (!p.isValid || !p.distance)
            continue;
        const float ddx = p.position.X - PlayerCache::localPlayerPos.X;
        const float ddz = p.position.Z - PlayerCache::localPlayerPos.Z;
        float right_c = (ddx * rx + ddz * rz) * scale;
        float fwd_c   = (ddx * lx + ddz * lz) * scale;
        if (right_c != 0.0f || fwd_c != 0.0f) {
            const float d = ImSqrt(right_c * right_c + fwd_c * fwd_c);
            if (d > maxr) {
                const float k = maxr / d;
                right_c *= k;
                fwd_c *= k;
            }
        }
        const ImVec2 pt(rc.x + right_c, rc.y - fwd_c);
        dl->AddCircleFilled(pt, 3.5f, acc, 12);
        if (vars.esp.radar_show_names)
            dl->AddText(f, fsize(f), pt + ImVec2(5.0f, -fsize(f) * 0.75f), name_c, p.name.c_str());
        if (vars.esp.radar_show_distance) {
            char bv[32];
            snprintf(bv, sizeof(bv), "%.0fm", p.distance);
            dl->AddText(f, fsize(f), pt + ImVec2(5.0f, 1.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.65f, a)), bv);
        }
    }

    dl->AddTriangleFilled(rc + ImVec2(0, -6.0f), rc + ImVec2(-4.5f, 4.0f), rc + ImVec2(4.5f, 4.0f),
                          ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, ImClamp(a + 0.1f, 0.2f, 1.0f))));
}

// ── esp ─────────────────────────────────────────────────────────────────────
void draw_esp() {
    if (!vars.esp.enabled)
        return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const RBX::Mat4 view = Globals::renderEngine.GetViewMat();
    const RBX::Vec3 camPos = cam_position();

    Mesh::Matrix4x4 mesh_view;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            mesh_view.m[i][j] = view.data[i * 4 + j];
    const bool chams_on = variables::ESP::meshChams && vars.esp.enabled;
    if (chams_on)
        MeshDxShader::BeginFrame(mesh_view, Mesh::Vector3(camPos.X, camPos.Y, camPos.Z), (float)ImGui::GetTime());

    static float s_clock = 0.0f;
    s_clock += ImMax(io.DeltaTime, 0.0f);

    static std::unordered_map<std::uintptr_t, float> last_hp;

    const float fw = io.DisplaySize.x;
    const float fh = io.DisplaySize.y;
    const float margin = dpi(24.0f);

    ImFont* f_name  = fnt(efonts.inter.SemiBold13);
    ImFont* f_small = fnt(efonts.inter.Text10);

    std::unordered_set<std::uintptr_t> alive;
    alive.reserve(PlayerCache::players.size() + 1);

    static std::vector<WorkspaceCache::CachedPart> s_ws_snapshot;
    if (vars.esp.visual_check)
        WorkspaceCache::TakeSnapshot(s_ws_snapshot);

    auto box_color = [&](const float c[4]) { return esp_col(c, 1.0f); };

    for (const auto& p : PlayerCache::players) {
        if (!p.isValid)
            continue;

        ImVec2 feet;
        const RBX::Vec3 foot_pos{ p.position.X, p.position.Y - (p.isR6 ? 3.0f : 2.5f), p.position.Z };
        if (!to_client(view, foot_pos, feet))
            continue;

        ImVec2 head = feet;
        const RBX::Vec3 head_pos = [&] {
            if (!p.headAddr)
                return p.position;
            const RBX::Vec3 hp = RBX::RbxInstance(p.headAddr).GetPos();
            return RBX::Vec3{ hp.X, hp.Y + 0.5f, hp.Z };
        }();
        to_client(view, head_pos, head);

        ImVec2 tor;
        if (!to_client(view, p.position, tor))
            continue;
        const bool on_screen = tor.x >= -margin && tor.x <= fw + margin &&
                               tor.y >= -margin && tor.y <= fh + margin;
        if (!on_screen)
            continue;

        if (chams_on && p.characterAddr) {
            const Mesh::Vector2 vp{ fw, fh };
            if (variables::ESP::meshChamsStyle == 2) {
                ShaderChams::DrawPlayer(dl, p.characterAddr, mesh_view, vp, 1.0f, 1.0f,
                                        (float)ImGui::GetTime(), variables::ESP::meshChamsStyle2);
            } else {
                MeshChams::Draw(dl, p.characterAddr, mesh_view, vp, 1.0f, 1.0f,
                                esp_col(variables::ESP::chamsFillColor, 1.0f));
            }
        }

        const bool vis = !vars.esp.visual_check || esp_visible(s_ws_snapshot, p.playerAddr, camPos, p.position);
        const float* bb = vis ? vars.esp.box_color : vars.esp.vischeck_hidden_color;

        const float hgt = ImMax(8.0f, feet.y - head.y);
        const float wdt = hgt * 0.62f;
        ImVec2 tl(head.x - wdt * 0.5f, head.y);
        ImVec2 br(head.x + wdt * 0.5f, feet.y);

        RBX::Vec3 box_wmin{}, box_wmax{};
        bool have_w = false;
        if (p.characterAddr) {
            ImVec2 ptl, pbr;
            if (esp_box_bounds(view, p.characterAddr, vars.esp.box_bounding == 0, ptl, pbr, box_wmin, box_wmax)) {
                have_w = true;
                tl = ptl;
                br = pbr;
            }
        }

        // ── caja ──
if (vars.esp.box) {
            if (vars.esp.box_style == 2) {
                if (p.rootPartAddr) {
                    RBX::Vec3 ctr;
                    float bh;
                    if (have_w) {
                        ctr = RBX::Vec3{ (box_wmin.X + box_wmax.X) * 0.5f,
                                         (box_wmin.Y + box_wmax.Y) * 0.5f,
                                         (box_wmin.Z + box_wmax.Z) * 0.5f };
                        bh = ImMax(8.0f, box_wmax.Y - box_wmin.Y);
                    } else {
                        ctr = RBX::Vec3{ (head_pos.X + foot_pos.X) * 0.5f,
                                         (head_pos.Y + foot_pos.Y) * 0.5f,
                                         (head_pos.Z + foot_pos.Z) * 0.5f };
                        const RBX::Vec3 dlt{ head_pos.X - foot_pos.X, head_pos.Y - foot_pos.Y,
                                             head_pos.Z - foot_pos.Z };
                        bh = ImMax(8.0f, ImSqrt(dlt.X * dlt.X + dlt.Y * dlt.Y + dlt.Z * dlt.Z));
                    }
                    esp_box_3d(dl, view, p.rootPartAddr, ctr, bh,
                               box_color(bb), box_color(vars.esp.box_color_secondary),
                               vars.esp.box_thickness, vars.esp.box_gradient);
                }
            } else {
                const ImU32 col = box_color(bb);
                const ImU32 col2 = vars.esp.box_gradient ? box_color(vars.esp.box_color_secondary)
                                                         : col;
                if (vars.esp.box_glow) {
                    const ImU32 glow = esp_col(vars.esp.box_glow_color, vars.esp.box_glow_strength);
                    if (vars.esp.box_style == 0)
                        dl->AddRect(tl, br, glow, 2.0f, 0, vars.esp.box_thickness + 4.0f);
                    else
                        esp_box_corners(dl, tl, br, glow, vars.esp.box_thickness + 4.0f);
                }
                if (vars.esp.outline_enabled)
                    esp_box_outline(dl, tl, br, vars.esp.box_thickness, vars.esp.outline_flags,
                                    IM_COL32(0, 0, 0, 190));
                if (vars.esp.box_filled)
                    dl->AddRectFilledMultiColor(tl, br,
                        esp_col(vars.esp.box_filled_top_color, 1.0f),
                        esp_col(vars.esp.box_filled_top_color, 1.0f),
                        esp_col(vars.esp.box_filled_bottom_color, 1.0f),
                        esp_col(vars.esp.box_filled_bottom_color, 1.0f));
                if (vars.esp.box_style == 0)
                    dl->AddRect(tl, br, col, 2.0f, 0, vars.esp.box_thickness);
                else
                    esp_box_corners(dl, tl, br, col2, vars.esp.box_thickness);
            }
        }

        // ── nombre ──
        if (vars.esp.name) {
            const ImU32 col = box_color(vars.esp.name_color);
            const float ny = tl.y - vars.esp.name_size - 3.0f;
            if (vars.esp.name_flags == 1) {
                dl->AddText(f_name, vars.esp.name_size, ImVec2(tl.x + 1.0f, ny + 1.0f),
                            IM_COL32(0, 0, 0, 200), p.name.c_str());
            } else if (vars.esp.name_flags == 2) {
                const ImU32 oc = IM_COL32(0, 0, 0, 220);
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (dx || dy)
                            dl->AddText(f_name, vars.esp.name_size, ImVec2(tl.x + dx, ny + dy),
                                        oc, p.name.c_str());
            }
            dl->AddText(f_name, vars.esp.name_size, ImVec2(tl.x, ny), col, p.name.c_str());
        }

        // ── salud ──
        if (vars.esp.health && p.maxHealth > 0.0f) {
            const float hx = tl.x - dpi(9.0f);
            const float ratio = ImClamp(p.health / p.maxHealth, 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(hx - 1.0f, tl.y - 1.0f),
                              ImVec2(hx + vars.esp.health_width + 1.0f, br.y + 1.0f),
                              IM_COL32(0, 0, 0, 170), 1.5f);
            const float hb = br.y - (br.y - tl.y) * ratio;
            dl->AddRectFilled(ImVec2(hx, hb), ImVec2(hx + vars.esp.health_width, br.y),
                              box_color(vars.esp.health_color), 1.0f);
            if (vars.esp.health_text) {
                char hbuf[16];
                snprintf(hbuf, sizeof(hbuf), "%.0f", p.health);
                dl->AddText(f_small, fsize(f_small),
                            ImVec2(hx - fsize(f_small) * 1.2f, hb - fsize(f_small) - dpi(2.0f)),
                            box_color(vars.esp.health_text_color), hbuf);
            }
        }

        // ── esqueleto (pixeles: phantomX paid) ──
        if (vars.esp.skeleton && p.characterAddr) {
            const auto& L = PlayerCache::GetLimbs(p.characterAddr);
            if (L.humanoid) {
                const ImU32 col = box_color(vars.esp.skeleton_color);
                const float th = vars.esp.skeleton_glow ? 3.0f : 1.2f;

                auto prim = [](std::uintptr_t part) {
                    return part ? memory->read<std::uintptr_t>(part + Offsets::BasePart::Primitive) : 0;
                };
                auto part_axis = [&](std::uintptr_t part, RBX::Vec3& top, RBX::Vec3& bot) -> bool {
                    const auto p = prim(part);
                    if (!p || !is_valid_ptr(p)) return false;
                    const RBX::Vec3 pos = memory->read<RBX::Vec3>(p + Offsets::Primitive::Position);
                    const RBX::CFrame rot = memory->read<RBX::CFrame>(p + Offsets::Primitive::Rotation);
                    const float hy = fabsf(memory->read<RBX::Vec3>(p + Offsets::Primitive::Size).Y) * 0.5f;
                    const RBX::Vec3 up{ rot.data[1], rot.data[4], rot.data[7] };
                    top = { pos.X + up.X * hy, pos.Y + up.Y * hy, pos.Z + up.Z * hy };
                    bot = { pos.X - up.X * hy, pos.Y - up.Y * hy, pos.Z - up.Z * hy };
                    return true;
                };
                auto part_pos = [&](std::uintptr_t part, RBX::Vec3& out) -> bool {
                    const auto p = prim(part);
                    if (!p || !is_valid_ptr(p)) return false;
                    out = memory->read<RBX::Vec3>(p + Offsets::Primitive::Position);
                    return true;
                };
                auto w2s = [&](const RBX::Vec3& a, const RBX::Vec3& b) {
                    ImVec2 sa, sb;
                    if (to_client(view, a, sa) && to_client(view, b, sb))
                        dl->AddLine(sa, sb, col, th);
                };
                auto lerp3 = [](const RBX::Vec3& a, const RBX::Vec3& b, float t) -> RBX::Vec3 {
                    return { a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t };
                };
                auto point_at_height = [&](const RBX::Vec3& top, const RBX::Vec3& bot,
                                          const RBX::Vec3& ref) -> RBX::Vec3 {
                    const float dy = top.Y - bot.Y;
                    if (fabsf(dy) < 0.001f) return lerp3(top, bot, 0.5f);
                    float t = (top.Y - ref.Y) / dy;
                    if (t < 0.0f) t = 0.0f;
                    else if (t > 1.0f) t = 1.0f;
                    return lerp3(top, bot, t);
                };

                if (L.r6) {
                    const float shoulder_drop = 0.18f;
                    RBX::Vec3 torso_top, torso_bot;
                    const bool torso_ok = part_axis(L.torso, torso_top, torso_bot);
                    RBX::Vec3 shoulder_c{};
                    if (torso_ok) {
                        shoulder_c = lerp3(torso_top, torso_bot, shoulder_drop);
                        w2s(shoulder_c, torso_bot);
                    }
                    RBX::Vec3 t, b;
                    if (part_axis(L.lArm, t, b)) {
                        RBX::Vec3 joint = t;
                        if (torso_ok) joint = point_at_height(t, b, shoulder_c);
                        if (torso_ok) w2s(shoulder_c, joint);
                        w2s(joint, b);
                    }
                    if (part_axis(L.rArm, t, b)) {
                        RBX::Vec3 joint = t;
                        if (torso_ok) joint = point_at_height(t, b, shoulder_c);
                        if (torso_ok) w2s(shoulder_c, joint);
                        w2s(joint, b);
                    }
                    if (part_axis(L.lLeg, t, b)) {
                        if (torso_ok) w2s(torso_bot, t);
                        w2s(t, b);
                    }
                    if (part_axis(L.rLeg, t, b)) {
                        if (torso_ok) w2s(torso_bot, t);
                        w2s(t, b);
                    }
                } else {
                    auto bone = [&](std::uintptr_t pa, std::uintptr_t pb) {
                        RBX::Vec3 a, b;
                        if (part_pos(pa, a) && part_pos(pb, b))
                            w2s(a, b);
                    };
                    const float shoulder_drop15 = 0.15f;
                    RBX::Vec3 ut_top, ut_bot;
                    const bool ut_ok = part_axis(L.upperTorso, ut_top, ut_bot);
                    RBX::Vec3 shoulder_c{};
                    if (ut_ok)
                        shoulder_c = lerp3(ut_top, ut_bot, shoulder_drop15);
                    auto shoulder_bone = [&](std::uintptr_t arm) {
                        RBX::Vec3 a_top, a_bot, a_c;
                        if (ut_ok && part_axis(arm, a_top, a_bot) && part_pos(arm, a_c)) {
                            const RBX::Vec3 joint = point_at_height(a_top, a_bot, shoulder_c);
                            w2s(shoulder_c, joint);
                            w2s(joint, a_c);
                        } else {
                            bone(L.upperTorso, arm);
                        }
                    };
                    bone(L.head,          L.upperTorso);
                    bone(L.upperTorso,    L.lowerTorso);
                    shoulder_bone(L.lUpperArm);
                    bone(L.lUpperArm,     L.lLowerArm);
                    bone(L.lLowerArm,     L.lHand);
                    shoulder_bone(L.rUpperArm);
                    bone(L.rUpperArm,     L.rLowerArm);
                    bone(L.rLowerArm,     L.rHand);
                    bone(L.lowerTorso,    L.lUpperLeg);
                    bone(L.lUpperLeg,     L.lLowerLeg);
                    bone(L.lLowerLeg,     L.lFoot);
                    bone(L.lowerTorso,    L.rUpperLeg);
                    bone(L.rUpperLeg,     L.rLowerLeg);
                    bone(L.rLowerLeg,     L.rFoot);
                }
            }
        }

        // ── cabeza / look ──
        if (vars.esp.head_dot)
            dl->AddCircleFilled(head, ImMax(1.0f, vars.esp.head_dot_size * 0.5f),
                                box_color(vars.esp.head_dot_color), 16);
        if (vars.esp.look_direction && p.headAddr) {
            const RBX::Vec3 hpos = RBX::RbxInstance(p.headAddr).GetPos();
            const RBX::CFrame hc  = RBX::RbxInstance(p.headAddr).GetCFrame();
            const RBX::Vec3 lv = hc.GetLookVector();
            if (ImFabs(lv.X) + ImFabs(lv.Y) + ImFabs(lv.Z) > 0.0001f) {
                const RBX::Vec3 e{ hpos.X + lv.X * vars.esp.look_direction_length,
                                   hpos.Y + lv.Y * vars.esp.look_direction_length,
                                   hpos.Z + lv.Z * vars.esp.look_direction_length };
                ImVec2 es;
                if (to_client(view, e, es))
                    dl->AddLine(head, es, box_color(vars.esp.look_direction_color), 1.2f);
            }
        }

        // ── flags ──
        if (vars.esp.flags_enabled) {
            const float fx = br.x + dpi(5.0f);
            float fy = tl.y;
            char fbuf[64];
            auto dflag = [&](const char* t, const float c[4]) {
                dl->AddText(f_small, vars.esp.flags_font_size, ImVec2(fx, fy), box_color(c), t);
                fy += vars.esp.flags_spacing;
            };
            if (vars.esp.flag_options[0]) {
                snprintf(fbuf, sizeof(fbuf), "HP: %.0f", p.health);
                dflag(fbuf, vars.esp.health_color);
            }
            if (vars.esp.flag_options[3] && p.tool.size() > 1)
                dflag(p.tool.c_str(), vars.esp.weapon_color);
            if (vars.esp.flag_options[4]) {
                snprintf(fbuf, sizeof(fbuf), "%.0fm", p.distance);
                dflag(fbuf, vars.esp.distance_color);
            }
            if (vars.esp.flag_options[9] && p.teamAddr && PlayerCache::localPlayerTeam &&
                p.teamAddr == PlayerCache::localPlayerTeam)
                dflag("Friend", vars.esp.vischeck_visible_color);
        }

        // ── avatar (placeholder de iniciales) ──
        if (vars.esp.avatar_enabled) {
            const float r = ImMax(6.0f, vars.esp.avatar_size * 0.5f);
            const ImVec2 ac(tl.x - r - dpi(4.0f), tl.y + r * 0.5f);
            dl->AddCircleFilled(ac, r, esp_col(vars.esp.avatar_outline_color, 0.35f), 24);
            dl->AddCircle(ac, r, box_color(vars.esp.avatar_outline_color), 24, vars.esp.avatar_outline_thickness);
            if (!p.name.empty()) {
                char ini[2] = { p.name[0], 0 };
                const ImVec2 ts = f_small->CalcTextSizeA(r * 0.8f, FLT_MAX, 0.0f, ini);
                dl->AddText(f_small, r * 0.8f, ImVec2(ac.x - ts.x * 0.5f, ac.y - ts.y * 0.5f),
                            IM_COL32(255, 255, 255, 235), ini);
            }
        }

        // ── info box ──
        if (vars.esp.player_info_box) {
            const float ph = fsize(f_small) + dpi(6.0f);
            const float pw = ImMax(f_small->CalcTextSizeA(fsize(f_small), FLT_MAX, 0.0f, p.name.c_str()).x + dpi(8.0f), wdt);
            const ImVec2 ip(tl.x - dpi(3.0f), tl.y - ph - dpi(2.0f));
            dl->AddRectFilled(ip, ImVec2(ip.x + pw, ip.y + ph), IM_COL32(10, 10, 12, 215), 3.0f);
            dl->AddRect(ip, ImVec2(ip.x + pw, ip.y + ph), IM_COL32(255, 255, 255, 28), 3.0f);
            dl->AddText(f_small, fsize(f_small), ip + ImVec2(dpi(3.0f), dpi(2.0f)),
                        IM_COL32(255, 255, 255, 235), p.name.c_str());
        }

        // ── snaplines ──
        if (vars.esp.snaplines) {
            float oy = fh * 0.5f;
            if (vars.esp.snaplines_position == 0)      oy = dpi(2.0f);
            else if (vars.esp.snaplines_position == 2) oy = fh - dpi(2.0f);
            ImU32 scol = box_color(vars.esp.snaplines_color);
            if (vars.esp.snaplines_gradient)
                scol = mix_u32(scol, box_color(vars.esp.snaplines_gradient_color),
                               ImClamp(p.distance / 150.0f, 0.0f, 1.0f));
            dl->AddLine(ImVec2(fw * 0.5f, oy), ImVec2(feet.x, feet.y - hgt * 0.5f),
                        scol, vars.esp.snaplines_thickness);
        }

        // ── ground circle ──
        if (vars.esp.ground_circle) {
            const int segs = ImMax(6, vars.esp.ground_circle_segments);
            const float off = vars.esp.ground_circle_animate ? s_clock * vars.esp.ground_circle_speed : 0.0f;
            const float rr = vars.esp.ground_circle_radius;
            const float gy = p.position.Y - 1.0f;
            static std::vector<ImVec2> gc_pts;
            gc_pts.clear();
            ImVec2 prev;
            bool have_prev = false;
            for (int i = 0; i <= segs; i++) {
                const float a = ((float)i / (float)segs) * IM_PI * 2.0f + off;
                const RBX::Vec3 w{ p.position.X + cosf(a) * rr, gy, p.position.Z + sinf(a) * rr };
                ImVec2 s;
                if (!to_client(view, w, s)) { have_prev = false; continue; }
                if (have_prev) {
                    ImU32 gcol = box_color(vars.esp.ground_circle_color);
                    if (vars.esp.ground_circle_gradient)
                        gcol = mix_u32(gcol, box_color(vars.esp.ground_circle_color2),
                                       (float)i / (float)segs);
                    dl->AddLine(prev, s, gcol, vars.esp.ground_circle_thickness);
                }
                gc_pts.push_back(s);
                prev = s;
                have_prev = true;
            }
            if (vars.esp.ground_circle_fill && gc_pts.size() > 2)
                dl->AddConvexPolyFilled(gc_pts.data(), (int)gc_pts.size(),
                                        esp_col(vars.esp.ground_circle_color, 0.5f));
        }

        // ── registramos dano & hitmarkers ──
        const auto lh = last_hp.find(p.playerAddr);
        if (lh != last_hp.end() && lh->second > p.health) {
            const float dmg = lh->second - p.health;
            g_dmg_floats.push_back(DmgFloat{ head, dmg, s_clock });
            if (g_dmg_floats.size() > 128)
                g_dmg_floats.erase(g_dmg_floats.begin());

            if (vars.visuals.hitmarker_enabled)
                g_hitmarkers.push_back(HitmarkerEntry{ head, (float)ImGui::GetTime(), vars.visuals.hitmarker_duration, dmg, false, vars.visuals.hitmarker_style });
            trigger_hitsound();

            if (p.health <= 0.0f)
                spawn_kill_effect(p.position);
        }
        if (vars.misc.kill_effects_test) {
            vars.misc.kill_effects_test = false;
            spawn_kill_effect(p.position);
        }
        last_hp[p.playerAddr] = p.health;
    }


    // ── contador de jugadores ──
    if (vars.esp.pf_player_counter) {
        char pbuf[32];
        snprintf(pbuf, sizeof(pbuf), "Players: %d", (int)PlayerCache::players.size());
        dl->AddText(f_small, fsize(f_small), ImVec2(dpi(12.0f), dpi(10.0f)),
                    IM_COL32(255, 255, 255, 215), pbuf);
    }

    // ── limpieza ──
    for (auto it = last_hp.begin(); it != last_hp.end();) {
        if (alive.find(it->first) == alive.end())
            it = last_hp.erase(it);
        else
            ++it;
    }
    draw_damage_floats(s_clock, dl, io.DisplaySize);
}

void draw_spectators() {
    egui::spectator_list_begin();
    for (const auto& p : PlayerCache::players)
        if (p.isValid)
            egui::spectator_list_add(p.name.c_str());
    egui::spectator_list_end();
}

egui::ExpIcon exp_icon(const std::string& cls) {
    using I = egui::ExpIcon;
    if (cls == "Workspace")      return I::Workspace;
    if (cls == "Part")           return I::Part;
    if (cls == "MeshPart")       return I::Mesh;
    if (cls == "Folder")         return I::Folder;
    if (cls == "Model")          return I::Model;
    if (cls == "Script")         return I::Script;
    if (cls == "LocalScript")    return I::LocalScript;
    if (cls == "ModuleScript")   return I::ModuleScript;
    if (cls == "Player")         return I::Player;
    if (cls == "Camera")         return I::Camera;
    if (cls == "Sound")          return I::Sound;
    if (cls == "Tool")           return I::Tool;
    if (cls == "Humanoid")       return I::Player;
    if (cls.find("Light") != std::string::npos)  return I::Light;
    if (cls.find("Value") != std::string::npos)  return I::Value;
    if (cls.find("Gui") != std::string::npos)    return I::Gui;
    return I::Unknown;
}

void draw_explorer() {
    if (!egui::settings.overlay.explorer && !vars.misc.explorer_enabled)
        return;
    using Clock = std::chrono::steady_clock;
    static auto last = Clock::now() - std::chrono::seconds(2);
    static std::vector<std::pair<std::string, std::string>> ws_children;
    static std::vector<std::string> plr_children;
    const auto now = Clock::now();
    if (now - last > std::chrono::milliseconds(250)) {
        last = now;
        ws_children.clear();
        plr_children.clear();
        if (Globals::workspace.Addr)
            for (const auto& c : Globals::workspace.GetChildList())
                ws_children.emplace_back(c.GetName(), c.GetClass());
        if (Globals::players.Addr)
            for (const auto& c : Globals::players.GetChildList())
                if (c.Addr != Globals::localPlayer.Addr)
                    plr_children.emplace_back(c.GetName());
        egui::g_explorer_instance_count =
            (int)(ws_children.size() + plr_children.size()) + (Globals::localPlayer.Addr ? 1 : 0);
    }

    using I = egui::ExpIcon;
    egui::explorer_begin();
    if (egui::explorer_node("Workspace", "Workspace", I::Workspace)) {
        for (const auto& r : ws_children)
            egui::explorer_item(r.first.c_str(), r.second.c_str(), exp_icon(r.second));
        egui::explorer_node_end();
    }
    if (Globals::players.Addr && egui::explorer_node("Players", "Players", I::Service)) {
        for (const auto& n : plr_children)
            egui::explorer_item(n.c_str(), "Player", I::Player);
        egui::explorer_node_end();
    }
    if (Globals::localPlayer.Addr)
        egui::explorer_item(Globals::localPlayer.GetName().c_str(), "Player", I::Player);

    egui::explorer_properties();
    if (egui::explorer_section("Status")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", PlayerCache::players.size());
        egui::explorer_property("Players", buf);
        const int ms = Ping::GetMs();
        snprintf(buf, sizeof(buf), ms >= 0 ? "%dms" : "--", ms);
        egui::explorer_property("Ping", buf);
        egui::explorer_property("Base", to_hex(memory->get_module_address()).c_str());
        egui::explorer_property("DataModel", to_hex(Globals::dataModel.Addr).c_str());
        egui::explorer_property("LocalPlayer", Globals::localPlayer.GetName().c_str());
    }
    egui::explorer_end();
}

} // namespace

void sync_chams_vars() {
    auto& e = vars.esp;
    variables::ESP::meshChams = e.chams_enabled;
    variables::ESP::meshChamsStyle = e.chams_style;
    variables::ESP::meshChamsStyle2 = e.chams_style2;
    variables::ESP::meshChamsDxMode = e.chams_dx_mode;
    variables::ESP::meshChamsOccludedDxMode = e.chams_occluded_mode;
    memcpy(variables::ESP::chamsFillColor, e.chams_fill_color, sizeof(e.chams_fill_color));
    memcpy(variables::ESP::meshChamsOccludedColor, e.chams_occluded_color, sizeof(e.chams_occluded_color));
    variables::ESP::meshChamsOutline = e.chams_outline;
    memcpy(variables::ESP::meshChamsOutlineColor, e.chams_outline_color, sizeof(e.chams_outline_color));
    variables::ESP::meshChamsOutlineFade = e.chams_outline_fade;
    variables::ESP::meshChamsOutlineStyle = e.chams_outline_style;
    variables::ESP::meshChamsOccluded = e.chams_occluded;
    variables::ESP::meshChamsGlow = e.chams_glow;
    variables::ESP::meshChamsLocalOff = e.chams_local_off;
    variables::ESP::engineChams = e.engine_chams_enabled;
    variables::ESP::engineChamsStyle = e.engine_chams_style;
    memcpy(variables::ESP::engineChamsColor, e.engine_chams_color, sizeof(e.engine_chams_color));
    variables::ESP::engineGhostColorIdx = e.engine_ghost_color_idx;
    variables::ESP::localPlayer = e.local_player;
    if (e.chams_enabled)
        MeshCache::Get().Refresh(true);
}

bool Render::run() {
    if (!create_overlay())
        return false;
    if (!create_device()) {
        cleanup_device();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    egui::initialize();
    if (!ImGui_ImplWin32_Init(g_overlay) || !ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        cleanup_device();
        return false;
    }
    MeshDxShader::Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        bool quit = false;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                quit = true;
        }
        if (quit)
            break;
        if (!memory->IsConnected())
            break;

        if (!sync_overlay()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        POINT cur;
        ::GetCursorPos(&cur);
        const ImVec2 client_mouse(cur.x - g_origin_x, cur.y - g_origin_y);
        update_clickthrough(client_mouse);

        if (g_ResizeWidth && g_ResizeHeight) {
            const UINT rw = g_ResizeWidth, rh = g_ResizeHeight;
            egui::glass_invalidate();
            cleanup_render_target();
            g_pSwapChain->ResizeBuffers(0, rw, rh, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            create_render_target();
            MeshDxShader::Resize(rw, rh);
        }

        egui::update_dpi();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (!egui::settings.state.menu_open)
            io.MousePos = client_mouse;

        refresh_players();
        sync_chams_vars();
        run_movement_features();
        run_world_features();
        draw_esp();
        run_aimbot();
        draw_visuals_overlays(Globals::renderEngine.GetViewMat());
        draw_aim_overlay();
        egui::spotify_update();
        egui::menu();
        egui::spotify_render();
        egui::watermark();
        egui::keybind_list_begin();
        egui::keybind_list_end();
        draw_spectators();
        egui::playerbar();
        draw_explorer();

        ImGui::Render();
        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        MeshDxShader::Flush(g_mainRenderTargetView);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    egui::glass_shutdown();
    egui::g_model3d.shutdown();
    MeshDxShader::Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device();
    ::PostMessageW(g_overlay, WM_NULL, 0, 0);
    ::DestroyWindow(g_overlay);
    ::UnregisterClassW(L"bottega_lol_overlay", ::GetModuleHandleW(nullptr));
    return true;
}
