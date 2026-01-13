#pragma once

#include "pch.h"

class DialogueMenuAccessibility
{
public:
    static DialogueMenuAccessibility* GetSingleton();

    void OnDialogueMenuOpened();
    void OnDialogueMenuClosed();
    void CheckSelection();
    void Reset();

private:
    DialogueMenuAccessibility() = default;
    ~DialogueMenuAccessibility() = default;
    DialogueMenuAccessibility(const DialogueMenuAccessibility&) = delete;
    DialogueMenuAccessibility(DialogueMenuAccessibility&&) = delete;
    DialogueMenuAccessibility& operator=(const DialogueMenuAccessibility&) = delete;
    DialogueMenuAccessibility& operator=(DialogueMenuAccessibility&&) = delete;

    std::string GetNPCName(RE::IMenu* a_menu);
    std::int32_t GetSelectedIndex(RE::IMenu* a_menu);
    std::string GetSelectedDialogueText(RE::IMenu* a_menu);
    std::string BuildIndicators(RE::IMenu* a_menu, std::int32_t a_selectedIndex);
    std::string GetChallengeDifficulty(std::int32_t a_level);
    void SpeakText(const std::string& a_text, bool a_interrupt = false);

    bool m_menuAnnounced = false;
    std::int32_t m_lastSelection = -1;

    // Scaleform paths for XDI DialogueMenu
    static constexpr const char* NPC_NAME_PATH = "root.name_tf.text";
    static constexpr const char* DIALOGUE_LIST_PATH = "root.List_mc";
};
