#include "config.h"

namespace
{
constexpr auto kBaseFile = "Data/SKSE/Plugins/SaferSaving.ini";
constexpr auto kUserFile = "Data/SKSE/Plugins/SaferSaving_custom.ini";
} // namespace

void SaferSaving::Config::Load()
{
    auto* store = REX::INI::SettingStore::GetSingleton();
    store->Init(kBaseFile, kUserFile);
    store->Load();
}
