#include "PipBoyMenuAccessibility.h"
#include "PipBoyInventoryPage.h"
#include "PipBoyDataPage.h"
#include "PipBoyMapPage.h"
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
    CheckTabChange(menu.get());

    // Delegate to active page for page-specific per-frame checks (e.g. Map mouse selection)
    if (auto* activePage = GetActivePage()) {
        activePage->CheckSelection();
    }
}

void PipBoyMenuAccessibility::CheckTabChange(RE::IMenu* a_menu)
{
    // Delegate to active page handler for tab changes within page
    if (auto* activePage = GetActivePage()) {
        activePage->CheckTabChange();
        return;
    }

    // For pages without a dedicated handler (STAT, RADIO), read tab from dataObj
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    const auto& dataObj = pipboyMenu->dataObj;
    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return;
    }

    Scaleform::GFx::Value tabValue;
    if (!dataObj.GetMember("CurrentTab", &tabValue)) {
        return;
    }

    int tab = -1;
    if (tabValue.IsInt()) tab = tabValue.GetInt();
    else if (tabValue.IsNumber()) tab = static_cast<int>(tabValue.GetNumber());
    else if (tabValue.IsUInt()) tab = static_cast<int>(tabValue.GetUInt());

    if (tab >= 0 && tab != m_lastTab) {
        m_lastTab = tab;

        // Get localized tab name based on current page
        std::string tabName;
        if (m_currentPage == Page::STAT) {
            static const char* kStatTabKeys[] = {
                "$PipboyConditionCategory", "$PipboySPECIALCategory", "$PipboyPerksCategory"
            };
            if (tab >= 0 && tab < 3) {
                tabName = LocalizationHelper::Translate(kStatTabKeys[tab]);
            }
        }
        // RADIO has no tabs

        if (!tabName.empty()) {
            SpeakText(tabName);
        } else {
            // Fallback for unknown tabs
            SpeakText("Tab " + std::to_string(tab + 1));
        }
    }
}

void PipBoyMenuAccessibility::Reset()
{
    m_isOpen = false;
    m_currentPage = Page::STAT;
    m_lastTab = -1;

    // Reset all page handlers
    if (m_inventoryPage) m_inventoryPage->Reset();
    if (m_dataPage) m_dataPage->Reset();
    if (m_mapPage) m_mapPage->Reset();
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
        case Page::INV:
            return m_inventoryPage.get();
        case Page::DATA:
            return m_dataPage.get();
        case Page::MAP:
            return m_mapPage.get();
        case Page::STAT:
        case Page::RADIO:
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

    // Update current page and reset tab tracking
    m_currentPage = a_newPage;
    m_lastTab = -1;

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

void PipBoyMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}
