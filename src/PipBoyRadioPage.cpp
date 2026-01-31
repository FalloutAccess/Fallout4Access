#include "PipBoyRadioPage.h"
#include "LocalizationHelper.h"
#include <RE/P/PipboyMenu.h>

void PipBoyRadioPage::OnPageActivated()
{
    REX::INFO("PipBoyRadioPage: Activated");
    m_announced = false;
    m_lastSelection = -1;
    m_lastRadioIndex = -1;
    m_lastStationName.clear();

    // Announce radio state after a frame delay for Scaleform to populate
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this]() {
            auto ui = RE::UI::GetSingleton();
            if (!ui) return;
            auto menu = ui->GetMenu("PipboyMenu"sv);
            if (!menu) return;
            AnnounceRadioState(menu.get());
        });
    }
}

void PipBoyRadioPage::OnPageDeactivated()
{
    REX::INFO("PipBoyRadioPage: Deactivated");
}

void PipBoyRadioPage::CheckSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("PipboyMenu"sv);
    if (!menu) return;

    std::int32_t selectedIndex = GetListSelectedIndex(menu.get());
    if (selectedIndex < 0 || selectedIndex == m_lastRadioIndex) return;
    m_lastRadioIndex = selectedIndex;

    Scaleform::GFx::Value entry;
    if (GetListSelectedEntry(menu.get(), &entry)) {
        AnnounceStation(entry);
    }
}

void PipBoyRadioPage::CheckTabChange()
{
    // RADIO page has no tabs
}

void PipBoyRadioPage::Reset()
{
    m_announced = false;
    m_lastSelection = -1;
    m_lastRadioIndex = -1;
    m_lastStationName.clear();
}

void PipBoyRadioPage::AnnounceRadioState(RE::IMenu* a_menu)
{
    if (!a_menu) return;

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);

    // Check if radio is on
    bool radioOn = pipboyMenu->radioMenuObj.radioModeOn;

    // Count stations and find active one from RadioList in dataObj
    const auto* dataObj = GetDataObj(a_menu);
    if (!dataObj) return;

    Scaleform::GFx::Value radioList;
    if (!dataObj->GetMember("RadioList", &radioList) || !radioList.IsArray()) return;

    std::uint32_t count = radioList.GetArraySize();
    std::string activeStation;

    for (std::uint32_t i = 0; i < count; i++) {
        Scaleform::GFx::Value entry;
        if (radioList.GetElement(i, &entry) && entry.IsObject()) {
            if (GetBoolMember(entry, "active")) {
                activeStation = GetStringMember(entry, "text");
                break;
            }
        }
    }

    std::string announcement;
    if (radioOn && !activeStation.empty()) {
        announcement = "Playing: " + activeStation;
    } else {
        announcement = std::to_string(count) + " stations";
    }

    SpeakText(announcement);
}

void PipBoyRadioPage::AnnounceStation(const Scaleform::GFx::Value& a_entry)
{
    std::string name = GetStringMember(a_entry, "text");
    if (name.empty()) return;

    // Avoid duplicate announcements
    if (name == m_lastStationName) return;
    m_lastStationName = name;

    bool isActive = GetBoolMember(a_entry, "active");
    bool isEnabled = GetBoolMember(a_entry, "enabled");

    std::string announcement = name;
    if (isActive) {
        announcement += ", playing";
    } else if (!isEnabled) {
        announcement += ", out of range";
    }

    SpeakText(announcement);
}

const Scaleform::GFx::Value* PipBoyRadioPage::GetRadioMenuObj(RE::IMenu* a_menu)
{
    if (!a_menu) return nullptr;
    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    return &pipboyMenu->radioMenuObj.menuObj;
}

std::int32_t PipBoyRadioPage::GetListSelectedIndex(RE::IMenu* a_menu)
{
    Scaleform::GFx::Value listMc;

    // Try 1: menuObj direct access
    const auto* radioObj = GetRadioMenuObj(a_menu);
    if (radioObj && radioObj->IsObject()) {
        if (GetNamedChild(*radioObj, "List_mc", &listMc)) {
            goto readIndex;
        }
    }

    // Try 2: movie path fallback (Radio has no tabs, List_mc is on page root)
    if (!GetListViaMovie(a_menu, "", "List_mc", &listMc)) {
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

bool PipBoyRadioPage::GetListSelectedEntry(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outEntry)
{
    if (!a_outEntry) return false;

    Scaleform::GFx::Value listMc;

    // Try 1: menuObj direct access
    const auto* radioObj = GetRadioMenuObj(a_menu);
    if (radioObj && radioObj->IsObject()) {
        if (GetNamedChild(*radioObj, "List_mc", &listMc)) {
            goto readEntry;
        }
    }

    // Try 2: movie path fallback
    if (!GetListViaMovie(a_menu, "", "List_mc", &listMc)) {
        return false;
    }

readEntry:
    if (listMc.GetMember("selectedEntry", a_outEntry) && a_outEntry->IsObject()) {
        return true;
    }

    return false;
}

std::string PipBoyRadioPage::GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name)
{
    Scaleform::GFx::Value val;
    if (a_obj.GetMember(a_name, &val) && val.IsString()) {
        return val.GetString();
    }
    return "";
}

bool PipBoyRadioPage::GetBoolMember(const Scaleform::GFx::Value& a_obj, const char* a_name)
{
    Scaleform::GFx::Value val;
    if (a_obj.GetMember(a_name, &val)) {
        if (val.IsBoolean()) return val.GetBoolean();
        if (val.IsNumber()) return val.GetNumber() != 0.0;
        if (val.IsInt()) return val.GetInt() != 0;
        if (val.IsUInt()) return val.GetUInt() != 0;
    }
    return false;
}
