#pragma once

#include "pch.h"
#include <RE/W/WorkshopMenu.h>
#include <RE/W/Workshop.h>
#include <REL/Hook.h>

// Provides accessibility for the Workshop building menu
// Uses AdvanceMovie vtable hook for per-frame navigation tracking
// and BSTEventSink for workshop mode and item placement events
class WorkshopMenuAccessibility :
    public RE::BSTEventSink<RE::Workshop::WorkshopModeEvent>,
    public RE::BSTEventSink<RE::Workshop::ItemPlacedEvent>,
    public RE::BSTEventSink<RE::Workshop::ItemDestroyedEvent>
{
public:
    static WorkshopMenuAccessibility* GetSingleton();

    // Install the vtable hook (call once at plugin startup)
    void Install();

    // Called by MenuAccessibility when WorkshopMenu opens/closes
    void OnWorkshopMenuOpened();
    void OnWorkshopMenuClosed();

    // Called by MenuAccessibility on H key press
    void AnnounceSettlementStats();

    // BSTEventSink overrides
    RE::BSEventNotifyControl ProcessEvent(
        const RE::Workshop::WorkshopModeEvent& a_event,
        RE::BSTEventSource<RE::Workshop::WorkshopModeEvent>* a_source) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::Workshop::ItemPlacedEvent& a_event,
        RE::BSTEventSource<RE::Workshop::ItemPlacedEvent>* a_source) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::Workshop::ItemDestroyedEvent& a_event,
        RE::BSTEventSource<RE::Workshop::ItemDestroyedEvent>* a_source) override;

