#include "ExamineMenuHook.h"
#include "ItemStatFormatter.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <RE/T/TESFullName.h>
#include <SRAL.h>

ExamineMenuHook* ExamineMenuHook::GetSingleton()
{
    static ExamineMenuHook singleton;
    return &singleton;
}

void ExamineMenuHook::Install()
{
    if (m_installed) {
        REX::DEBUG("ExamineMenuHook: Already installed");
        return;
    }

    bool allSucceeded = true;

    // Hook ExamineMenu::AdvanceMovie (vtable index 4)
    static REL::HookVFT<void(RE::ExamineMenu*, float, std::uint64_t)> examineHook(
        VTABLE::ExamineMenu[0],
        4,
        HookedExamineAdvanceMovie
    );
    if (examineHook.Enable()) {
        s_advanceMovieHook = &examineHook;
        REX::INFO("ExamineMenuHook: ExamineMenu hook installed");
    } else {
        REX::ERROR("ExamineMenuHook: Failed to install ExamineMenu hook");
        allSucceeded = false;
    }

    // Hook PowerArmorModMenu::AdvanceMovie (separate callback for correct trampoline)
    static REL::HookVFT<void(RE::ExamineMenu*, float, std::uint64_t)> powerArmorHook(
        VTABLE::PowerArmorModMenu[0],
        4,
        HookedPowerArmorAdvanceMovie
    );
    if (powerArmorHook.Enable()) {
        s_powerArmorAdvanceMovieHook = &powerArmorHook;
        REX::INFO("ExamineMenuHook: PowerArmorModMenu hook installed");
    } else {
        REX::ERROR("ExamineMenuHook: Failed to install PowerArmorModMenu hook");
        allSucceeded = false;
    }

    // Hook CookingMenu::AdvanceMovie (no header in CommonLibF4, use raw VTABLE ID)
    static REL::HookVFT<void(RE::ExamineMenu*, float, std::uint64_t)> cookingHook(
        REL::ID(1395125),
        4,
        HookedCookingAdvanceMovie
    );
    if (cookingHook.Enable()) {
        s_cookingAdvanceMovieHook = &cookingHook;
        REX::INFO("ExamineMenuHook: CookingMenu hook installed");
    } else {
        REX::ERROR("ExamineMenuHook: Failed to install CookingMenu hook");
        allSucceeded = false;
    }

    // Hook RobotModMenu::AdvanceMovie (no header in CommonLibF4, use raw VTABLE ID)
    static REL::HookVFT<void(RE::ExamineMenu*, float, std::uint64_t)> robotModHook(
        REL::ID(1057885),
        4,
        HookedRobotModAdvanceMovie
    );
    if (robotModHook.Enable()) {
        s_robotModAdvanceMovieHook = &robotModHook;
        REX::INFO("ExamineMenuHook: RobotModMenu hook installed");
    } else {
        REX::ERROR("ExamineMenuHook: Failed to install RobotModMenu hook");
        allSucceeded = false;
    }

    m_installed = true;
    if (allSucceeded) {
        REX::INFO("ExamineMenuHook: All workbench hooks installed successfully");
    } else {
        REX::WARN("ExamineMenuHook: Some workbench hooks failed to install");
    }
}

void ExamineMenuHook::ResetState()
{
    m_lastMode = RE::ExamineMenu::INSPECT_MODE_STATE::kItemSelect;
    m_lastItemSelection = -1;
    m_lastModSlotSelection = -1;
    m_lastModChoiceSelection = -1;
    m_firstCheck = true;
    m_frameDelay = FRAME_DELAY_AFTER_MODE_CHANGE;  // Wait for Scaleform to populate on menu open
    m_lastSpokenText.clear();  // Clear deduplication cache
}

void ExamineMenuHook::HookedExamineAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }
    GetSingleton()->ProcessAdvanceMovie(a_this);
}

