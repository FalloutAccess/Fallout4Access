#pragma once

#include "PipBoyPageBase.h"

// Handles the RADIO page of the Pip-Boy (page index 4)
// Shows a list of radio stations that can be tuned in/out.
// No tabs - single page with a BSScrollingList of stations.
//
// Data source: dataObj.RadioList (array of station objects)
// Selection tracking: polls menuObj List_mc.selectedIndex per-frame
class PipBoyRadioPage : public PipBoyPageBase
{
public:
    PipBoyRadioPage() = default;
    ~PipBoyRadioPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;

private:
    // Announce info about the current radio state on page activation
    void AnnounceRadioState(RE::IMenu* a_menu);

    // Announce the currently selected station
    void AnnounceStation(const Scaleform::GFx::Value& a_entry);

    // Get the radio sub-menu's menuObj for accessing the loaded SWF
    const Scaleform::GFx::Value* GetRadioMenuObj(RE::IMenu* a_menu);

    // Read selectedIndex from List_mc (tries menuObj, then movie path)
    std::int32_t GetListSelectedIndex(RE::IMenu* a_menu);

    // Read selectedEntry from List_mc (tries menuObj, then movie path)
    bool GetListSelectedEntry(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outEntry);

    // Helpers
    static std::string GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name);
    static bool GetBoolMember(const Scaleform::GFx::Value& a_obj, const char* a_name);

    // State
    std::int32_t m_lastRadioIndex = -1;
    std::string m_lastStationName;
};
