#include "PipBoyInventoryPage.h"
#include "LocalizationHelper.h"

void PipBoyInventoryPage::OnPageActivated()
{
    REX::INFO("PipBoyInventoryPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;

    // Announce current tab (item will be announced by hook via onInvItemSelection)
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto tab = GetCurrentTab(menu.get());
    if (tab >= 0) {
        m_lastTab = tab;
        SpeakText(GetLocalizedTabName(tab));
    }
}

void PipBoyInventoryPage::OnPageDeactivated()
{
    REX::INFO("PipBoyInventoryPage: Deactivated");
}

void PipBoyInventoryPage::CheckSelection()
{
    // Item announcements are handled by PipBoyCallbackHook via onInvItemSelection callback.
    // No per-frame polling needed for inventory items.
}

void PipBoyInventoryPage::CheckTabChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_lastSelection = -1;
        SpeakText(GetLocalizedTabName(currentTab));
    }
}

void PipBoyInventoryPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
}

std::string PipBoyInventoryPage::GetLocalizedTabName(std::int32_t a_tabIndex)
{
    if (a_tabIndex >= 0 && a_tabIndex < kTabCount) {
        return LocalizationHelper::Translate(kTabKeys[a_tabIndex]);
    }
    return "Unknown";
}
