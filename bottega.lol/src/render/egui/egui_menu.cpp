// egui_menu.cpp

#include "egui.h"
#include "egui_settings.h"
#include "egui_colors.h"
#include "egui_glass.h"
#include "egui_keys.h"
#include "egui_variables.h"
#include "imgui_internal.h"
#include <vector>
#include <string>
#include <cstdio>
#include <Windows.h>
#include "core/net/ping.h"

void egui::menu() {
    // Antes del early-out: los hotkeys tienen que seguir funcionando con el menu
    // cerrado, que es justo cuando se usan.
    egui::hotkey_poll();

    static bool was_key_down = false;
    bool is_key_down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (is_key_down && !was_key_down)
        settings.state.menu_open = !settings.state.menu_open;
    was_key_down = is_key_down;

    fade_to(settings.alpha.menu, settings.state.menu_open);

    vars.gui.menu_was_closed = !vars.gui.menu_open;
    vars.gui.menu_open       = settings.state.menu_open;
    vars.gui.menu_open_alpha = settings.alpha.menu;

    if (settings.alpha.menu <= 0.001f)
        return;

    // El tamaño sale de las medidas del diseño: layout manda.
    settings.size = ImVec2(
        layout.main_w + layout.panel_gap + layout.preview_w + layout.shadow_pad * 2.0f,
        layout.header_h + layout.tabbar_h + layout.content_h + layout.footer_h + layout.shadow_pad * 2.0f);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    static ImVec2 applied_size = ImVec2(0.0f, 0.0f);
    const bool size_changed = (applied_size.x != settings.size.x || applied_size.y != settings.size.y);
    if (size_changed && applied_size.x > 0.0f && settings.state.menu_window_center.x >= 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(settings.state.menu_window_center.x - settings.size.x * 0.5f,
                                       settings.state.menu_window_center.y - settings.size.y * 0.5f),
                                ImGuiCond_Always);
    }
    applied_size = settings.size;

    ImGui::SetNextWindowSize(settings.size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, settings.alpha.menu);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin(settings.project_name, &settings.state.menu_open, settings.window_flags)) {
        settings.state.menu_window_pos = ImGui::GetWindowPos();
        const ImVec2 win_size = ImGui::GetWindowSize();
        settings.state.menu_window_center = ImVec2(
            settings.state.menu_window_pos.x + win_size.x * 0.5f,
            settings.state.menu_window_pos.y + win_size.y * 0.5f);

        g_any_slider_active = false;
        egui::template_shell();
    }
    ImGui::End();

    ImGui::PopStyleVar(4);
}

namespace egui {

// ──────────────────────────────────────────────────────────────────────────────
// Marco compartido de los overlays
// ──────────────────────────────────────────────────────────────────────────────
static float g_ov_alpha = 1.0f;

static ImColor ov(const ImVec4& c, float mul = 1.0f) { return fade(c, g_ov_alpha * mul); }

static void overlay_frame(ImVec2 p_min, ImVec2 p_max, bool accent_bar = false)
{
    edraw.shadow_rect(p_min, p_max, ov(ImVec4(0, 0, 0, 1)), layout.shadow * 0.6f, layout.ov_rounding);
    if (!glass_pane(edraw.list, p_min, p_max, layout.ov_rounding, colors.background, g_ov_alpha))
        edraw.rect_filled(p_min, p_max, ov(colors.background), layout.ov_rounding);
    edraw.rect(p_min, p_max, ov(colors.window_border), layout.ov_rounding, 0, layout.border);

    if (!accent_bar) return;
    const float bar_h = ImMax(1.0f, dpi(2.0f));
    const float rad   = ImMax(0.0f, ImMin(layout.ov_rounding, (p_max.x - p_min.x) * 0.5f - 1.0f));

    ImDrawList* dl = edraw.list ? edraw.list : ImGui::GetWindowDrawList();
    dl->PushClipRect(p_min, ImVec2(p_max.x, p_min.y + bar_h), true);
    edraw.rect_filled(p_min, ImVec2(p_max.x, p_min.y + rad * 2.0f + 1.0f), ov(colors.accent),
                      rad, ImDrawFlags_RoundCornersTop);
    dl->PopClipRect();
}

// Esquina de la pantalla desde la que crece cada overlay.
static ImVec2 viewport_min() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImVec2(vp->Pos.x + layout.ov_margin, vp->Pos.y + layout.ov_margin);
}
static ImVec2 viewport_max() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImVec2(vp->Pos.x + vp->Size.x - layout.ov_margin,
                  vp->Pos.y + vp->Size.y - layout.ov_margin);
}

static float g_watermark_bottom = 0.0f;
static float g_watermark_alpha  = 0.0f;

// ──────────────────────────────────────────────────────────────────────────────
// Piezas compartidas
// ──────────────────────────────────────────────────────────────────────────────

static float digit_slot(ImFont* f, int n)
{
    static ImFont* c_font = nullptr;
    static float   c_size = 0.0f;
    static float   c_w    = 0.0f;

    const float size = fsize(f);
    if (c_font != f || c_size != size) {
        c_font = f; c_size = size; c_w = 0.0f;
        for (char c = '0'; c <= '9'; c++)
            c_w = ImMax(c_w, f->CalcTextSizeA(size, FLT_MAX, 0.0f, &c, &c + 1).x);
    }
    return c_w * (float)n;
}

static void draw_slot(ImFont* f, float right_x, float cy, const ImVec4& col, const char* txt,
                      float dy = 0.0f)
{
    const ImVec2 ts = measure(f, txt);
    draw_text(f, ImVec2(right_x - ts.x, cy - ts.y * 0.5f + dy), col, txt);
}

