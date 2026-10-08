#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "ini-overrides.h"

#include <SimpleIni.h>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

namespace
{
using SaferSaving::IniOverrides::Apply;
using SaferSaving::IniOverrides::Override;

// loads text the way the plugin does, so checks compare values rather than formatting
struct Parsed
{
    explicit Parsed(const std::string& a_text)
    {
        file.SetUnicode(true);
        file.SetQuotes(true);
        REQUIRE(file.LoadData(a_text) >= 0);
    }

    std::optional<std::string> Value(const char* a_section, const char* a_key) const
    {
        const char* value = file.GetValue(a_section, a_key);
        return value ? std::optional<std::string>{value} : std::nullopt;
    }

    bool HasSection(const char* a_section) const { return file.GetSection(a_section) != nullptr; }

    int Keys(const char* a_section) const { return file.GetSectionSize(a_section); }

    CSimpleIniA file;
};

std::string Run(const std::string& a_text, const std::vector<Override>& a_overrides)
{
    const auto result = Apply(a_text, a_overrides);
    REQUIRE(result.has_value());
    return *result;
}
} // namespace

TEST_CASE("set adds a key and its section to empty text")
{
    const Parsed ini{Run("", {{"Saves", "iMaxManualSaves", "20"}})};
    CHECK(ini.Value("Saves", "iMaxManualSaves") == "20");
}

TEST_CASE("set replaces a key and keeps its neighbours")
{
    const Parsed ini{Run("[Movement]\nbSneaking = false\nbMoving = false\n", {{"Movement", "bSneaking", "true"}})};
    CHECK(ini.Value("Movement", "bSneaking") == "true");
    CHECK(ini.Value("Movement", "bMoving") == "false");
    CHECK(ini.Keys("Movement") == 2);
}

TEST_CASE("delete removes a key and keeps its neighbours")
{
    const Parsed ini{
        Run("[Movement]\nbSneaking = false\nbMoving = false\n", {{"Movement", "bSneaking", std::nullopt}})};
    CHECK_FALSE(ini.Value("Movement", "bSneaking").has_value());
    CHECK(ini.Value("Movement", "bMoving") == "false");
}

TEST_CASE("deleting the last key drops the section")
{
    const Parsed ini{
        Run("[Combat]\nbInCombat = false\n[Movement]\nbMoving = false\n", {{"Combat", "bInCombat", std::nullopt}})};
    CHECK_FALSE(ini.HasSection("Combat"));
    CHECK(ini.Value("Movement", "bMoving") == "false");
}

TEST_CASE("deleting a missing key changes nothing")
{
    const Parsed ini{Run("[Movement]\nbMoving = false\n",
                         {{"Movement", "bSneaking", std::nullopt}, {"Combat", "bInCombat", std::nullopt}})};
    CHECK(ini.Keys("Movement") == 1);
    CHECK(ini.Value("Movement", "bMoving") == "false");
    CHECK_FALSE(ini.HasSection("Combat"));
}

TEST_CASE("full-line comments on untouched keys survive")
{
    const auto text =
        Run("[Movement]\n; keep sneaking off\nbSneaking = false\nbMoving = false\n", {{"Movement", "bMoving", "true"}});
    CHECK(text.find("; keep sneaking off") != std::string::npos);
    CHECK(Parsed{text}.Value("Movement", "bMoving") == "true");
}

TEST_CASE("a batch applies every set and delete")
{
    const Parsed ini{
        Run("[Combat]\nbInCombat = false\n[Movement]\nbSneaking = false\n", {{"Combat", "bInCombat", std::nullopt},
                                                                             {"Movement", "bMoving", "false"},
                                                                             {"Movement", "bSneaking", std::nullopt}})};
    CHECK_FALSE(ini.HasSection("Combat"));
    CHECK(ini.Value("Movement", "bMoving") == "false");
    CHECK_FALSE(ini.Value("Movement", "bSneaking").has_value());
}

TEST_CASE("deletes on empty text leave it empty")
{
    CHECK(Run("", {{"Combat", "bInCombat", std::nullopt}}).empty());
}

TEST_CASE("a trailing note stays on an untouched key and goes with a rewritten one")
{
    const auto text =
        Run("[State]\nbAnimationDriven = false   # allows saving while seated\nbGrabbing = false # hold\n",
            {{"State", "bAnimationDriven", "true"}});
    const Parsed ini{text};
    CHECK(ini.Value("State", "bAnimationDriven") == "true");
    CHECK(ini.Value("State", "bGrabbing") == "false # hold");
    CHECK(text.find("allows saving while seated") == std::string::npos);
}

TEST_CASE("keys match regardless of case, so no duplicate is added")
{
    const Parsed ini{Run("[movement]\nbsneaking = true\n", {{"Movement", "bSneaking", "false"}})};
    CHECK(ini.Keys("Movement") == 1);
    CHECK(ini.Value("Movement", "bSneaking") == "false");
}

TEST_CASE("a UTF-8 byte order mark is read through")
{
    const Parsed ini{Run("\xEF\xBB\xBF[Movement]\nbSneaking = false\n", {{"Movement", "bMoving", "false"}})};
    CHECK(ini.Keys("Movement") == 2);
    CHECK(ini.Value("Movement", "bSneaking") == "false");
    CHECK(ini.Value("Movement", "bMoving") == "false");
}
