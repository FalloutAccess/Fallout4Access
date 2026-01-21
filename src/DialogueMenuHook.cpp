#include "DialogueMenuHook.h"
#include "SpeechManager.h"
#include "LocalizationHelper.h"
#include <F4SE/F4SE.h>

DialogueMenuHook* DialogueMenuHook::GetSingleton()
{
    static DialogueMenuHook singleton;
    return &singleton;
}

void DialogueMenuHook::Install()
{
    if (m_installed) {
        return;
    }

    // Hook DialogueMenu::AdvanceMovie (vtable index 4)
    // VTABLE::DialogueMenu[0] is the primary vtable (IMenu/GameMenuBase)
    static REL::HookVFT<void(RE::DialogueMenu*, float, std::uint64_t)> advanceHook(
        VTABLE::DialogueMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (advanceHook.Enable()) {
        s_advanceMovieHook = &advanceHook;
        REX::INFO("DialogueMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("DialogueMenuHook: Failed to install AdvanceMovie hook");
    }

    m_installed = true;
}

void DialogueMenuHook::OnDialogueMenuOpened()
{
    REX::INFO("DialogueMenuHook: Menu opened");

    // Reset state - opening announcement will happen in CheckStateChanges when data is ready
    m_menuOpen = true;
    m_openingAnnouncementDone = false;
    m_lastSelection = -1;
    m_lastSpokenText.clear();
}

void DialogueMenuHook::OnDialogueMenuClosed()
{
    REX::INFO("DialogueMenuHook: Menu closed");
    m_menuOpen = false;
}

void DialogueMenuHook::ResetState()
{
    m_openingAnnouncementDone = false;
    m_lastSelection = -1;
    m_lastSpokenText.clear();
}

void DialogueMenuHook::HookedAdvanceMovie(RE::DialogueMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for state changes
    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        hook->CheckStateChanges(a_this);
    }
}

void DialogueMenuHook::CheckStateChanges(RE::DialogueMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Check if the list is visible (alpha > 0)
    if (!IsListVisible(a_menu)) {
        return;
    }

    // If opening announcement not done yet, try to do it now
    if (!m_openingAnnouncementDone) {
        std::int32_t currentSelection = GetSelectedIndex(a_menu);

        // Wait until we have a valid selection (data is populated)
        if (currentSelection >= 0) {
            AnnounceDialogueOpened(a_menu);
            m_lastSelection = currentSelection;
            m_openingAnnouncementDone = true;
        }
        return;
    }

    // Check for selection change
    std::int32_t currentSelection = GetSelectedIndex(a_menu);

    if (currentSelection >= 0 && currentSelection != m_lastSelection) {
        m_lastSelection = currentSelection;
        AnnounceSelectionChange(a_menu, currentSelection);
    }
}

bool DialogueMenuHook::IsListVisible(RE::DialogueMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return false;

    Scaleform::GFx::Value alphaValue;
    if (a_menu->uiMovie->GetVariable(&alphaValue, "root.List_mc.alpha")) {
        if (alphaValue.IsNumber()) {
            double alpha = alphaValue.GetNumber();
            return alpha >= 0.5;
        }
    }

    // If we can't read alpha, check if selectedIndex is valid as proxy for visibility
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, "root.List_mc.selectedIndex")) {
        if (indexValue.IsNumber() || indexValue.IsInt() || indexValue.IsUInt()) {
            std::int32_t index = -1;
            if (indexValue.IsInt()) index = indexValue.GetInt();
            else if (indexValue.IsNumber()) index = static_cast<std::int32_t>(indexValue.GetNumber());
            else if (indexValue.IsUInt()) index = static_cast<std::int32_t>(indexValue.GetUInt());
            return index >= 0;
        }
    }

    return false;
}

void DialogueMenuHook::AnnounceDialogueOpened(RE::DialogueMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return;

    // Announce NPC name
    std::string npcName = GetNPCName(a_menu);

    if (!npcName.empty()) {
        SpeakText("Dialogue with " + npcName);
    }

    // Announce first selection
    std::int32_t selectedIndex = GetSelectedIndex(a_menu);
    if (selectedIndex >= 0) {
        std::string text = GetDialogueTextAtIndex(a_menu, selectedIndex);
        if (!text.empty()) {
            std::string indicators = BuildIndicators(a_menu, selectedIndex);
            std::string fullText = text;
            if (!indicators.empty()) {
                fullText += ". " + indicators;
            }
            SpeakText(fullText);
            m_lastSpokenText = fullText;
        }
    }
}