// ──────────────────────────────────────────────────────────────────────────────
// Watermark
// ──────────────────────────────────────────────────────────────────────────────
void watermark()
{
    // Visible con o sin menu: es un overlay de estado, cuando esta marcado se ve.
    fade_to(g_watermark_alpha, settings.overlay.watermark);

    if (g_watermark_alpha <= 0.001f) { g_watermark_bottom = viewport_min().y; return; }

    ImFont* f_name = fnt(efonts.inter.Bold12);
    ImFont* f_val  = fnt(efonts.inter.Bold11);
    ImFont* f_unit = fnt(efonts.inter.Text10);

    SYSTEMTIME st;
    ::GetLocalTime(&st);

    char v_fps[12], v_ms[12], v_time[12];
    snprintf(v_fps,  sizeof(v_fps),  "%d", (int)ImMin(ImGui::GetIO().Framerate + 0.5f, 999.0f));
    snprintf(v_ms,   sizeof(v_ms),   "%d", Ping::GetMs());
    snprintf(v_time, sizeof(v_time), "%02d:%02d", st.wHour, st.wMinute);

    const float rule_w = ImMax(2.0f, dpi(2.0f));
    const float gap    = dpi(13.0f);  // entre bloques
    const float gap_u  = dpi(3.0f);   // entre numero y unidad

    const ImVec2 name_sz = measure(f_name, settings.project_name);
    const float slot   = digit_slot(f_val, 3);
    const float u_fps  = measure(f_unit, "fps").x;
    const float u_ms   = measure(f_unit, "ms").x;
    const float w_time = measure(f_val, "00:00").x;

    const float w = layout.ov_pad * 2.0f
                  + rule_w + dpi(9.0f) + name_sz.x
                  + gap + slot + gap_u + u_fps
                  + gap + slot + gap_u + u_ms
                  + gap + w_time;
    const float h = layout.ov_pad * 2.0f + ImMax(name_sz.y, fsize(f_val));

    const float slide = (1.0f - g_watermark_alpha) * dpi(8.0f);
    const float top   = viewport_min().y - slide;
    const ImVec2 p_max(viewport_max().x, top + h);
    const ImVec2 p_min(p_max.x - w, top);

    g_ov_alpha = g_watermark_alpha;
    edraw.list = ImGui::GetForegroundDrawList();
    overlay_frame(p_min, p_max);

    const float cy = (p_min.y + p_max.y) * 0.5f;
    float x = p_min.x + layout.ov_pad;

    edraw.rect_filled(ImVec2(x, p_min.y + layout.ov_pad * 0.7f),
                      ImVec2(x + rule_w, p_max.y - layout.ov_pad * 0.7f),
                      ov(colors.accent), rule_w * 0.5f);
    x += rule_w + dpi(9.0f);

    draw_text(f_name, ImVec2(x, cy - name_sz.y * 0.5f - dpi(1.0f)), ov(colors.accent), settings.project_name);
    x += name_sz.x + gap;

    draw_slot(f_val, x + slot, cy, ov(colors.text_primary), v_fps);
    x += slot + gap_u;
    draw_text(f_unit, ImVec2(x, cy - fsize(f_unit) * 0.5f - dpi(1.0f)), ov(colors.text_muted), "fps");
    x += u_fps + gap;

    draw_slot(f_val, x + slot, cy, ov(colors.text_primary), v_ms);
    x += slot + gap_u;
    draw_text(f_unit, ImVec2(x, cy - fsize(f_unit) * 0.5f - dpi(1.0f)), ov(colors.text_muted), "ms");
    x += u_ms + gap;

    draw_slot(f_val, x + w_time, cy, ov(colors.text_sec), v_time);

    edraw.list = nullptr;
    g_ov_alpha = 1.0f;

    g_watermark_bottom = ImLerp(viewport_min().y, p_max.y, g_watermark_alpha);
}

// ──────────────────────────────────────────────────────────────────────────────
// Listas (keybinds y spectators)
// ──────────────────────────────────────────────────────────────────────────────
struct ListEntry {
    std::string left;      // nombre
    std::string right;     // tecla (keybinds)
    std::string extra;     // modo (keybinds)
    float t      = 0.0f;   // 0 fuera, 1 dentro
    float act    = 0.0f;   // resaltado de "ahora mismo activo"
    bool  live   = false;  // mandada en este frame
    bool  active = false;  // tecla pulsada ahora mismo
};

struct ListState {
    std::vector<ListEntry> rows;
    float alpha = 0.0f;    // fundido del bloque entero
};
static ListState g_keybinds, g_spectators;

static void list_begin(ListState& s)
{
    for (size_t i = 0; i < s.rows.size(); i++) s.rows[i].live = false;
}

// La fila se busca por nombre para no perder su animacion entre frames. El filtro
// por `live` es lo que permite repetidos: dos entradas con el mismo nombre en el
// mismo frame caen en filas distintas en vez de pisarse.
static ListEntry& list_row(ListState& s, const char* name)
{
    for (size_t i = 0; i < s.rows.size(); i++)
        if (!s.rows[i].live && s.rows[i].left == name) return s.rows[i];

    s.rows.push_back(ListEntry());
    s.rows.back().left = name;
    return s.rows.back();
}

static int list_advance(ListState& s, bool show, float& rows_h)
{
    int live = 0;
    rows_h = 0.0f;
    for (size_t i = 0; i < s.rows.size(); ) {
        ListEntry& r = s.rows[i];
        fade_to(r.t, r.live && show);
        fade_to(r.act, r.active && r.live, hover_speed);

        if (!r.live && r.t <= 0.0f) { s.rows.erase(s.rows.begin() + (int)i); continue; }

        rows_h += layout.ov_row_h * r.t;
        if (r.live) live++;
        i++;
    }
    return live;
}

static void draw_text_sh(ImFont* f, ImVec2 pos, const ImVec4& col, const char* s, float a = 1.0f)
{
    draw_text(f, ImVec2(pos.x + dpi(1.0f), pos.y + dpi(1.0f)), ov(ImVec4(0, 0, 0, 0.75f), a), s);
    draw_text(f, pos, ov(col, a), s);
}

static float tracked_w(ImFont* f, const char* s, float track)
{
    float w = 0.0f;
    for (const char* p = s; *p; p++) {
        const char c = (char)ImToUpper(*p);
        w += f->CalcTextSizeA(fsize(f), FLT_MAX, 0.0f, &c, &c + 1).x + track;
    }
    return w > 0.0f ? w - track : 0.0f;
}

