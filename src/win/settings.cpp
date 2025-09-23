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
    // naive parsing: find "control_mode": <num>, "ai": <num>
    auto findNum = [&](const std::string &key)->int{
        size_t p = raw.find(key);
        if (p==std::string::npos) return -1;
        p = raw.find_first_of("0123456789", p);
        if (p==std::string::npos) return -1;
        return raw[p]-'0';
    };
    int cm = findNum("control_mode"); if (cm>=0) s.control_mode = cm;
    int ai = findNum("\"ai\""); if (ai>=0) s.ai = ai;
    return s;
}

bool SettingsManager::save(const std::wstring &path, const Settings &s) {
    std::string p2 = w_to_utf8(path);
    std::ofstream ofs(p2, std::ios::trunc | std::ios::binary);
    if (!ofs) return false;
    ofs << "{\n";
    ofs << "  \"control_mode\": " << s.control_mode << ",\n";
    ofs << "  \"ai\": " << s.ai << "\n";
    ofs << "}\n";
    return true;
}
