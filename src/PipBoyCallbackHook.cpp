#include "PipBoyCallbackHook.h"
#include "ItemStatFormatter.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>
#include <SRAL.h>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <RE/B/BSScaleformManager.h>
#include <RE/B/BSScaleformTranslator.h>
#include <RE/B/BSTranslator.h>
#include <RE/B/BSFixedString.h>

PipBoyCallbackHook* PipBoyCallbackHook::GetSingleton()
{
    static PipBoyCallbackHook singleton;
    return &singleton;
}

void PipBoyCallbackHook::Install()
{
    if (m_installed) {
        REX::DEBUG("PipBoyCallbackHook: Already installed");
        return;
    }

    // Hook PipboyMenu's Call function (vtable index 1, inherited from SWFToCodeFunctionHandler)
    // VTABLE::PipboyMenu[0] is the primary vtable (SWFToCodeFunctionHandler/IMenu vtable)
    // Index 1 is the Call virtual function
    static REL::HookVFT<void(RE::PipboyMenu*, const Scaleform::GFx::FunctionHandler::Params&)> hook(
        VTABLE::PipboyMenu[0],  // First vtable entry
        1,                       // Call is at index 1
        HookedCall
    );

    if (hook.Enable()) {
        s_callHook = &hook;
        m_installed = true;
        REX::INFO("PipBoyCallbackHook: Hook installed successfully");
    } else {
        REX::ERROR("PipBoyCallbackHook: Failed to install hook");
    }
}

void PipBoyCallbackHook::Uninstall()
{
    if (!m_installed || !s_callHook) {
        return;
    }

    if (s_callHook->Disable()) {
        m_installed = false;
        REX::INFO("PipBoyCallbackHook: Hook uninstalled");
    }
}

void PipBoyCallbackHook::HookedCall(RE::PipboyMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params)
{
    auto* hook = GetSingleton();

    // Get function ID from userData
    auto funcID = reinterpret_cast<std::intptr_t>(a_params.userData);

    // Log callback for debugging (only first few times to avoid spam)
    static int logCount = 0;
    if (logCount < 100) {
        // Log arg types for 4-arg callbacks to debug identification
        if (a_params.argCount == 4 && a_params.args) {
            REX::DEBUG("PipBoyCallbackHook: Call funcID={}, argCount=4, arg0[Int={},UInt={},Num={}], arg1[Arr={},Obj={}]",
                funcID,
                a_params.args[0].IsInt(), a_params.args[0].IsUInt(), a_params.args[0].IsNumber(),
                a_params.args[1].IsArray(), a_params.args[1].IsObject());
        } else {
            REX::DEBUG("PipBoyCallbackHook: Call received - funcID={}, argCount={}", funcID, a_params.argCount);
        }
        logCount++;
    }

    // Try to identify callback by function ID or argument signature
    // onInvItemSelection: 4 args (int selectedIndex, Array InfoObj, Array selectedInfoObj, DisplayObject page)
    // Note: IsNumber() only matches doubles, use IsInt()/IsUInt() for integers
    bool arg0IsNumeric = a_params.args && (a_params.args[0].IsNumber() || a_params.args[0].IsInt() || a_params.args[0].IsUInt());
    bool arg1IsArray = a_params.args && a_params.args[1].IsArray();

    if (a_params.argCount == 4 && arg0IsNumeric && arg1IsArray) {
        // Cache the function ID for future fast matching
        if (hook->m_invItemSelectionID < 0) {
            hook->m_invItemSelectionID = static_cast<std::int32_t>(funcID);
            REX::INFO("PipBoyCallbackHook: Identified onInvItemSelection funcID={}", funcID);
        }

        if (funcID == hook->m_invItemSelectionID) {
            hook->HandleInvItemSelection(a_params);
        }
    }
    // onQuestSelection: 3 args (Array objectivesList, Object selectedQuest, DisplayObject page)
    else if (a_params.argCount == 3 && a_params.args && a_params.args[0].IsArray() && a_params.args[1].IsObject()) {
        if (hook->m_questSelectionID < 0) {
            hook->m_questSelectionID = static_cast<std::int32_t>(funcID);
            REX::INFO("PipBoyCallbackHook: Identified onQuestSelection funcID={}", funcID);
        }

        if (funcID == hook->m_questSelectionID) {
            hook->HandleQuestSelection(a_params);
        }
    }

    // Call the original function to maintain game behavior
    if (s_callHook) {
        (*s_callHook)(a_this, a_params);
    }
}