static void draw_tracked_sh(ImFont* f, ImVec2 pos, const ImVec4& col, const char* s,
                            float track, float a = 1.0f)
{
    for (const char* p = s; *p; p++) {
        const char c = (char)ImToUpper(*p);
        draw_text(f, ImVec2(pos.x + dpi(1.0f), pos.y + dpi(1.0f)), ov(ImVec4(0, 0, 0, 0.75f), a), &c, &c + 1);
        draw_text(f, pos, ov(col, a), &c, &c + 1);
        pos.x += f->CalcTextSizeA(fsize(f), FLT_MAX, 0.0f, &c, &c + 1).x + track;
    }
}

static void keybind_push(const char* name, int key, int mode, bool active)
{
    if (!name || !*name) return;
    // Sin tecla solo entra el que va en "always on": ese no necesita ninguna, y
    // si la funcion esta encendida tiene que verse igual que los demas.
    if (key <= 0 && mode != 0) return;

    static const char* modes[] = { "always", "hold", "toggle" };

    ListEntry& e = list_row(g_keybinds, name);
    e.live   = true;
    e.right  = (key > 0 && key < IM_ARRAYSIZE(::keys)) ? ::keys[key] : "on";
    e.extra  = (mode >= 0 && mode < IM_ARRAYSIZE(modes)) ? modes[mode] : modes[0];
    e.active = active;
}

// La lista sale sola de los hotkeys registrados: lo que tengas en el menu es lo
// que se ve, sin repetirlo aqui. keybind_list_add sigue estando para meter a
// mano algo que no pase por un widget.
void keybind_list_begin()
{
    hotkey_poll();
    list_begin(g_keybinds);

    for (int i = 0, n = hotkey_count(); i < n; i++) {
        const char* name = nullptr;
        int key = 0, mode = 0;
        bool active = false, enabled = false;
        if (!hotkey_info(i, &name, &key, &mode, &active, &enabled)) continue;

        // Con tecla, entra siempre. Sin tecla, solo el "always on" que ademas
        // este encendido: si su checkbox esta apagado, la funcion no esta on.
        if (key > 0 || (mode == 0 && enabled))
            keybind_push(name, key, mode, active);
    }
}

void keybind_list_add(const char* name, int key, int mode)
{
    keybind_push(name, key, mode, (mode == 0) || ((::GetAsyncKeyState(key) & 0x8000) != 0));
}

void keybind_list_end()
{
    ListState& s = g_keybinds;
    const bool show = settings.overlay.keybinds;

    ImFont* f_key  = fnt(efonts.inter.Bold10);
    ImFont* f_name = fnt(efonts.inter.Text12);

    float rows_h = 0.0f;
    const int live = list_advance(s, show, rows_h);

    fade_to(s.alpha, show && live > 0);
    if (s.alpha <= 0.001f) return;

    const float track = dpi(0.9f);   // tracking de las versalitas
    const float rail  = ImMax(2.0f, dpi(2.0f));
    const float gap   = dpi(12.0f);

    float w_key = 0.0f;
    for (size_t i = 0; i < s.rows.size(); i++)
        if (s.rows[i].live)
            w_key = ImMax(w_key, tracked_w(f_key, s.rows[i].right.c_str(), track));

    const float key_x  = viewport_min().x + rail + dpi(10.0f)
                       - (1.0f - s.alpha) * dpi(8.0f);   // entra desde su borde
    const float name_x = key_x + w_key + gap;

    // Crece hacia arriba desde abajo: al añadir un bind no baila lo que ya hay.
    const float bottom = viewport_max().y;
    const float top    = bottom - rows_h;

    g_ov_alpha = s.alpha;
    edraw.list = ImGui::GetForegroundDrawList();

    float y = top;
    for (size_t i = 0; i < s.rows.size(); i++) {
        ListEntry& r = s.rows[i];
        const float rh = layout.ov_row_h * r.t;
        if (rh <= 0.01f) continue;

        const float a  = r.t;
        const float cy = y + rh * 0.5f;
        const float rx = viewport_min().x - (1.0f - s.alpha) * dpi(8.0f);

        const ImVec4 rail_col = ImLerp(colors.text_faint, colors.accent, r.act);
        edraw.rect_filled(ImVec2(rx, y + dpi(2.0f)), ImVec2(rx + rail, y + rh - dpi(2.0f)),
                          ov(rail_col, a * (0.35f + 0.65f * r.act)), rail * 0.5f);

        const float key_h = fsize(f_key);
        draw_tracked_sh(f_key, ImVec2(key_x, cy - key_h * 0.5f),
                        ImLerp(colors.text_sec, colors.accent, r.act),
                        r.right.c_str(), track, a);

        const ImVec2 ns = measure(f_name, r.left.c_str());
        draw_text_sh(f_name, ImVec2(name_x, cy - ns.y * 0.5f - dpi(1.0f)),
                     ImLerp(colors.text, colors.text_primary, r.act), r.left.c_str(), a);

        y += rh;
    }

    edraw.list = nullptr;
    g_ov_alpha = 1.0f;
}

void spectator_list_begin() { list_begin(g_spectators); }

void spectator_list_add(const char* name)
{
    if (!name || !*name) return;
    list_row(g_spectators, name).live = true;
}

