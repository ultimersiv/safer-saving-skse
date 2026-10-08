#include "config.h"

#include "ini-overrides.h"

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

bool SaferSaving::Config::Persist(std::span<Entry* const> a_entries)
{
    std::vector<IniOverrides::Override> overrides;
    overrides.reserve(a_entries.size());
    for (const auto* entry : a_entries)
    {
        overrides.push_back({entry->section, entry->key, entry->Override()});
    }

    std::error_code ec;
    const auto existed = std::filesystem::exists(kUserFile, ec);

    std::string text;
    if (existed)
    {
        std::ifstream in{kUserFile, std::ios::binary};
        if (!in)
        {
            logs::error("could not read {}", kUserFile);
            return false;
        }
        text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }

    const auto updated = IniOverrides::Apply(text, overrides);
    if (!updated)
    {
        logs::error("could not parse {}; leaving it alone", kUserFile);
        return false;
    }

    // nothing to keep and no file before, so do not leave an empty one behind
    if (!existed && updated->empty())
    {
        return true;
    }

    std::ofstream out{kUserFile, std::ios::binary | std::ios::trunc};
    out << *updated;
    out.flush();
    if (!out)
    {
        logs::error("could not write {}", kUserFile);
        return false;
    }

    return true;
}

bool SaferSaving::Config::Persist(Entry& a_entry)
{
    Entry* const entries[]{&a_entry};
    return Persist(entries);
}
