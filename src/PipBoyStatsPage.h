#pragma once

#include "PipBoyPageBase.h"

// Handles the STAT page of the Pip-Boy (page index 0)
// Tabs: Status/Condition(0), SPECIAL(1), Perks(2)
//
// Status tab: reads HP, AP, limb conditions, damage/resist, active effects from dataObj
// SPECIAL tab: reads SPECIALList array, polls List_mc for selection changes
// Perks tab: reads PerksList array, polls List_mc for selection changes
class PipBoyStatsPage : public PipBoyPageBase
{
public:
    PipBoyStatsPage() = default;
    ~PipBoyStatsPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;

private:
    enum class Tab {
        Status = 0,
        SPECIAL = 1,
        Perks = 2
    };

    // Tab localization keys
    static constexpr const char* kTabKeys[] = {
        "$PipboyConditionCategory", "$PipboySPECIALCategory", "$PipboyPerksCategory"
    };
    static constexpr int kTabCount = 3;

    std::string GetLocalizedTabName(std::int32_t a_tabIndex);

    // Status tab - reads all stats from dataObj on activation
    void AnnounceStatusOverview(RE::IMenu* a_menu);

    // SPECIAL tab - polls List_mc.selectedIndex for selection changes
    void CheckSpecialSelection(RE::IMenu* a_menu);
    void AnnounceSpecialStat(const Scaleform::GFx::Value& a_entry);

    // Perks tab - polls List_mc.selectedIndex for selection changes
    void CheckPerksSelection(RE::IMenu* a_menu);
    void AnnouncePerk(const Scaleform::GFx::Value& a_entry);
    std::string GetPerksDescriptionText(RE::IMenu* a_menu);
    void AnnouncePerksRankChange(RE::IMenu* a_menu, const std::string& a_currentDesc);

    // Get the menuObj for a sub-menu to access loaded SWF components
    // For STAT page: statsMenuObj (tab 0), specialMenuObj (tab 1), perksMenuObj (tab 2)
    const Scaleform::GFx::Value* GetActiveSubMenuObj(RE::IMenu* a_menu);

    // Read selectedIndex from a list component. Tries menuObj first, then movie path fallback.
    std::int32_t GetListSelectedIndex(RE::IMenu* a_menu, const char* a_tabName, const char* a_listName = "List_mc");

    // Read selectedEntry from a list component. Tries menuObj first, then movie path fallback.
    bool GetListSelectedEntry(RE::IMenu* a_menu, const char* a_tabName, Scaleform::GFx::Value* a_outEntry, const char* a_listName = "List_mc");

    // Helpers for reading numeric values from Scaleform
    static double GetNumberMember(const Scaleform::GFx::Value& a_obj, const char* a_name, double a_default = 0.0);
    static std::int32_t GetIntMember(const Scaleform::GFx::Value& a_obj, const char* a_name, std::int32_t a_default = 0);
    static std::string GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name);

    // State
    Tab m_currentTab = Tab::Status;
    bool m_statusAnnounced = false;

    // SPECIAL selection tracking
    std::int32_t m_lastSpecialIndex = -1;

    // Perks selection tracking
    std::int32_t m_lastPerksIndex = -1;
    std::string m_lastPerksDescription;
};
