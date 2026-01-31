#include "PipBoyDataPage.h"
#include "PipBoyCallbackHook.h"
#include "LocalizationHelper.h"
#include <RE/P/PipboyMenu.h>

void PipBoyDataPage::OnPageActivated()
{
    REX::INFO("PipBoyDataPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
    m_lastWorkshopIndex = -1;
    m_lastStatsCategoryIndex = -1;

    // Announce current tab
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto tab = GetCurrentTab(menu.get());
    if (tab >= 0) {
        m_lastTab = tab;
        m_currentDataTab = static_cast<Tab>(tab);
        SpeakText(GetLocalizedTabName(tab));
    }
}

void PipBoyDataPage::OnPageDeactivated()
{
    REX::INFO("PipBoyDataPage: Deactivated");
    PipBoyCallbackHook::GetSingleton()->ClearQuestState();
}

void PipBoyDataPage::CheckSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    switch (m_currentDataTab) {
        case Tab::Quests:
            // Quest selection is handled by PipBoyCallbackHook callbacks
            break;
        case Tab::Workshops:
            CheckWorkshopSelection(menu.get());
            break;
        case Tab::Stats:
            CheckStatsSelection(menu.get());
            break;
    }
}

void PipBoyDataPage::CheckTabChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    auto currentTab = GetCurrentTab(menu.get());
    if (currentTab >= 0 && currentTab != m_lastTab) {
        m_lastTab = currentTab;
        m_currentDataTab = static_cast<Tab>(currentTab);
        m_lastSelection = -1;
        m_lastWorkshopIndex = -1;
        m_lastStatsCategoryIndex = -1;

        // Clear quest state when leaving Quests tab
        if (currentTab != static_cast<std::int32_t>(Tab::Quests)) {
            PipBoyCallbackHook::GetSingleton()->ClearQuestState();
        }
        SpeakText(GetLocalizedTabName(currentTab));
    }
}

void PipBoyDataPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastTab = -1;
    m_currentDataTab = Tab::Quests;
    m_lastWorkshopIndex = -1;
    m_lastStatsCategoryIndex = -1;
    PipBoyCallbackHook::GetSingleton()->ClearQuestState();
}

std::string PipBoyDataPage::GetLocalizedTabName(std::int32_t a_tabIndex)
{
    if (a_tabIndex >= 0 && a_tabIndex < kTabCount) {
        return LocalizationHelper::Translate(kTabKeys[a_tabIndex]);
    }
    return "Unknown";
}

// ============================================================================
// Workshop Tab
// ============================================================================

void PipBoyDataPage::CheckWorkshopSelection(RE::IMenu* a_menu)
{
    const auto* workshopObj = GetWorkshopMenuObj(a_menu);
    if (!workshopObj || !workshopObj->IsObject()) return;

    std::int32_t selectedIndex = GetListSelectedIndex(a_menu, *workshopObj, "WorkshopsTab_mc", "List_mc");
    if (selectedIndex < 0 || selectedIndex == m_lastWorkshopIndex) return;
    m_lastWorkshopIndex = selectedIndex;

    Scaleform::GFx::Value entry;
    if (GetListSelectedEntry(a_menu, *workshopObj, &entry, "WorkshopsTab_mc", "List_mc")) {
        AnnounceWorkshop(entry);
    }
}

void PipBoyDataPage::AnnounceWorkshop(const Scaleform::GFx::Value& a_entry)
{
    std::string name = GetStringMember(a_entry, "text");
    if (name.empty()) return;

    std::string announcement = name;

    // Read workshopData array (7 stats)
    Scaleform::GFx::Value workshopData;
    if (a_entry.GetMember("workshopData", &workshopData) && workshopData.IsArray()) {
        static const char* kStatKeys[] = {
            "$WS_People", "$WS_Food", "$WS_Water", "$WS_Power",
            "$WS_Safety", "$WS_Beds", "$WS_Happy"
        };
        static constexpr int kStatCount = 7;

        std::uint32_t count = workshopData.GetArraySize();
        for (std::uint32_t i = 0; i < count && i < static_cast<std::uint32_t>(kStatCount); i++) {
            Scaleform::GFx::Value stat;
            if (!workshopData.GetElement(i, &stat) || !stat.IsObject()) continue;

            std::int32_t value = GetIntMember(stat, "value");
            std::int32_t rating = GetIntMember(stat, "rating");

            std::string statName = LocalizationHelper::Translate(kStatKeys[i]);
            announcement += ", " + statName + " " + std::to_string(value);

            // Rating: 0=normal, 1=good (green arrow), -1=bad (red arrow)
            if (rating > 0) {
                announcement += " up";
            } else if (rating < 0) {
                announcement += " down";
            }
        }
    }

    // Check if under attack
    Scaleform::GFx::Value raiderVal;
    if (a_entry.GetMember("raider", &raiderVal)) {
        bool underAttack = false;
        if (raiderVal.IsBoolean()) underAttack = raiderVal.GetBoolean();
        else if (raiderVal.IsInt()) underAttack = raiderVal.GetInt() != 0;
        else if (raiderVal.IsNumber()) underAttack = raiderVal.GetNumber() != 0.0;

        if (underAttack) {
            announcement += ", under attack";
        }
    }

    SpeakText(announcement);
}

// ============================================================================
// Stats/Log Tab
// ============================================================================

