#include "PipBoyMenuAccessibility.h"
#include "PipBoyInventoryPage.h"
#include "PipBoyDataPage.h"
#include "PipBoyMapPage.h"
#include "PipBoyCallbackHook.h"
#include "SpeechManager.h"
#include <RE/P/PipboyMenu.h>
#include <SRAL.h>

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
            announcement += GetPageName(page);
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

    // Check for page change first
    auto currentPage = GetCurrentPage(menu.get());

    if (currentPage != m_currentPage) {
        SwitchToPage(currentPage);
        return;
    }

    // Delegate to active page handler
    if (auto* activePage = GetActivePage()) {
        activePage->CheckSelection();
    }
}

void PipBoyMenuAccessibility::CheckPageChange()
{
    if (!m_isOpen) {
        return;
    }

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentPage = GetCurrentPage(menu.get());

    if (currentPage != m_currentPage) {
        SwitchToPage(currentPage);
    }
}

void PipBoyMenuAccessibility::OnNavigate(bool a_up)
{
    if (!m_isOpen) return;

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    // Check for page change first
    auto currentPage = GetCurrentPage(menu.get());
    if (currentPage != m_currentPage) {
        SwitchToPage(currentPage);
        return;
    }

    // Delegate to inventory page if that's the active page
    if (m_currentPage == Page::INV && m_inventoryPage) {
        if (a_up) {
            m_inventoryPage->OnNavigateUp();
        } else {
            m_inventoryPage->OnNavigateDown();
        }
    } else if (auto* activePage = GetActivePage()) {
        // For other pages, fall back to CheckSelection
        activePage->CheckSelection();
    }
}

void PipBoyMenuAccessibility::CheckTabChange()
{
    if (!m_isOpen) {
        return;
    }

    // Delegate to active page handler for tab changes within page
    if (auto* activePage = GetActivePage()) {
        activePage->CheckTabChange();
    } else {
        // For unimplemented pages, try to read and announce tab directly
        auto ui = RE::UI::GetSingleton();
        if (!ui) {
            return;
        }

        auto menu = ui->GetMenu("PipboyMenu"sv);
        if (!menu || !menu->uiMovie) {
            return;
        }

        Scaleform::GFx::Value tabValue;
        bool gotTab = menu->uiMovie->GetVariable(&tabValue, "root.DataObj.CurrentTab");

        if (gotTab) {
            int tab = -1;
            if (tabValue.IsInt()) tab = tabValue.GetInt();
            else if (tabValue.IsNumber()) tab = static_cast<int>(tabValue.GetNumber());
            else if (tabValue.IsUInt()) tab = static_cast<int>(tabValue.GetUInt());

            if (tab >= 0 && tab != m_lastTab) {
                m_lastTab = tab;
                // Announce generic tab number for unimplemented pages
                std::string announcement = "Tab " + std::to_string(tab + 1);
                SpeakText(announcement);
            }
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

    // Cast to PipboyMenu to access the dataObj member directly
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);

    // Access dataObj member which is a Scaleform::GFx::Value at offset 0x0F0
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return Page::STAT;
    }

    // Get CurrentPage member from dataObj
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
            // Not implemented yet - return nullptr
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
    SpeakText(GetPageName(a_newPage));

    // Activate new page
    if (auto* newPage = GetActivePage()) {
        newPage->OnPageActivated();
    }
}

const char* PipBoyMenuAccessibility::GetPageName(Page a_page)
{
    switch (a_page) {
        case Page::STAT:  return "Status";
        case Page::INV:   return "Inventory";
        case Page::DATA:  return "Data";
        case Page::MAP:   return "Map";
        case Page::RADIO: return "Radio";
        default:          return "Unknown";
    }
}

void PipBoyMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}
