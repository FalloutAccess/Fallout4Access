#include "PipBoyDataPage.h"

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
}

void PipBoyDataPage::CheckSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    // First check if tab changed
    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_lastSelection = -1;  // Reset selection on tab change
        SpeakText(GetTabName(currentTab));
        return;
    }

    // TODO: Data page selection tracking requires accessing the loaded page's List_mc
    // which is different from the main dataObj. This needs further investigation
    // of the Pipboy_DataPage.swf structure.
    REX::DEBUG("PipBoyDataPage: Tab={} - selection tracking not yet implemented", currentTab);
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
        m_lastSelection = -1;  // Reset selection on tab change
        SpeakText(GetTabName(currentTab));
    }
}

void PipBoyDataPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
}

// TODO: Quest, Workshop, and Stats data retrieval methods need to be updated
// to use the dataObj approach. For now they are stubbed out.

PipBoyDataPage::QuestInfo PipBoyDataPage::GetQuestInfo(RE::IMenu*, std::int32_t)
{
    return QuestInfo();
}

PipBoyDataPage::WorkshopInfo PipBoyDataPage::GetWorkshopInfo(RE::IMenu*, std::int32_t)
{
    return WorkshopInfo();
}

std::string PipBoyDataPage::GetStatInfo(RE::IMenu*, std::int32_t)
{
    return "";
}

std::string PipBoyDataPage::BuildQuestAnnouncement(const QuestInfo& a_quest)
{
    return a_quest.text;
}

std::string PipBoyDataPage::BuildWorkshopAnnouncement(const WorkshopInfo& a_workshop)
{
    return a_workshop.text;
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