void DialogueMenuHook::AnnounceSelectionChange(RE::DialogueMenu* a_menu, std::int32_t a_selectedIndex)
{
    if (!a_menu || !a_menu->uiMovie || a_selectedIndex < 0) return;

    std::string text = GetDialogueTextAtIndex(a_menu, a_selectedIndex);
    if (text.empty()) return;

    std::string indicators = BuildIndicators(a_menu, a_selectedIndex);
    std::string fullText = text;
    if (!indicators.empty()) {
        fullText += ". " + indicators;
    }

    // Deduplicate - don't repeat the same text
    if (fullText != m_lastSpokenText) {
        m_lastSpokenText = fullText;
        SpeakText(fullText);
    }
}

std::string DialogueMenuHook::GetNPCName(RE::DialogueMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return "";

    Scaleform::GFx::Value nameValue;
    if (a_menu->uiMovie->GetVariable(&nameValue, "root.name_tf.text") && nameValue.IsString()) {
        return LocalizationHelper::CleanString(nameValue.GetString());
    }

    return "";
}

std::int32_t DialogueMenuHook::GetSelectedIndex(RE::DialogueMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return -1;

    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, "root.List_mc.selectedIndex")) {
        if (indexValue.IsInt()) return indexValue.GetInt();
        if (indexValue.IsNumber()) return static_cast<std::int32_t>(indexValue.GetNumber());
        if (indexValue.IsUInt()) return static_cast<std::int32_t>(indexValue.GetUInt());
    }

    return -1;
}

std::string DialogueMenuHook::GetDialogueTextAtIndex(RE::DialogueMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return "";

    // Get entryList
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, "root.List_mc.entryList") || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return LocalizationHelper::CleanString(textValue.GetString());
    }

    return "";
}

std::string DialogueMenuHook::BuildIndicators(RE::DialogueMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) return "";

    // Get entryList
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, "root.List_mc.entryList") || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        return "";
    }

    std::vector<std::string> indicators;
    Scaleform::GFx::Value prop;

    // Check for speech challenge
    if (entry.GetMember("challengeLevel", &prop) && prop.IsNumber()) {
        int level = static_cast<int>(prop.GetNumber());
        if (level > 0) {
            // Check if already attempted (challengeResult != -1)
            bool notAttempted = true;
            Scaleform::GFx::Value challengeResult;
            if (entry.GetMember("challengeResult", &challengeResult) && challengeResult.IsNumber()) {
                int result = static_cast<int>(challengeResult.GetNumber());
                if (result != -1) {
                    notAttempted = false;
                    if (result == 1) {
                        indicators.push_back("speech challenge succeeded");
                    } else {
                        indicators.push_back("speech challenge failed");
                    }
                }
            }
            if (notAttempted) {
                std::string difficulty = GetChallengeDifficulty(level);
                if (!difficulty.empty()) {
                    indicators.push_back(difficulty + " speech challenge");
                } else {
                    indicators.push_back("speech challenge");
                }
            }
        }
    }

    // Check if already said
    if (entry.GetMember("said", &prop) && prop.IsBoolean() && prop.GetBoolean()) {
        indicators.push_back("already said");
    }

    // Check for barter option
    if (entry.GetMember("isBarterOption", &prop) && prop.IsBoolean() && prop.GetBoolean()) {
        indicators.push_back("barter");
    }

    // Check for inventory/trade option
    if (entry.GetMember("isInventoryOption", &prop) && prop.IsBoolean() && prop.GetBoolean()) {
        indicators.push_back("trade");
    }

    // Check if ends conversation
    if (entry.GetMember("endsScene", &prop) && prop.IsBoolean() && prop.GetBoolean()) {
        indicators.push_back("ends conversation");
    }

    // Build final string
    std::string result;
    for (size_t i = 0; i < indicators.size(); ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += indicators[i];
    }

    return result;
}

std::string DialogueMenuHook::GetChallengeDifficulty(std::int32_t a_level)
{
    // Challenge levels: 1/5 = easy (yellow), 2/6 = medium (orange), 3/7 = hard (red)
    switch (a_level) {
        case 1:
        case 5:
            return "easy";
        case 2:
        case 6:
            return "medium";
        case 3:
        case 7:
            return "hard";
        default:
            return "";
    }
}

void DialogueMenuHook::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
