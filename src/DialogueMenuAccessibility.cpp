#include "DialogueMenuAccessibility.h"
#include "SpeechManager.h"
#include <SRAL.h>

DialogueMenuAccessibility* DialogueMenuAccessibility::GetSingleton()
{
    static DialogueMenuAccessibility singleton;
    return &singleton;
}

void DialogueMenuAccessibility::OnDialogueMenuOpened()
{
    m_menuAnnounced = false;
    m_lastSelection = -1;

    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("DialogueMenu"sv);
    if (!menu || !menu->uiMovie) return;

    // Announce NPC name when dialogue opens
    std::string npcName = GetNPCName(menu.get());
    if (!npcName.empty()) {
        SpeakText("Dialogue with " + npcName);
        m_menuAnnounced = true;
    }

    // Also announce the first selection if available
    CheckSelection();
}

void DialogueMenuAccessibility::OnDialogueMenuClosed()
{
    Reset();
}

void DialogueMenuAccessibility::Reset()
{
    m_menuAnnounced = false;
    m_lastSelection = -1;
}

void DialogueMenuAccessibility::CheckSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("DialogueMenu"sv);
    if (!menu || !menu->uiMovie) return;

    // Check if the list is visible (alpha > 0)
    Scaleform::GFx::Value listMc;
    if (!menu->uiMovie->GetVariable(&listMc, DIALOGUE_LIST_PATH)) {
        return;
    }

    Scaleform::GFx::Value alphaValue;
    if (listMc.GetMember("alpha", &alphaValue)) {
        double alpha = 0.0;
        if (alphaValue.IsNumber()) {
            alpha = alphaValue.GetNumber();
        }
        if (alpha < 0.5) {
            // List is faded out, don't announce
            return;
        }
    }

    // If menu wasn't announced yet (NPC name was empty initially), try again
    if (!m_menuAnnounced) {
        std::string npcName = GetNPCName(menu.get());
        if (!npcName.empty()) {
            SpeakText("Dialogue with " + npcName);
            m_menuAnnounced = true;
        }
    }

    std::int32_t currentSelection = GetSelectedIndex(menu.get());
    REX::DEBUG("DialogueMenu selection: current={} last={}", currentSelection, m_lastSelection);

    if (currentSelection >= 0 && currentSelection != m_lastSelection) {
        m_lastSelection = currentSelection;

        std::string text = GetSelectedDialogueText(menu.get());
        if (!text.empty()) {
            std::string indicators = BuildIndicators(menu.get(), currentSelection);
            std::string fullText = text;
            if (!indicators.empty()) {
                fullText += ". " + indicators;
            }
            SpeakText(fullText);
        }
    }
}

std::string DialogueMenuAccessibility::GetNPCName(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value nameValue;
    if (a_menu->uiMovie->GetVariable(&nameValue, NPC_NAME_PATH)) {
        if (nameValue.IsString()) {
            return nameValue.GetString();
        }
    }

    return "";
}

std::int32_t DialogueMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return -1;
    }

    std::string indexPath = std::string(DIALOGUE_LIST_PATH) + ".selectedIndex";
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, indexPath.c_str())) {
        if (indexValue.IsInt()) {
            return indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            return static_cast<std::int32_t>(indexValue.GetNumber());
        }
    }

    return -1;
}

std::string DialogueMenuAccessibility::GetSelectedDialogueText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    auto selectedIndex = GetSelectedIndex(a_menu);
    if (selectedIndex < 0) {
        return "";
    }

    // Get entryList array
    std::string entryListPath = std::string(DIALOGUE_LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(selectedIndex) >= entryList.GetArraySize()) {
        return "";
    }

    // Get the selected entry
    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(selectedIndex), &entry) || !entry.IsObject()) {
        return "";
    }

    // Get the text property
    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    return "";
}

std::string DialogueMenuAccessibility::BuildIndicators(RE::IMenu* a_menu, std::int32_t a_selectedIndex)
{
    if (!a_menu || !a_menu->uiMovie || a_selectedIndex < 0) {
        return "";
    }

    // Get entryList array
    std::string entryListPath = std::string(DIALOGUE_LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(a_selectedIndex) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_selectedIndex), &entry) || !entry.IsObject()) {
        return "";
    }

    std::vector<std::string> indicators;

    // Check for speech challenge
    Scaleform::GFx::Value challengeLevel, challengeResult;
    if (entry.GetMember("challengeLevel", &challengeLevel) && challengeLevel.IsNumber()) {
        int level = static_cast<int>(challengeLevel.GetNumber());
        if (level > 0) {
            // Check if already attempted (challengeResult != -1)
            bool notAttempted = true;
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
                indicators.push_back(GetChallengeDifficulty(level) + " speech challenge");
            }
        }
    }

    // Check if already said
    Scaleform::GFx::Value said;
    if (entry.GetMember("said", &said) && said.IsBoolean() && said.GetBoolean()) {
        indicators.push_back("already said");
    }

    // Check for barter option
    Scaleform::GFx::Value isBarterOption;
    if (entry.GetMember("isBarterOption", &isBarterOption) && isBarterOption.IsBoolean() && isBarterOption.GetBoolean()) {
        indicators.push_back("barter");
    }

    // Check for inventory/trade option
    Scaleform::GFx::Value isInventoryOption;
    if (entry.GetMember("isInventoryOption", &isInventoryOption) && isInventoryOption.IsBoolean() && isInventoryOption.GetBoolean()) {
        indicators.push_back("trade");
    }

    // Check if ends conversation
    Scaleform::GFx::Value endsScene;
    if (entry.GetMember("endsScene", &endsScene) && endsScene.IsBoolean() && endsScene.GetBoolean()) {
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

std::string DialogueMenuAccessibility::GetChallengeDifficulty(std::int32_t a_level)
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

void DialogueMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}
