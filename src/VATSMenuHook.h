#pragma once

#include "pch.h"
#include <REL/Hook.h>

// Hooks VATSMenu::AdvanceMovie to check for state changes every frame
// Tracks target changes, body part selection, health, and distance
class VATSMenuHook
{
public:
    static VATSMenuHook* GetSingleton();

    // Install the vtable hook (only installs once, stays active)
    void Install();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by VATSMenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    VATSMenuHook() = default;
    ~VATSMenuHook() = default;
    VATSMenuHook(const VATSMenuHook&) = delete;
    VATSMenuHook(VATSMenuHook&&) = delete;
    VATSMenuHook& operator=(const VATSMenuHook&) = delete;
    VATSMenuHook& operator=(VATSMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking - called after original AdvanceMovie
    void CheckStateChanges(RE::IMenu* a_menu);
    void CheckTargetChange(RE::IMenu* a_menu);
    void CheckBodyPartChange(RE::IMenu* a_menu);

    // Data extraction
    std::string GetCurrentTargetName();
    std::uint32_t GetCurrentBodyPart(RE::IMenu* a_menu);
    float GetTargetHealthPercent();
    float GetTargetDistance();
    std::string GetTargetLevel(RE::IMenu* a_menu);

    // Helper to get target actor from multiple sources
    RE::Actor* GetTargetActor();

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    int m_frameDelay = 0;
    std::string m_lastTargetName;
    std::uint32_t m_lastBodyPart = 0xFFFFFFFF;

    // Frame delay after menu open for Scaleform to populate
    static constexpr int FRAME_DELAY_AFTER_OPEN = 3;

    // Scaleform paths for VATSMenu
    static constexpr const char* SELECTED_PART_PATH = "root.MenuInstance.SelectedPart";
    static constexpr const char* TARGET_LEVEL_PATH = "root.MenuInstance.ResistancesInstance.Level.text";

    // The vtable hook
    static inline REL::HookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
