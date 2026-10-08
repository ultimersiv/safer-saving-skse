#include "ui.h"

#include "pch.h"

#include "autosave-blocker.h"
#include "autosave.h"
#include "config.h"
#include "save-guard.h"
#include "save-prune.h"

// the framework header pulls in windows.h and is not warning clean
#pragma warning(push, 0)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <SKSEMenuFramework.h>
#pragma warning(pop)

namespace
{
namespace Config = SaferSaving::Config;

constexpr auto kSection = "Safer Saving";
constexpr ImGuiMCP::ImVec4 kWarningColor{1.0f, 0.4f, 0.4f, 1.0f};
constexpr std::size_t kTextCapacity{256};

// every framework export the pages call; an older build missing one would crash on first render
constexpr const char* kExports[]{
    "AddSectionItem",
    "igBeginPopupModal",
    "igButton",
    "igCheckbox",
    "igCloseCurrentPopup",
    "igCollapsingHeader_TreeNodeFlags",
    "igEndPopup",
    "igInputText",
    "igIsItemActive",
    "igIsItemDeactivatedAfterEdit",
    "igOpenPopup_Str",
    "igPopID",
    "igPushID_Str",
    "igSameLine",
    "igSeparator",
    "igSeparatorText",
    "igSetItemTooltipV",
    "igSliderInt",
    "igSpacing",
    "igTextV",
    "igTextColoredV",
    "igTextWrappedV",
};

// set when the last write to the custom ini failed, cleared by the next good one
bool g_writeFailed{false};

struct Check
{
    Config::Bool* setting;
    const char* label;
    const char* help;
};

struct Group
{
    const char* title;
    std::span<const Check> checks;
};

// labels and help follow the comments in SaferSaving.ini
const Check kCombat[]{
    {&Config::inCombat, "In combat", nullptr},
    {&Config::attacking, "Attacking", "Mid-swing, bow draws, bashes and spellcasting."},
    {&Config::weaponDrawn, "Weapon or spell drawn", "Including the draw and sheathe animations."},
    {&Config::killmove, "In a killmove", nullptr},
    {&Config::enemiesNearby, "Enemies nearby",
     "Hostile actors close by, the same test vanilla uses before it lets you wait or fast travel. Trips slightly "
     "earlier than In combat."},
};

const Check kMovement[]{
    {&Config::moving, "Moving", nullptr},     {&Config::sprinting, "Sprinting", nullptr},
    {&Config::sneaking, "Sneaking", nullptr}, {&Config::swimming, "Swimming", nullptr},
    {&Config::flying, "Flying", nullptr},     {&Config::midair, "Jumping or falling", nullptr},
    {&Config::mounted, "Mounted", nullptr},
};

const Check kState[]{
    {&Config::notAlive, "Dead, dying or unconscious", nullptr},
    {&Config::bleedingOut, "Bleeding out", nullptr},
    {&Config::knockedDown, "Knocked down", nullptr},
    {&Config::staggered, "Staggered", nullptr},
    {&Config::sitSleepTransition, "Sitting down or standing up", "Also getting in or out of a bed."},
    {&Config::animationDriven, "Paired or scripted animation",
     "Also covers furniture idles, so leaving it on means you cannot save while seated."},
    {&Config::grabbing, "Holding an object", "With grab or telekinesis."},
    {&Config::controlsDisabled, "Controls taken away", "Scripted scenes and anything else that takes your controls."},
    {&Config::trespassing, "Trespassing", "Standing somewhere you are not allowed to be."},
    {&Config::notLoaded, "Surroundings still loading", "Before your surroundings have finished loading in."},
};

const Check kMenus[]{
    {&Config::itemMenus, "Item menus", "Inventory, container, barter, gift, magic, favourites and the pause wheel."},
    {&Config::dialogue, "Dialogue", nullptr},
    {&Config::book, "Book", nullptr},
    {&Config::crafting, "Crafting", nullptr},
    {&Config::lockpicking, "Lockpicking", nullptr},
    {&Config::levelUpTraining, "Level up and training", nullptr},
    {&Config::sleepWait, "Sleep and wait", nullptr},
    {&Config::loading, "Loading screen", nullptr},
    {&Config::characterCreation, "Character creation", nullptr},
};

const Group kGroups[]{
    {"Combat", kCombat},
    {"Movement", kMovement},
    {"State", kState},
    {"Menus", kMenus},
};

Config::Entry* const kGeneralEntries[]{
    &Config::notify,          &Config::messagePrefix, &Config::settleSeconds,  &Config::disableVanillaAutosaves,
    &Config::autoSaveMinutes, &Config::autoSaveSlots, &Config::maxManualSaves,
};

std::vector<Config::Entry*> CheckEntries()
{
    std::vector<Config::Entry*> entries;
    for (const auto& group : kGroups)
    {
        for (const auto& check : group.checks)
        {
            entries.push_back(check.setting);
        }
    }

    return entries;
}

void Apply()
{
    SaferSaving::AutoSave::Configure();
    SaferSaving::SavePrune::Configure();
    SaferSaving::ApplyAutosaveBlock();
    // the panel is an overlay and fires no menu event, so nothing else would refresh the flag
    SaferSaving::Reevaluate();
}

void Commit(std::span<Config::Entry* const> a_entries)
{
    g_writeFailed = !Config::Persist(a_entries);
    Apply();
}

void Commit(Config::Entry& a_entry)
{
    g_writeFailed = !Config::Persist(a_entry);
    Apply();
}

// keyed by the ini entry, so equal labels never collide
void PushEntryID(const Config::Entry& a_entry)
{
    ImGuiMCP::PushID(a_entry.section);
    ImGuiMCP::PushID(a_entry.key);
}

void PopEntryID()
{
    ImGuiMCP::PopID();
    ImGuiMCP::PopID();
}

void Help(const char* a_help)
{
    if (a_help)
    {
        ImGuiMCP::SetItemTooltip("%s", a_help);
    }
}

std::string Trim(std::string_view a_text)
{
    const auto first = a_text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }

