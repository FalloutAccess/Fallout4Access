#pragma once

#include "pch.h"
#include "PipBoyPageBase.h"
#include <memory>

class PipBoyInventoryPage;
class PipBoyDataPage;
class PipBoyMapPage;

// Main coordinator for Pip-Boy menu accessibility
// Manages page lifecycle and routes input to active page.
// Page/tab changes are detected per-frame via AdvanceMovie hook polling.
class PipBoyMenuAccessibility
{
public:
    static PipBoyMenuAccessibility* GetSingleton();

    // Lifecycle methods (called by MenuAccessibility)
    void OnPipBoyMenuOpened();
    void OnPipBoyMenuClosed();

    // Per-frame state check (called from PipBoyCallbackHook::HookedAdvanceMovie)
    // Detects page changes, tab changes, and delegates to active page
    void CheckSelection();

    void Reset();

    // Getter for map page (needed by MenuAccessibility for scanner keys)
    PipBoyMapPage* GetMapPage() { return m_mapPage.get(); }

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
    std::string GetLocalizedPageName(Page a_page);

    // Tab change detection (called per-frame from CheckSelection)
    void CheckTabChange(RE::IMenu* a_menu);

    // Utilities
    void SpeakText(const std::string& a_text, bool a_interrupt = false);

    // State
    bool m_isOpen = false;
    Page m_currentPage = Page::STAT;
    int m_lastTab = -1;  // For tracking tab changes on pages without dedicated handlers

    // Page handlers
    std::unique_ptr<PipBoyInventoryPage> m_inventoryPage;
    std::unique_ptr<PipBoyDataPage> m_dataPage;
    std::unique_ptr<PipBoyMapPage> m_mapPage;
};
