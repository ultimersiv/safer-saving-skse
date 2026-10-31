#include "ini-overrides.h"

#include <SimpleIni.h>

std::optional<std::string> SaferSaving::IniOverrides::Apply(std::string_view a_text,
                                                            std::span<const Override> a_overrides)
{
    // a UTF-16 file reads as empty past its first NUL, so refuse it rather than replace it
    if (a_text.find('\0') != std::string_view::npos)
    {
        return std::nullopt;
    }

    // same flags REX loads with, so both read the file alike
    CSimpleIniA file;
    file.SetUnicode(true);
    file.SetQuotes(true);

    if (!a_text.empty() && file.LoadData(a_text.data(), a_text.size()) < 0)
    {
        return std::nullopt;
    }

    for (const auto& item : a_overrides)
    {
        const std::string section{item.section};
        const std::string key{item.key};
        if (item.value)
        {
            file.SetValue(section.c_str(), key.c_str(), item.value->c_str());
        }
        else
        {
            file.Delete(section.c_str(), key.c_str(), true);
        }
    }

    std::string out;
    if (file.Save(out) < 0)
    {
        return std::nullopt;
    }

    return out;
}