void PipBoyCallbackHook::HandleInvItemSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
    // arg[0] = selectedIndex (int) - index in filtered list, NOT used directly
    // arg[1] = ItemCard_mc.InfoObj (Array of stat entries)
    // arg[2] = PaperDoll_mc.selectedInfoObj (Array of booleans for armor slots)
    // arg[3] = page reference (Pipboy_InvPage instance with List_mc.selectedEntry)

    REX::DEBUG("PipBoyCallbackHook: onInvItemSelection callback received");

    // Get the InfoObj array
    const auto& infoObj = a_params.args[1];
    if (!infoObj.IsArray()) {
        REX::DEBUG("PipBoyCallbackHook: InfoObj is not an array");
        return;
    }

    // arg[3] is the page reference (Pipboy_InvPage) - use this to get selectedEntry
    const auto& pageRef = a_params.args[3];
    if (!pageRef.IsObject()) {
        REX::DEBUG("PipBoyCallbackHook: Page reference (arg[3]) is not an object");
        return;
    }

    AnnounceInventoryItem(pageRef, infoObj);
}

void PipBoyCallbackHook::HandleQuestSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
    // arg[0] = objectivesList (Array to be populated)
    // arg[1] = selectedQuest (Object with quest data)
    // arg[2] = page reference (DisplayObject)

    const auto& questEntry = a_params.args[1];
    if (!questEntry.IsObject()) {
        return;
    }

    AnnounceQuest(questEntry);
}

void PipBoyCallbackHook::HandleWorkshopSelection(const Scaleform::GFx::FunctionHandler::Params& a_params)
{
    // Similar pattern - extract workshop data and announce
    if (a_params.argCount < 2) return;

    const auto& workshopEntry = a_params.args[1];
    if (!workshopEntry.IsObject()) {
        return;
    }

    AnnounceWorkshop(workshopEntry);
}

