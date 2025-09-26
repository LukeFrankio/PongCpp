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
    Settings s;
    std::string p = w_to_utf8(path);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return s;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    // naive parsing: find numeric tokens for known keys
    auto findNum = [&](const std::string &key)->int{
        size_t p = raw.find(key);
        if (p==std::string::npos) return -1;
        p = raw.find_first_of("0123456789", p);
        if (p==std::string::npos) return -1;
        return raw[p]-'0';
    };
    int cm = findNum("control_mode"); if (cm>=0) s.control_mode = cm;
    int ai = findNum("\"ai\""); if (ai>=0) s.ai = ai;
    int rend = findNum("renderer"); if (rend>=0) s.renderer = rend;
    int qual = findNum("quality"); if (qual>=0) s.quality = qual;
    auto loadOpt = [&](const char* key, int &dst){ int v = findNum(key); if (v>=0) dst = v; };
    loadOpt("pt_rays_per_frame", s.pt_rays_per_frame);
    loadOpt("pt_max_bounces", s.pt_max_bounces);
    loadOpt("pt_internal_scale", s.pt_internal_scale);
    loadOpt("pt_roughness", s.pt_roughness);
    loadOpt("pt_emissive", s.pt_emissive);
    loadOpt("pt_accum_alpha", s.pt_accum_alpha);
    loadOpt("pt_denoise_strength", s.pt_denoise_strength);
    loadOpt("pt_force_full_pixel_rays", s.pt_force_full_pixel_rays);
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
    ofs << "  \"pt_force_full_pixel_rays\": " << s.pt_force_full_pixel_rays << "\n";
    ofs << "}\n";
    return true;
}
