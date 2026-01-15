#pragma once

#include "pch.h"
#include <atomic>
#include <chrono>
#include <unordered_map>

// Handles accessibility for HUD notifications (messages, quest updates, objective updates)
// Uses background thread polling to detect changes in Scaleform notification arrays/text fields
class HUDNotificationAccessibility
{
public:
    static HUDNotificationAccessibility* GetSingleton();

    // Lifecycle (called from MenuAccessibility)
    void OnHUDMenuOpened();
    void OnHUDMenuClosed();

private:
    HUDNotificationAccessibility() = default;
    ~HUDNotificationAccessibility() = default;
    HUDNotificationAccessibility(const HUDNotificationAccessibility&) = delete;
    HUDNotificationAccessibility(HUDNotificationAccessibility&&) = delete;
    HUDNotificationAccessibility& operator=(const HUDNotificationAccessibility&) = delete;
    HUDNotificationAccessibility& operator=(HUDNotificationAccessibility&&) = delete;

    // Background thread polling (uses real sleep, not task queue recursion)
    void StartPollingThread();
    void PollNotifications();

    // Notification type handlers
    void CheckMessages(RE::IMenu* a_menu);
    void CheckQuestUpdates(RE::IMenu* a_menu);
    void CheckObjectiveUpdates(RE::IMenu* a_menu);
    void CheckXPUpdates(RE::IMenu* a_menu);

    // Deduplication system
    bool ShouldAnnounce(const std::string& a_text);
    void RecordAnnouncement(const std::string& a_text);
    void CleanExpiredAnnouncements();

    // Utilities
    void SpeakText(const std::string& a_text);
    std::uint64_t GetCurrentTimeMs() const;

    // State tracking (atomic for thread safety)
    std::atomic<bool> m_isOpen{false};

    // Track last seen array sizes to detect new notifications
    std::uint32_t m_lastMessageCount = 0;
    std::uint32_t m_lastObjectiveCount = 0;
    std::string m_lastQuestUpdate;
    std::string m_lastXPValue;

    // Deduplication: text -> last_spoken_time_ms
    std::unordered_map<std::string, std::uint64_t> m_announcementHistory;
    static constexpr std::uint64_t kDeduplicationWindowMs = 2000;  // 2 seconds
    static constexpr std::uint64_t kPollIntervalMs = 200;  // 5 checks per second

    // Scaleform paths (discovered from HUDMenu.swf)
    static constexpr const char* MESSAGES_ARRAY_PATH = "root.HUDNotificationsGroup_mc.Messages_mc.MessageArray";
    static constexpr const char* QUEST_NAME_PATH = "root.HUDNotificationsGroup_mc.QuestUpdates_mc.QuestName_tf.text";
    static constexpr const char* QUEST_TYPE_PATH = "root.HUDNotificationsGroup_mc.QuestUpdates_mc.UpdateType_tf.text";
    static constexpr const char* OBJECTIVES_ARRAY_PATH = "root.HUDNotificationsGroup_mc.ObjectiveUpdates_mc.ObjectiveDataV";
    static constexpr const char* XP_NUMBER_PATH = "root.HUDNotificationsGroup_mc.XPMeter_mc.NumberText.text";
};
