#pragma once

#include "pch.h"
#include "ItemIdentity.h"
#include <REL/Hook.h>
#include <unordered_map>

// Hooks HUDMenu::AdvanceMovie to check for notification changes every frame
// This replaces unreliable background thread polling with efficient per-frame checks
class HUDMenuHook
{
public:
    static HUDMenuHook* GetSingleton();

    // Install/uninstall the vtable hook
    void Install();
    void Uninstall();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset state tracking (call when HUD opens)
    void ResetState();

    // Called by HUDNotificationAccessibility when HUD opens/closes
    void SetHUDOpen(bool a_open) { m_hudOpen = a_open; }

private:
    HUDMenuHook() = default;
    ~HUDMenuHook() = default;
    HUDMenuHook(const HUDMenuHook&) = delete;
    HUDMenuHook(HUDMenuHook&&) = delete;
    HUDMenuHook& operator=(const HUDMenuHook&) = delete;
    HUDMenuHook& operator=(HUDMenuHook&&) = delete;

    // The hooked AdvanceMovie function
    static void HookedAdvanceMovie(RE::HUDMenu* a_this, float a_timeDelta, std::uint64_t a_time);

    // The hooked OnButtonEvent function (for keyboard input)
    static void HookedOnButtonEvent(RE::HUDMenu* a_this, const RE::ButtonEvent* a_event);

    // State checking - called after original AdvanceMovie
    void CheckNotifications(RE::HUDMenu* a_menu);

    // Check different notification types
    void CheckMessages(RE::IMenu* a_menu);
    void CheckQuestUpdates(RE::IMenu* a_menu);
    void CheckObjectiveUpdates(RE::IMenu* a_menu);
    void CheckXPUpdates(RE::IMenu* a_menu);
    void CheckTutorialText(RE::IMenu* a_menu);
    void CheckLevelUp(RE::IMenu* a_menu);
    void CheckSubtitles(RE::IMenu* a_menu);

    // Quick Loot (QuickContainerWidget) tracking
    void CheckQuickLoot(RE::HUDMenu* a_menu);
    bool IsQuickLootVisible(RE::HUDMenu* a_menu);
    std::int32_t GetQuickLootSelectedIndex(RE::HUDMenu* a_menu);
    void SetQuickLootSelectedIndex(RE::HUDMenu* a_menu, std::int32_t a_index);
    ItemIdentity GetQuickLootItemAtIndex(RE::HUDMenu* a_menu, std::int32_t a_index);
    std::string GetQuickLootContainerName(RE::HUDMenu* a_menu);
    void AnnounceQuickLootOpened(RE::HUDMenu* a_menu);
    void AnnounceQuickLootItem(const ItemIdentity& a_item);

    // Quick Loot keyboard navigation (arrow keys)
    void HandleQuickLootNavigation(RE::HUDMenu* a_menu, bool a_up);
    std::int32_t GetQuickLootItemCount(RE::HUDMenu* a_menu);
    void SetQuickLootSelection(RE::HUDMenu* a_menu, std::int32_t a_index);
    void SimulateQuickLootScroll(RE::HUDMenu* a_menu, bool a_up);

    // Debug: dump Quick Container input mappings to discover user event names
    void DebugDumpQuickContainerMappings();

    // Debug: find and examine HUDQuickContainer in hudObjects
    void DebugDumpHUDQuickContainer(RE::HUDMenu* a_menu);

    // Deduplication - only announce each unique message once per session
    bool ShouldAnnounce(const std::string& a_text);
    void RecordAnnouncement(const std::string& a_text);
    void CleanExpiredAnnouncements();

    // Utility
    void SpeakText(const std::string& a_text);
    std::uint64_t GetCurrentTimeMs() const;

    // State
    bool m_installed = false;
    bool m_hudOpen = false;

    // Track seen message indices to detect new messages
    std::uint32_t m_lastMessageCount = 0;
    std::uint32_t m_lastObjectiveCount = 0;
    std::string m_lastQuestUpdate;