    const auto last = a_text.find_last_not_of(" \t");
    return std::string{a_text.substr(first, last - first + 1)};
}

void Toggle(Config::Bool& a_setting, const char* a_label, const char* a_help)
{
    PushEntryID(a_setting);
    auto value = a_setting.GetValue();
    if (ImGuiMCP::Checkbox(a_label, &value))
    {
        a_setting.SetValue(value);
        Commit(a_setting);
    }
    Help(a_help);
    PopEntryID();
}

// a_zero names what zero means, or is null when zero is just a number
void Slider(Config::I32& a_setting, const char* a_label, std::int32_t a_min, std::int32_t a_max, const char* a_format,
            const char* a_zero, const char* a_help)
{
    PushEntryID(a_setting);
    // an out-of-range ini value shows clamped and is only rewritten once edited
    auto value         = std::clamp(a_setting.GetValue(), a_min, a_max);
    const auto* format = value == 0 && a_zero ? a_zero : a_format;
    if (ImGuiMCP::SliderInt(a_label, &value, a_min, a_max, format, ImGuiMCP::ImGuiSliderFlags_AlwaysClamp))
    {
        a_setting.SetValue(value);
    }
    // the file is written once the drag ends, not on every frame of it
    if (ImGuiMCP::IsItemDeactivatedAfterEdit())
    {
        Commit(a_setting);
    }
    Help(a_help);
    PopEntryID();
}

// there is a single text setting, so one buffer serves
void TextField(Config::Str& a_setting, const char* a_label, const char* a_help)
{
    static std::array<char, kTextCapacity> buffer{};
    static bool editing{false};

    PushEntryID(a_setting);
    if (!editing)
    {
        const auto value = a_setting.GetValue();
        const auto size  = (std::min)(value.size(), buffer.size() - 1);
        std::copy_n(value.data(), size, buffer.data());
        buffer[size] = '\0';
    }

    ImGuiMCP::InputText(a_label, buffer.data(), buffer.size());
    editing = ImGuiMCP::IsItemActive();
    if (ImGuiMCP::IsItemDeactivatedAfterEdit())
    {
        a_setting.SetValue(Trim(buffer.data()));
        Commit(a_setting);
    }
    Help(a_help);
    PopEntryID();
}

void WriteWarning()
{
    if (g_writeFailed)
    {
        ImGuiMCP::TextColored(kWarningColor, "%s",
                              "Could not write SaferSaving_custom.ini. Changes apply now but will not survive a "
                              "restart.");
    }
}

