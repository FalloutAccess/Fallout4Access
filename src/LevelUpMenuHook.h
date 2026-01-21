#pragma once

#include "pch.h"
#include <REL/Hook.h>

// Hooks LevelUpMenu::AdvanceMovie to check for state changes every frame
// Tracks perk grid navigation (row/column) and applies platform hotpatch
// to enable keyboard navigation on PC
class LevelUpMenuHook
{
public:
    static LevelUpMenuHook* GetSingleton();

    // Install the vtable hook (only installs once, stays active)
    void Install();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by MenuAccessibility/LevelUpMenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    LevelUpMenuHook() = default;
    ~LevelUpMenuHook() = default;
    LevelUpMenuHook(const LevelUpMenuHook&) = delete;
    LevelUpMenuHook(LevelUpMenuHook&&) = delete;
    LevelUpMenuHook& operator=(const LevelUpMenuHook&) = delete;
    LevelUpMenuHook& operator=(LevelUpMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking - called after original AdvanceMovie
    void CheckStateChanges(RE::IMenu* a_menu);

    // Apply platform hotpatch to enable keyboard navigation
    // Sets GridView_mc.platform = 1 (gamepad mode) to bypass PC keyboard check
    void ApplyPlatformHotpatch(RE::IMenu* a_menu);

    // Track selection changes
    void CheckSelectionChange(RE::IMenu* a_menu);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_hotpatchApplied = false;
    bool m_gridViewErrorLogged = false;  // Prevent log spam
    int m_frameDelay = 0;
    std::uint32_t m_lastRow = 0xFFFFFFFF;
    std::uint32_t m_lastCol = 0xFFFFFFFF;
    std::string m_lastDescription;  // Track description text for Alt/Ctrl rank switching

    // Frame delay after menu open for Scaleform to populate
    static constexpr int FRAME_DELAY_AFTER_OPEN = 15;

    // Scaleform paths for LevelUpMenu
    // The menu structure is: root.Menu_mc.GridView_mc (PerkGrid instance)
    static constexpr const char* GRID_VIEW_PATH = "root.Menu_mc.GridView_mc";
    static constexpr const char* SELECTED_PERK_CLIP_PATH = "root.Menu_mc.GridView_mc.selectedPerkClip";
    // Description text field - changes when rank switches via Alt/Ctrl
    static constexpr const char* DESCRIPTION_TEXT_PATH = "root.Menu_mc.PerkInfo_mc.DescriptionText_tf.text";

    // The vtable hook
    static inline REL::HookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
