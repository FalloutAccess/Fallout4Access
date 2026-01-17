#include "PipBoyInventoryPage.h"

void PipBoyInventoryPage::OnPageActivated()
{
    REX::INFO("PipBoyInventoryPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;

    // Announce current tab (item will be announced by hook via onInvItemSelection)
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto tab = GetCurrentTab(menu.get());
    if (tab >= 0) {
        m_lastTab = tab;
        SpeakText(GetTabName(tab));
    }
}

void PipBoyInventoryPage::OnPageDeactivated()
{
    REX::INFO("PipBoyInventoryPage: Deactivated");
}

void PipBoyInventoryPage::CheckSelection()
{
    // Item announcements are handled by PipBoyCallbackHook via onInvItemSelection callback.
    // This method only handles tab change detection.

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    // Check if tab changed
    auto currentTab = GetCurrentTab(menu.get());

    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        SpeakText(GetTabName(currentTab));
        // Item announcement will be triggered by the hook when onInvItemSelection fires
    }
}

void PipBoyInventoryPage::OnNavigateUp()
{
    // Item announcements are now handled by PipBoyCallbackHook via onInvItemSelection callback
    // This method is kept for interface compatibility but does nothing
}

void PipBoyInventoryPage::OnNavigateDown()
{
    // Item announcements are now handled by PipBoyCallbackHook via onInvItemSelection callback
    // This method is kept for interface compatibility but does nothing
}

std::int32_t PipBoyInventoryPage::GetItemCount(RE::IMenu* a_menu)
{
    Scaleform::GFx::Value invItems;
    if (GetInvItems(a_menu, &invItems)) {
        return static_cast<std::int32_t>(invItems.GetArraySize());
    }
    return 0;
}

std::int32_t PipBoyInventoryPage::GetInitialSelection(RE::IMenu* a_menu)
{
    // Get the saved selection from InvSelectedItems for the current tab
    return GetInventoryListSelection(a_menu);
}

void PipBoyInventoryPage::CheckTabChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_lastSelection = -1;  // Reset selection on tab change
        SpeakText(GetTabName(currentTab));
    }
}

void PipBoyInventoryPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
}

PipBoyInventoryPage::InventoryItem PipBoyInventoryPage::GetInventoryItem(RE::IMenu* a_menu, std::int32_t a_index)
{
    InventoryItem item;

    if (!a_menu || a_index < 0) {
        return item;
    }

    // Get the InvItems array using the base class helper
    Scaleform::GFx::Value invItems;
    if (!GetInvItems(a_menu, &invItems)) {
        REX::DEBUG("PipBoyInventoryPage: Could not get InvItems array");
        return item;
    }

    if (static_cast<std::uint32_t>(a_index) >= invItems.GetArraySize()) {
        REX::DEBUG("PipBoyInventoryPage: Index {} out of bounds (size={})", a_index, invItems.GetArraySize());
        return item;
    }

    // Get the item entry
    Scaleform::GFx::Value entry;
    if (!invItems.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        REX::DEBUG("PipBoyInventoryPage: Could not get entry at index {}", a_index);
        return item;
    }

    // Read item properties
    Scaleform::GFx::Value prop;

    // Text (name)
    if (entry.GetMember("text", &prop) && prop.IsString()) {
        item.text = CleanLocalizationString(prop.GetString());
    }

    // Count
    if (entry.GetMember("count", &prop)) {
        if (prop.IsInt()) {
            item.count = prop.GetInt();
        } else if (prop.IsNumber()) {
            item.count = static_cast<std::int32_t>(prop.GetNumber());
        } else if (prop.IsUInt()) {
            item.count = static_cast<std::int32_t>(prop.GetUInt());
        }
    }

    // Equip state (0=unequipped, >0=equipped)
    if (entry.GetMember("equipState", &prop)) {
        if (prop.IsInt()) {
            item.equipState = prop.GetInt();
        } else if (prop.IsNumber()) {
            item.equipState = static_cast<std::int32_t>(prop.GetNumber());
        } else if (prop.IsUInt()) {
            item.equipState = static_cast<std::int32_t>(prop.GetUInt());
        }
    }

    // Legendary
    if (entry.GetMember("isLegendary", &prop) && prop.IsBoolean()) {
        item.isLegendary = prop.GetBoolean();
    }

    // Tagged for search
    if (entry.GetMember("taggedForSearch", &prop) && prop.IsBoolean()) {
        item.taggedForSearch = prop.GetBoolean();
    }

    // Favorite
    if (entry.GetMember("favorite", &prop)) {
        if (prop.IsBoolean()) {
            item.favorite = prop.GetBoolean();
        } else if (prop.IsNumber() || prop.IsInt() || prop.IsUInt()) {
            // favorite can be a number (quickkey slot) where 0 = not favorited
            int favVal = 0;
            if (prop.IsInt()) favVal = prop.GetInt();
            else if (prop.IsNumber()) favVal = static_cast<int>(prop.GetNumber());
            else if (prop.IsUInt()) favVal = static_cast<int>(prop.GetUInt());
            item.favorite = (favVal > 0);
        }
    }

    // Parse ItemCard for detailed stats
    ParseItemCard(a_menu, item);

    return item;
}

void PipBoyInventoryPage::ParseItemCard(RE::IMenu* a_menu, InventoryItem& a_item)
{
    // ItemCard is part of the loaded page SWF, not the main DataObj
    // For now, we skip detailed item stats parsing as it requires different access pattern
    // The basic item info (name, count, equipped) from InvItems is sufficient
    (void)a_menu;
    (void)a_item;
}

std::string PipBoyInventoryPage::BuildItemAnnouncement(const InventoryItem& a_item)
{
    std::string announcement = a_item.text;

    // Add count if more than 1
    if (a_item.count > 1) {
        announcement += ", " + std::to_string(a_item.count);
    }

    // Add equipped status
    if (a_item.equipState > 0) {
        announcement += ", equipped";
    }

    // Add legendary indicator
    if (a_item.isLegendary) {
        announcement += ", legendary";
    }

    // Add favorite indicator
    if (a_item.favorite) {
        announcement += ", favorite";
    }

    // Add tagged for search indicator
    if (a_item.taggedForSearch) {
        announcement += ", tagged";
    }

    // Add damage/armor stat
    if (!a_item.damageOrArmor.empty()) {
        announcement += ", " + a_item.damageOrArmor;
    }

    // Add value
    if (a_item.value > 0) {
        announcement += ", value " + std::to_string(static_cast<int>(a_item.value));
    }

    // Add weight
    if (a_item.weight > 0) {
        char weightStr[32];
        snprintf(weightStr, sizeof(weightStr), "%.1f", a_item.weight);
        announcement += ", weight " + std::string(weightStr);
    }

    // Add effects (first 2 only to keep announcement manageable)
    size_t effectCount = std::min(a_item.effects.size(), static_cast<size_t>(2));
    for (size_t i = 0; i < effectCount; ++i) {
        announcement += ", " + a_item.effects[i];
    }

    return announcement;
}

const char* PipBoyInventoryPage::GetTabName(std::int32_t a_tabIndex)
{
    switch (static_cast<Tab>(a_tabIndex)) {
        case Tab::Weapons: return "Weapons";
        case Tab::Apparel: return "Apparel";
        case Tab::Aid:     return "Aid";
        case Tab::Misc:    return "Miscellaneous";
        case Tab::Junk:    return "Junk";
        case Tab::Mods:    return "Mods";
        case Tab::Ammo:    return "Ammo";
        default:           return "Unknown";
    }
}
