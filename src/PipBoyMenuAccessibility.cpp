#include "PipBoyMenuAccessibility.h"
#include "PipBoyInventoryPage.h"
#include "PipBoyDataPage.h"
#include "PipBoyMapPage.h"
#include "PipBoyStatsPage.h"
#include "PipBoyRadioPage.h"
#include "PipBoyCallbackHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <RE/P/PipboyMenu.h>

PipBoyMenuAccessibility* PipBoyMenuAccessibility::GetSingleton()
{
    static PipBoyMenuAccessibility singleton;
    return &singleton;
}

PipBoyMenuAccessibility::PipBoyMenuAccessibility()
{
    m_inventoryPage = std::make_unique<PipBoyInventoryPage>();
    m_dataPage = std::make_unique<PipBoyDataPage>();
    m_mapPage = std::make_unique<PipBoyMapPage>();
    m_statsPage = std::make_unique<PipBoyStatsPage>();
    m_radioPage = std::make_unique<PipBoyRadioPage>();
}

void PipBoyMenuAccessibility::OnPipBoyMenuOpened()
{
    m_isOpen = true;
    REX::INFO("PipBoyMenuAccessibility: Menu opened");

    // Install the callback hook (only installs once, stays active)
    PipBoyCallbackHook::GetSingleton()->Install();

    // Reset duplicate tracking in the hook
    PipBoyCallbackHook::GetSingleton()->ResetLastAnnounced();

    // Schedule deferred announcement to let Scaleform populate
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this]() {
            if (!m_isOpen) return;

            auto ui = RE::UI::GetSingleton();
            if (!ui) return;

            auto menu = ui->GetMenu("PipboyMenu"sv);
            if (!menu) return;

            // Get current page and announce
            auto page = GetCurrentPage(menu.get());
            m_currentPage = page;

            // Announce Pip-Boy opened and current page
            std::string announcement = "Pip-Boy, ";
            announcement += GetLocalizedPageName(page);
            SpeakText(announcement);

            // Activate the current page handler
            if (auto* activePage = GetActivePage()) {
                activePage->OnPageActivated();
            }
        });
    }
}

void PipBoyMenuAccessibility::OnPipBoyMenuClosed()
{
    REX::INFO("PipBoyMenuAccessibility: Menu closed");

    // Deactivate current page
    if (auto* activePage = GetActivePage()) {
        activePage->OnPageDeactivated();
    }

    Reset();
}

void PipBoyMenuAccessibility::CheckSelection()
{
    if (!m_isOpen) {
        return;
    }

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) {
        return;
    }

    // Check for page change
    auto currentPage = GetCurrentPage(menu.get());
    if (currentPage != m_currentPage) {
        SwitchToPage(currentPage);
        return;
    }

    // Check for tab change
    CheckTabChange();

    // Delegate to active page for page-specific per-frame checks (e.g. Map mouse selection)
    if (auto* activePage = GetActivePage()) {
        activePage->CheckSelection();
    }
}

void PipBoyMenuAccessibility::CheckTabChange()
{
    // Delegate to active page handler for tab changes within page
    if (auto* activePage = GetActivePage()) {
        activePage->CheckTabChange();
    }
}

void PipBoyMenuAccessibility::Reset()
{
    m_isOpen = false;
    m_currentPage = Page::STAT;

    // Reset all page handlers
    if (m_inventoryPage) m_inventoryPage->Reset();
    if (m_dataPage) m_dataPage->Reset();
    if (m_mapPage) m_mapPage->Reset();
    if (m_statsPage) m_statsPage->Reset();
    if (m_radioPage) m_radioPage->Reset();
}

PipBoyMenuAccessibility::Page PipBoyMenuAccessibility::GetCurrentPage(RE::IMenu* a_menu)
{
    if (!a_menu) {
        return Page::STAT;
    }

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return Page::STAT;
    }

    Scaleform::GFx::Value pageValue;
    if (dataObj.GetMember("CurrentPage", &pageValue)) {
        int pageNum = 0;
        if (pageValue.IsInt()) {
            pageNum = pageValue.GetInt();
        } else if (pageValue.IsNumber()) {
            pageNum = static_cast<int>(pageValue.GetNumber());
        } else if (pageValue.IsUInt()) {
            pageNum = static_cast<int>(pageValue.GetUInt());
        }

        if (pageNum >= 0 && pageNum <= 4) {
            return static_cast<Page>(pageNum);
        }
    }

    return Page::STAT;
}

PipBoyPageBase* PipBoyMenuAccessibility::GetActivePage()
{
    switch (m_currentPage) {
        case Page::STAT:
            return m_statsPage.get();
        case Page::INV:
            return m_inventoryPage.get();
        case Page::DATA:
            return m_dataPage.get();
        case Page::MAP:
            return m_mapPage.get();
        case Page::RADIO:
            return m_radioPage.get();
        default:
            return nullptr;
    }
}

void PipBoyMenuAccessibility::SwitchToPage(Page a_newPage)
{
    REX::INFO("PipBoyMenuAccessibility: Switching from page {} to {}",
              static_cast<int>(m_currentPage), static_cast<int>(a_newPage));

    // Deactivate old page
    if (auto* oldPage = GetActivePage()) {
        oldPage->OnPageDeactivated();
    }

    // Update current page
    m_currentPage = a_newPage;

    // Announce page change
    SpeakText(GetLocalizedPageName(a_newPage));

    // Activate new page
    if (auto* newPage = GetActivePage()) {
        newPage->OnPageActivated();
    }
}

std::string PipBoyMenuAccessibility::GetLocalizedPageName(Page a_page)
{
    static const char* kPageKeys[] = { "$STAT", "$INV", "$DATA", "$MAP", "$RADIO" };
    int idx = static_cast<int>(a_page);
    if (idx >= 0 && idx < 5) {
        return LocalizationHelper::Translate(kPageKeys[idx]);
    }
    return "Unknown";
}

void PipBoyMenuAccessibility::AnnounceInventoryStatus()
{
    if (!m_isOpen || m_currentPage != Page::INV) {
        return;
    }

    if (m_inventoryPage) {
        m_inventoryPage->AnnounceCarryWeightAndCaps();
    }
}

void PipBoyMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}