void spectator_list_end()
{
    ListState& s = g_spectators;
    const bool show = settings.overlay.spectators;

    ImFont* f_head = fnt(efonts.inter.Bold10);
    ImFont* f_name = fnt(efonts.inter.Text12);

    float rows_h = 0.0f;
    const int live = list_advance(s, show, rows_h);

    fade_to(s.alpha, show && live > 0);
    if (s.alpha <= 0.001f) return;

    char head[48];
    snprintf(head, sizeof(head), "%d watching", live);

    const float track = dpi(0.9f);
    const float rail  = ImMax(2.0f, dpi(2.0f));
    const float dot_r = dpi(3.0f);

    const float head_w = tracked_w(f_head, head, track);
    const float head_h = fsize(f_head) + layout.ov_gap;

    const float rx    = viewport_max().x + (1.0f - s.alpha) * dpi(8.0f);
    const float right = rx - rail - dpi(10.0f);
    const float top   = g_watermark_bottom + layout.ov_gap;

    g_ov_alpha = s.alpha;
    edraw.list = ImGui::GetForegroundDrawList();

    const float pulse = 0.55f + 0.45f * ImSin((float)ImGui::GetTime() * 2.2f);
    edraw.rect_filled(ImVec2(rx - rail, top), ImVec2(rx, top + head_h + rows_h),
                      ov(colors.accent, 0.35f + 0.4f * pulse), rail * 0.5f);

    // Titular: punto + "N WATCHING" en versalitas, pegado a la derecha.
    draw_tracked_sh(f_head, ImVec2(right - head_w, top), colors.accent, head, track);
    edraw.circle_filled(ImVec2(right - head_w - dpi(9.0f), top + fsize(f_head) * 0.5f),
                        dot_r, ov(colors.accent, 0.6f + 0.4f * pulse), 16);

    float y = top + head_h;
    for (size_t i = 0; i < s.rows.size(); i++) {
        ListEntry& r = s.rows[i];
        const float rh = layout.ov_row_h * r.t;
        if (rh <= 0.01f) continue;

        const ImVec2 ns = measure(f_name, r.left.c_str());
        draw_text_sh(f_name, ImVec2(right - ns.x + (1.0f - r.t) * dpi(6.0f),
                                    y + (rh - ns.y) * 0.5f - dpi(1.0f)),
                     colors.text, r.left.c_str(), r.t);
        y += rh;
    }

    edraw.list = nullptr;
    g_ov_alpha = 1.0f;
}

// ──────────────────────────────────────────────────────────────────────────────
// Player bar: solo aparece mientras hay un objetivo con aim asistido.
// ──────────────────────────────────────────────────────────────────────────────
void playerbar()
{
    if (!vars.gui.show_playerbar || !aim_lock_active || aim_lock_name.empty())
        return;

    ImFont* f_name = fnt(efonts.inter.Bold12);
    ImFont* f_val  = fnt(efonts.inter.Text12);

    char hp[16], dd[32];
    snprintf(hp, sizeof(hp), "%.0f", aim_lock_health);
    snprintf(dd, sizeof(dd), "%.0fm", aim_lock_distance);

    const float name_w = measure(f_name, aim_lock_name.c_str()).x;
    const float hp_w   = measure(f_val, hp).x;
    const float dd_w   = measure(f_val, dd).x;
    const float pad    = layout.ov_pad;
    const float htxt   = fsize(f_name);

    const float w = ImMin(pad * 2.0f + name_w + dpi(18.0f) + dpi(90.0f) +
                          dpi(12.0f) + hp_w + dpi(10.0f) + dd_w + pad * 2.0f,
                          viewport_max().x - viewport_min().x);
    const float h = pad * 2.0f + htxt;

    const float cx = (viewport_min().x + viewport_max().x) * 0.5f;
    const ImVec2 p_min(cx - w * 0.5f, viewport_max().y - h - dpi(12.0f));
    const ImVec2 p_max(p_min.x + w, p_min.y + h);

    g_ov_alpha = 1.0f;
    edraw.list = ImGui::GetForegroundDrawList();
    overlay_frame(p_min, p_max, true);

    const float cy = (p_min.y + p_max.y) * 0.5f;
    float x = p_min.x + pad;

    draw_text(f_name, ImVec2(x, cy - htxt * 0.5f - dpi(1.0f)),
              ov(colors.text_primary), aim_lock_name.c_str());
    x += name_w + dpi(18.0f);

    const float bw = dpi(90.0f);
    const float bh = ImMax(4.0f, dpi(4.0f));
    const ImVec2 bp0(x, cy - bh * 0.5f);
    const ImVec2 bp1(x + bw, cy + bh * 0.5f);
    edraw.rect_filled(bp0, bp1, ov(colors.control), bh * 0.5f);
    const float ratio = aim_lock_max_health > 0.0f
        ? ImClamp(aim_lock_health / aim_lock_max_health, 0.0f, 1.0f) : 0.0f;
    if (ratio > 0.0f)
        edraw.rect_filled(bp0, ImVec2(bp0.x + bw * ratio, bp1.y), ov(colors.accent), bh * 0.5f);
    x += bw + dpi(12.0f);

    draw_text(f_val, ImVec2(x, cy - fsize(f_val) * 0.5f - dpi(1.0f)), ov(colors.text_sec), hp);
    x += hp_w + dpi(10.0f);
    draw_text(f_val, ImVec2(x, cy - fsize(f_val) * 0.5f - dpi(1.0f)), ov(colors.text_muted), dd);

    edraw.list = nullptr;
    g_ov_alpha = 1.0f;
}

// ──────────────────────────────────────────────────────────────────────────────
// Explorer
// ──────────────────────────────────────────────────────────────────────────────
static bool  g_explorer_begun = false;       // se llamo a Begin -> toca End + PopStyleVar
static bool  g_explorer_open_frame = false;  // Begin devolvio true -> hay contenido
static float g_explorer_alpha = 0.0f;
static char  g_exp_search_inst[64] = "";
static char  g_exp_search_prop[64] = "";
static bool  g_exp_paused = false;

static int         g_exp_depth = 0;
static ImGuiID     g_exp_sel_id = 0;
static std::string g_exp_sel_name, g_exp_sel_class;