    // XP tracking - wait for value to stabilize before announcing
    std::string m_lastXPValue;
    std::string m_pendingXPValue;
    std::uint64_t m_xpValueStableTime = 0;
    bool m_xpAnnounced = false;
    static constexpr std::uint64_t kXPStabilizeMs = 500;

    // Tutorial and Level Up tracking
    std::string m_lastTutorialText;
    std::string m_lastLevelUpText;
    std::string m_pendingLevelUpText;
    std::uint64_t m_levelUpStableTime = 0;
    bool m_levelUpAnnounced = false;
    static constexpr std::uint64_t kLevelUpStabilizeMs = 300;  // Wait for animation to finish

    // Subtitle tracking
    std::string m_lastSubtitleText;

    // Quick Loot tracking
    bool m_quickLootVisible = false;
    std::int32_t m_lastQuickLootIndex = -1;
    std::string m_lastQuickLootContainer;
    std::string m_lastQuickLootItemName;  // Track item name to detect changes after taking items
    bool m_quickLootOpeningDone = false;

    // HUDQuickContainer discovery (for direct state manipulation)
    void* m_hudQuickContainerPtr = nullptr;
    std::uint32_t m_hudQuickContainerIndex = 0;
    void* m_hudQuickContainerDataModelPtr = nullptr;
    std::uint32_t m_hudQuickContainerDataModelOffset = 0;

    // Helper to get RTTI type name from a pointer
    const char* GetRTTITypeName(void* a_ptr);

    // Find and cache the HUDQuickContainerDataModel pointer
    void* FindQuickContainerDataModel(RE::HUDMenu* a_menu);

    // Call the game's native QuickContainerUp/Down functions
    void CallNativeQuickContainerUp(void* a_dataModel);
    void CallNativeQuickContainerDown(void* a_dataModel);

    // Deduplication: text -> last_spoken_time_ms
    std::unordered_map<std::string, std::uint64_t> m_announcementHistory;
    static constexpr std::uint64_t kDeduplicationWindowMs = 2000;  // 2 seconds

    // Scaleform paths for HUDMenu notifications
    static constexpr const char* MESSAGES_MC_PATH = "root.HUDNotificationsGroup_mc.Messages_mc";
    static constexpr const char* QUEST_NAME_PATH = "root.HUDNotificationsGroup_mc.QuestUpdates_mc.QuestName_tf.text";
    static constexpr const char* QUEST_TYPE_PATH = "root.HUDNotificationsGroup_mc.QuestUpdates_mc.UpdateType_tf.text";
    static constexpr const char* OBJECTIVES_MC_PATH = "root.HUDNotificationsGroup_mc.ObjectiveUpdates_mc";
    static constexpr const char* XP_NUMBER_PATH = "root.HUDNotificationsGroup_mc.XPMeter_mc.NumberText.text";
    static constexpr const char* TUTORIAL_TEXT_PATH = "root.HUDNotificationsGroup_mc.TutorialText_mc.TutorialText_tf.text";
    static constexpr const char* LEVELUP_TEXT_PATH = "root.HUDNotificationsGroup_mc.XPMeter_mc.LeveUpTextClip.textField.text";  // Note: "LeveUp" typo in game

    // Subtitle paths
    static constexpr const char* SUBTITLE_TEXT_PATH = "root.BottomCenterGroup_mc.SubtitleText_mc.SubtitleText_tf.text";
    static constexpr const char* SPEAKER_NAME_PATH = "root.BottomCenterGroup_mc.SubtitleText_mc.SpeakerName_tf.text";

    // Quick Loot paths (QuickContainerWidget is in CenterGroup_mc)
    static constexpr const char* QUICKLOOT_WIDGET_PATH = "root.CenterGroup_mc.QuickContainerWidget_mc";

    // The vtable hooks
    static inline REL::HookVFT<void(RE::HUDMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
    static inline REL::HookVFT<void(RE::HUDMenu*, const RE::ButtonEvent*)>* s_buttonEventHook = nullptr;
};
