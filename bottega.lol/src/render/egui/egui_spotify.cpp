#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "egui_spotify.h"
#include "egui.h"
#include "egui_settings.h"
#include "egui_colors.h"
#include "egui_glass.h"
#include "egui_variables.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <cmath>

#pragma comment(lib, "windowsapp.lib")

namespace egui {
namespace {

using hns = std::chrono::duration<long long, std::ratio<1, 10000000>>;

bool g_ok = false;

winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager g_manager = nullptr;
winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession g_session = nullptr;

std::wstring g_title;
std::wstring g_artist;
bool         g_playing = false;
bool         g_has     = false;
long long    g_pos     = 0;
long long    g_dur     = 0;
std::chrono::steady_clock::time_point g_t0{};
std::chrono::steady_clock::time_point g_fetch_at{};

constexpr double k_fetch_ms = 250.0;

ImVec2 g_card_pos{ -1.f, -1.f };
const float k_card_w = 320.0f;
const float k_card_h = 110.0f;
float       g_alpha  = 0.0f;
bool        g_dragging = false;

enum btn { BTN_PREV = 1, BTN_PLAY, BTN_NEXT, BTN_NONE = 0 };

std::string fmt_time(long long h)
{
    long long secs = h / 10000000LL;
    long long mm = secs / 60;
    long long ss = secs % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld", mm, ss);
    return buf;
}

void ensure_winrt()
{
    static bool init_done = false;
    if (init_done) return;
    init_done = true;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        g_ok = true;
    } catch (...) {
        g_ok = false;
    }
}

std::string utf8_of(const std::wstring& ws)
{
    if (ws.empty()) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out((size_t)need, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], need, nullptr, nullptr);
    return out;
}

std::string trunc8(ImFont* f, const char* s, float max_w)
{
    if (!s || !*s) return {};
    if (measure(f, s).x <= max_w) return s;
    std::string cur = s;
    while (cur.size() > 1) {
        cur.pop_back();
        const std::string t = cur + "...";
        if (measure(f, t.c_str()).x <= max_w) return t;
    }
    return "...";
}

void draw_icon(ImDrawList* dl, ImVec2 c, char icon, ImColor col)
{
    if (icon == 'l') { // Prev
        dl->AddTriangleFilled(ImVec2(c.x + 3.f, c.y - 4.f), ImVec2(c.x + 3.f, c.y + 4.f),
                              ImVec2(c.x - 3.f, c.y), col);
        dl->AddLine(ImVec2(c.x - 4.f, c.y - 4.f), ImVec2(c.x - 4.f, c.y + 4.f), col, 1.5f);
    } else if (icon == 'r') { // Next
        dl->AddTriangleFilled(ImVec2(c.x - 3.f, c.y - 4.f), ImVec2(c.x - 3.f, c.y + 4.f),
                              ImVec2(c.x + 3.f, c.y), col);
        dl->AddLine(ImVec2(c.x + 4.f, c.y - 4.f), ImVec2(c.x + 4.f, c.y + 4.f), col, 1.5f);
    } else if (icon == 'P') { // Pause
        dl->AddRectFilled(ImVec2(c.x - 4.f, c.y - 4.f), ImVec2(c.x - 1.5f, c.y + 4.f), col);
        dl->AddRectFilled(ImVec2(c.x + 1.5f, c.y - 4.f), ImVec2(c.x + 4.f, c.y + 4.f), col);
    } else { // Play
        dl->AddTriangleFilled(ImVec2(c.x - 3.f, c.y - 4.5f), ImVec2(c.x - 3.f, c.y + 4.5f),
                              ImVec2(c.x + 4.5f, c.y), col);
    }
}

void draw_control_btn(ImDrawList* dl, ImVec2 pos, ImVec2 sz, bool hov, char icon)
{
    const float r = layout.control_rounding;
    edraw.rect_filled(pos, pos + sz, hov ? colors.control_hov : colors.control, r);
    edraw.rect(pos, pos + sz, hov ? mix(colors.control_border, colors.accent, 0.6f) : ImColor(colors.control_border),
               r, ImDrawFlags_None, layout.border);

    ImVec2 c = pos + sz * 0.5f;
    ImColor icon_col = hov ? colors.text_primary : colors.text_sec;
    draw_icon(dl, c, icon, icon_col);
}

// Marco de la tarjeta: el mismo lenguaje visual que los overlays del menu.
void card_frame(ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, float alpha)
{
    edraw.list = dl;
    const float rad = layout.ov_rounding;

    edraw.shadow_rect(p_min, p_max, ImColor(0, 0, 0, (int)(255.0f * alpha)), layout.shadow * 0.6f, rad);
    if (!glass_pane(dl, p_min, p_max, rad, colors.background, alpha))
        edraw.rect_filled(p_min, p_max, fade(colors.background, alpha), rad);
    edraw.rect(p_min, p_max, fade(colors.window_border, alpha), rad, 0, layout.border);

    const float bar_h = ImMax(1.0f, dpi(2.0f));
    dl->PushClipRect(p_min, ImVec2(p_max.x, p_min.y + bar_h), true);
    edraw.rect_filled(p_min, ImVec2(p_max.x, p_min.y + rad * 2.0f + 1.0f), fade(colors.accent, alpha),
                      rad, ImDrawFlags_RoundCornersTop);
    dl->PopClipRect();

    edraw.list = nullptr;
}

} // namespace