void PipBoyCallbackHook::AnnounceInventoryItem(const Scaleform::GFx::Value& a_pageRef, const Scaleform::GFx::Value& a_infoObj)
{
    // Extract item identity from page.List_mc.selectedEntry (reliable, synchronous)
    ItemIdentity identity;

    Scaleform::GFx::Value listMc, selectedEntry;
    if (a_pageRef.GetMember("List_mc", &listMc) && listMc.IsObject()) {
        if (listMc.GetMember("selectedEntry", &selectedEntry) && selectedEntry.IsObject()) {
            Scaleform::GFx::Value prop;

            if (selectedEntry.GetMember("text", &prop) && prop.IsString()) {
                identity.name = CleanLocalizationString(prop.GetString());
            }

            if (selectedEntry.GetMember("count", &prop)) {
                if (prop.IsInt()) identity.count = prop.GetInt();
                else if (prop.IsNumber()) identity.count = static_cast<std::int32_t>(prop.GetNumber());
                else if (prop.IsUInt()) identity.count = static_cast<std::int32_t>(prop.GetUInt());
            }

            if (selectedEntry.GetMember("equipState", &prop)) {
                if (prop.IsInt()) identity.equipState = prop.GetInt();
                else if (prop.IsNumber()) identity.equipState = static_cast<std::int32_t>(prop.GetNumber());
                else if (prop.IsUInt()) identity.equipState = static_cast<std::int32_t>(prop.GetUInt());
            }

            if (selectedEntry.GetMember("isLegendary", &prop) && prop.IsBoolean()) {
                identity.isLegendary = prop.GetBoolean();
            }

            if (selectedEntry.GetMember("favorite", &prop)) {
                if (prop.IsBoolean()) {
                    identity.isFavorite = prop.GetBoolean();
                } else if (prop.IsNumber() || prop.IsInt() || prop.IsUInt()) {
                    int favVal = 0;
                    if (prop.IsInt()) favVal = prop.GetInt();
                    else if (prop.IsNumber()) favVal = static_cast<int>(prop.GetNumber());
                    else if (prop.IsUInt()) favVal = static_cast<int>(prop.GetUInt());
                    identity.isFavorite = (favVal > 0);
                }
            }

            if (selectedEntry.GetMember("taggedForSearch", &prop) && prop.IsBoolean()) {
                identity.isTagged = prop.GetBoolean();
            }

            REX::DEBUG("PipBoyCallbackHook: Extracted identity: '{}'", identity.name);
        } else {
            REX::DEBUG("PipBoyCallbackHook: List_mc.selectedEntry access failed");
            return;
        }
    } else {
        REX::DEBUG("PipBoyCallbackHook: List_mc access from page reference failed");
        return;
    }

    if (!identity.IsValid()) {
        REX::DEBUG("PipBoyCallbackHook: Could not extract item identity");
        return;
    }

    // Time-based dedup: skip if same item announced within 100ms
    // Game fires onInvItemSelection 3+ times per item change
    auto now = std::chrono::steady_clock::now();
    if (identity.name == m_lastAnnouncedItemName) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAnnouncedTime);
        if (elapsed.count() < 100) {
            REX::DEBUG("PipBoyCallbackHook: Dedup - same item '{}' within {}ms", identity.name, elapsed.count());
            return;
        }
    }
    m_lastAnnouncedItemName = identity.name;
    m_lastAnnouncedTime = now;

    // Announce identity right away (responsive feedback)
    SpeakText(identity.BuildBasicAnnouncement());
    REX::DEBUG("PipBoyCallbackHook: Announced identity immediately: '{}'", identity.name);

    // Schedule deferred stats announcement (1 frame delay for InfoObj to update)
    // Store InfoObj by value - hoping it's a reference that will update
    auto* taskInterface = F4SE::GetTaskInterface();
    if (taskInterface) {
        // Copy the InfoObj value - test if it updates by next frame
        Scaleform::GFx::Value storedInfoObj = a_infoObj;

        taskInterface->AddTask([this, storedInfoObj, itemName = identity.name]() {
            // Re-parse InfoObj - if it's a live reference, it should have fresh data now
            auto stats = ItemStatFormatter::ParseInfoObj(storedInfoObj);

            if (stats.empty()) {
                REX::DEBUG("PipBoyCallbackHook: Deferred stats empty for '{}'", itemName);
                return;
            }

            std::string statsAnnouncement = ItemStatFormatter::BuildStatAnnouncement(stats);
            if (!statsAnnouncement.empty()) {
                SpeakText(statsAnnouncement.substr(2));  // Remove leading ", "
                REX::DEBUG("PipBoyCallbackHook: Deferred stats for '{}': {}", itemName, statsAnnouncement);
            }
        });
    }
}