void ExamineMenuHook::HookedPowerArmorAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_powerArmorAdvanceMovieHook) {
        (*s_powerArmorAdvanceMovieHook)(a_this, a_timeDelta, a_time);
    }
    GetSingleton()->ProcessAdvanceMovie(a_this);
}

void ExamineMenuHook::HookedCookingAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_cookingAdvanceMovieHook) {
        (*s_cookingAdvanceMovieHook)(a_this, a_timeDelta, a_time);
    }
    GetSingleton()->ProcessAdvanceMovie(a_this);
}

void ExamineMenuHook::HookedRobotModAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    if (s_robotModAdvanceMovieHook) {
        (*s_robotModAdvanceMovieHook)(a_this, a_timeDelta, a_time);
    }
    GetSingleton()->ProcessAdvanceMovie(a_this);
}

void ExamineMenuHook::ProcessAdvanceMovie(RE::ExamineMenu* a_this)
{
    if (m_menuOpen) {
        CheckStateChanges(a_this);
    }
}

void ExamineMenuHook::CheckStateChanges(RE::ExamineMenu* a_menu)
{
    if (!a_menu) return;

    // Wait for initial frame delay after menu open
    if (m_frameDelay > 0) {
        m_frameDelay--;
        return;
    }

    // Mode-agnostic approach: check ALL lists for selection changes
    // This avoids unreliable mode detection

    // Check item list (weapon/armor selection)
    OnItemSelectMode(a_menu);

    // Check mod slot list (which slot to modify)
    OnModSlotMode(a_menu);

    // Check mod choice list (which mod to install)
    OnModChoiceMode(a_menu);
}

void ExamineMenuHook::OnModeChanged(RE::ExamineMenu* a_menu, RE::ExamineMenu::INSPECT_MODE_STATE a_newMode)
{
    (void)a_menu;  // Used after frame delay

    // Don't immediately call mode handlers - let frame delay elapse first
    // so Scaleform has time to populate data. The mode handlers will be
    // called after the delay and will announce the first item.
    switch (a_newMode) {
        case RE::ExamineMenu::INSPECT_MODE_STATE::kItemSelect: {
            // First announcement will happen after frame delay
            REX::DEBUG("ExamineMenuHook: Entered item select mode");
            break;
        }
        case RE::ExamineMenu::INSPECT_MODE_STATE::kModSlotSelect: {
            REX::DEBUG("ExamineMenuHook: Entered mod slot select mode");
            break;
        }
        case RE::ExamineMenu::INSPECT_MODE_STATE::kSelectNewMod: {
            REX::DEBUG("ExamineMenuHook: Entered mod choice mode");
            break;
        }
        case RE::ExamineMenu::INSPECT_MODE_STATE::kInvComponentSelect: {
            SpeakText("Select components");
            break;
        }
        case RE::ExamineMenu::INSPECT_MODE_STATE::kConfirm: {
            SpeakText("Confirm modification");
            break;
        }
    }
}

void ExamineMenuHook::OnItemSelectMode(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Check if itemList is valid
    bool itemListValid = !a_menu->itemList.IsUndefined() && a_menu->itemList.IsObject();

    // Get selection from inventory list using path-based access
    std::int32_t currentSelection = -1;

    // First try path-based access (more reliable)
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, "root.InventoryBase_mc.InventoryList_mc.selectedIndex")) {
        if (indexValue.IsInt()) {
            currentSelection = indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            currentSelection = static_cast<std::int32_t>(indexValue.GetNumber());
        } else if (indexValue.IsUInt()) {
            currentSelection = static_cast<std::int32_t>(indexValue.GetUInt());
        }
    }

    // Fallback to member variable if path failed
    if (currentSelection < 0 && itemListValid) {
        currentSelection = GetSelectedIndex(a_menu->itemList);
    }

    if (currentSelection >= 0 && currentSelection != m_lastItemSelection) {
        m_lastItemSelection = currentSelection;

        // Build and announce item description with full stats
        std::string announcement = BuildItemAnnouncement(a_menu, currentSelection);
        if (!announcement.empty()) {
            SpeakText(announcement);
        }
    }
}

