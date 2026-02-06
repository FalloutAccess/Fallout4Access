#pragma once

#include "pch.h"
#include <REL/Hook.h>

// Provides accessibility for the Workshop Caravan (supply line) destination menu
// This is a separate menu (Workshop_CaravanMenu.swf) with a scrolling settlement list
class WorkshopCaravanMenuAccessibility
{
public:
    static WorkshopCaravanMenuAccessibility* GetSingleton();

    // Install the vtable hook (call once at plugin startup)
    void Install();

    // Called by MenuAccessibility when caravan menu opens/closes
    void OnCaravanMenuOpened();
    void OnCaravanMenuClosed();

private:
    WorkshopCaravanMenuAccessibility() = default;
    ~WorkshopCaravanMenuAccessibility() = default;
    WorkshopCaravanMenuAccessibility(const WorkshopCaravanMenuAccessibility&) = delete;
    WorkshopCaravanMenuAccessibility(WorkshopCaravanMenuAccessibility&&) = delete;
    WorkshopCaravanMenuAccessibility& operator=(const WorkshopCaravanMenuAccessibility&) = delete;
    WorkshopCaravanMenuAccessibility& operator=(WorkshopCaravanMenuAccessibility&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking (called every frame when menu is open)
    void CheckStateChanges(RE::IMenu* a_menu);
    void CheckSelectionChange(RE::IMenu* a_menu);

    // Data extraction
    std::string GetHeaderText(RE::IMenu* a_menu);
    std::string GetSelectedSettlement(RE::IMenu* a_menu);
    std::int32_t GetSelectedIndex(RE::IMenu* a_menu);
    std::int32_t GetListSize(RE::IMenu* a_menu);

    // Utility
    void SpeakText(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    int m_frameDelay = 0;
    std::int32_t m_lastSelectedIndex = -1;
    std::string m_lastSpokenText;

    // Frame delay constant
    static constexpr int FRAME_DELAY_AFTER_OPEN = 3;

    // Scaleform paths
    static constexpr const char* HEADER_TEXT_PATH = "root.Header_tf.text";
    static constexpr const char* CARAVAN_LIST_PATH = "root.CaravanList_mc";
    static constexpr const char* SELECTED_INDEX_PATH = "root.CaravanList_mc.selectedIndex";
    static constexpr const char* ENTRY_LIST_PATH = "root.CaravanList_mc.entryList";

    // The vtable hook
    static inline REL::HookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