void spotify_update()
{
    if (!vars.gui.show_spotify)
        return;
    ensure_winrt();
    if (!g_ok) return;
    try {
        if (!g_manager)
            g_manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (!g_manager) { g_has = false; return; }

        auto session = g_manager.GetCurrentSession();
        g_session = session;
        if (!session) { g_has = false; return; }

        auto now = std::chrono::steady_clock::now();
        if (now - g_fetch_at < std::chrono::milliseconds((long long)k_fetch_ms))
            return;

        auto props = session.TryGetMediaPropertiesAsync().get();
        auto tl = session.GetTimelineProperties();
        auto pb = session.GetPlaybackInfo();

        g_title = props ? props.Title() : L"";
        g_artist = props ? props.Artist() : L"";

        auto stt = pb ? pb.PlaybackStatus()
                      : winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed;
        g_playing = (stt == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        const long long start = tl.StartTime().count();
        const long long end = tl.EndTime().count();
        g_dur = (end > start) ? (end - start) : 0;
        g_pos = (tl.Position().count() >= start) ? (tl.Position().count() - start) : 0;
        g_has = true;

        g_t0 = now;
        g_fetch_at = now;
    } catch (...) {
        g_has = false;
    }

    if (!g_has || g_title.empty()) {
        HWND spotifyWnd = nullptr;
        struct EnumCtx { HWND* out; };
        EnumCtx ctx{ &spotifyWnd };
        ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            char className[256];
            if (::GetClassNameA(hwnd, className, sizeof(className))) {
                if (strcmp(className, "Chrome_WidgetWin_0") == 0) {
                    wchar_t title[512];
                    if (::GetWindowTextW(hwnd, title, 512) > 0) {
                        std::wstring t(title);
                        if (t != L"Spotify" && t != L"Spotify Free" && t != L"Spotify Premium" && t.find(L" - ") != std::wstring::npos) {
                            *reinterpret_cast<EnumCtx*>(lp)->out = hwnd;
                            return FALSE;
                        }
                    }
                }
            }
            return TRUE;
        }, (LPARAM)&ctx);

        if (spotifyWnd) {
            wchar_t title[512];
            if (::GetWindowTextW(spotifyWnd, title, 512) > 0) {
                std::wstring full(title);
                size_t sep = full.find(L" - ");
                if (sep != std::wstring::npos) {
                    g_artist = full.substr(0, sep);
                    g_title = full.substr(sep + 3);
                    g_has = true;
                    g_playing = true;
                }
            }
        }
    }
}