void ExamineMenuHook::OnModSlotMode(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Try path-based access first
    std::int32_t currentSelection = -1;
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, "root.ModSlotBase_mc.ModSlotList_mc.selectedIndex")) {
        if (indexValue.IsInt()) {
            currentSelection = indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            currentSelection = static_cast<std::int32_t>(indexValue.GetNumber());
        } else if (indexValue.IsUInt()) {
            currentSelection = static_cast<std::int32_t>(indexValue.GetUInt());
        }
    }

    // Fallback to member variable
    if (currentSelection < 0) {
        currentSelection = GetSelectedIndex(a_menu->modSlotList);
    }

    if (currentSelection >= 0 && currentSelection != m_lastModSlotSelection) {
        m_lastModSlotSelection = currentSelection;

        std::string slotName = GetModSlotName(a_menu, currentSelection);
        if (!slotName.empty()) {
            SpeakText(slotName);
        }
    }
}

void ExamineMenuHook::OnModChoiceMode(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Check if modChoiceList is valid
    bool modChoiceListValid = !a_menu->modChoiceList.IsUndefined() && a_menu->modChoiceList.IsObject();

    // Try member variable first for mod choice
    std::int32_t currentSelection = -1;

    if (modChoiceListValid) {
        currentSelection = GetSelectedIndex(a_menu->modChoiceList);
    }

    if (currentSelection >= 0 && currentSelection != m_lastModChoiceSelection) {
        m_lastModChoiceSelection = currentSelection;

        // Get mod name
        std::string modName = GetModName(a_menu, currentSelection);
        if (modName.empty()) return;

        std::string announcement = modName;

        // Get mod description
        std::string description = GetModDescription(a_menu);
        if (!description.empty()) {
            announcement += ". " + description;
        }

        // Get requirements
        std::string requirements = GetModRequirements(a_menu, currentSelection);
        if (!requirements.empty()) {
            announcement += ". " + requirements;
        }

        SpeakText(announcement);
    }
}

void ExamineMenuHook::OnRequirementsMode(RE::ExamineMenu* a_menu)
{
    // Component selection mode - read from requirements list
    // For now, just track that we're in this mode
    (void)a_menu;
}

void ExamineMenuHook::OnConfirmMode(RE::ExamineMenu* a_menu)
{
    // Confirmation mode - usually just a yes/no dialog
    (void)a_menu;
}

std::int32_t ExamineMenuHook::GetSelectedIndex(const Scaleform::GFx::Value& a_list)
{
    if (a_list.IsUndefined() || !a_list.IsObject()) {
        return -1;
    }

    Scaleform::GFx::Value indexValue;
    if (a_list.GetMember("selectedIndex", &indexValue)) {
        if (indexValue.IsInt()) {
            return indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            return static_cast<std::int32_t>(indexValue.GetNumber());
        } else if (indexValue.IsUInt()) {
            return static_cast<std::int32_t>(indexValue.GetUInt());
        }
    }

    return -1;
}

std::string ExamineMenuHook::GetItemName(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value nameValue;
    if (a_menu->uiMovie->GetVariable(&nameValue, ITEM_NAME_PATH)) {
        if (nameValue.IsString()) {
            return CleanLocalizationString(nameValue.GetString());
        }
    }

    return "";
}

std::string ExamineMenuHook::GetItemStats(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    // Get item card stats via Scaleform path (member itemInfoList is never populated)
    Scaleform::GFx::Value infoObj;
    if (!a_menu->uiMovie->GetVariable(&infoObj, ITEM_CARD_PATH) || !infoObj.IsArray()) {
        return "";
    }

    // Use ItemStatFormatter to parse and build announcement
    auto stats = ItemStatFormatter::ParseInfoObj(infoObj);
    return ItemStatFormatter::BuildStatAnnouncement(stats);
}

