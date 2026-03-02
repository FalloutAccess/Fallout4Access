#pragma once

#include "pch.h"
#include <RE/T/TerminalMenu.h>

// Hooks TerminalMenu::AdvanceMovie to check for state changes every frame
// This replaces unreliable background thread polling with efficient per-frame checks
class TerminalMenuHook
{
public:
    static TerminalMenuHook* GetSingleton();

    // Install/uninstall the vtable hook
    void Install();
    void Uninstall();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by TerminalMenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    TerminalMenuHook() = default;
    ~TerminalMenuHook() = default;
    TerminalMenuHook(const TerminalMenuHook&) = delete;
    TerminalMenuHook(TerminalMenuHook&&) = delete;
    TerminalMenuHook& operator=(const TerminalMenuHook&) = delete;
    TerminalMenuHook& operator=(TerminalMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::TerminalMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking
    void CheckStateChanges(RE::TerminalMenu* a_menu);

    // Announcement helpers
    void AnnounceMode(RE::TerminalMenu::Mode a_mode);
    void AnnounceSelection(RE::TerminalMenu* a_menu, std::int32_t a_index);
    void AnnounceDisplayText(RE::TerminalMenu* a_menu);

    // Scaleform helpers
    std::int32_t GetSelectedIndex(RE::TerminalMenu* a_menu);
    std::string GetSelectedItemText(RE::TerminalMenu* a_menu, std::int32_t a_index);
    std::string GetDisplayText(RE::TerminalMenu* a_menu);
    std::string GetHackingAttemptsText(RE::TerminalMenu* a_menu);

    // Utility
    void SpeakText(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    RE::TerminalMenu::Mode m_lastMode = RE::TerminalMenu::Mode::kInit;
    std::int32_t m_lastSelection = -1;
    std::string m_lastDisplayText;

    // Scaleform paths
    static constexpr const char* MENU_LIST_PATH = "root.TerminalBase_mc.MenuItemList_mc";
    static constexpr const char* DISPLAY_TEXT_PATH = "root.TerminalBase_mc.strDisplayText";
    static constexpr const char* HACKING_ATTEMPTS_PATH = "root.TerminalBase_mc.HackingAttempts_tf.text";

    // The vtable hook
    static inline REL::HookVFT<void(RE::TerminalMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
