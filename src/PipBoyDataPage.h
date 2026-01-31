#pragma once

#include "PipBoyPageBase.h"

// Handles the DATA page of the Pip-Boy
// Tabs: Quests(0), Workshops(1), Stats(2)
// Quest selection is handled by PipBoyCallbackHook callbacks.
// Workshops and Stats use per-frame polling of List_mc.selectedIndex.
// Tab changes are detected per-frame from the AdvanceMovie hook.
class PipBoyDataPage : public PipBoyPageBase
{
public:
    PipBoyDataPage() = default;
    ~PipBoyDataPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;

private:
    // Tab definitions
    enum class Tab {
        Quests = 0,
        Workshops = 1,
        Stats = 2
    };

    // Tab localization keys (ordered by tab index)
    static constexpr const char* kTabKeys[] = { "$QUESTS", "$SANCTUARY", "$LOG" };
    static constexpr int kTabCount = 3;

    std::string GetLocalizedTabName(std::int32_t a_tabIndex);

    // Workshop tab - polls List_mc.selectedIndex for selection changes
    void CheckWorkshopSelection(RE::IMenu* a_menu);
    void AnnounceWorkshop(const Scaleform::GFx::Value& a_entry);

    // Stats tab - polls CategoryList_mc.selectedIndex for category changes
    void CheckStatsSelection(RE::IMenu* a_menu);
    void AnnounceStatsCategory(const Scaleform::GFx::Value& a_entry);

    // Get the menuObj for the appropriate DATA sub-menu
    const Scaleform::GFx::Value* GetWorkshopMenuObj(RE::IMenu* a_menu);
    const Scaleform::GFx::Value* GetLogMenuObj(RE::IMenu* a_menu);

    // Read selectedIndex from a BSScrollingList (tries menuObj, then movie path)
    std::int32_t GetListSelectedIndex(RE::IMenu* a_menu, const Scaleform::GFx::Value& a_menuObj, const char* a_tabName, const char* a_listName);

    // Read selectedEntry from a BSScrollingList (tries menuObj, then movie path)
    bool GetListSelectedEntry(RE::IMenu* a_menu, const Scaleform::GFx::Value& a_menuObj, Scaleform::GFx::Value* a_outEntry, const char* a_tabName, const char* a_listName);

    // Helpers
    static std::string GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name);
    static std::int32_t GetIntMember(const Scaleform::GFx::Value& a_obj, const char* a_name, std::int32_t a_default = 0);

    // Current tab tracking
    Tab m_currentDataTab = Tab::Quests;

    // Workshop selection tracking
    std::int32_t m_lastWorkshopIndex = -1;

    // Stats category selection tracking
    std::int32_t m_lastStatsCategoryIndex = -1;
};