private:
    WorkshopMenuAccessibility() = default;
    ~WorkshopMenuAccessibility() override = default;
    WorkshopMenuAccessibility(const WorkshopMenuAccessibility&) = delete;
    WorkshopMenuAccessibility(WorkshopMenuAccessibility&&) = delete;
    WorkshopMenuAccessibility& operator=(const WorkshopMenuAccessibility&) = delete;
    WorkshopMenuAccessibility& operator=(WorkshopMenuAccessibility&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::WorkshopMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // State checking (called every frame when menu is open)
    void CheckStateChanges(RE::WorkshopMenu* a_menu);

    // Combined navigation check — announces path + name + details as one speech
    void CheckNavigationChange(RE::WorkshopMenu* a_menu);
    void CheckEditModeChange(RE::WorkshopMenu* a_menu);

    // Data extraction (only reads visible elements)
    std::string GetDisplayName(RE::WorkshopMenu* a_menu);
    std::string GetDisplayPath(RE::WorkshopMenu* a_menu);
    std::string GetDescription(RE::WorkshopMenu* a_menu);
    std::string GetRequirements(RE::WorkshopMenu* a_menu);
    std::string GetIconCardInfo(RE::WorkshopMenu* a_menu);
    std::string GetPerkRequirements(RE::WorkshopMenu* a_menu);
    std::string GetBuiltItemCount(RE::WorkshopMenu* a_menu);

    // Settlement stats
    std::string GetStatValue(RE::WorkshopMenu* a_menu, const char* a_path);

    // Utility
    void SpeakText(const std::string& a_text);
    bool IsMovieClipVisible(RE::WorkshopMenu* a_menu, const char* a_mcPath);
    std::string ReadResourceIcon(RE::WorkshopMenu* a_menu, const char* a_iconPath);
    static const char* GetResourceNameFromFrame(int a_frame);
    static const char* GetAssignmentNameFromFrame(int a_frame);

    // State
    bool m_installed = false;
    bool m_menuOpen = false;
    bool m_eventsRegistered = false;
    int m_frameDelay = 0;
    std::string m_lastDisplayName;
    std::string m_lastDisplayPath;
    bool m_lastInEditMode = false;
    std::string m_lastSpokenText;

    // Frame delay constant
    static constexpr int FRAME_DELAY_AFTER_OPEN = 3;

    // Scaleform paths — text fields
    static constexpr const char* ITEM_NAME_PATH = "root.WorkshopBase_mc.ItemNameBase_mc.ItemName_tf.text";
    static constexpr const char* DISPLAY_PATH_PATH = "root.WorkshopBase_mc.DisplayPathBase_mc.DisplayPath_tf.text";
    static constexpr const char* DESCRIPTION_PATH = "root.WorkshopBase_mc.DescriptionBase_mc.Description_tf.text";
    static constexpr const char* BUILT_COUNT_PATH = "root.WorkshopBase_mc.BuiltItemCountText_mc.ItemCount_tf.text";

    // Scaleform paths — parent containers (for visibility checks)
    static constexpr const char* DESCRIPTION_MC_PATH = "root.WorkshopBase_mc.DescriptionBase_mc";
    static constexpr const char* ICON_CARD_MC_PATH = "root.WorkshopBase_mc.IconCardBase_mc";
    static constexpr const char* REQUIREMENTS_MC_PATH = "root.WorkshopBase_mc.RequirementsBase_mc";
    static constexpr const char* PERK0_MC_PATH = "root.WorkshopBase_mc.PerkPanel0_mc";
    static constexpr const char* PERK1_MC_PATH = "root.WorkshopBase_mc.PerkPanel1_mc";

    // Requirements list
    static constexpr const char* REQUIREMENTS_LIST_PATH = "root.WorkshopBase_mc.RequirementsBase_mc.RequirementsList_mc.entryList";

    // Settlement stats
    static constexpr const char* PEOPLE_VALUE_PATH = "root.WorkshopBase_mc.HappinessPeopleBase_mc.PeopleValue_tf.text";
    static constexpr const char* FOOD_VALUE_PATH = "root.WorkshopBase_mc.HappinessFoodBase_mc.FoodValue_tf.text";
    static constexpr const char* WATER_VALUE_PATH = "root.WorkshopBase_mc.HappinessWaterBase_mc.WaterValue_tf.text";
    static constexpr const char* POWER_VALUE_PATH = "root.WorkshopBase_mc.HappinessPowerBase_mc.PowerValue_tf.text";
    static constexpr const char* SAFETY_VALUE_PATH = "root.WorkshopBase_mc.HappinessSafetyBase_mc.SafetyValue_tf.text";
    static constexpr const char* BEDS_VALUE_PATH = "root.WorkshopBase_mc.HappinessBedsBase_mc.BedsValue_tf.text";
    static constexpr const char* HAPPINESS_VALUE_PATH = "root.WorkshopBase_mc.HappinessHappyBase_mc.HappyValue_tf.text";

    // Icon card text paths
    static constexpr const char* REQUIRES_TEXT_PATH = "root.WorkshopBase_mc.IconCardBase_mc.IconBackgroundClip_mc.RequiresText_tf.text";
    static constexpr const char* PRODUCES_TEXT_PATH = "root.WorkshopBase_mc.IconCardBase_mc.IconBackgroundClip_mc.ProducesText_tf.text";
    static constexpr const char* ASSIGNED_TEXT_PATH = "root.WorkshopBase_mc.IconCardBase_mc.IconBackgroundClip_mc.AssignedText_tf.text";

    // Icon card resource icon paths (MovieClips with currentFrame identifying resource type)
    static constexpr const char* REQUIRES_ICON1_PATH = "root.WorkshopBase_mc.IconCardBase_mc.RequirementIcons_mc.RequiresIcon1_mc";
    static constexpr const char* REQUIRES_ICON2_PATH = "root.WorkshopBase_mc.IconCardBase_mc.RequirementIcons_mc.RequiresIcon2_mc";
    static constexpr const char* PRODUCES_ICON1_PATH = "root.WorkshopBase_mc.IconCardBase_mc.ProducesIcons_mc.ProducesIcon1_mc";
    static constexpr const char* PRODUCES_ICON2_PATH = "root.WorkshopBase_mc.IconCardBase_mc.ProducesIcons_mc.ProducesIcon2_mc";
    static constexpr const char* ASSIGNMENT_ICON_PATH = "root.WorkshopBase_mc.IconCardBase_mc.AssignmentIcon_mc";

    // Perk panel text paths
    static constexpr const char* PERK0_NAME_PATH = "root.WorkshopBase_mc.PerkPanel0_mc.PerkName_tf.text";
    static constexpr const char* PERK1_NAME_PATH = "root.WorkshopBase_mc.PerkPanel1_mc.PerkName_tf.text";

    // The vtable hook
    static inline REL::HookVFT<void(RE::WorkshopMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
