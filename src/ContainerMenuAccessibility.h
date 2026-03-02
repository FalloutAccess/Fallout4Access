#pragma once

#include "pch.h"
#include "ItemIdentity.h"
#include <RE/C/ContainerMenu.h>

// Provides accessibility for ContainerMenu (looting from containers, corpses, pickpocketing)
// Uses AdvanceMovie vtable hook for per-frame state tracking
class ContainerMenuAccessibility
{
public:
    static ContainerMenuAccessibility* GetSingleton();

    // Called by MenuAccessibility when ContainerMenu opens/closes
    void OnContainerMenuOpened();
    void OnContainerMenuClosed();

    // Install the vtable hook (call once at plugin startup)
    void Install();

    // Called by MenuAccessibility when Q key is pressed to switch sides
    void SwitchSides();

private:
    ContainerMenuAccessibility() = default;
    ~ContainerMenuAccessibility() = default;
    ContainerMenuAccessibility(const ContainerMenuAccessibility&) = delete;
    ContainerMenuAccessibility(ContainerMenuAccessibility&&) = delete;
    ContainerMenuAccessibility& operator=(const ContainerMenuAccessibility&) = delete;
    ContainerMenuAccessibility& operator=(ContainerMenuAccessibility&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::ContainerMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // The hooked OnButtonEvent function (for keyboard input)
    static void HookedOnButtonEvent(RE::ContainerMenu* a_this, const RE::ButtonEvent* a_event);

    // State checking (called every frame when menu is open)
    void CheckStateChanges(RE::ContainerMenu* a_menu);

    // Focus tracking (container vs player inventory)
    void CheckFocusChange(RE::ContainerMenu* a_menu);
    bool IsContainerFocused(RE::ContainerMenu* a_menu);

    // Filter/category tracking (left/right arrows change filter)
    void CheckFilterChange(RE::ContainerMenu* a_menu);
    std::string GetCurrentFilterName(RE::ContainerMenu* a_menu, bool a_containerSide);

    // Sort mode tracking
    void CheckSortChange(RE::ContainerMenu* a_menu);
    std::string GetSortButtonLabel(RE::ContainerMenu* a_menu);

    // Item selection tracking
    void CheckItemSelection(RE::ContainerMenu* a_menu);
    std::int32_t GetSelectedIndex(RE::ContainerMenu* a_menu, bool a_containerSide);
    ItemIdentity GetItemAtIndex(RE::ContainerMenu* a_menu, std::int32_t a_index, bool a_containerSide);

    // Pickpocket mode
    void CheckPickpocketPercentage(RE::ContainerMenu* a_menu);
    std::string GetPickpocketPercentage(RE::ContainerMenu* a_menu);
    bool IsPickpocketMode(RE::ContainerMenu* a_menu);

    // Announcement helpers
    void AnnounceContainerOpened(RE::ContainerMenu* a_menu);
    void AnnounceItem(const ItemIdentity& a_item, bool a_containerSide);
    void AnnounceFocusChange(bool a_containerSide);
    std::string GetContainerName(RE::ContainerMenu* a_menu);
    std::string GetItemStats(RE::ContainerMenu* a_menu);

    // Utility
    void SpeakText(const std::string& a_text);

    // State tracking
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_openingAnnouncementDone = false;   // Skip item announcements until container name announced
    bool m_containerFocused = true;           // true = container side, false = player side
    std::int32_t m_lastContainerIndex = -1;
    std::int32_t m_lastPlayerIndex = -1;
    std::string m_lastContainerFilterName;    // Filter/category name for container
    std::string m_lastPlayerFilterName;       // Filter/category name for player inventory
    std::string m_lastSortLabel;              // Sort button label (e.g., "SORT (DMG)")
    std::string m_lastPickpocketPercent;
    bool m_isPickpocketMode = false;

    // Scaleform paths (no Menu_mc prefix - ContainerMenu uses root directly)
    static constexpr const char* CONTAINER_SELECTED_PATH = "root.containerIsSelected";
    static constexpr const char* CONTAINER_LIST_PATH = "root.ContainerList_mc";
    static constexpr const char* PLAYER_LIST_PATH = "root.PlayerInventory_mc.PlayerList_mc";
    static constexpr const char* CONTAINER_NAME_PATH = "root.ContainerInventory_mc.ContainerListHeader.headerText";
    static constexpr const char* PICKPOCKET_PERCENT_PATH = "root.PickpocketInfo_mc.Percent_tf.text";
    static constexpr const char* PICKPOCKET_VISIBLE_PATH = "root.PickpocketInfo_mc";

    // The vtable hooks
    static inline REL::HookVFT<void(RE::ContainerMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
    static inline REL::HookVFT<void(RE::ContainerMenu*, const RE::ButtonEvent*)>* s_buttonEventHook = nullptr;
};
