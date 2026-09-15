#pragma once
//
// egui_glass — fondo de cristal para los marcos del SDK.
// Basado en la idea de github.com/Pondot/liquidDX11 (MIT).
//
//     glass_invalidate();   despues de recrear el render target (resize)
//     glass_shutdown();     antes de soltar el device
//
#include "imgui.h"
#include "egui_settings.h"
#include <d3d11.h>

namespace egui {

    struct Glass {
        bool  enabled    = true;
        bool  cards      = true;    // ...tambien las tarjetas de begin_group()

        float blur       = 1.00f;   // 0 = fondo nitido, 1 = todo el desenfoque
        float tint       = 0.50f;   // cuanto color de la ventana va por encima

        float tint_light = 0.45f;
        float thickness  = 4.0f;    // ancho del bisel del borde, en px
        float refraction = 25.0f;   // px que desplaza el bisel la lectura del fondo
        float chroma     = 0.83f;   // separacion RGB dentro de la refraccion
        float grain      = 0.00f;   // ruido, mata el banding del degradado

        float sheen      = 0.00f;   // brillo del canto (fresnel)
        float specular   = 0.00f;   // reflejo de la luz en el bisel

        // Direccion de la luz en pantalla (y positiva = hacia abajo).
        float light_x    = -0.55f;
        float light_y    = -0.83f;
    };
    inline Glass glass;

    bool glass_pane(ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, float rounding,
                    const ImVec4& tint_col, float alpha, bool inner = false);

    void glass_capture();

    // El swapchain ha cambiado de tamaño: hay que rehacer las texturas.
    void glass_invalidate();

    // Suelta todo. Obligatorio antes de destruir el device.
    void glass_shutdown();
}
