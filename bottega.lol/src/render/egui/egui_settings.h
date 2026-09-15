#pragma once
#include "imgui.h"
#include "egui_colors.h"
#include <d3d11.h>

inline ID3D11Device* g_pd3dDevice = nullptr;
inline ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
inline IDXGISwapChain* g_pSwapChain = nullptr;
inline UINT            g_ResizeWidth = 0, g_ResizeHeight = 0;
inline ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

namespace egui {

    struct Layout {
        // radios
        float window_rounding  = 10.0f;
        float card_rounding    = 8.0f;
        float control_rounding = 6.0f;

        // marco exterior (hueco reservado dentro de la ventana ImGui para la sombra)
        float shadow_pad = 26.0f;

        // paneles
        float main_w    = 700.0f;
        float preview_w = 290.0f;
        float preview_h = 385.0f;
        float panel_gap = 20.0f;

        // bandas de la ventana principal
        float header_h  = 43.0f;
        float tabbar_h  = 46.0f;
        float content_h = 600.0f;
        float footer_h  = 36.0f;

        // navegacion superior
        float tab_h     = 26.0f;
        float tab_pad_x = 12.0f;
        float tab_gap   = 4.0f;

        // zona de contenido
        float content_pad = 14.0f;
        float col_gap     = 14.0f;

        // tarjetas
        float card_gap   = 12.0f;
        float card_pad_x = 14.0f;
        float card_pad_t = 12.0f;
        float card_pad_b = 14.0f;

        // widgets
        float row_gap    = 7.0f;   // ItemSpacing.y
        float row_h      = 18.0f;  // alto de una fila de checkbox
        float check_box  = 14.0f;
        float check_gap  = 10.0f;
        float control_h  = 32.0f;  // combo / input / boton
        float track_h    = 6.0f;   // slider
        float swatch     = 16.0f;
        float pill_w     = 40.0f;  // ancho MINIMO de la pastilla de keybind
        float pill_pad_x = 10.0f;  // aire a los lados del texto de la pastilla
        float pill_h     = 20.0f;
        float label_gap  = 6.0f;   // hueco entre el titulo de un widget y su caja
        float widget_top = 7.0f;   // aire extra encima de un widget con titulo

        // color picker
        float pick_w   = 176.0f;
        float pick_bar = 12.0f;
        float pick_gap = 6.0f;
        float pick_pad = 8.0f;

        // overlays (watermark, keybinds, spectators)
        float ov_pad       = 10.0f;  // padding interior
        float ov_gap       = 6.0f;   // separacion entre filas
        float ov_margin    = 14.0f;  // separacion al borde de la pantalla
        float ov_rounding  = 8.0f;
        float ov_row_h     = 22.0f;  // da de si para la pastilla de tecla (pill_h)

        // explorer
        float exp_w        = 300.0f;
        float exp_list_h   = 210.0f; // alto del arbol de instancias
        float exp_props_h  = 270.0f; // alto de la banda de propiedades
        float exp_row_h    = 26.0f;  // fila del arbol
        float exp_prop_h   = 22.0f;  // fila de propiedad

        // barra de scroll propia (va superpuesta sobre el padding, no reserva ancho)
        float scroll_w      = 4.0f;
        float scroll_margin = 5.0f;  // separacion al borde de la ventana
        float scroll_min    = 28.0f; // alto minimo del pulgar

        // grosores
        float border   = 1.0f;     // borde de ventana / tarjeta / control
        float shadow   = 24.0f;    // grosor de la sombra exterior
    };

    inline Layout layout;
    inline const Layout layout_design;

    static_assert(sizeof(Layout) % sizeof(float) == 0, "Layout debe ser solo floats");

    struct Settings {
        ImVec2 size = ImVec2(1062, 777); // recalculado en menu() a partir de layout
        const char* project_name = "bottega.lol";
        const char* user_tag     = "bottega";
        int rounding = 10;

        Theme theme = Theme::Dark;

        float dpi_scale = 1.0f;
        float current_dpi_scale = 1.0f;
        float scale_current = 1.0f;

        struct Alpha { float menu = 0.0f; } alpha;

        struct Overlays {
            bool watermark  = false;
            bool keybinds   = false;
            bool spectators = false;
            bool explorer   = false;   // HOME lo alterna (el menu va con INSERT)
        } overlay;

        struct State {
            bool menu_open = false;
            ImVec2 menu_window_center = ImVec2(-1.f, -1.f);
            ImVec2 menu_window_pos = ImVec2(0, 0);
        } state;

        // Ventana sin decoracion: el marco lo dibuja egui a mano.
        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings;
    };

    inline Settings settings;

    inline float dpi(float v) { return v * settings.current_dpi_scale; }

    inline float font_stretch() {
        return settings.scale_current > 0.0f
            ? settings.current_dpi_scale / settings.scale_current
            : 1.0f;
    }
}
