#include "input-listener.h"

#include "pch.h"

#include "save-guard.h"

namespace
{
bool IsQuicksavePress(const RE::InputEvent& a_event)
{
    if (a_event.GetEventType() != RE::INPUT_EVENT_TYPE::kButton)
    {
        return false;
    }

    // press edge only, so holding the key does not spam
    const auto* button = a_event.AsButtonEvent();
    if (!button || !button->IsDown())
    {
        return false;
    }

    const auto* userEvents = RE::UserEvents::GetSingleton();
    return userEvents && a_event.QUserEvent() == userEvents->quicksave;
}

class InputListener : public RE::BSTEventSink<RE::InputEvent*>
{
  public:
    static InputListener* GetSingleton()
    {
        static InputListener singleton;
        return std::addressof(singleton);
    }

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                          RE::BSTEventSource<RE::InputEvent*>* /* a_source */) override
    {
        if (!a_event)
        {
            return RE::BSEventNotifyControl::kContinue;
        }

        // events come as a linked list
        for (const auto* event = *a_event; event; event = event->next)
        {
            if (IsQuicksavePress(*event))
            {
                SaferSaving::NotifyBlockedSaveAttempt();
                break;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

  private:
    InputListener()                     = default;
    InputListener(const InputListener&) = delete;
    InputListener(InputListener&&)      = delete;

    ~InputListener() override = default;

    InputListener& operator=(const InputListener&) = delete;
    InputListener& operator=(InputListener&&)      = delete;
};
} // namespace

void SaferSaving::RegisterInputListener()
{
    auto* manager = RE::BSInputDeviceManager::GetSingleton();
    if (!manager)
    {
        logs::error("no input device manager; quicksave notifications are inactive");
        return;
    }
    manager->AddEventSink(InputListener::GetSingleton());
    logs::info("input listener registered");
}