void PipBoyDataPage::CheckStatsSelection(RE::IMenu* a_menu)
{
    const auto* logObj = GetLogMenuObj(a_menu);
    if (!logObj || !logObj->IsObject()) return;

    std::int32_t selectedIndex = GetListSelectedIndex(a_menu, *logObj, "StatsTab_mc", "CategoryList_mc");
    if (selectedIndex < 0 || selectedIndex == m_lastStatsCategoryIndex) return;
    m_lastStatsCategoryIndex = selectedIndex;

    // Read the selected category from the GeneralStatsList in dataObj
    const auto* dataObj = GetDataObj(a_menu);
    if (!dataObj) return;

    Scaleform::GFx::Value statsList;
    if (!dataObj->GetMember("GeneralStatsList", &statsList) || !statsList.IsArray()) return;

    std::uint32_t count = statsList.GetArraySize();
    if (static_cast<std::uint32_t>(selectedIndex) >= count) return;

    Scaleform::GFx::Value category;
    if (statsList.GetElement(static_cast<std::uint32_t>(selectedIndex), &category) && category.IsObject()) {
        AnnounceStatsCategory(category);
    }
}

void PipBoyDataPage::AnnounceStatsCategory(const Scaleform::GFx::Value& a_entry)
{
    std::string categoryName = GetStringMember(a_entry, "text");
    if (categoryName.empty()) return;

    std::string announcement = categoryName;

    // Read stat entries from statArray
    Scaleform::GFx::Value statArray;
    if (a_entry.GetMember("statArray", &statArray) && statArray.IsArray()) {
        std::uint32_t count = statArray.GetArraySize();
        for (std::uint32_t i = 0; i < count; i++) {
            Scaleform::GFx::Value stat;
            if (!statArray.GetElement(i, &stat) || !stat.IsObject()) continue;

            std::string statName = GetStringMember(stat, "text");
            if (statName.empty()) continue;

            // Value can be numeric or string
            Scaleform::GFx::Value valueVal;
            std::string valueStr;
            if (stat.GetMember("value", &valueVal)) {
                if (valueVal.IsString()) {
                    valueStr = valueVal.GetString();
                } else if (valueVal.IsInt()) {
                    valueStr = std::to_string(valueVal.GetInt());
                } else if (valueVal.IsNumber()) {
                    double num = valueVal.GetNumber();
                    // Format without decimals if it's a whole number
                    if (num == static_cast<double>(static_cast<std::int64_t>(num))) {
                        valueStr = std::to_string(static_cast<std::int64_t>(num));
                    } else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.1f", num);
                        valueStr = buf;
                    }
                } else if (valueVal.IsUInt()) {
                    valueStr = std::to_string(valueVal.GetUInt());
                }
            }

            if (!valueStr.empty()) {
                announcement += ". " + statName + ": " + valueStr;
            } else {
                announcement += ". " + statName;
            }
        }
    }

    SpeakText(announcement);
}

// ============================================================================
// Sub-menu access helpers
// ============================================================================

const Scaleform::GFx::Value* PipBoyDataPage::GetWorkshopMenuObj(RE::IMenu* a_menu)
{
    if (!a_menu) return nullptr;
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    return &pipboyMenu->workshopMenuObj.menuObj;
}

const Scaleform::GFx::Value* PipBoyDataPage::GetLogMenuObj(RE::IMenu* a_menu)
{
    if (!a_menu) return nullptr;
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    return &pipboyMenu->logMenuObj.menuObj;
}

std::int32_t PipBoyDataPage::GetListSelectedIndex(RE::IMenu* a_menu, const Scaleform::GFx::Value& a_menuObj, const char* a_tabName, const char* a_listName)
{
    Scaleform::GFx::Value listMc;

    // Try 1: menuObj direct access
    if (GetNamedChild(a_menuObj, a_listName, &listMc)) {
        goto readIndex;
    }

    // Try 2: movie path fallback
    if (!GetListViaMovie(a_menu, a_tabName, a_listName, &listMc)) {
        return -1;
    }

readIndex:
    Scaleform::GFx::Value selVal;
    if (!listMc.GetMember("selectedIndex", &selVal)) {
        return -1;
    }

    if (selVal.IsInt()) return selVal.GetInt();
    if (selVal.IsNumber()) return static_cast<std::int32_t>(selVal.GetNumber());
    if (selVal.IsUInt()) return static_cast<std::int32_t>(selVal.GetUInt());

    return -1;
}

bool PipBoyDataPage::GetListSelectedEntry(RE::IMenu* a_menu, const Scaleform::GFx::Value& a_menuObj, Scaleform::GFx::Value* a_outEntry, const char* a_tabName, const char* a_listName)
{
    if (!a_outEntry) return false;

    Scaleform::GFx::Value listMc;

    // Try 1: menuObj direct access
    if (GetNamedChild(a_menuObj, a_listName, &listMc)) {
        goto readEntry;
    }

    // Try 2: movie path fallback
    if (!GetListViaMovie(a_menu, a_tabName, a_listName, &listMc)) {
        return false;
    }

readEntry:
    if (listMc.GetMember("selectedEntry", a_outEntry) && a_outEntry->IsObject()) {
        return true;
    }

    return false;
}

// ============================================================================
// Value extraction helpers
// ============================================================================

std::string PipBoyDataPage::GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name)
{
    Scaleform::GFx::Value val;
    if (a_obj.GetMember(a_name, &val) && val.IsString()) {
        return val.GetString();
    }
    return "";
}

std::int32_t PipBoyDataPage::GetIntMember(const Scaleform::GFx::Value& a_obj, const char* a_name, std::int32_t a_default)
{
    Scaleform::GFx::Value val;
    if (a_obj.GetMember(a_name, &val)) {
        if (val.IsInt()) return val.GetInt();
        if (val.IsNumber()) return static_cast<std::int32_t>(val.GetNumber());
        if (val.IsUInt()) return static_cast<std::int32_t>(val.GetUInt());
    }
    return a_default;
}
