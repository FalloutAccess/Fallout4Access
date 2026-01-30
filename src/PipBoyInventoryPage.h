#pragma once

#include "PipBoyPageBase.h"
#include <vector>
#include <string>

// Handles the INV (Inventory) page of the Pip-Boy
// Tabs: Weapons(0), Apparel(1), Aid(2), Misc(3), Junk(4), Mods(5), Ammo(6)
// Item selection is handled by PipBoyCallbackHook (onInvItemSelection callback).
// Tab changes are detected per-frame from the AdvanceMovie hook.
class PipBoyInventoryPage : public PipBoyPageBase
{
public:
    PipBoyInventoryPage() = default;
    ~PipBoyInventoryPage() override = default;

    // PipBoyPageBase interface
    void OnPageActivated() override;
    void OnPageDeactivated() override;
    void CheckSelection() override;
    void CheckTabChange() override;
    void Reset() override;

private:
    // Tab localization keys (ordered by tab index)
    static constexpr const char* kTabKeys[] = {
        "$InventoryCategoryWeapons",
        "$InventoryCategoryApparel",
        "$InventoryCategoryAid",
        "$InventoryCategoryMisc",
        "$InventoryCategoryJunk",
        "$InventoryCategoryMods",
        "$InventoryCategoryAmmo"
    };
    static constexpr int kTabCount = 7;

    std::string GetLocalizedTabName(std::int32_t a_tabIndex);
};
