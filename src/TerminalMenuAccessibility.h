#pragma once

#include "pch.h"
#include <RE/T/TerminalMenu.h>

class TerminalMenuAccessibility
{
public:
    static TerminalMenuAccessibility* GetSingleton();

    void OnTerminalMenuOpened();
    void OnTerminalMenuClosed();
    void CheckSelection();
    void Reset();

    // Schedule a deferred state check (call after any input)
    void ScheduleStateCheck();

    // Start continuous polling for mode changes
    void StartContinuousPolling();

    // Called when Enter/Accept is pressed - prepares for text change
    void OnAcceptPressed();

private:
    void ScheduleDelayedChecks(int a_count);
    void ContinuePolling();
    void ScheduleNextPollCheck();
    void CheckStateChange();
    void AnnounceCurrentState(RE::IMenu* a_menu, RE::TerminalMenu::Mode a_mode, bool a_modeChanged);

    TerminalMenuAccessibility() = default;
    ~TerminalMenuAccessibility() = default;
    TerminalMenuAccessibility(const TerminalMenuAccessibility&) = delete;
    TerminalMenuAccessibility(TerminalMenuAccessibility&&) = delete;
    TerminalMenuAccessibility& operator=(const TerminalMenuAccessibility&) = delete;
    TerminalMenuAccessibility& operator=(TerminalMenuAccessibility&&) = delete;

    // Mode detection
    RE::TerminalMenu::Mode GetCurrentMode(RE::IMenu* a_menu);

    // Text retrieval (bypasses animation - reads str* properties)
    std::string GetHeaderText(RE::IMenu* a_menu);
    std::string GetWelcomeText(RE::IMenu* a_menu);
    std::string GetDisplayText(RE::IMenu* a_menu);

    // List mode
    std::int32_t GetSelectedIndex(RE::IMenu* a_menu);
    std::string GetSelectedItemText(RE::IMenu* a_menu);

    // Hacking mode
    std::string GetHackingAttemptsText(RE::IMenu* a_menu);

    // Utilities
    void SpeakText(const std::string& a_text, bool a_interrupt = false);

    // State tracking
    bool m_isOpen = false;
    bool m_menuAnnounced = false;
    RE::TerminalMenu::Mode m_lastMode = RE::TerminalMenu::Mode::kInit;
    std::int32_t m_lastSelection = -1;
    std::string m_lastDisplayText;
    int m_pollCounter = 0;

    // Scaleform paths for TerminalMenu
    // TerminalBase_mc is the Terminal instance on MainTimeline
    // Pre-animation text values (immediate access)
    static constexpr const char* HEADER_TEXT_PATH = "root.TerminalBase_mc.strHeaderText";
    static constexpr const char* WELCOME_TEXT_PATH = "root.TerminalBase_mc.strWelcomeText";
    static constexpr const char* DISPLAY_TEXT_PATH = "root.TerminalBase_mc.strDisplayText";
    // Menu list
    static constexpr const char* MENU_LIST_PATH = "root.TerminalBase_mc.MenuItemList_mc";
    // Hacking info
    static constexpr const char* HACKING_ATTEMPTS_PATH = "root.TerminalBase_mc.HackingAttempts_tf.text";
};