void explorer_begin()
{
    static bool was_down = false;
    const bool is_down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    if (is_down && !was_down)
        settings.overlay.explorer = !settings.overlay.explorer;
    was_down = is_down;

    fade_to(g_explorer_alpha, settings.overlay.explorer && settings.state.menu_open);

    g_explorer_open_frame = false;
    g_explorer_begun = false;
    if (g_explorer_alpha <= 0.001f) return;

    const float head_h = layout.header_h;
    const float search_h = layout.ov_pad + layout.control_h + layout.ov_pad;
    const float h = head_h + search_h + layout.exp_list_h + layout.exp_props_h
                  + layout.footer_h + layout.shadow_pad * 2.0f;
    const ImVec2 size(layout.exp_w + layout.shadow_pad * 2.0f, h);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + layout.ov_margin - layout.shadow_pad,
                                   vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_explorer_alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    g_explorer_begun = true;
    if (!ImGui::Begin("##egui_explorer", &settings.overlay.explorer, settings.window_flags))
        return; // el End() lo hace explorer_end()
    g_explorer_open_frame = true;
    g_exp_depth = 0;
    push_font_stretch();

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImVec2 m_min(win->Pos.x + layout.shadow_pad, win->Pos.y + layout.shadow_pad);
    const ImVec2 m_max(m_min.x + layout.exp_w, win->Pos.y + size.y - layout.shadow_pad);

    edraw.shadow_rect(m_min, m_max, ImColor(0, 0, 0, 255), layout.shadow, layout.window_rounding);
    if (!glass_pane(ImGui::GetWindowDrawList(), m_min, m_max, layout.window_rounding,
                    colors.background, ImGui::GetStyle().Alpha))
        edraw.rect_filled(m_min, m_max, colors.background, layout.window_rounding);
    edraw.rect(m_min, m_max, colors.window_border, layout.window_rounding, 0, layout.border);

    // Cabecera: nombre + "Explorer" + un contador de instancias.
    ImFont* f_logo = fnt(efonts.inter.Bold12);
    ImFont* f_txt  = fnt(efonts.inter.Text12);
    const float pad = dpi(14.0f);
    const float cy = m_min.y + layout.header_h * 0.5f;

    float x = m_min.x + pad;
    const ImVec2 logo_sz = measure(f_logo, settings.project_name);
    draw_text(f_logo, ImVec2(x, cy - logo_sz.y * 0.5f - dpi(1.0f)), colors.accent, settings.project_name);
    x += logo_sz.x + dpi(14.0f);

    const ImVec2 exp_sz = measure(f_txt, "Explorer");
    draw_text(f_txt, ImVec2(x, cy - exp_sz.y * 0.5f - dpi(1.0f)), colors.text_sec, "Explorer");

    char count[24];
    snprintf(count, sizeof(count), "%d", ImMax(g_explorer_instance_count, 0));
    const ImVec2 cnt_sz = measure(f_txt, count);
    draw_text(f_txt, ImVec2(m_max.x - pad - cnt_sz.x, cy - cnt_sz.y * 0.5f - dpi(2.0f)), colors.accent, count);

    float y = m_min.y + layout.header_h;

    // Buscador + Pause.
    const float btn_w = dpi(62.0f);
    ImGui::SetCursorScreenPos(ImVec2(m_min.x + pad, y + layout.ov_pad));
    ImGui::PushItemWidth(layout.exp_w - pad * 2.0f - btn_w - layout.ov_gap);
    input_text("##exp_search", "Search instances...", g_exp_search_inst, sizeof(g_exp_search_inst));
    ImGui::PopItemWidth();

    ImGui::SameLine(0.0f, layout.ov_gap);
    if (button(g_exp_paused ? "Resume" : "Pause", ImVec2(btn_w, layout.control_h)))
        g_exp_paused = !g_exp_paused;

    y += layout.ov_pad + layout.control_h + layout.ov_pad;

    // Arbol de instancias: mismo scroll suave y misma barra propia que el menu.
    draw_text(fnt(efonts.inter.Text11), ImVec2(m_min.x + pad, y), colors.section_header, "Instances");
    y += dpi(18.0f);

    ImGui::SetCursorScreenPos(ImVec2(m_min.x, y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 0.0f));
    begin_child("##exp_instances", ImVec2(layout.exp_w, layout.exp_list_h - dpi(18.0f)));
}

static ImVec4 exp_icon_color(ExpIcon i)
{
    switch (i) {
        case ExpIcon::Service:      return ImVec4(0.45f, 0.62f, 0.95f, 1.f);
        case ExpIcon::Workspace:    return ImVec4(0.40f, 0.78f, 0.50f, 1.f);
        case ExpIcon::Folder:       return ImVec4(0.95f, 0.75f, 0.35f, 1.f);
        case ExpIcon::Model:        return ImVec4(0.62f, 0.68f, 0.78f, 1.f);
        case ExpIcon::Part:         return ImVec4(0.72f, 0.76f, 0.82f, 1.f);
        case ExpIcon::Mesh:         return ImVec4(0.55f, 0.72f, 0.88f, 1.f);
        case ExpIcon::Script:       return ImVec4(0.35f, 0.70f, 0.95f, 1.f);
        case ExpIcon::LocalScript:  return ImVec4(0.35f, 0.85f, 0.70f, 1.f);
        case ExpIcon::ModuleScript: return ImVec4(0.72f, 0.55f, 0.95f, 1.f);
        case ExpIcon::Player:       return ImVec4(0.40f, 0.80f, 0.90f, 1.f);
        case ExpIcon::Camera:       return ImVec4(0.65f, 0.65f, 0.72f, 1.f);
        case ExpIcon::Light:        return ImVec4(0.98f, 0.85f, 0.40f, 1.f);
        case ExpIcon::Sound:        return ImVec4(0.92f, 0.45f, 0.68f, 1.f);
        case ExpIcon::Gui:          return ImVec4(0.58f, 0.62f, 0.95f, 1.f);
        case ExpIcon::Value:        return ImVec4(0.45f, 0.85f, 0.60f, 1.f);
        case ExpIcon::Tool:         return ImVec4(0.85f, 0.60f, 0.40f, 1.f);
        default:                    return ImVec4(0.55f, 0.55f, 0.62f, 1.f);
    }
}

