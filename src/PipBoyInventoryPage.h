#pragma once

#include "PipBoyPageBase.h"
#include <vector>
#include <string>

// Handles the INV (Inventory) page of the Pip-Boy
// Tabs: Weapons(0), Apparel(1), Aid(2), Misc(3), Junk(4), Mods(5), Ammo(6)
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
    const char* GetPageName() const override { return "Inventory"; }

private:
    // Inventory item data
    struct InventoryItem {
        std::string text;              // Display name
        std::int32_t count = 0;        // Stack size
        std::int32_t equipState = 0;   // 0=unequipped, >0=equipped
        bool isLegendary = false;
        bool taggedForSearch = false;
        bool favorite = false;
        // ItemCard stats
        float value = 0.0f;
        float weight = 0.0f;
        std::string damageOrArmor;     // "45 DAM" or "20 DR"
        std::vector<std::string> effects;
    };

    // Tab definitions
    enum class Tab {
        Weapons = 0,
        Apparel = 1,
        Aid = 2,
        Misc = 3,
        Junk = 4,
        Mods = 5,
        Ammo = 6
    };

    // Data retrieval
    InventoryItem GetInventoryItem(RE::IMenu* a_menu, std::int32_t a_index);
    void ParseItemCard(RE::IMenu* a_menu, InventoryItem& a_item);

    // Announcement building
    std::string BuildItemAnnouncement(const InventoryItem& a_item);
    const char* GetTabName(std::int32_t a_tabIndex);

    // Selection tracking - we track this ourselves since Scaleform access is difficult
    std::int32_t GetItemCount(RE::IMenu* a_menu);
    std::int32_t GetInitialSelection(RE::IMenu* a_menu);

public:
    // Called by MenuAccessibility to handle navigation
    void OnNavigateUp();
    void OnNavigateDown();
};
