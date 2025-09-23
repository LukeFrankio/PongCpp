#pragma once
#include <string>

struct Settings {
    int control_mode = 0; // 0=keyboard,1=mouse
    int ai = 1; // 0=easy,1=normal,2=hard
};

class SettingsManager {
public:
    SettingsManager() = default;
    Settings load(const std::wstring &path);
    bool save(const std::wstring &path, const Settings &s);
};