static void draw_exp_icon(ImVec2 c, float s, ExpIcon icon, float alpha)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 base = exp_icon_color(icon);
    const ImColor col = fade(base, alpha);
    const ImColor dim = fade(base, alpha * 0.45f);
    const float h = s * 0.5f;
    const float rr = ImMax(1.0f, s * 0.18f);
    const float th = ImMax(1.0f, dpi(1.3f));

    auto box = [&](float inset, ImColor cc) {
        edraw.rect_filled(ImVec2(c.x - h + inset, c.y - h + inset),
                          ImVec2(c.x + h - inset, c.y + h - inset), cc, rr);
    };
    auto lines = [&](int n) { // rayitas de "documento"
        for (int i = 0; i < n; i++) {
            const float ly = c.y - h * 0.42f + i * (s * 0.26f);
            edraw.line(ImVec2(c.x - h * 0.40f, ly), ImVec2(c.x + h * 0.40f, ly),
                       fade(ImVec4(1, 1, 1, 1), alpha * 0.85f), th);
        }
    };

    switch (icon) {
        case ExpIcon::Service:
            edraw.circle_filled(c, h * 0.92f, col, 20);
            edraw.circle_filled(c, h * 0.38f, fade(colors.background, alpha), 16);
            break;

        case ExpIcon::Workspace:
            box(0.f, dim);
            // "suelo" claro en la mitad inferior
            edraw.rect_filled(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y + h), col, rr,
                              ImDrawFlags_RoundCornersBottom);
            break;

        case ExpIcon::Folder:
            // pestaña + cuerpo
            edraw.rect_filled(ImVec2(c.x - h, c.y - h), ImVec2(c.x - h * 0.15f, c.y - h * 0.45f), col, rr);
            edraw.rect_filled(ImVec2(c.x - h, c.y - h * 0.62f), ImVec2(c.x + h, c.y + h), col, rr);
            break;

        case ExpIcon::Model:
            box(s * 0.22f, dim);
            edraw.rect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, rr, 0, th);
            break;

        case ExpIcon::Part:
            box(0.f, dim);
            edraw.rect_filled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y - h * 0.25f), col, rr,
                              ImDrawFlags_RoundCornersTop);
            break;

        case ExpIcon::Mesh: {
            // rombo (malla vista de canto)
            const ImVec2 p[4] = { ImVec2(c.x, c.y - h), ImVec2(c.x + h, c.y),
                                  ImVec2(c.x, c.y + h), ImVec2(c.x - h, c.y) };
            dl->AddConvexPolyFilled(p, 4, col);
            break;
        }

        case ExpIcon::Script:
        case ExpIcon::LocalScript:
        case ExpIcon::ModuleScript:
            box(0.f, col);
            lines(icon == ExpIcon::ModuleScript ? 2 : 3);
            break;

        case ExpIcon::Player:
            edraw.circle_filled(ImVec2(c.x, c.y - h * 0.34f), h * 0.40f, col, 16);
            // hombros
            edraw.rect_filled(ImVec2(c.x - h * 0.62f, c.y + h * 0.14f),
                              ImVec2(c.x + h * 0.62f, c.y + h), col, rr, ImDrawFlags_RoundCornersTop);
            break;

        case ExpIcon::Camera: {
            edraw.rect_filled(ImVec2(c.x - h, c.y - h * 0.55f), ImVec2(c.x + h * 0.35f, c.y + h * 0.7f), col, rr);
            const ImVec2 p[3] = { ImVec2(c.x + h * 0.42f, c.y - h * 0.35f),
                                  ImVec2(c.x + h, c.y - h * 0.7f),
                                  ImVec2(c.x + h, c.y + h * 0.55f) };
            dl->AddConvexPolyFilled(p, 3, col);
            break;
        }

        case ExpIcon::Light:
            edraw.circle_filled(c, h * 0.45f, col, 16);
            for (int i = 0; i < 6; i++) {
                const float a = i * (2.0f * IM_PI / 6.0f);
                edraw.line(ImVec2(c.x + ImCos(a) * h * 0.68f, c.y + ImSin(a) * h * 0.68f),
                           ImVec2(c.x + ImCos(a) * h * 1.0f,  c.y + ImSin(a) * h * 1.0f), col, th);
            }
            break;

        case ExpIcon::Sound: {
            edraw.rect_filled(ImVec2(c.x - h * 0.9f, c.y - h * 0.3f),
                              ImVec2(c.x - h * 0.3f, c.y + h * 0.3f), col, rr * 0.5f);
            const ImVec2 p[3] = { ImVec2(c.x - h * 0.35f, c.y),
                                  ImVec2(c.x + h * 0.25f, c.y - h * 0.85f),
                                  ImVec2(c.x + h * 0.25f, c.y + h * 0.85f) };
            dl->AddConvexPolyFilled(p, 3, col);
            edraw.circle(ImVec2(c.x + h * 0.25f, c.y), h * 0.72f, col, 16, th);
            break;
        }

        case ExpIcon::Gui:
            edraw.rect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, rr, 0, th);
            edraw.rect_filled(ImVec2(c.x - h * 0.45f, c.y - h * 0.45f),
                              ImVec2(c.x + h * 0.45f, c.y + h * 0.45f), col, rr * 0.6f);
            break;

        case ExpIcon::Value: {
            const ImVec2 p[4] = { ImVec2(c.x, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y),
                                  ImVec2(c.x, c.y + h * 0.85f), ImVec2(c.x - h * 0.85f, c.y) };
            dl->AddConvexPolyFilled(p, 4, dim);
            edraw.circle_filled(c, h * 0.3f, col, 12);
            break;
        }

        case ExpIcon::Tool:
            edraw.rect_filled(ImVec2(c.x - h * 0.28f, c.y - h), ImVec2(c.x + h * 0.28f, c.y + h), col, rr);
            edraw.rect_filled(ImVec2(c.x - h, c.y - h * 0.9f), ImVec2(c.x + h, c.y - h * 0.35f), dim, rr);
            break;

        default:
            edraw.circle_filled(c, h * 0.5f, col, 12);
            break;
    }
}

// ── Arbol ─────────────────────────────────────────────────────────────────────

