#pragma once

#include "pch.h"
#include <RE/P/PipboyMenu.h>

// Abstract base class for Pip-Boy page handlers
// Each page (STAT, INV, DATA, MAP, RADIO) extends this class.
// CheckSelection() and CheckTabChange() are called per-frame from the AdvanceMovie hook.
class PipBoyPageBase
{
public:
    virtual ~PipBoyPageBase() = default;

    // Lifecycle hooks
    virtual void OnPageActivated() = 0;
    virtual void OnPageDeactivated() = 0;

    // Per-frame state checks (called from AdvanceMovie via PipBoyMenuAccessibility)
    virtual void CheckSelection() = 0;  // Page-specific per-frame checks (e.g. Map mouse selection)
    virtual void CheckTabChange() = 0;  // Detect tab changes within the page

    // State management
    virtual void Reset() = 0;

protected:
    // Get the dataObj from PipboyMenu (direct member access, much more reliable)
    static const Scaleform::GFx::Value* GetDataObj(RE::IMenu* a_menu);

    // Common data access helpers using dataObj
    std::int32_t GetCurrentTab(RE::IMenu* a_menu);
    bool GetInvSelectedItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray);
    bool GetInvItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray);

    // Get the live selection from the loaded inventory page's List_mc
    std::int32_t GetInventoryListSelection(RE::IMenu* a_menu);

    // Find a named child on a DisplayObject. Tries multiple approaches:
    // 1. GetMember (works for callback-provided page refs)
    // 2. Invoke("getChildByName") (AS3 DisplayObjectContainer method)
    // 3. movie->GetVariable with computed paths
    static bool GetNamedChild(const Scaleform::GFx::Value& a_displayObj, const char* a_childName, Scaleform::GFx::Value* a_outChild);

    // Get a list component by navigating through the movie's variable resolution.
    // Uses paths like "_currentPage.SPECIALTab_mc.List_mc" to bypass menuObj limitations.
    static bool GetListViaMovie(RE::IMenu* a_menu, const char* a_tabName, const char* a_listName, Scaleform::GFx::Value* a_outList);

    // Common string utilities
    void SpeakText(const std::string& a_text, bool a_interrupt = false);
    std::string CleanLocalizationString(const std::string& a_text);

    // State tracking (common to all pages)
    bool m_announced = false;
    std::int32_t m_lastSelection = -1;
    std::int32_t m_lastTab = -1;
};
