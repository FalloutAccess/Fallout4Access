#pragma once

#include "pch.h"
#include <RE/E/ExamineMenu.h>
#include <RE/P/PowerArmorModMenu.h>

// Hooks ExamineMenu::AdvanceMovie to check for state changes every frame
// Tracks mode transitions, item selection, mod slot selection, and mod choice selection
class ExamineMenuHook
{
public:
    static ExamineMenuHook* GetSingleton();

    // Install the vtable hook (only installs once, stays active)
    void Install();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when menu opens)
    void ResetState();

    // Called by ExamineMenuAccessibility when menu opens/closes
    void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
    ExamineMenuHook() = default;
    ~ExamineMenuHook() = default;
    ExamineMenuHook(const ExamineMenuHook&) = delete;
    ExamineMenuHook(ExamineMenuHook&&) = delete;
    ExamineMenuHook& operator=(const ExamineMenuHook&) = delete;
    ExamineMenuHook& operator=(ExamineMenuHook&&) = delete;

    // Hooked AdvanceMovie functions (one per menu type for correct trampoline calls)
    static void HookedExamineAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time);
    static void HookedPowerArmorAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time);
    static void HookedCookingAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time);
    static void HookedRobotModAdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // Shared accessibility logic called after each trampoline
    void ProcessAdvanceMovie(RE::ExamineMenu* a_this);

    // State checking
    void CheckStateChanges(RE::ExamineMenu* a_menu);

    // Mode-specific handlers
    void OnModeChanged(RE::ExamineMenu* a_menu, RE::ExamineMenu::INSPECT_MODE_STATE a_newMode);
    void OnItemSelectMode(RE::ExamineMenu* a_menu);
    void OnModSlotMode(RE::ExamineMenu* a_menu);
    void OnModChoiceMode(RE::ExamineMenu* a_menu);
    void OnRequirementsMode(RE::ExamineMenu* a_menu);
    void OnConfirmMode(RE::ExamineMenu* a_menu);

    // Data extraction helpers
    std::int32_t GetSelectedIndex(const Scaleform::GFx::Value& a_list);
    std::string GetItemName(RE::ExamineMenu* a_menu);
    std::string GetItemStats(RE::ExamineMenu* a_menu);
    std::string GetModSlotName(RE::ExamineMenu* a_menu, std::int32_t a_index);
    std::string GetModName(RE::ExamineMenu* a_menu, std::int32_t a_index);
    std::string GetModDescription(RE::ExamineMenu* a_menu);
    std::string GetModRequirements(RE::ExamineMenu* a_menu, std::int32_t a_index);
    std::string BuildItemAnnouncement(RE::ExamineMenu* a_menu, std::int32_t a_index);

    // Utility
    void SpeakText(const std::string& a_text);
    std::string CleanLocalizationString(const std::string& a_text);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_firstCheck = true;  // First check after menu open
    int m_frameDelay = 0;      // Frames to wait after mode change for Scaleform to populate
    RE::ExamineMenu::INSPECT_MODE_STATE m_lastMode = RE::ExamineMenu::INSPECT_MODE_STATE::kItemSelect;
    std::int32_t m_lastItemSelection = -1;
    std::int32_t m_lastModSlotSelection = -1;
    std::int32_t m_lastModChoiceSelection = -1;
    std::string m_lastSpokenText;  // For deduplication

    // Frame delay constants
    static constexpr int FRAME_DELAY_AFTER_MODE_CHANGE = 3;  // Wait 3 frames for Scaleform to populate

    // Scaleform paths
    static constexpr const char* ITEM_NAME_PATH = "root.ItemName_tf.text";
    static constexpr const char* INVENTORY_LIST_PATH = "root.InventoryBase_mc.InventoryList_mc";
    static constexpr const char* MOD_SLOT_LIST_PATH = "root.ModSlotBase_mc.ModSlotList_mc";
    static constexpr const char* SLOT_LABEL_PATH = "root.ModSlotBase_mc.SlotsLabel_tf.text";
    static constexpr const char* ITEM_CARD_PATH = "root.ItemCardList_mc.InfoObj";
    static constexpr const char* MOD_DESCRIPTION_PATH = "root.ModDescriptionBase_mc.ModDescription_tf.text";
    static constexpr const char* LEGENDARY_DESC_PATH = "root.LegendaryItemDescription_tf.text";

    // The vtable hooks (one per menu type, all call the same HookedAdvanceMovie)
    static inline REL::THookVFT<void(RE::ExamineMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
    static inline REL::THookVFT<void(RE::ExamineMenu*, float, std::uint64_t)>* s_powerArmorAdvanceMovieHook = nullptr;
    static inline REL::THookVFT<void(RE::ExamineMenu*, float, std::uint64_t)>* s_cookingAdvanceMovieHook = nullptr;
    static inline REL::THookVFT<void(RE::ExamineMenu*, float, std::uint64_t)>* s_robotModAdvanceMovieHook = nullptr;
};