std::string ExamineMenuHook::GetModSlotName(RE::ExamineMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return "";

    // Try path-based access first
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, "root.ModSlotBase_mc.ModSlotList_mc.entryList")) {
        // Path failed, try member
        // Fallback to member variable
        if (a_menu->modSlotList.IsUndefined() || !a_menu->modSlotList.GetMember("entryList", &entryList)) {
            // Member also failed
            return "";
        }
    }

    if (!entryList.IsArray()) {
        // Removed debug log
        return "";
    }

    // entryList found

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        // Removed debug log
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        // Removed debug log
        return "";
    }

    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return CleanLocalizationString(textValue.GetString());
    }

    // Removed debug log
    return "";
}

std::string ExamineMenuHook::GetModName(RE::ExamineMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return "";

    // Try path-based access first
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, "root.ModSlotBase_mc.ModSlotList_mc.entryList")) {
        // Removed debug log
        // Fallback to member variable
        if (a_menu->modChoiceList.IsUndefined() || !a_menu->modChoiceList.GetMember("entryList", &entryList)) {
            // Removed debug log
            return "";
        }
    }

    if (!entryList.IsArray()) {
        // Removed debug log
        return "";
    }

    // entryList found

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        // Removed debug log
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        // Removed debug log
        return "";
    }

    std::string modName;
    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        modName = CleanLocalizationString(textValue.GetString());
    }

    // Check if this is the currently installed mod
    Scaleform::GFx::Value currentModVal;
    if (entry.GetMember("currentMod", &currentModVal) && currentModVal.IsBoolean()) {
        if (currentModVal.GetBoolean()) {
            modName += " (installed)";
        }
    }

    // Check if player has required components
    Scaleform::GFx::Value hasRequiredVal;
    if (entry.GetMember("hasRequired", &hasRequiredVal) && hasRequiredVal.IsBoolean()) {
        if (!hasRequiredVal.GetBoolean()) {
            modName += " (missing components)";
        }
    }

    return modName;
}

std::string ExamineMenuHook::GetModDescription(RE::ExamineMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value descValue;
    if (a_menu->uiMovie->GetVariable(&descValue, MOD_DESCRIPTION_PATH)) {
        if (descValue.IsString()) {
            std::string desc = descValue.GetString();
            if (!desc.empty()) {
                return CleanLocalizationString(desc);
            }
        }
    }

    return "";
}

std::string ExamineMenuHook::GetModRequirements(RE::ExamineMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu) return "";

    // Access the C++ modChoiceArray for detailed requirements
    if (static_cast<std::uint32_t>(a_index) >= a_menu->modChoiceArray.size()) {
        return "";
    }

    const auto& modChoice = a_menu->modChoiceArray[a_index];

    std::string requirements;

    // Get required items (components)
    if (modChoice.requiredItems && !modChoice.requiredItems->empty()) {
        requirements = "Requires: ";
        bool first = true;
        for (const auto& [form, value] : *modChoice.requiredItems) {
            if (!first) {
                requirements += ", ";
            }
            first = false;

            std::string name;
            if (form) {
                // Use TESFullName::GetFullName static method to get the display name
                auto fullName = RE::TESFullName::GetFullName(*form);
                if (!fullName.empty()) {
                    name = std::string(fullName);
                } else if (form->GetFormEditorID()) {
                    name = form->GetFormEditorID();
                }
            }

            if (name.empty()) {
                name = "Unknown";
            }

            // value.i is the count
            requirements += std::to_string(value.i) + " " + name;
        }
    }

    // Get required perks
    if (!modChoice.requiredPerks.empty()) {
        if (!requirements.empty()) {
            requirements += ". ";
        }
        requirements += "Perks: ";
        bool first = true;
        for (const auto& [perk, rank] : modChoice.requiredPerks) {
            if (!first) {
                requirements += ", ";
            }
            first = false;

            std::string perkName;
            if (perk) {
                // Use TESFullName::GetFullName static method to get the display name
                auto fullName = RE::TESFullName::GetFullName(*perk);
                if (!fullName.empty()) {
                    perkName = std::string(fullName);
                } else if (perk->GetFormEditorID()) {
                    perkName = perk->GetFormEditorID();
                }
            }

            if (perkName.empty()) {
                perkName = "Unknown Perk";
            }

            if (rank > 1) {
                perkName += " rank " + std::to_string(rank);
            }
            requirements += perkName;
        }
    }

    return requirements;
}

