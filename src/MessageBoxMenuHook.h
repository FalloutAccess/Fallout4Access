#pragma once

#include "pch.h"
#include <RE/M/MessageBoxMenu.h>
#include <REL/Hook.h>

// Hooks MessageBoxMenu::AdvanceMovie to monitor button selection changes
// Announces body text and button count on open, selected button on navigation
class MessageBoxMenuHook
{
public:
    static MessageBoxMenuHook* GetSingleton();

    // Install the vtable hook (called once at plugin startup)
    void Install();

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by MenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    MessageBoxMenuHook() = default;
    ~MessageBoxMenuHook() = default;
    MessageBoxMenuHook(const MessageBoxMenuHook&) = delete;
    MessageBoxMenuHook(MessageBoxMenuHook&&) = delete;
    MessageBoxMenuHook& operator=(const MessageBoxMenuHook&) = delete;
    MessageBoxMenuHook& operator=(MessageBoxMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::MessageBoxMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // Check for content and selection changes (called every frame when menu is open)
    void CheckContentChanges(RE::MessageBoxMenu* a_menu);

    // Scaleform accessors
    std::string GetBodyText(RE::IMenu* a_menu);
    std::int32_t GetButtonCount(RE::IMenu* a_menu);
    std::int32_t GetSelectedIndex(RE::IMenu* a_menu);
    std::string GetButtonText(RE::IMenu* a_menu, std::int32_t a_index);

    // Utility
    void SpeakText(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_initialAnnouncementDone = false;
    std::int32_t m_lastSelection = -1;
    std::int32_t m_buttonCount = 0;
    std::int32_t m_framesSinceOpen = 0;  // Track frames for delayed data population
    static constexpr std::int32_t MAX_RETRY_FRAMES = 10;  // Retry for ~166ms at 60fps
    std::string m_lastBodyText;
    std::vector<std::string> m_lastButtonLabels;

    // Scaleform paths (from SWF analysis - Menu_mc is the stage instance)
    static constexpr const char* BODY_TEXT_PATH = "root.Menu_mc.Body_tf.text";
    static constexpr const char* LIST_PATH = "root.Menu_mc.List_mc";

    // The vtable hook
    static inline REL::HookVFT<void(RE::MessageBoxMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
