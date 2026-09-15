#pragma once
//

class c_variables
{
public:

    struct
    {
        float dpi = 1.f;
        int stored_dpi = 100;
        bool dpi_changed = true;
        bool menu_open = false;
        int active_tab = 0;
        int active_subtab[6] = { 0, 0, 0, 0, 0, 0 };
        float tab_transition_alpha = 1.0f;
        float subtab_transition_alpha = 1.0f;

        int preview_mode = 0;
        float preview_alpha = 0.0f;

        float menu_open_alpha = 0.0f;
        bool menu_was_closed = true;

        bool show_playerlist = false;
        bool show_playerbar = false;
        bool show_spotify = false;
        bool show_active_hotkeys = false;
        bool show_watermark = false;

        bool playerlist_spawned = false;
        bool playerbar_spawned = false;
        bool spotify_spawned = false;
        bool active_hotkeys_spawned = false;
        bool watermark_spawned = false;

        float playerlist_spawn_alpha = 0.0f;
        float playerbar_spawn_alpha = 0.0f;
        float spotify_spawn_alpha = 0.0f;
        float active_hotkeys_spawn_alpha = 0.0f;
        float watermark_spawn_alpha = 0.0f;
    } gui;

    struct
    {
        bool enabled = false;

        bool box = false;
        float box_color[4] = { 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 1.0f };
        float box_thickness = 2.0f;
        int box_style = 0;
        bool box_gradient = false;
        int box_bounding = 0;   // 0 parts = caja de las partes del cuerpo, 1 mesh = caja del mesh completo
        float box_color_secondary[4] = { 1.0f, 0.5f, 0.8f, 1.0f };
        bool box_filled = false;
        float box_filled_top_color[4] = { 1.0f, 0.0f, 0.0f, 0.3f };
        float box_filled_bottom_color[4] = { 0.0f, 0.0f, 1.0f, 0.3f };

        bool box_glow = false;
        float box_glow_color[4] = { 0.5f, 0.0f, 1.0f, 0.5f };
        float box_glow_strength = 1.0f;

        bool name = false;
        float name_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float name_size = 12.0f;
        int name_flags = 0;

        bool health = false;
        float health_width = 3.0f;
        float health_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

        bool health_text = false;
        float health_text_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        bool flags_enabled = false;
        bool flag_options[10] = { false };
        float flags_spacing = 12.0f;
        float flags_font_size = 10.0f;

        bool distance = false;
        float distance_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool distance_fade_enabled = false;
        float distance_fade_start = 100.0f;
        float distance_fade_max = 150.0f;

