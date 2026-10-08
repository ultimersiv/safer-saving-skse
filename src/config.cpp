#include "config.h"

#include "ini-overrides.h"

namespace
{
constexpr auto kBaseFile   = "Data/SKSE/Plugins/SaferSaving.ini";
constexpr auto kUserFile   = "Data/SKSE/Plugins/SaferSaving_custom.ini";
constexpr auto kBackupFile = "Data/SKSE/Plugins/SaferSaving_custom.ini.bak";

// set once the custom ini has been copied to its backup this session
bool g_backedUp{false};
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
    if (ec)
    {
        logs::error("could not check {}", kUserFile);
        return false;
    }

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
        if (in.bad())
        {
            logs::error("could not read {}", kUserFile);
            return false;
        }
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

    // the rewrite drops what SimpleIni cannot read, so keep the file as it was before the first edit
    if (existed && !g_backedUp)
    {
        if (std::filesystem::copy_file(kUserFile, kBackupFile, std::filesystem::copy_options::overwrite_existing, ec))
        {
            g_backedUp = true;
        }
        else
        {
            logs::warn("could not back up {}", kUserFile);
        }
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
