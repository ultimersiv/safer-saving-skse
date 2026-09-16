#include "notification.h"

#include "pch.h"

void SaferSaving::ShowNotification(const char* a_text)
{
    if (!a_text)
    {
        return;
    }

    auto* queue = RE::UIMessageQueue::GetSingleton();
    if (!queue)
    {
        return;
    }

    // no DebugNotification in CommonLibSSE-NG, so post the HUD message directly
    auto* data = RE::UIMessageDataFactory::Create<RE::HUDData>();
    if (!data)
    {
        return;
    }

    data->type = RE::HUD_MESSAGE_TYPE::kNotification;
    data->text = a_text;

    queue->AddMessage(RE::HUDMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kUpdate, data);
}