void PipBoyCallbackHook::AnnounceQuest(const Scaleform::GFx::Value& a_questEntry)
{
    std::string questName;
    std::string description;
    bool isActive = false;
    bool isCompleted = false;
    bool isDivider = false;

    Scaleform::GFx::Value prop;

    // Check if divider
    if (a_questEntry.GetMember("isDivider", &prop) && prop.IsBoolean()) {
        isDivider = prop.GetBoolean();
    }

    if (isDivider) {
        // Get divider text
        if (a_questEntry.GetMember("text", &prop) && prop.IsString()) {
            questName = CleanLocalizationString(prop.GetString());
        }
        if (!questName.empty()) {
            SpeakText(questName);
        }
        return;
    }

    // Get quest name
    if (a_questEntry.GetMember("text", &prop) && prop.IsString()) {
        questName = CleanLocalizationString(prop.GetString());
    }

    // Get description
    if (a_questEntry.GetMember("description", &prop) && prop.IsString()) {
        description = CleanLocalizationString(prop.GetString());
    }

    // Get active status
    if (a_questEntry.GetMember("active", &prop) && prop.IsBoolean()) {
        isActive = prop.GetBoolean();
    }

    // Get completed status
    if (a_questEntry.GetMember("completed", &prop) && prop.IsBoolean()) {
        isCompleted = prop.GetBoolean();
    }

    if (questName.empty()) {
        return;
    }

    // Build announcement
    std::string announcement = questName;

    if (isActive) {
        announcement += ", active";
    }

    if (isCompleted) {
        announcement += ", completed";
    }

    if (!description.empty()) {
        announcement += ". " + description;
    }

    SpeakText(announcement);
}

void PipBoyCallbackHook::AnnounceWorkshop(const Scaleform::GFx::Value& a_workshopEntry)
{
    std::string name;
    float happiness = 0.0f;
    int population = 0;

    Scaleform::GFx::Value prop;

    // Get workshop name
    if (a_workshopEntry.GetMember("text", &prop) && prop.IsString()) {
        name = CleanLocalizationString(prop.GetString());
    }

    // Get happiness
    if (a_workshopEntry.GetMember("happiness", &prop)) {
        if (prop.IsNumber()) happiness = static_cast<float>(prop.GetNumber());
        else if (prop.IsInt()) happiness = static_cast<float>(prop.GetInt());
    }

    // Get population
    if (a_workshopEntry.GetMember("population", &prop)) {
        if (prop.IsInt()) population = prop.GetInt();
        else if (prop.IsNumber()) population = static_cast<int>(prop.GetNumber());
    }

    if (name.empty()) {
        return;
    }

    // Build announcement
    std::string announcement = name;

    if (population > 0) {
        announcement += ", " + std::to_string(population) + " settlers";
    }

    if (happiness > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f%%", happiness);
        announcement += ", " + std::string(buf) + " happiness";
    }

    SpeakText(announcement);
}

void PipBoyCallbackHook::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}

std::string PipBoyCallbackHook::CleanLocalizationString(const std::string& a_text)
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

std::string PipBoyCallbackHook::TranslateKey(const std::string& a_key)
{
    auto* sfManager = RE::BSScaleformManager::GetSingleton();
    if (!sfManager) {
        return CleanLocalizationString(a_key);
    }

    auto* translator = sfManager->GetTranslator();
    if (!translator) {
        return CleanLocalizationString(a_key);
    }

    // Convert to wide string for lookup
    std::wstring wideKey(a_key.begin(), a_key.end());
    RE::BSFixedStringWCS lookupKey(wideKey.c_str());

    auto& translationMap = translator->translator.translationMap;
    auto it = translationMap.find(lookupKey);

    if (it != translationMap.end()) {
        std::wstring wideResult(it->second.c_str());
        return WideToNarrow(wideResult);
    }

    return CleanLocalizationString(a_key);
}

std::string PipBoyCallbackHook::WideToNarrow(const std::wstring& a_wide)
{
    if (a_wide.empty()) return "";
    // Use Windows API via REX for proper UTF-8 conversion
    std::string result;
    result.reserve(a_wide.size() * 3);  // UTF-8 can be up to 3 bytes per wchar
    for (wchar_t wc : a_wide) {
        if (wc < 0x80) {
            result += static_cast<char>(wc);
        } else if (wc < 0x800) {
            result += static_cast<char>(0xC0 | (wc >> 6));
            result += static_cast<char>(0x80 | (wc & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (wc >> 12));
            result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }
    return result;
}
