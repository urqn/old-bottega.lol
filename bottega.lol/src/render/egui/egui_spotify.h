#pragma once
#include "imgui.h"

namespace egui {

    // feedback de Windows Media Control (SMTC): el micrófono de la sesion de
    // Spotify global. spotify_update() refresca el estado, spotify_render()
    // pinta la tarjeta y spotify_hovered() dice si un punto (en coords del
    // cliente de la overlay) cae encima de la tarjeta, para que la overlay
    // deje de ser transparente justo cuando hay raton encima.
    void spotify_update();
    void spotify_render();
    bool spotify_hovered(const ImVec2& client_pos);
}