        bool weapon = false;
        float weapon_color[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

        bool skeleton = false;
        float skeleton_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool skeleton_glow = false;

        bool head_dot = false;
        float head_dot_color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        float head_dot_size = 4.0f;

        bool look_direction = false;
        float look_direction_color[4] = { 0.0f, 1.0f, 1.0f, 1.0f };
        float look_direction_length = 10.0f;

        bool visual_check = false;
        float vischeck_visible_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        float vischeck_hidden_color[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

        bool outline_enabled = false;
        int outline_flags = 15;

        bool radar_enabled = false;
        bool radar_show_names = true;
        bool radar_show_distance = true;
        float radar_size = 200.0f;
        float radar_range = 150.0f;
        float radar_alpha = 0.9f;

        bool oov_arrows = false;
        int oov_arrow_style = 0;
        float oov_arrow_size = 20.0f;
        float oov_arrow_distance = 100.0f;
        float oov_arrow_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        bool avatar_enabled = false;
        float avatar_size = 40.0f;
        float avatar_outline_color[4] = { 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 0.78f };
        float avatar_outline_thickness = 2.0f;

        bool player_info_box = false;
        bool local_player = false;

        bool snaplines = false;
        int snaplines_position = 1;
        float snaplines_color[4] = { 1.0f, 1.0f, 1.0f, 0.8f };
        float snaplines_thickness = 1.0f;
        bool snaplines_gradient = false;
        float snaplines_gradient_color[4] = { 0.5f, 0.5f, 1.0f, 0.3f };

        bool ground_circle = false;
        float ground_circle_radius = 3.0f;
        int ground_circle_segments = 64;
        float ground_circle_color[4] = { 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 0.6f };
        float ground_circle_color2[4] = { 0.3f, 0.7f, 1.0f, 0.3f };
        bool ground_circle_gradient = true;
        bool ground_circle_animate = true;
        float ground_circle_speed = 1.0f;
        bool ground_circle_fill = false;
        float ground_circle_thickness = 2.0f;

        bool damage_indicators = false;
        float damage_color[4] = { 1.0f, 0.2f, 0.2f, 1.0f };
        float damage_font_size = 18.0f;
        float damage_float_speed = 50.0f;
        float damage_lifetime = 2.0f;
        bool damage_outline = true;
        float damage_outline_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        bool pf_player_counter = false;

        bool chams_enabled = false;
        int chams_style = 0;         // 0 dx flat (cajas por parte), 1 dx modes, 2 cpu styles, 3 parts
        int chams_style2 = 0;        // cpu shader style index
        int chams_dx_mode = 2;
        int chams_occluded_mode = 2;
        float chams_fill_color[4] = { 1.0f, 0.3f, 0.6f, 1.0f };
        float chams_occluded_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        bool chams_outline = true;
        float chams_outline_color[4] = { 1.0f, 0.6f, 0.2f, 1.0f };
        float chams_outline_fade = 1.0f;
        int chams_outline_style = 0;
        bool chams_occluded = false;
        float chams_glow = 0.35f;
        float chams_local_off = 1.2f;

        bool engine_chams_enabled = false;
        int engine_chams_style = 0; // 0 default, 1 ghost, 2 wireframe, 3 colored frame, 4 colored, 5 smoke no shadow, 6 smoke, 7 invisible
        float engine_chams_color[4] = { 1.0f, 0.3f, 0.6f, 1.0f };
        int engine_ghost_color_idx = 0;
    } esp;

    struct
    {
        bool enabled = false;

        bool aim_key_active = false;
        int aim_key = -1;
        int aim_key_mode = 1;   // 0 always on, 1 hold, 2 toggle

        bool fov_check = false;
        float fov_radius = 100.0f;

        bool fov_circle_enabled = false;
        int fov_style = 0;
        float fov_color[4] = { 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 0.6f };
        bool fov_filled = false;

        bool crosshair_enabled = false;
        float crosshair_size = 8.0f;
        float crosshair_color[4] = { 1.0f, 1.0f, 1.0f, 0.8f };

        float smoothness = 5.0f;
        bool aim_at_head = true;
        bool aim_at_torso = false;
        int aim_method = 0;     // 0 mouse (mover el cursor), 1 camera (escribir la camara)

        bool show_target_indicator = false;
        bool draw_line_to_target = false;
        float target_line_color[4] = { 1.0f, 0.2f, 0.2f, 0.6f };

        bool target_team = false;
        bool target_knocked = false;
        float max_distance = 500.0f;

        bool teamcheck = true;

        bool pf_silent_enabled = false;
        bool pf_silent_auto_shoot = false;
        float pf_silent_smoothness = 1.0f;
        bool pf_silent_prediction = false;
        float pf_silent_prediction_amount = 0.5f;
        int pf_silent_method = 2;      // 0 viewport, 1 mouse, 2 raycast, 3 magic bullet, 4 phantom forces
        bool pf_silent_mb_force = false;
        float pf_silent_bullet_speed = 1200.0f;

        int pf_silent_key = 0;       // 0 = same as the aimbot key
        int pf_silent_key_mode = 1;  // 0 always on, 1 hold, 2 toggle
        bool pf_silent_key_active = false;

        int pf_force_magic_key = 0;    // 0 = off (engine-side force keybind, like paid)
        int pf_force_magic_mode = 1;   // 0 always on, 1 hold, 2 toggle
        bool pf_force_magic_active = false;

    } aimbot;

    struct
    {
        bool world_ambient_enabled = false;
        float world_ambient_color[3] = { 1.0f, 1.0f, 1.0f };
        float world_outdoor_ambient_color[3] = { 1.0f, 1.0f, 1.0f };

        bool world_atmosphere_enabled = false;
        float world_atmos_color[3] = { 0.75f, 0.85f, 1.0f };
        float world_atmos_decay[3] = { 0.4f, 0.4f, 0.4f };
        float world_atmos_density = 0.0f;
        float world_atmos_glare = 0.0f;
        float world_atmos_haze = 0.0f;
        float world_atmos_offset = 0.0f;

        bool world_fog_enabled = false;
        float world_fog_start = 0.0f;
        float world_fog_end = 100000.0f;
        float world_fog_color[3] = { 0.75f, 0.75f, 0.75f };

        bool world_brightness_enabled = false;
        float world_brightness = 5.0f;

        bool world_exposure_enabled = false;
        float world_exposure = 0.0f;

        bool world_fov_enabled = false;
        float world_fov = 70.0f;

        bool world_shadows_disabled = false;

        bool world_clocktime_enabled = false;
        float world_clocktime_value = 12.0f;

        bool particles_enabled = false;
        int particle_style = 0;
        int particle_count = 300;
        float particle_speed = 1.0f;
        float particle_wind = 1.0f;
        float particle_glow = 1.0f;

    } world;

    struct
    {
        bool bullet_tracers_enabled = false;
        int bullet_tracer_style = 0;
        float bullet_tracer_color[4] = { 179.0f / 255.0f, 143.0f / 255.0f, 228.0f / 255.0f, 1.0f };
        float bullet_tracer_thickness = 2.0f;
        float bullet_tracer_lifetime = 1.0f;

        bool hitmarker_enabled = false;
        int hitmarker_style = 0;
        float hitmarker_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float hitmarker_headshot_color[4] = { 1.0f, 0.2f, 0.2f, 1.0f };
        float hitmarker_size = 15.0f;
        float hitmarker_thickness = 2.0f;
        float hitmarker_duration = 0.6f;
        bool hitmarker_show_damage = false;

        bool hitsound_enabled = false;
        int hitsound_index = 1;
        float hitsound_volume = 50.0f;

    } visuals;

    struct
    {
        bool kill_effects_enabled = false;
        int kill_effects_type = 1;
        int kill_effects_particle_count = 50;
        float kill_effects_lifetime = 3.0f;
        bool kill_effects_glow = true;
        bool kill_effects_test = false;

        bool hitbox_expander_enabled = false;
        float hitbox_expander_size = 2.0f;
        bool hitbox_expander_no_collide = false;
        bool hitbox_expander_transparent = false;
        float hitbox_expander_transparency = 0.7f;
        bool hitbox_expander_visualize = false;

        bool pf_fly = false;
        int pf_fly_key = -1;
        int pf_fly_key_mode = 2;
        int pf_fly_mode = 0;
        float pf_fly_speed = 25.0f;

        bool walkspeed = false;
        float walkspeed_value = 16.0f;

        bool noclip = false;
        int noclip_key = -1;
        int noclip_key_mode = 2;

        bool jump_power = false;
        float jump_power_value = 50.0f;

        bool third_person = false;
        float third_person_distance = 15.0f;

        bool explorer_enabled = false;

    } misc;

};

inline c_variables vars;
