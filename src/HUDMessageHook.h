#pragma once

#include "pch.h"
#include <mutex>
#include <unordered_map>

// Hooks SendHUDMessage::ShowHUDMessage and ShowHUDNotification to capture ALL HUD messages.
// ShowHUDMessage handles item pickups, location discoveries, perks, etc.
// ShowHUDNotification handles radio signal found/lost notifications.
// Uses manual trampoline allocation for proper function detouring.
class HUDMessageHook
{
public:
    static HUDMessageHook* GetSingleton();

    // Install the function hooks (call once at plugin load)
    void Install();

private:
    HUDMessageHook() = default;
    ~HUDMessageHook() = default;
    HUDMessageHook(const HUDMessageHook&) = delete;
    HUDMessageHook(HUDMessageHook&&) = delete;
    HUDMessageHook& operator=(const HUDMessageHook&) = delete;
    HUDMessageHook& operator=(HUDMessageHook&&) = delete;

    // Hook function that intercepts ShowHUDMessage calls
    static void HookedShowHUDMessage(const char* a_message, const char* a_sound,
                                      bool a_throttle, bool a_warning);

    // Hook function that intercepts ShowHUDNotification calls (radio signals)
    static void HookedShowHUDNotification(const char* a_message);

    // Announce a message via screen reader with deduplication
    void AnnounceMessage(const char* a_message, const char* a_label);

    // Original function pointers (point to our trampolines)
    using OriginalFunc_t = void(*)(const char*, const char*, bool, bool);
    using OriginalNotificationFunc_t = void(*)(const char*);
    static inline OriginalFunc_t s_originalFunc{ nullptr };
    static inline OriginalNotificationFunc_t s_originalNotificationFunc{ nullptr };

    // Deduplication - prevent announcing the same message multiple times in quick succession
    bool ShouldAnnounce(const std::string& a_text);
    void RecordAnnouncement(const std::string& a_text);
    std::uint64_t GetCurrentTimeMs() const;

    // Utility
    void SpeakText(const std::string& a_text);

    // Thread safety for deduplication map
    mutable std::mutex m_mutex;

    // Deduplication: text -> last_spoken_time_ms
    std::unordered_map<std::string, std::uint64_t> m_announcementHistory;
    static constexpr std::uint64_t kDeduplicationWindowMs = 2000;  // 2 seconds

    // Installation guard
    bool m_installed{ false };

    // Function IDs from CommonLibF4
    static constexpr REL::ID kShowHUDMessageID{ 2222440 };
    static constexpr REL::ID kShowHUDNotificationID{ 2222474 };
};
