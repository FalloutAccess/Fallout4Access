#pragma once

#include "pch.h"
#include <RE/D/DialogueMenu.h>

// Provides accessibility for DialogueMenu (NPC conversations)
// Uses AdvanceMovie vtable hook for per-frame state tracking
class DialogueMenuHook
{
public:
    static DialogueMenuHook* GetSingleton();

    // Called by MenuAccessibility when DialogueMenu opens/closes
    void OnDialogueMenuOpened();
    void OnDialogueMenuClosed();

    // Install the vtable hook (call once at plugin startup)
    void Install();

    // Reset state for new dialogue
    void ResetState();

private:
    DialogueMenuHook() = default;
    ~DialogueMenuHook() = default;
    DialogueMenuHook(const DialogueMenuHook&) = delete;
    DialogueMenuHook(DialogueMenuHook&&) = delete;
    DialogueMenuHook& operator=(const DialogueMenuHook&) = delete;
    DialogueMenuHook& operator=(DialogueMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::DialogueMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking (called every frame when menu is open)
    void CheckStateChanges(RE::DialogueMenu* a_menu);

    // Announcement helpers
    void AnnounceDialogueOpened(RE::DialogueMenu* a_menu);
    void AnnounceSelectionChange(RE::DialogueMenu* a_menu, std::int32_t a_selectedIndex);

    // Scaleform accessors
    std::string GetNPCName(RE::DialogueMenu* a_menu);
    std::int32_t GetSelectedIndex(RE::DialogueMenu* a_menu);
    std::string GetDialogueTextAtIndex(RE::DialogueMenu* a_menu, std::int32_t a_index);
    std::string BuildIndicators(RE::DialogueMenu* a_menu, std::int32_t a_index);
    std::string GetChallengeDifficulty(std::int32_t a_level);
    bool IsListVisible(RE::DialogueMenu* a_menu);

    // Utility
    void SpeakText(const std::string& a_text);

    // State tracking
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_openingAnnouncementDone = false;
    std::int32_t m_lastSelection = -1;
    std::string m_lastSpokenText;  // Deduplication

    // Scaleform paths
    static constexpr const char* NPC_NAME_PATH = "name_tf";
    static constexpr const char* DIALOGUE_LIST_PATH = "List_mc";

    // The vtable hook
    static inline REL::THookVFT<void(RE::DialogueMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