// Filtro del buscador: sin distinguir mayusculas y por subcadena, como Studio.
static bool exp_match(const char* name)
{
    if (!g_exp_search_inst[0]) return true;
    if (!name) return false;
    for (const char* p = name; *p; p++) {
        const char* a = p; const char* b = g_exp_search_inst;
        while (*a && *b && (char)tolower((unsigned char)*a) == (char)tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}
static bool exp_filtering() { return g_exp_search_inst[0] != '\0'; }

// Fila comun de nodo y hoja. container => lleva flecha de desplegar.
static bool exp_row(const char* name, const char* class_name, ExpIcon icon, bool container)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(name);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID k_open = id ^ 0x0DEA1u;

    bool open = exp_filtering() ? true : st->GetBool(k_open, g_exp_depth == 0);

    if (!exp_match(name)) return open; // no se pinta, pero sus hijos si se visitan

    ImFont* f_name = fnt(efonts.inter.Text12);
    ImFont* f_cls  = fnt(efonts.inter.Text11);

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = layout.exp_row_h;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(ImVec2(w, h), 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return open;

    const float indent  = dpi(14.0f);
    const float arrow_w = dpi(9.0f);   // columna reservada a la flecha
    const float icon_s  = dpi(13.0f);
    const float depth_x = exp_filtering() ? 0.0f : g_exp_depth * indent;

    const float col_x  = pos.x + depth_x;
    const float arrow_x = col_x + arrow_w * 0.5f;                       // centro
    const float icon_x  = col_x + arrow_w + dpi(2.0f) + icon_s * 0.5f;  // centro
    const float text_x  = icon_x + icon_s * 0.5f + dpi(5.0f);           // borde izq.

    bool hovered, held;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) {
        // Click en la flecha despliega; en el resto de la fila, selecciona.
        const bool on_arrow = container && (ImGui::GetIO().MousePos.x < icon_x - dpi(3.0f));
        if (on_arrow && !exp_filtering()) {
            open = !open;
            st->SetBool(k_open, open);
        }
        else {
            g_exp_sel_id = id;
            g_exp_sel_name = name ? name : "";
            g_exp_sel_class = class_name ? class_name : "";
        }
    }
    // Doble click en cualquier sitio: desplegar, como en Studio.
    if (container && !exp_filtering() && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        open = !open;
        st->SetBool(k_open, open);
    }

    const bool selected = (g_exp_sel_id == id);
    const float sel_t = anim(anim_value(id), selected ? 1.0f : 0.0f);
    const float hov_t = anim(anim_hover(id), hovered ? 1.0f : 0.0f);
    const float alpha = ImGui::GetStyle().Alpha;

    const float bg_a = ImMax(sel_t, hov_t * 0.5f);
    if (bg_a > 0.001f)
        edraw.rect_filled(bb.Min, bb.Max,
                          fade(ImLerp(colors.surface_hov, colors.surface_sel, sel_t), bg_a),
                          ImMax(1.0f, dpi(5.0f)));
    // Guias de indentacion: una linea por cada nivel por encima del actual.
    if (!exp_filtering())
        for (int d = 0; d < g_exp_depth; d++) {
            const float gx = pos.x + d * indent + dpi(7.0f);
            edraw.line(ImVec2(gx, pos.y), ImVec2(gx, pos.y + h), fade(colors.divider, alpha), layout.border);
        }

    if (container) {
        const float t = anim(id ^ 0xA2201u, open ? 1.0f : 0.0f, 18.0f);
        // Triangulo que gira de "derecha" (cerrado) a "abajo" (abierto).
        const float sz = dpi(3.6f);
        const float ang = t * (IM_PI * 0.5f);
        const float ca = ImCos(ang), sa = ImSin(ang);
        ImVec2 p[3];
        const ImVec2 base[3] = { ImVec2(-sz * 0.6f, -sz), ImVec2(-sz * 0.6f, sz), ImVec2(sz * 0.9f, 0.0f) };
        for (int i = 0; i < 3; i++)
            p[i] = ImVec2(arrow_x + base[i].x * ca - base[i].y * sa,
                          pos.y + h * 0.5f + base[i].x * sa + base[i].y * ca);
        ImVec4 ac = ImLerp(colors.text_disabled, colors.text_sec, hov_t);
        ac.w *= alpha;
        ImGui::GetWindowDrawList()->AddTriangleFilled(p[0], p[1], p[2], ImColor(ac));
    }

    draw_exp_icon(ImVec2(icon_x, pos.y + h * 0.5f), icon_s, icon, alpha);

    const ImVec2 ns = measure(f_name, name);
    draw_text(f_name, ImVec2(text_x, pos.y + (h - ns.y) * 0.5f - dpi(1.0f)),
              ImLerp(ImLerp(colors.text, colors.text_primary, hov_t), colors.text_primary, sel_t), name);

    if (class_name && *class_name) {
        const ImVec2 cs = measure(f_cls, class_name);
        const float cx = bb.Max.x - dpi(8.0f) - cs.x;
        // Si no cabe junto al nombre se omite, antes que superponerlos.
        if (cx > text_x + ns.x + dpi(10.0f))
            draw_text(f_cls, ImVec2(cx, pos.y + (h - cs.y) * 0.5f - dpi(1.0f)),
                      ImLerp(colors.text_faint, colors.text_disabled, ImMax(sel_t, hov_t)), class_name);
    }
    return open;
}

bool explorer_node(const char* name, const char* class_name, ExpIcon icon)
{
    if (!g_explorer_open_frame) return false;
    if (ImGui::GetCurrentWindow()->SkipItems) return false;

    const bool open = exp_row(name, class_name, icon, true);
    if (open) g_exp_depth++;
    return open;
}

void explorer_node_end()
{
    if (!g_explorer_open_frame) return;
    if (g_exp_depth > 0) g_exp_depth--;
}

void explorer_item(const char* name, const char* class_name, ExpIcon icon)
{
    if (!g_explorer_open_frame) return;
    if (ImGui::GetCurrentWindow()->SkipItems) return;
    exp_row(name, class_name, icon, false);
}

const char* explorer_selected_name()  { return g_exp_sel_name.c_str(); }
const char* explorer_selected_class() { return g_exp_sel_class.c_str(); }

void explorer_properties(const char* selected_name)
{
    if (!g_explorer_open_frame) return;
    if (!selected_name || !*selected_name) selected_name = g_exp_sel_class.c_str();

    g_exp_depth = 0;      // por si el arbol quedo desbalanceado
    end_child();          // cierra el arbol
    ImGui::PopStyleVar(); // WindowPadding del arbol

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImVec2 m_min(win->Pos.x + layout.shadow_pad, win->Pos.y + layout.shadow_pad);
    const float pad = dpi(14.0f);
    const float y = m_min.y + layout.header_h + layout.ov_pad + layout.control_h + layout.ov_pad
                  + layout.exp_list_h;

    edraw.line(ImVec2(m_min.x, y), ImVec2(m_min.x + layout.exp_w, y), colors.divider, layout.border);

    // "Properties · <seleccionada>"
    ImFont* f = fnt(efonts.inter.Text11);
    const float ly = y + layout.ov_pad;
    float x = m_min.x + pad;
    const ImVec2 ps = measure(f, "Properties");
    draw_text(f, ImVec2(x, ly), colors.section_header, "Properties");
    x += ps.x + dpi(6.0f);
    const ImVec2 dsz = measure(f, "\xc2\xb7"); // punto medio
    draw_text(f, ImVec2(x, ly), colors.text_faint, "\xc2\xb7");
    x += dsz.x + dpi(6.0f);
    if (selected_name && *selected_name)
        draw_text(f, ImVec2(x, ly), colors.accent, selected_name);

    float yy = ly + dpi(18.0f);
    ImGui::SetCursorScreenPos(ImVec2(m_min.x + pad, yy));
    ImGui::PushItemWidth(layout.exp_w - pad * 2.0f);
    input_text("##exp_search_prop", "Search properties...", g_exp_search_prop, sizeof(g_exp_search_prop));
    ImGui::PopItemWidth();
    yy += layout.control_h + layout.ov_pad;

    ImGui::SetCursorScreenPos(ImVec2(m_min.x, yy));
    const float avail_h = (m_min.y + layout.header_h + layout.ov_pad + layout.control_h + layout.ov_pad
                           + layout.exp_list_h + layout.exp_props_h) - yy;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 0.0f));
    begin_child("##exp_props", ImVec2(layout.exp_w, ImMax(avail_h, dpi(40.0f))));
}

