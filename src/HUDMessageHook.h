#pragma once

#include "pch.h"
#include <mutex>
#include <unordered_map>

// Hooks SendHUDMessage::ShowHUDMessage to capture ALL HUD notification messages
// This is the central entry point for item pickups, location discoveries, perks, etc.
// These messages bypass Scaleform and are rendered directly by the game engine.
// Uses manual trampoline allocation for proper function detouring.
class HUDMessageHook
{
public:
    static HUDMessageHook* GetSingleton();

    // Install the function hook (call once at plugin load)
    void Install();

private:
    HUDMessageHook() = default;
    ~HUDMessageHook() = default;
    HUDMessageHook(const HUDMessageHook&) = delete;
    HUDMessageHook(HUDMessageHook&&) = delete;
    HUDMessageHook& operator=(const HUDMessageHook&) = delete;
    HUDMessageHook& operator=(HUDMessageHook&&) = delete;

    // Our hook function that intercepts ShowHUDMessage calls
    static void HookedShowHUDMessage(const char* a_message, const char* a_sound,
                                      bool a_throttle, bool a_warning);

    // Original function pointer (points to our trampoline, or direct address as fallback)
    using OriginalFunc_t = void(*)(const char*, const char*, bool, bool);
    static inline OriginalFunc_t s_originalFunc{ nullptr };

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

    // Function ID from CommonLibF4
    static constexpr REL::ID kShowHUDMessageID{ 2222440 };
};
