#include "settings.h"
#include <fstream>
#include <windows.h>

static std::string w_to_utf8(const std::wstring &s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string out; out.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, NULL, NULL);
    return out;
}

static std::wstring utf8_to_w(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring out; out.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

Settings SettingsManager::load(const std::wstring &path) {
    Settings s; // defaults
    std::string p = w_to_utf8(path);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return s;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    // Very small hand-rolled integer extractor: looks for "key" then ':' then optional whitespace then an integer (possibly multi-digit, optional leading minus though not used here)
    auto extractInt = [&](const std::string &key, int &dst){
        size_t pos = raw.find("\"" + key + "\"");
        if (pos == std::string::npos) return;
        pos = raw.find(':', pos);
        if (pos == std::string::npos) return;
        pos++;
        while (pos < raw.size() && (raw[pos]==' '||raw[pos]=='\t')) pos++;
        bool neg=false; if (pos<raw.size() && raw[pos]=='-'){ neg=true; pos++; }
        long val=0; bool any=false;
        while (pos < raw.size() && raw[pos]>='0' && raw[pos]<='9') { any=true; val = val*10 + (raw[pos]-'0'); pos++; }
        if (!any) return; if (neg) val = -val; dst = (int)val;
    };
    extractInt("control_mode", s.control_mode);
    extractInt("ai", s.ai);
    extractInt("renderer", s.renderer);
    extractInt("quality", s.quality);
    extractInt("game_mode", s.game_mode);
    
    // Load game mode config (new system)
    int temp_int = 0;
    extractInt("gm_multiball", temp_int); s.mode_config.multiball = (temp_int != 0);
    extractInt("gm_multiball_count", s.mode_config.multiball_count);
    extractInt("gm_obstacles", temp_int); s.mode_config.obstacles = (temp_int != 0);
    extractInt("gm_obstacles_moving", temp_int); s.mode_config.obstacles_moving = (temp_int != 0);
    extractInt("gm_blackholes", temp_int); s.mode_config.blackholes = (temp_int != 0);
    extractInt("gm_blackholes_moving", temp_int); s.mode_config.blackholes_moving = (temp_int != 0);
    extractInt("gm_blackhole_count", s.mode_config.blackhole_count);
    extractInt("gm_three_enemies", temp_int); s.mode_config.three_enemies = (temp_int != 0);
    
    // Validate game mode config
    if(s.mode_config.multiball_count < 2) s.mode_config.multiball_count = 2;
    if(s.mode_config.multiball_count > 5) s.mode_config.multiball_count = 5;
    if(s.mode_config.blackhole_count < 1) s.mode_config.blackhole_count = 1;
    if(s.mode_config.blackhole_count > 5) s.mode_config.blackhole_count = 5;
    
    extractInt("pt_rays_per_frame", s.pt_rays_per_frame);
    extractInt("pt_max_bounces", s.pt_max_bounces);
    extractInt("pt_internal_scale", s.pt_internal_scale);
    extractInt("pt_roughness", s.pt_roughness);
    extractInt("pt_emissive", s.pt_emissive);
    extractInt("pt_paddle_emissive", s.pt_paddle_emissive);
    extractInt("pt_force_4wide_simd", s.pt_force_4wide_simd);
    extractInt("pt_accum_alpha", s.pt_accum_alpha);
    extractInt("pt_denoise_strength", s.pt_denoise_strength);
    extractInt("pt_force_full_pixel_rays", s.pt_force_full_pixel_rays);
    extractInt("pt_use_ortho", s.pt_use_ortho);
    extractInt("pt_rr_enable", s.pt_rr_enable);
    extractInt("pt_rr_start_bounce", s.pt_rr_start_bounce);
    extractInt("pt_rr_min_prob_pct", s.pt_rr_min_prob_pct);
    extractInt("pt_fanout_enable", s.pt_fanout_enable);
    extractInt("pt_fanout_cap", s.pt_fanout_cap);
    extractInt("pt_fanout_abort", s.pt_fanout_abort);
    // New soft shadow / PBR fields (backward compatible: defaults remain if not present)
    extractInt("pt_soft_shadow_samples", s.pt_soft_shadow_samples);
    extractInt("pt_light_radius_pct", s.pt_light_radius_pct);
    extractInt("pt_pbr_enable", s.pt_pbr_enable);
    // Phase 5: Advanced optimization settings
    extractInt("pt_tile_size", s.pt_tile_size);
    extractInt("pt_use_blue_noise", s.pt_use_blue_noise);
    extractInt("pt_use_cosine_weighted", s.pt_use_cosine_weighted);
    extractInt("pt_use_stratified", s.pt_use_stratified);
    extractInt("pt_use_halton", s.pt_use_halton);
    extractInt("pt_adaptive_shadows", s.pt_adaptive_shadows);
    extractInt("pt_use_bilateral", s.pt_use_bilateral);
    extractInt("pt_bilateral_sigma_space", s.pt_bilateral_sigma_space);
    extractInt("pt_bilateral_sigma_color", s.pt_bilateral_sigma_color);
    extractInt("pt_light_cull_distance", s.pt_light_cull_distance);
    // Recording mode
    extractInt("recording_mode", s.recording_mode);
        extractInt("player_mode", s.player_mode);
        extractInt("recording_fps", s.recording_fps);
        extractInt("recording_duration", s.recording_duration);
        extractInt("physics_mode", s.physics_mode);
        extractInt("speed_mode", s.speed_mode);
        extractInt("hud_show_play", s.hud_show_play);
        extractInt("hud_show_record", s.hud_show_record);
        if(s.physics_mode<0||s.physics_mode>1) s.physics_mode=1;
        if(s.speed_mode<0||s.speed_mode>1) s.speed_mode=0;
        s.hud_show_play = s.hud_show_play?1:0;
        s.hud_show_record = s.hud_show_record?1:0;
        if(s.recording_fps < 15) s.recording_fps = 15; else if(s.recording_fps > 60) s.recording_fps = 60;
        if(s.player_mode < 0 || s.player_mode > 2) s.player_mode = 0;
    // Defensive clamp after load
    if(s.pt_soft_shadow_samples < 1) s.pt_soft_shadow_samples = 1; else if(s.pt_soft_shadow_samples > 64) s.pt_soft_shadow_samples = 64;
    if(s.pt_light_radius_pct < 10) s.pt_light_radius_pct = 10; else if(s.pt_light_radius_pct > 500) s.pt_light_radius_pct = 500;
    if(s.pt_pbr_enable!=0) s.pt_pbr_enable = 1;
    return s;
}

bool SettingsManager::save(const std::wstring &path, const Settings &s) {
    std::string p2 = w_to_utf8(path);
    std::ofstream ofs(p2, std::ios::trunc | std::ios::binary);
    if (!ofs) return false;
    ofs << "{\n";
    ofs << "  \"control_mode\": " << s.control_mode << ",\n";
    ofs << "  \"ai\": " << s.ai << ",\n";
    ofs << "  \"renderer\": " << s.renderer << ",\n";
    ofs << "  \"quality\": " << s.quality << ",\n"; // legacy
    ofs << "  \"game_mode\": " << s.game_mode << ",\n";
    
    // Save game mode config (new system)
    ofs << "  \"gm_multiball\": " << (s.mode_config.multiball ? 1 : 0) << ",\n";
    ofs << "  \"gm_multiball_count\": " << s.mode_config.multiball_count << ",\n";
    ofs << "  \"gm_obstacles\": " << (s.mode_config.obstacles ? 1 : 0) << ",\n";
    ofs << "  \"gm_obstacles_moving\": " << (s.mode_config.obstacles_moving ? 1 : 0) << ",\n";
    ofs << "  \"gm_blackholes\": " << (s.mode_config.blackholes ? 1 : 0) << ",\n";
    ofs << "  \"gm_blackholes_moving\": " << (s.mode_config.blackholes_moving ? 1 : 0) << ",\n";
    ofs << "  \"gm_blackhole_count\": " << s.mode_config.blackhole_count << ",\n";
    ofs << "  \"gm_three_enemies\": " << (s.mode_config.three_enemies ? 1 : 0) << ",\n";
    
    ofs << "  \"pt_rays_per_frame\": " << s.pt_rays_per_frame << ",\n";
    ofs << "  \"pt_max_bounces\": " << s.pt_max_bounces << ",\n";
    ofs << "  \"pt_internal_scale\": " << s.pt_internal_scale << ",\n";
    ofs << "  \"pt_roughness\": " << s.pt_roughness << ",\n";
    ofs << "  \"pt_emissive\": " << s.pt_emissive << ",\n";
    ofs << "  \"pt_paddle_emissive\": " << s.pt_paddle_emissive << ",\n";
    ofs << "  \"pt_force_4wide_simd\": " << s.pt_force_4wide_simd << ",\n";
    ofs << "  \"pt_accum_alpha\": " << s.pt_accum_alpha << ",\n";
    ofs << "  \"pt_denoise_strength\": " << s.pt_denoise_strength << ",\n";
    ofs << "  \"pt_force_full_pixel_rays\": " << s.pt_force_full_pixel_rays << ",\n";
    ofs << "  \"pt_use_ortho\": " << s.pt_use_ortho << ",\n";
    ofs << "  \"pt_rr_enable\": " << s.pt_rr_enable << ",\n";
    ofs << "  \"pt_rr_start_bounce\": " << s.pt_rr_start_bounce << ",\n";
    ofs << "  \"pt_rr_min_prob_pct\": " << s.pt_rr_min_prob_pct << ",\n";
    ofs << "  \"pt_fanout_enable\": " << s.pt_fanout_enable << ",\n";
    ofs << "  \"pt_fanout_cap\": " << s.pt_fanout_cap << ",\n";
    ofs << "  \"pt_fanout_abort\": " << s.pt_fanout_abort << ",\n";
    ofs << "  \"pt_soft_shadow_samples\": " << s.pt_soft_shadow_samples << ",\n";
    ofs << "  \"pt_light_radius_pct\": " << s.pt_light_radius_pct << ",\n";
    ofs << "  \"pt_pbr_enable\": " << s.pt_pbr_enable << ",\n";
    ofs << "  \"recording_mode\": " << s.recording_mode << "\n";
        ofs << "  \"player_mode\": " << s.player_mode << ",\n";
        ofs << "  \"recording_fps\": " << s.recording_fps << ",\n";
        ofs << "  \"recording_duration\": " << s.recording_duration << ",\n";
        ofs << "  \"physics_mode\": " << s.physics_mode << ",\n";
        ofs << "  \"speed_mode\": " << s.speed_mode << ",\n";
        ofs << "  \"hud_show_play\": " << s.hud_show_play << ",\n";
        ofs << "  \"hud_show_record\": " << s.hud_show_record << ",\n";
    // Phase 5: Save optimization settings
    ofs << "  \"pt_tile_size\": " << s.pt_tile_size << ",\n";
    ofs << "  \"pt_use_blue_noise\": " << s.pt_use_blue_noise << ",\n";
    ofs << "  \"pt_use_cosine_weighted\": " << s.pt_use_cosine_weighted << ",\n";
    ofs << "  \"pt_use_stratified\": " << s.pt_use_stratified << ",\n";
    ofs << "  \"pt_use_halton\": " << s.pt_use_halton << ",\n";
    ofs << "  \"pt_adaptive_shadows\": " << s.pt_adaptive_shadows << ",\n";
    ofs << "  \"pt_use_bilateral\": " << s.pt_use_bilateral << ",\n";
    ofs << "  \"pt_bilateral_sigma_space\": " << s.pt_bilateral_sigma_space << ",\n";
    ofs << "  \"pt_bilateral_sigma_color\": " << s.pt_bilateral_sigma_color << ",\n";
    ofs << "  \"pt_light_cull_distance\": " << s.pt_light_cull_distance << "\n";
    ofs << "}\n";
    return true;
}
