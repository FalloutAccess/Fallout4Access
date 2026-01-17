#pragma once

#include "pch.h"
#include <RE/P/PipboyMenu.h>

// Abstract base class for Pip-Boy page handlers
// Each page (STAT, INV, DATA, MAP, RADIO) extends this class
class PipBoyPageBase
{
public:
    virtual ~PipBoyPageBase() = default;

    // Lifecycle hooks
    virtual void OnPageActivated() = 0;
    virtual void OnPageDeactivated() = 0;

    // Input handlers
    virtual void CheckSelection() = 0;  // Called on Up/Down navigation
    virtual void CheckTabChange() = 0;  // Called on Q/E or Left/Right for sub-tabs

    // State management
    virtual void Reset() = 0;

    // Get the page name for announcements
    virtual const char* GetPageName() const = 0;

protected:
    // Get the dataObj from PipboyMenu (direct member access, much more reliable)
    static const Scaleform::GFx::Value* GetDataObj(RE::IMenu* a_menu);

    // Common data access helpers using dataObj
    std::int32_t GetCurrentTab(RE::IMenu* a_menu);
    bool GetInvSelectedItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray);
    bool GetInvItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray);

    // Get the live selection from the loaded inventory page's List_mc
    std::int32_t GetInventoryListSelection(RE::IMenu* a_menu);

    // Common string utilities
    void SpeakText(const std::string& a_text, bool a_interrupt = false);
    std::string CleanLocalizationString(const std::string& a_text);

    // State tracking (common to all pages)
    bool m_announced = false;
    std::int32_t m_lastSelection = -1;
    std::int32_t m_lastTab = -1;
};