std::string ExamineMenuHook::BuildItemAnnouncement(RE::ExamineMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return "";

    // Get entry list using path-based access (more reliable)
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, "root.InventoryBase_mc.InventoryList_mc.entryList")) {
        // Removed debug log
        // Fallback to member variable
        if (a_menu->itemList.IsUndefined() || !a_menu->itemList.GetMember("entryList", &entryList)) {
            return GetItemName(a_menu);
        }
    }

    if (!entryList.IsArray()) {
        // Removed debug log
        return GetItemName(a_menu);
    }

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        return "";
    }

    std::string announcement;

    // Item name
    Scaleform::GFx::Value textVal;
    if (entry.GetMember("text", &textVal) && textVal.IsString()) {
        announcement = CleanLocalizationString(textVal.GetString());
    }

    if (announcement.empty()) {
        return "";
    }

    // Count
    Scaleform::GFx::Value countVal;
    if (entry.GetMember("count", &countVal)) {
        int count = 0;
        if (countVal.IsInt()) count = countVal.GetInt();
        else if (countVal.IsNumber()) count = static_cast<int>(countVal.GetNumber());
        else if (countVal.IsUInt()) count = static_cast<int>(countVal.GetUInt());

        if (count > 1) {
            announcement += ", " + std::to_string(count);
        }
    }

    // Equipped state
    Scaleform::GFx::Value equipVal;
    if (entry.GetMember("equipState", &equipVal)) {
        int equipState = 0;
        if (equipVal.IsInt()) equipState = equipVal.GetInt();
        else if (equipVal.IsNumber()) equipState = static_cast<int>(equipVal.GetNumber());
        else if (equipVal.IsUInt()) equipState = static_cast<int>(equipVal.GetUInt());

        if (equipState > 0) {
            announcement += ", equipped";
        }
    }

    // Legendary indicator
    Scaleform::GFx::Value legendaryVal;
    if (entry.GetMember("isLegendary", &legendaryVal) && legendaryVal.IsBoolean()) {
        if (legendaryVal.GetBoolean()) {
            announcement += ", legendary";
        }
    }

    // Best in class
    Scaleform::GFx::Value bestVal;
    if (entry.GetMember("bestInClass", &bestVal) && bestVal.IsBoolean()) {
        if (bestVal.GetBoolean()) {
            announcement += ", best";
        }
    }

    // Add item card stats (DAM, Value, Weight, etc.)
    std::string stats = GetItemStats(a_menu);
    if (!stats.empty()) {
        announcement += ". " + stats;
    }

    // Add legendary description if present
    Scaleform::GFx::Value legendaryDesc;
    if (a_menu->uiMovie->GetVariable(&legendaryDesc, LEGENDARY_DESC_PATH)) {
        if (legendaryDesc.IsString()) {
            std::string desc = legendaryDesc.GetString();
            if (!desc.empty()) {
                announcement += ". " + CleanLocalizationString(desc);
            }
        }
    }

    return announcement;
}

std::string ExamineMenuHook::CleanLocalizationString(const std::string& a_text)
{
    return LocalizationHelper::CleanString(a_text);
}

void ExamineMenuHook::SpeakText(const std::string& a_text)
{
    if (a_text.empty()) return;

    // Deduplicate - don't repeat the same text
    if (a_text == m_lastSpokenText) {
        return;
    }
    m_lastSpokenText = a_text;

    SpeechManager::GetSingleton()->Speak(a_text);
}
