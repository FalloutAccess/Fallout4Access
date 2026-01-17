#pragma once

#include "pch.h"
#include "PipBoyPageBase.h"
#include <memory>

class PipBoyInventoryPage;
class PipBoyDataPage;

// Main coordinator for Pip-Boy menu accessibility
// Manages page lifecycle and routes input to active page
class PipBoyMenuAccessibility
{
public:
    static PipBoyMenuAccessibility* GetSingleton();

    // Lifecycle methods (called by MenuAccessibility)
    void OnPipBoyMenuOpened();
    void OnPipBoyMenuClosed();

    // Input handlers (called by MenuAccessibility)
    void CheckSelection();   // Navigation keys (Up/Down/W/S)
    void CheckPageChange();  // Page keys (1-5) or shoulder buttons
    void CheckTabChange();   // Tab keys (Q/E) or d-pad left/right
    void OnNavigate(bool a_up);  // Direction-specific navigation (Up/Down)

    void Reset();

private:
    PipBoyMenuAccessibility();
    ~PipBoyMenuAccessibility() = default;
    PipBoyMenuAccessibility(const PipBoyMenuAccessibility&) = delete;
    PipBoyMenuAccessibility(PipBoyMenuAccessibility&&) = delete;
    PipBoyMenuAccessibility& operator=(const PipBoyMenuAccessibility&) = delete;
    PipBoyMenuAccessibility& operator=(PipBoyMenuAccessibility&&) = delete;

    // Page management
    enum class Page {
        STAT = 0,
        INV = 1,
        DATA = 2,
        MAP = 3,
        RADIO = 4
    };

    Page GetCurrentPage(RE::IMenu* a_menu);
    PipBoyPageBase* GetActivePage();
    void SwitchToPage(Page a_newPage);
    const char* GetPageName(Page a_page);

    // Utilities
    void SpeakText(const std::string& a_text, bool a_interrupt = false);

    // State
    bool m_isOpen = false;
    Page m_currentPage = Page::STAT;
    int m_lastTab = -1;  // For tracking tab changes on unimplemented pages

    // Page handlers
    std::unique_ptr<PipBoyInventoryPage> m_inventoryPage;
    std::unique_ptr<PipBoyDataPage> m_dataPage;
    // Future: m_statPage, m_mapPage, m_radioPage

    // Scaleform paths
    static constexpr const char* CURRENT_PAGE_PATH = "root.DataObj.CurrentPage";
};
