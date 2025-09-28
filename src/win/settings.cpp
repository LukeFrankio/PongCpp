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
    extractInt("pt_rays_per_frame", s.pt_rays_per_frame);
    extractInt("pt_max_bounces", s.pt_max_bounces);
    extractInt("pt_internal_scale", s.pt_internal_scale);
    extractInt("pt_roughness", s.pt_roughness);
    extractInt("pt_emissive", s.pt_emissive);
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
    ofs << "  \"pt_rays_per_frame\": " << s.pt_rays_per_frame << ",\n";
    ofs << "  \"pt_max_bounces\": " << s.pt_max_bounces << ",\n";
    ofs << "  \"pt_internal_scale\": " << s.pt_internal_scale << ",\n";
    ofs << "  \"pt_roughness\": " << s.pt_roughness << ",\n";
    ofs << "  \"pt_emissive\": " << s.pt_emissive << ",\n";
    ofs << "  \"pt_accum_alpha\": " << s.pt_accum_alpha << ",\n";
    ofs << "  \"pt_denoise_strength\": " << s.pt_denoise_strength << ",\n";
    ofs << "  \"pt_force_full_pixel_rays\": " << s.pt_force_full_pixel_rays << ",\n";
    ofs << "  \"pt_use_ortho\": " << s.pt_use_ortho << ",\n";
    ofs << "  \"pt_rr_enable\": " << s.pt_rr_enable << ",\n";
    ofs << "  \"pt_rr_start_bounce\": " << s.pt_rr_start_bounce << ",\n";
    ofs << "  \"pt_rr_min_prob_pct\": " << s.pt_rr_min_prob_pct << "\n";
    ofs << "  \"pt_fanout_enable\": " << s.pt_fanout_enable << ",\n";
    ofs << "  \"pt_fanout_cap\": " << s.pt_fanout_cap << ",\n";
    ofs << "  \"pt_fanout_abort\": " << s.pt_fanout_abort << "\n";
    ofs << "}\n";
    return true;
}
