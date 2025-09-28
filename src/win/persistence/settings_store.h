#pragma once
#include <string>
struct Settings;
struct SettingsDelta;
class SettingsStore { public: const Settings& get() const; void apply(const SettingsDelta&); bool load(const std::wstring&); bool save(const std::wstring&); private: Settings* ptr=nullptr; };
