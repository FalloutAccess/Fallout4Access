#pragma once

#include "pch.h"
#include <RE/L/LoadingMenu.h>
#include <REL/Hook.h>

// Hooks LoadingMenu::AdvanceMovie to announce loading screen content
// - Announces player level, VDSG catalog number, and loading tip on menu open
// - Detects and announces tip text changes when tips rotate (~20 seconds)
// - Skips announcements during minimal mode (black screen only)
class LoadingMenuHook
{
public:
    static LoadingMenuHook* GetSingleton();

    // Install the vtable hook (call once at plugin load)
    void Install();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by MenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    LoadingMenuHook() = default;
    ~LoadingMenuHook() = default;
    LoadingMenuHook(const LoadingMenuHook&) = delete;
    LoadingMenuHook(LoadingMenuHook&&) = delete;
    LoadingMenuHook& operator=(const LoadingMenuHook&) = delete;
    LoadingMenuHook& operator=(LoadingMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::LoadingMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // Initial content announcement (with retry for delayed Scaleform population)
    void AnnounceInitialContent(RE::LoadingMenu* a_menu);

    // Per-frame state checking for tip rotation
    void CheckStateChanges(RE::LoadingMenu* a_menu);

    // Scaleform helpers
    std::string GetTipText(RE::LoadingMenu* a_menu);
    std::string GetVDSGNumber(RE::LoadingMenu* a_menu);
    std::string GetPlayerLevel(RE::LoadingMenu* a_menu);
    bool IsMinimalMode(RE::LoadingMenu* a_menu);

    // XP progress calculation (returns -1 on error, 0-100 on success)
    std::int32_t GetXPProgressPercent();

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_initialAnnouncementDone = false;
    std::int32_t m_framesSinceOpen = 0;
    std::string m_lastTipText;

    // Scaleform paths
    static constexpr const char* TIP_TEXT_PATH = "root.FilterHolder_mc.Menu_mc.LeftText_mc.LoadScreenText_tf.text";
    static constexpr const char* VDSG_NUMBER_PATH = "root.FilterHolder_mc.Menu_mc.LeftText_mc.VDSGText_tf.text";
    static constexpr const char* PLAYER_LEVEL_PATH = "root.FilterHolder_mc.Menu_mc.LeftText_mc.LevelText_tf.text";
    static constexpr const char* MINIMAL_MODE_PATH = "root.FilterHolder_mc.Menu_mc.BlackRect_mc.visible";

    // Retry window for initial announcement (10 frames = ~166ms at 60 FPS)
    static constexpr std::int32_t MAX_RETRY_FRAMES = 10;

    // The vtable hook
    static inline REL::HookVFT<void(RE::LoadingMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