void ResetButton(const char* a_page, std::span<Config::Entry* const> a_entries)
{
    ImGuiMCP::Spacing();
    ImGuiMCP::Separator();
    if (ImGuiMCP::Button("Reset to defaults"))
    {
        ImGuiMCP::OpenPopup("Reset to defaults?");
    }

    if (ImGuiMCP::BeginPopupModal("Reset to defaults?", nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGuiMCP::Text("Reset all %s settings to defaults?", a_page);
        if (ImGuiMCP::Button("Yes"))
        {
            for (auto* entry : a_entries)
            {
                entry->Reset();
            }
            Commit(a_entries);
            ImGuiMCP::CloseCurrentPopup();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("No"))
        {
            ImGuiMCP::CloseCurrentPopup();
        }
        ImGuiMCP::EndPopup();
    }
}

void __stdcall RenderGeneral()
{
    WriteWarning();

    ImGuiMCP::SeparatorText("Notification");
    Toggle(Config::notify, "Explain blocked quicksaves",
           "Show why saving is blocked when you press the quicksave key. Autosaves and saves made by scripts stay "
           "silent.");
    TextField(Config::messagePrefix, "Message prefix",
              "Text put in front of the reason. A space is added for you. Leave it blank for the reason on its own.");

    ImGuiMCP::SeparatorText("Load");
    Slider(Config::settleSeconds, "Wait after loading", 0, Config::kMaxSettleSeconds, "%d s", "Off",
           "Seconds to refuse saving after a loading screen, after loading a save, and after starting a new game. "
           "Ctrl+click to type an exact value.");

    ImGuiMCP::SeparatorText("Autosave");
    Toggle(Config::disableVanillaAutosaves, "Disable vanilla autosaves",
           "Turns off the game's own autosaves on pause, travel, waiting and resting, and the ones scripts ask for. "
           "Unticking this does not turn them back on; do that in Settings, Gameplay.");
    Slider(Config::autoSaveMinutes, "Autosave interval", 0, Config::kMaxIntervalMinutes, "%d min", "Off",
           "Minutes of play between automatic saves. Paused time and loading screens do not count. A save that falls "
           "due while saving is blocked happens at the next safe moment. Ctrl+click to type an exact value.");
    Slider(Config::autoSaveSlots, "Autosave slots", Config::kMinSlots, Config::kMaxSlots, "%d", nullptr,
           "How many slots to rotate through. Lowering this leaves the extra files behind for you to delete.");

    ImGuiMCP::SeparatorText("Saves");
    Slider(Config::maxManualSaves, "Manual saves kept", 0, Config::kMaxManualSaves, "%d", "Keep all",
           "How many manual saves to keep per character. Each new manual save sends the oldest past this count to the "
           "Recycle Bin. Quicksaves, autosaves and other characters are left alone. "
           "Ctrl+click to type an exact value.");

    ResetButton("General", kGeneralEntries);
}

void __stdcall RenderChecks()
{
    WriteWarning();

    ImGuiMCP::TextWrapped("%s", "Saving is blocked while any ticked condition holds.");
    for (const auto& group : kGroups)
    {
        if (!ImGuiMCP::CollapsingHeader(group.title, ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen))
        {
            continue;
        }

        for (const auto& check : group.checks)
        {
            Toggle(*check.setting, check.label, check.help);
        }
    }

    static const auto entries = CheckEntries();
    ResetButton("Checks", entries);
}
} // namespace

void SaferSaving::UI::Register()
{
    // optional; without it the ini stays the only way in
    if (!GetMenuFrameworkModule())
    {
        logs::info("SKSE Menu Framework not found; in-game menu disabled");
        return;
    }

    for (const auto* name : kExports)
    {
        if (!GetMenuFrameworkFunction<FARPROC>(name))
        {
            logs::warn("SKSE Menu Framework lacks {}; in-game menu disabled", name);
            return;
        }
    }

    SKSEMenuFramework::SetSection(kSection);
    SKSEMenuFramework::AddSectionItem("General", RenderGeneral);
    SKSEMenuFramework::AddSectionItem("Checks", RenderChecks);
    logs::info("settings added to SKSE Menu Framework");
}
