#include "PipBoyDataPage.h"
#include "PipBoyCallbackHook.h"

void PipBoyDataPage::OnPageActivated()
{
    REX::INFO("PipBoyDataPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;

    // Announce current tab
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto tab = GetCurrentTab(menu.get());
    if (tab >= 0) {
        m_lastTab = tab;
        SpeakText(GetTabName(tab));
    }
}

void PipBoyDataPage::OnPageDeactivated()
{
    REX::INFO("PipBoyDataPage: Deactivated");
    PipBoyCallbackHook::GetSingleton()->ClearQuestState();
}

void PipBoyDataPage::CheckSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    // Check if tab changed
    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_lastSelection = -1;
        // Clear quest state when leaving Quests tab
        if (currentTab != static_cast<std::int32_t>(Tab::Quests)) {
            PipBoyCallbackHook::GetSingleton()->ClearQuestState();
        }
        SpeakText(GetTabName(currentTab));
        return;
    }

    // Quest/Workshop/Stats selection is handled by PipBoyCallbackHook callbacks.
    // The game's onQuestSelection/onWorkshopSelection callbacks fire on navigation,
    // which the hook intercepts and announces.
}

void PipBoyDataPage::CheckTabChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_lastSelection = -1;
        // Clear quest state when leaving Quests tab
        if (currentTab != static_cast<std::int32_t>(Tab::Quests)) {
            PipBoyCallbackHook::GetSingleton()->ClearQuestState();
        }
        SpeakText(GetTabName(currentTab));
    }
}

void PipBoyDataPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
    PipBoyCallbackHook::GetSingleton()->ClearQuestState();
}

const char* PipBoyDataPage::GetTabName(std::int32_t a_tabIndex)
{
    switch (static_cast<Tab>(a_tabIndex)) {
        case Tab::Quests:    return "Quests";
        case Tab::Workshops: return "Workshops";
        case Tab::Stats:     return "Stats";
        default:             return "Unknown";
    }
}