bool explorer_section(const char* title)
{
    if (!g_explorer_open_frame) return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImFont* f = fnt(efonts.inter.Bold11);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = layout.exp_prop_h;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiID id = window->GetID(title);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID k_open = id ^ 0x5EC0u;
    bool open = st->GetBool(k_open, true); // los grupos nacen desplegados

    ImGui::ItemSize(ImVec2(w, h), 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return open;

    bool hovered, held;
    if (ImGui::ButtonBehavior(bb, id, &hovered, &held)) {
        open = !open;
        st->SetBool(k_open, open);
    }

    const float hov_t = anim(anim_hover(id), hovered ? 1.0f : 0.0f);
    const float open_t = anim(anim_value(id), open ? 1.0f : 0.0f, 18.0f);
    const float alpha = ImGui::GetStyle().Alpha;

    if (hov_t > 0.001f)
        edraw.rect_filled(bb.Min, bb.Max, fade(colors.surface_hov, hov_t), ImMax(1.0f, dpi(5.0f)));

    // Mismo triangulo giratorio que en el arbol: cerrado apunta a la derecha.
    const float s = dpi(3.6f);
    const float ang = open_t * (IM_PI * 0.5f);
    const float ca = ImCos(ang), sa = ImSin(ang);
    const ImVec2 base[3] = { ImVec2(-s * 0.6f, -s), ImVec2(-s * 0.6f, s), ImVec2(s * 0.9f, 0.0f) };
    const float cx = pos.x + dpi(6.0f), ccy = pos.y + h * 0.5f;
    ImVec2 p[3];
    for (int i = 0; i < 3; i++)
        p[i] = ImVec2(cx + base[i].x * ca - base[i].y * sa, ccy + base[i].x * sa + base[i].y * ca);

    ImVec4 tri = ImLerp(colors.text_muted, colors.text_sec, hov_t);
    tri.w *= alpha;
    ImGui::GetWindowDrawList()->AddTriangleFilled(p[0], p[1], p[2], ImColor(tri));

    const ImVec2 ts = measure(f, title);
    draw_text(f, ImVec2(pos.x + dpi(15.0f), pos.y + (h - ts.y) * 0.5f - dpi(1.0f)),
              ImLerp(colors.text_sec, colors.text_primary, hov_t), title);

    return open;
}

void explorer_property(const char* name, const char* value)
{
    if (!g_explorer_open_frame) return;

    ImFont* f_n = fnt(efonts.inter.Text11);
    ImFont* f_v = fnt(efonts.inter.Text11);

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = layout.exp_prop_h;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const float split = pos.x + w * 0.46f;

    const ImVec2 ns = measure(f_n, name);
    draw_text(f_n, ImVec2(pos.x + dpi(14.0f), pos.y + (h - ns.y) * 0.5f - dpi(1.0f)),
              colors.text_muted, name);

    if (value && *value) {
        const ImVec2 vs = measure(f_v, value);
        const ImVec4 clip(split, pos.y, pos.x + w, pos.y + h);
        edraw.text(fnt(f_v), fsize(f_v), ImVec2(split, pos.y + (h - vs.y) * 0.5f - dpi(1.0f)),
                   colors.text, value, nullptr, 0.0f, &clip);
    }

    ImGui::Dummy(ImVec2(w, h));
}

void explorer_end(const char* status)
{
    if (!status || !*status) status = g_exp_sel_class.c_str();
    if (!g_explorer_begun) return;

    if (!g_explorer_open_frame) {
        // Begin devolvio false: no hay contenido que cerrar, solo la ventana.
        ImGui::End();
        ImGui::PopStyleVar(4);
        g_explorer_begun = false;
        return;
    }

    end_child();          // cierra las propiedades
    ImGui::PopStyleVar(); // WindowPadding de las propiedades

    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImVec2 m_min(win->Pos.x + layout.shadow_pad, win->Pos.y + layout.shadow_pad);
    const ImVec2 m_max(m_min.x + layout.exp_w, win->Pos.y + win->Size.y - layout.shadow_pad);

    // Sin divisor: igual que en la cabecera, la barra de estado se separa sola.
    const float fy = m_max.y - layout.footer_h;

    ImFont* f = fnt(efonts.inter.Text10);
    const float cy = fy + layout.footer_h * 0.5f;
    const float dx = m_min.x + dpi(14.0f) + dpi(3.0f);
    edraw.circle_filled(ImVec2(dx, cy), dpi(3.0f), colors.accent, 12);

    if (status && *status) {
        const ImVec2 ss = measure(f, status);
        draw_text(f, ImVec2(dx + dpi(9.0f), cy - ss.y * 0.5f - dpi(1.0f)), colors.text_sec, status);
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
    g_explorer_begun = false;
}

} // namespace egui