void spotify_render()
{
    fade_to(g_alpha, vars.gui.show_spotify);
    if (g_alpha <= 0.001f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    if (g_card_pos.x < 0.f)
    {
        g_card_pos.x = io.DisplaySize.x - k_card_w - dpi(16.f);
        g_card_pos.y = io.DisplaySize.y - k_card_h - dpi(16.f);
    }

    ImVec2 o = g_card_pos;
    ImVec2 b(o.x + k_card_w, o.y + k_card_h);
    const float header_h = dpi(22.f);

    // Dragging: cuando esta transparente el rastreo del raton lo hace render.cpp.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (io.MousePos.x >= o.x && io.MousePos.x <= b.x &&
            io.MousePos.y >= o.y && io.MousePos.y <= o.y + header_h)
            g_dragging = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        g_dragging = false;

    if (g_dragging)
    {
        g_card_pos.x += io.MouseDelta.x;
        g_card_pos.y += io.MouseDelta.y;
        g_card_pos.x = ImClamp(g_card_pos.x, 0.f, io.DisplaySize.x - k_card_w);
        g_card_pos.y = ImClamp(g_card_pos.y, 0.f, io.DisplaySize.y - k_card_h);
        o = g_card_pos;
        b = ImVec2(o.x + k_card_w, o.y + k_card_h);
    }

    card_frame(dl, o, b, g_alpha);
    const ImColor a = fade(colors.accent, g_alpha);
    const ImColor t1 = fade(colors.text_primary, g_alpha);
    const ImColor t2 = fade(colors.text, g_alpha);
    const ImColor t3 = fade(colors.text_muted, g_alpha);

    // Header
    const char* ttl = "spotify";
    ImFont* f_head = fnt(efonts.inter.Bold11);
    const ImVec2 tsz = measure(f_head, ttl);
    edraw.text(f_head, fsize(f_head),
               ImVec2(o.x + ImFloor((k_card_w - tsz.x) * 0.5f), o.y + ImFloor((header_h - tsz.y) * 0.5f) + 1.f),
               t3, ttl);
    edraw.line(ImVec2(o.x + dpi(10.f), o.y + header_h), ImVec2(b.x - dpi(10.f), o.y + header_h), a, 1.f);

    // Portada (placeholder: disco con aguja, mismo lenguaje que el resto)
    const ImVec2 art_org(o.x + dpi(10.f), o.y + header_h + dpi(8.f));
    const float art_sz = dpi(40.f);
    edraw.rect_filled(art_org, art_org + ImVec2(art_sz, art_sz), fade(colors.control, g_alpha), dpi(6.f));
    edraw.rect(art_org, art_org + ImVec2(art_sz, art_sz), fade(colors.control_border, g_alpha), dpi(6.f), 0, layout.border);
    dl->AddCircleFilled(ImVec2(art_org.x + art_sz * 0.5f, art_org.y + art_sz * 0.5f), art_sz * 0.32f, a, 24);
    dl->AddCircleFilled(ImVec2(art_org.x + art_sz * 0.5f, art_org.y + art_sz * 0.5f), dpi(2.5f), t1, 12);

    // Texto
    const float tx = art_org.x + art_sz + dpi(10.f);
    const float title_max = k_card_w - (art_sz + dpi(24.f)) - dpi(75.f);

    ImFont* f_title = fnt(efonts.inter.Text12);
    ImFont* f_sub   = fnt(efonts.inter.Text10);

    std::string title = g_has && !g_title.empty() ? trunc8(f_title, utf8_of(g_title).c_str(), title_max) : "no music playing";
    edraw.text(f_title, fsize(f_title), ImVec2(tx, o.y + header_h + dpi(8.f)), t1, title.c_str());

    if (g_has && !g_artist.empty())
    {
        const std::string artist = trunc8(f_sub, utf8_of(g_artist).c_str(), title_max);
        edraw.text(f_sub, fsize(f_sub), ImVec2(tx, o.y + header_h + dpi(8.f) + fsize(f_title) + dpi(2.f)), t3, artist.c_str());
    }

    // Botones
    const float bs = dpi(22.f);
    const float bgap = dpi(6.f);
    ImVec2 btns_pos(o.x + k_card_w - dpi(10.f) - bs * 3.f - bgap * 2.f, o.y + header_h + dpi(8.f));

    btn hov = BTN_NONE;
    for (int i = 0; i < 3; i++)
    {
        const ImVec2 bp(btns_pos.x + (bs + bgap) * i, btns_pos.y);
        if (io.MousePos.x >= bp.x && io.MousePos.x <= bp.x + bs &&
            io.MousePos.y >= bp.y && io.MousePos.y <= bp.y + bs)
            hov = (btn)(i + 1);
    }

    if (hov != BTN_NONE && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_session)
    {
        try {
            if (hov == BTN_PREV) g_session.TrySkipPreviousAsync().get();
            else if (hov == BTN_PLAY) {
                if (g_playing) g_session.TryPauseAsync().get();
                else g_session.TryPlayAsync().get();
            }
            else if (hov == BTN_NEXT) g_session.TrySkipNextAsync().get();
        } catch (...) {}
    }

    draw_control_btn(dl, btns_pos, ImVec2(bs, bs), hov == BTN_PREV, 'l');
    draw_control_btn(dl, ImVec2(btns_pos.x + bs + bgap, btns_pos.y), ImVec2(bs, bs), hov == BTN_PLAY, g_playing ? 'P' : 'p');
    draw_control_btn(dl, ImVec2(btns_pos.x + (bs + bgap) * 2, btns_pos.y), ImVec2(bs, bs), hov == BTN_NEXT, 'r');

    // Barra de progreso
    const float pbx = o.x + dpi(10.f);
    const float pbw = k_card_w - dpi(20.f);
    const float pby = b.y - dpi(17.f);
    const float pbh = dpi(4.f);

    const float frac = (g_dur > 0) ? ImClamp((float)g_pos / (float)g_dur, 0.f, 1.f) : 0.f;

    edraw.rect_filled(ImVec2(pbx, pby), ImVec2(pbx + pbw, pby + pbh), fade(colors.control, g_alpha), pbh * 0.5f);
    if (frac > 0.001f)
    {
        const float fw = ImMax(pbh, pbw * frac);
        edraw.rect_filled(ImVec2(pbx, pby), ImVec2(pbx + fw, pby + pbh), a, pbh * 0.5f);
    }

    std::string cur_t = fmt_time(g_pos);
    std::string dur_t = fmt_time(g_dur);
    edraw.text(f_sub, fsize(f_sub), ImVec2(pbx, pby - fsize(f_sub) - dpi(2.f)), t3, cur_t.c_str());
    const ImVec2 dsz = measure(f_sub, dur_t.c_str());
    edraw.text(f_sub, fsize(f_sub), ImVec2(pbx + pbw - dsz.x, pby - fsize(f_sub) - dpi(2.f)), t3, dur_t.c_str());
}

bool spotify_hovered(const ImVec2& client_pos)
{
    if (g_alpha <= 0.001f || g_card_pos.x < 0.f)
        return false;
    return client_pos.x >= g_card_pos.x && client_pos.x <= g_card_pos.x + k_card_w &&
           client_pos.y >= g_card_pos.y && client_pos.y <= g_card_pos.y + k_card_h;
}

} // namespace egui