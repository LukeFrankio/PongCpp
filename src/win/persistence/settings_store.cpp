#include "settings_store.h"
#include "../settings.h"
#include <memory>
#include <windows.h>
const Settings& SettingsStore::get() const { static Settings d; if(!ptr) return d; return *ptr; }
void SettingsStore::apply(const SettingsDelta&){}
bool SettingsStore::load(const std::wstring&){ if(!ptr) ptr=new Settings(); return true; }
bool SettingsStore::save(const std::wstring&){ return true; }
