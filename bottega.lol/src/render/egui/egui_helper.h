#pragma once
// egui_helper.h
//
// Usage reference for the whole egui SDK, in English. Nothing here is compiled
// into anything: include it or just read it. Every function listed below is
// declared in egui.h unless another header is named.
//
// The SDK is a thin layer over Dear ImGui: it never touches D3D outside of the
// ImGui render, it keeps its own immediate-mode API, and every widget is drawn
// by hand so the look does not depend on ImGui's style stack.
//
//
// ============================================================================
//  1. LIFECYCLE  —  where each call goes in your frame loop
// ============================================================================
//
//   egui::initialize();                     once, after the D3D device exists
//                                           and before the first NewFrame.
//                                           Loads fonts, uploads the embedded
//                                           images, registers the built-in
//                                           hotkeys.
//
//   loop:
//       egui::update_dpi();                 BETWEEN frames only (before
//                                           ImGui_ImplXXX_NewFrame). It may
//                                           rebuild the font atlas, and doing
//                                           that inside a frame leaves ImGui
//                                           pointing at freed fonts.
//       ImGui_ImplDX11_NewFrame();
//       ImGui_ImplWin32_NewFrame();
//       ImGui::NewFrame();
//
//       <your scene>                        the glass refracts whatever is in
//                                           the backbuffer, so anything you
//                                           draw before the menu shows through
//                                           it. See demo_backdrop() in main.cpp.
//
//       egui::menu();                       the window itself (HOME toggles it)
//       egui::watermark();                  overlays, in any order
//       egui::keybind_list_begin();
//       egui::keybind_list_end();
//       egui::spectator_list_begin();
//       ...
//       egui::spectator_list_end();
//       egui::explorer_begin(); ... egui::explorer_end();
//
//       ImGui::Render();
//       ImGui_ImplDX11_RenderDrawData(...);
//
//   on resize:   egui::glass_invalidate();  after recreating the render target
//   on shutdown: egui::glass_shutdown();    BEFORE releasing the device
//                egui::g_model3d.shutdown();
//
//
// ============================================================================
//  2. SETTINGS  —  egui_settings.h
// ============================================================================
//
//   egui::settings.project_name       text in the header (also the ImGui window id)
//   egui::settings.user_tag           text on the right of the header
//   egui::settings.state.menu_open    open/closed; the fade is handled for you
//   egui::settings.overlay.watermark  per-overlay switches
//   egui::settings.overlay.keybinds
//   egui::settings.overlay.spectators
//   egui::settings.overlay.explorer
//   egui::settings.dpi_scale          ask for a scale; update_dpi() applies it
//
//   egui::layout                      every measurement of the design, ALREADY
//                                     scaled by the current DPI. Use these
//                                     directly (layout.row_h, layout.control_h,
//                                     layout.card_pad_x, layout.pick_w ...).
//   egui::dpi(v)                      scale a loose design pixel value. Never
//                                     apply it to a layout field: those are
//                                     scaled already.
//   egui::colors                      color tokens (egui_colors.h). Only one
//                                     theme ships (Theme::Dark); set_theme()
//                                     interpolates instead of snapping.
//
//
// ============================================================================
//  3. LAYOUT  —  building a page
// ============================================================================
//
//   template_shell()                  draws the whole window: frame, header,
//                                     tab bar, both columns, footer, preview.
//                                     Called by menu(); call it yourself only
//                                     if you build your own window.
//   panel_left(w, h)                  left column: your feature cards
//   panel_right(w, h, title)          right column
//   panel_preview(w, h)               the 2D/3D preview panel
//
//   begin_group(id, width)            a card. Everything until end_group() goes
//   end_group()                       inside it; the background is drawn on a
//                                     separate draw channel, so the card knows
//                                     its real height before painting itself.
//   group_title(text)                 title row inside a card
//   group_tabs(id, items, n, &cur)    sub-tabs inside a card. Returns true on
//                                     change. Pair it with the switch pattern
//                                     used in panel_left().
//   section_label(text)               small caps separator inside a card
//   gap(h)                            vertical space, in design px
//   same_line_right(width)            put the next item flush to the right edge
//                                     of the current row, `width` wide. This is
//                                     how a checkbox gets a key pill or a color
//                                     swatch on its right.
//   begin_child(name, size)           scrolling region with the SDK scrollbar
//   end_child()
//
//
// ============================================================================
//  4. WIDGETS  —  all return true when the value changed
// ============================================================================
//
//   checkbox("Enabled", &b)
//   slider_float("Smoothness", &f, 1.f, 30.f, "%.1f")
//   combo("Effect", &i, "Explosion\0Confetti\0Sparks\0\0")
//   multi_combo("Parts", bool_array, "Head\0Torso\0Legs\0\0")
//   colorpicker("Color", float4)              label + swatch, opens the picker
//   color_button("##id", float4, alpha)       just the swatch
//   button("Match monitor DPI", size)
//   icon_button("Load", ICON_DOWNLOAD, size)  icon from icomoon/icomoon.h
//   input_text("Label", "hint", buf, size)
//   input_text_hint("hint", buf, size)        no label row
//   text_editor("Label", buf, size, {w,h})    multiline
//
//   The item width comes from ImGui::CalcItemWidth(), which begin_group() sets
//   to the card width. Set it yourself with ImGui::PushItemWidth() if you need
//   a narrower control.
//
//
// ============================================================================
//  5. HOTKEYS  —  the part that has real state behind it
// ============================================================================
//
// A hotkey is three things: a virtual-key code (int), a mode (int) and,
// optionally, the bool it drives.
//
//   mode 0  Always On   the feature is on, no key needed
//   mode 1  Hold        on while the key is physically down
//   mode 2  Toggle      each press flips it, and it stays
//
// Widgets:
//
//   key_pill("##id", &key)                    just the pill. Left click starts
//                                             capture ("..."); after that any
//                                             key or mouse button binds it,
//                                             including with the cursor still
//                                             over the pill. ESC cancels and
//                                             clears. Right click clears too,
//                                             but only when it is not capturing:
//                                             during capture it binds Mouse 2.
//   key_pill_mode("##id", &key, &mode)        same pill, plus a right-click menu
//                                             with Always On / Hold / Toggle.
//   hotkey("Fly Key", &key, &mode)            label row + key_pill_mode
//   hotkey_bind("Fly Key", &key, &mode, &on)  the same row, and the mode really
//                                             drives `on` every frame.
//
// Registry:
//
//   hotkey_register("Name", &key, &mode, &value)
//       Declares a hotkey without drawing it. Do this in your setup for every
//       bind you want listed from the start: a widget only registers itself the
//       first time its tab is actually drawn.
//
//   hotkey_poll()
//       Evaluates every registered hotkey. Runs once per frame no matter how
//       many times you call it; menu() and keybind_list_begin() already do.
//       It is skipped while a pill is capturing a key and while ImGui wants
//       text input, so binding a key never fires it.
//
//   hotkey_active("Name") -> bool             current state
//   hotkey_count() / hotkey_info(i, ...)      walk the registry. The last
//                                             out-param, `enabled`, is the bool
//                                             it drives (true when it has none).
//
// Rules worth knowing:
//   - The hotkey writes to its bool only when a key is bound AND the mode uses
//     that key (Hold or Toggle). With no bind, or on Always On, the checkbox
//     next to it stays in charge — otherwise Always On would nail the feature
//     to "on" and the checkbox would look broken.
//   - Toggle state starts from the bool's current value at registration time.
//   - The keybind overlay lists the registry on its own, so a hotkey shows up
//     there with no extra code. keybind_list_add() is still there for entries
//     that do not come from a widget.
//
// Example:
//
//     // setup
//     egui::hotkey_register("Fly Key", &vars.misc.pf_fly_key,
//                                      &vars.misc.pf_fly_key_mode,
//                                      &vars.misc.pf_fly);
//     // menu
//     egui::checkbox("Fly", &vars.misc.pf_fly);
//     egui::hotkey_bind("Fly Key", &vars.misc.pf_fly_key,
//                                  &vars.misc.pf_fly_key_mode,
//                                  &vars.misc.pf_fly);
//     // game code
//     if (vars.misc.pf_fly) do_fly();
//
//
// ============================================================================
//  6. OVERLAYS  —  drawn over the game, not inside the menu
// ============================================================================
//
//   watermark()                       name + fps + ms + clock, top right
//
//   keybind_list_begin()              bottom left, grows upward: a new bind
//   keybind_list_add(name, key, mode) pushes the older ones up instead of
//   keybind_list_end()                shifting everything down. Rows fade in and
//                                     out, and light up while their key is
//                                     active. Fed automatically from the hotkey
//                                     registry; add() is optional.
//                                     A bound key is always listed. An Always On
//                                     hotkey with no key is listed too whenever
//                                     its feature is enabled — it needs no key
//                                     to be on — and shows ON in the key column.
//
//   spectator_list_begin()            top right, under the watermark
//   spectator_list_add(name)
//   spectator_list_end()
//
//
// ============================================================================
//  7. EXPLORER  —  immediate-mode tree (INSERT toggles it)
// ============================================================================
//
//     egui::explorer_begin();
//     if (egui::explorer_node("Workspace", "Workspace", I::Workspace)) {
//         egui::explorer_item("Camera", "Camera", I::Camera);
//         egui::explorer_node_end();          // only when the node returned true
//     }
//     egui::explorer_properties();            // properties band, selected item
//     if (egui::explorer_section("Data")) {
//         egui::explorer_property("ClassName", egui::explorer_selected_class());
//     }
//     egui::explorer_end();
//
//   The selection lives inside the explorer: explorer_selected_name() and
//   explorer_selected_class() give you the current one. Icons come from
//   egui::ExpIcon.
//
//
// ============================================================================
//  8. GLASS  —  egui_glass.h
// ============================================================================
//
// Every frame the SDK copies the backbuffer just before ImGui paints, blurs it
// with a dual Kawase chain, and each frame (menu, preview, explorer, overlays)
// refracts that copy through a rounded-rect SDF.
//
//   egui::glass.enabled        turn the whole thing off
//   egui::glass.cards          also give the cards of begin_group() glass
//   egui::glass.blur           0 = sharp background, 1 = full blur
//   egui::glass.tint           how much window color goes on top (0.50)
//   egui::glass.refraction     px the bevel displaces the background read
//   egui::glass.thickness      bevel width, in px
//
//   glass_pane(dl, min, max, rounding, tint, alpha, inner)
//       Inserts a pane into a draw list. Returns false when it could not (no
//       device, glass off, background not captured yet) and then YOU paint the
//       opaque background instead — that is what every frame in the SDK does.
//       `inner` = the pane lives inside a window that already has glass.
//
// Anything you draw on top of a pane competes with a background you do not
// control. The SDK never solves that with an extra surface: it uses a one pixel
// shadow (see the overlays) or raises the opacity of the token instead.
//
//
// ============================================================================
//  9. IMAGES  —  egui_images.h
// ============================================================================
//
//   create_texture_from_memory(dev, data, size, &srv, &w, &h)
//       Decodes PNG/JPG from memory with WIC (no D3DX, no stb_image) and
//       uploads it. Ask for it during initialize(), never mid-frame:
//       decompressing a big image inside a frame shows up as a hitch.
//
//   egui::eimages.character / .background     already uploaded by initialize()
//
//
// ============================================================================
//  10. 3D MODEL  —  egui_model.h + model3d.h
// ============================================================================
//
// Paste your .obj as a byte array in egui_model.h, between the markers, and it
// is picked up automatically: the preview panel starts on the 3D tab and the
// orbit and auto-fit keep working. Leave the placeholder and the SDK draws its
// built-in procedural figure instead.
//
//   egui::g_model3d.load_obj("path.obj")              from disk
//   egui::g_model3d.load_obj_from_memory(data, size)  from a byte array
//   egui::g_model3d.auto_fit / .fill                  framing
//   egui::g_model3d.orbit_speed / .pitch_limit        drag behaviour
//   egui::g_model3d.bg[4]                             viewport background, alpha
//                                                     included (it is also the
//                                                     clear color of the 3D
//                                                     render target)
//
// Loading has to happen before the first render; init() runs on the first frame
// the 3D tab is visible.
//
//
// ============================================================================
//  11. DRAWING  —  when you write your own widget
// ============================================================================
//
//   egui::edraw.rect_filled(min, max, col, rounding, flags)
//   egui::edraw.rect(min, max, col, rounding, flags, thickness)
//   egui::edraw.rect_filled_multi_color_rounding(...)   rounded gradient fill
//   egui::edraw.circle / .circle_filled / .line / .polyline
//   egui::edraw.text(font, size, pos, col, text, end, wrap, clip)
//   egui::edraw.image(tex, min, max, uv0, uv1, tint)
//   egui::edraw.shadow_rect(min, max, col, thickness, rounding)
//   egui::edraw.list                                   target draw list; null =
//                                                      current window
//
//   Every one of them multiplies the color by ImGui's current style alpha, so a
//   widget fades with the menu for free.
//
//   Helpers (egui.h):
//     fnt(f) / fsize(f) / measure(f, text)     fonts, with the sub-pixel stretch
//     draw_text(f, pos, col, text, end)
//     fade(col, a) / mix(a, b, t)
//     anim(id, target, speed)                  per-id eased value, 0..1
//     anim_hover(id) / anim_value(id)          derived ids for those states
//     fade_to(cur, on, speed) / ramp_to(...)
//
//   The usual skeleton:
//
//     bool my_widget(const char* label, bool* v) {
//         ImGuiWindow* w = ImGui::GetCurrentWindow();
//         if (w->SkipItems) return false;
//         const ImVec2 pos = ImGui::GetCursorScreenPos();
//         const ImGuiID id = w->GetID(label);
//         const ImRect bb(pos, ImVec2(pos.x + width, pos.y + egui::layout.row_h));
//         ImGui::ItemSize(bb.GetSize(), 0.0f);
//         if (!ImGui::ItemAdd(bb, id)) return false;
//         bool hovered, held;
//         const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
//         ...draw with egui::edraw...
//         return pressed;
//     }
//
//   Two things that bite:
//     - Ask for the item first (ItemAdd) and draw after, so the drawing uses
//       this frame's value and not the previous one.
//     - Popups: anchor them yourself with ImGui::SetNextWindowPos() before
//       BeginPopup(), or ImGui drops them wherever the mouse was when they were
//       opened. begin_dropdown() and color_button() show the pattern, including
//       flipping the popup above when it does not fit below.
