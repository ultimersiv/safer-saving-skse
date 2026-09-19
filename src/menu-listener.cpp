#include "menu-listener.h"

#include "pch.h"

#include "autosave-blocker.h"
#include "save-guard.h"

namespace
{
bool IsLoadingMenu(const RE::BSFixedString& a_menuName)
{
    const auto* strings = RE::InterfaceStrings::GetSingleton();
    return strings && a_menuName == strings->loadingMenu;
}

bool IsJournalMenu(const RE::BSFixedString& a_menuName)
{
    const auto* strings = RE::InterfaceStrings::GetSingleton();
    return strings && a_menuName == strings->journalMenu;
}

class MenuListener : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
  public:
    static MenuListener* GetSingleton()
    {
        static MenuListener singleton;
        return std::addressof(singleton);
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>* /* a_source */) override
    {
        if (!a_event)
        {
            return RE::BSEventNotifyControl::kContinue;
        }

        // pausing menus stop actor updates, so the frame hook never sees them open
        if (a_event->opening)
        {
            if (SaferSaving::IsMenuInBlockList(a_event->menuName))
            {
                SaferSaving::BlockForMenu();
            }
            else if (IsJournalMenu(a_event->menuName))
            {
                // the save tab lives in here and the game is paused, so one evaluation holds
                SaferSaving::Reevaluate();
            }

            return RE::BSEventNotifyControl::kContinue;
        }

        if (IsLoadingMenu(a_event->menuName))
        {
            SaferSaving::BeginSettle();
            return RE::BSEventNotifyControl::kContinue;
        }

        // the settings tab is the only way the player can turn the vanilla autosaves back on
        if (IsJournalMenu(a_event->menuName))
        {
            SaferSaving::ApplyAutosaveBlock();
        }

        SaferSaving::Reevaluate();

        return RE::BSEventNotifyControl::kContinue;
    }

  private:
    MenuListener()                    = default;
    MenuListener(const MenuListener&) = delete;
    MenuListener(MenuListener&&)      = delete;

    ~MenuListener() override = default;

    MenuListener& operator=(const MenuListener&) = delete;
    MenuListener& operator=(MenuListener&&)      = delete;
};
} // namespace

void SaferSaving::RegisterMenuListener()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui)
    {
        logs::error("no UI singleton; menu checks are inactive");
        return;
    }
    ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuListener::GetSingleton());
    logs::info("menu listener registered");
}
