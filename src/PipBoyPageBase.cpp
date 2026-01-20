#include "PipBoyPageBase.h"
#include "SpeechManager.h"
#include <SRAL.h>

const Scaleform::GFx::Value* PipBoyPageBase::GetDataObj(RE::IMenu* a_menu)
{
    if (!a_menu) {
        return nullptr;
    }

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return nullptr;
    }

    return &pipboyMenu->dataObj;
}

std::int32_t PipBoyPageBase::GetCurrentTab(RE::IMenu* a_menu)
{
    const auto* dataObj = GetDataObj(a_menu);
    if (!dataObj) {
        return -1;
    }

    Scaleform::GFx::Value tabValue;
    if (dataObj->GetMember("CurrentTab", &tabValue)) {
        int tab = -1;
        if (tabValue.IsInt()) {
            tab = tabValue.GetInt();
        } else if (tabValue.IsNumber()) {
            tab = static_cast<int>(tabValue.GetNumber());
        } else if (tabValue.IsUInt()) {
            tab = static_cast<int>(tabValue.GetUInt());
        }
        return tab;
    }

    return -1;
}

bool PipBoyPageBase::GetInvSelectedItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray)
{
    const auto* dataObj = GetDataObj(a_menu);
    if (!dataObj || !a_outArray) {
        return false;
    }

    if (dataObj->GetMember("InvSelectedItems", a_outArray) && a_outArray->IsArray()) {
        return true;
    }

    return false;
}

bool PipBoyPageBase::GetInvItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray)
{
    const auto* dataObj = GetDataObj(a_menu);
    if (!dataObj || !a_outArray) {
        return false;
    }

    if (dataObj->GetMember("InvItems", a_outArray) && a_outArray->IsArray()) {
        return true;
    }

    return false;
}

std::int32_t PipBoyPageBase::GetInventoryListSelection(RE::IMenu* a_menu)
{
    // We cannot directly access the live List_mc.selectedIndex because it's in
    // a dynamically loaded SWF. Instead, we use InvSelectedItems from dataObj
    // which stores the saved selection per tab.
    //
    // Note: This gets the selection from the C++ side's data, which is updated
    // when onInvItemSelection callback is received from ActionScript.

    if (!a_menu) {
        return -1;
    }

    auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
    const auto& dataObj = pipboyMenu->dataObj;

    if (dataObj.IsUndefined() || !dataObj.IsObject()) {
        return -1;
    }

    // Get current tab
    Scaleform::GFx::Value tabValue;
    int currentTab = 0;
    if (dataObj.GetMember("CurrentTab", &tabValue)) {
        if (tabValue.IsInt()) currentTab = tabValue.GetInt();
        else if (tabValue.IsNumber()) currentTab = static_cast<int>(tabValue.GetNumber());
        else if (tabValue.IsUInt()) currentTab = static_cast<int>(tabValue.GetUInt());
    }

    // Get InvSelectedItems array - this is updated by the game when selection changes
    Scaleform::GFx::Value invSelectedItems;
    if (dataObj.GetMember("InvSelectedItems", &invSelectedItems) && invSelectedItems.IsArray()) {
        std::uint32_t size = invSelectedItems.GetArraySize();
        if (currentTab >= 0 && static_cast<std::uint32_t>(currentTab) < size) {
            Scaleform::GFx::Value selectedVal;
            if (invSelectedItems.GetElement(static_cast<std::uint32_t>(currentTab), &selectedVal)) {
                int index = -1;
                if (selectedVal.IsInt()) index = selectedVal.GetInt();
                else if (selectedVal.IsNumber()) index = static_cast<int>(selectedVal.GetNumber());
                else if (selectedVal.IsUInt()) index = static_cast<int>(selectedVal.GetUInt());
                return index;
            }
        }
    }

    return -1;
}

void PipBoyPageBase::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}

std::string PipBoyPageBase::CleanLocalizationString(const std::string& a_text)
{
    if (a_text.empty()) {
        return a_text;
    }

    std::string result = a_text;

    // Remove $ prefix used for localization keys
    if (result[0] == '$') {
        result = result.substr(1);
    }

    // Replace underscores and hyphens with spaces for better speech
    for (char& c : result) {
        if (c == '_' || c == '-') {
            c = ' ';
        }
    }

    return result;
}
