#pragma once
#include "imgui.h"

namespace egui {

    enum class Theme { Dark };

    struct DesignTokens {
        ImVec4 primary       = ImVec4(179/255.f, 143/255.f, 228/255.f, 1.0f); // #B38FE4
        ImVec4 background    = ImVec4(18/255.f,  18/255.f,  20/255.f,  1.0f); // #121214  ventana
        ImVec4 page          = ImVec4(12/255.f,  12/255.f,  14/255.f,  1.0f); // #0C0C0E  fondo de pagina
        ImVec4 header        = ImVec4(18/255.f,  18/255.f,  20/255.f,  1.0f); // #121214
        ImVec4 divider       = ImVec4(31/255.f,  31/255.f,  34/255.f,  1.0f); // #1F1F22  separadores
        ImVec4 window_border = ImVec4(42/255.f,  42/255.f,  46/255.f,  1.0f); // #2A2A2E

        ImVec4 card          = ImVec4(24/255.f,  24/255.f,  26/255.f,  1.0f); // #18181A  tarjeta / grupo
        ImVec4 card_border   = ImVec4(35/255.f,  35/255.f,  38/255.f,  1.0f); // #232326

        ImVec4 control       = ImVec4(32/255.f,  32/255.f,  35/255.f,  1.0f); // #202023  combo / track / check
        ImVec4 control_hov   = ImVec4(41/255.f,  41/255.f,  45/255.f,  1.0f); // #29292D
        ImVec4 control_border= ImVec4(46/255.f,  46/255.f,  51/255.f,  1.0f); // #2E2E33
        ImVec4 checkbox_border = ImVec4(58/255.f, 58/255.f, 64/255.f,  1.0f); // #3A3A40

        ImVec4 surface_sel   = ImVec4(42/255.f,  42/255.f,  47/255.f,  1.0f); // #2A2A2F  tab activa
        ImVec4 surface_hov   = ImVec4(30/255.f,  30/255.f,  33/255.f,  1.0f); // #1E1E21
        ImVec4 surface_sec   = ImVec4(24/255.f,  24/255.f,  26/255.f,  1.0f); // #18181A  popups

        ImVec4 text_primary  = ImVec4(244/255.f, 244/255.f, 245/255.f, 1.0f); // #F4F4F5
        ImVec4 text          = ImVec4(212/255.f, 212/255.f, 216/255.f, 1.0f); // #D4D4D8
        ImVec4 text_sec      = ImVec4(161/255.f, 161/255.f, 170/255.f, 1.0f); // #A1A1AA
        ImVec4 text_muted    = ImVec4(113/255.f, 113/255.f, 122/255.f, 1.0f); // #71717A
        ImVec4 text_disabled = ImVec4(90/255.f,  90/255.f,  99/255.f,  1.0f); // #5A5A63
        ImVec4 text_faint    = ImVec4(70/255.f,  70/255.f,  77/255.f,  1.0f); // #46464D
        ImVec4 section_header= ImVec4(138/255.f, 138/255.f, 147/255.f, 1.0f); // #8A8A93

        ImVec4 tab_sel       = ImVec4(42/255.f,  42/255.f,  47/255.f,  1.0f); // #2A2A2F
        ImVec4 btn_bg        = ImVec4(32/255.f,  32/255.f,  35/255.f,  1.0f); // #202023
        ImVec4 border        = ImVec4(42/255.f,  42/255.f,  46/255.f,  1.0f); // #2A2A2E
        ImVec4 accent        = ImVec4(179/255.f, 143/255.f, 228/255.f, 1.0f); // #B38FE4
        ImVec4 online        = ImVec4(37/255.f,  211/255.f, 102/255.f, 1.0f); // #25D366
        ImVec4 scroll        = ImVec4(58/255.f,  58/255.f,  64/255.f,  1.0f); // #3A3A40
        ImVec4 swatch_border = ImVec4(1, 1, 1, 0.10f);

        struct Widgets {
            struct Checkbox {
                ImVec4 background         = ImVec4(32/255.f, 32/255.f, 35/255.f, 1.0f); // #202023
                ImVec4 outline_background = ImVec4(58/255.f, 58/255.f, 64/255.f, 1.0f); // #3A3A40
            } checkbox;
            struct Combo {
                ImVec4 background         = ImVec4(32/255.f, 32/255.f, 35/255.f, 1.0f);
                ImVec4 outline_background = ImVec4(46/255.f, 46/255.f, 51/255.f, 1.0f);
            } combo;
            struct Button {
                ImVec4 background         = ImVec4(32/255.f, 32/255.f, 35/255.f, 1.0f);
                ImVec4 background_light   = ImVec4(41/255.f, 41/255.f, 45/255.f, 1.0f);
                ImVec4 outline_background = ImVec4(46/255.f, 46/255.f, 51/255.f, 1.0f);
            } button;
            struct Input {
                ImVec4 background         = ImVec4(32/255.f, 32/255.f, 35/255.f, 1.0f);
                ImVec4 background_light   = ImVec4(41/255.f, 41/255.f, 45/255.f, 1.0f);
                ImVec4 outline_background = ImVec4(46/255.f, 46/255.f, 51/255.f, 1.0f);
            } input;
            struct Slider {
                ImVec4 background         = ImVec4(32/255.f, 32/255.f, 35/255.f, 1.0f);
                ImVec4 outline_background = ImVec4(46/255.f, 46/255.f, 51/255.f, 1.0f);
            } slider;
            struct Keybind {
                ImVec4 text             = ImVec4(113/255.f, 113/255.f, 122/255.f, 1.0f); // #71717A
                ImVec4 text_disabled    = ImVec4(90/255.f,  90/255.f,  99/255.f,  1.0f);
                ImVec4 background_hover = ImVec4(41/255.f,  41/255.f,  45/255.f,  1.0f);
                ImVec4 background       = ImVec4(32/255.f,  32/255.f,  35/255.f,  1.0f);
            } keybind;
        } widgets;
    };
    static_assert(sizeof(DesignTokens) % sizeof(float) == 0, "DesignTokens debe ser solo ImVec4");

    inline DesignTokens theme_tokens(Theme)
    {
        return DesignTokens();
    }

    // Paleta en uso. Es la animada: egui la interpola hacia el tema elegido.
    inline DesignTokens colors;
}
