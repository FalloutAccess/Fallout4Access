#pragma once

#include "pch.h"

// PickRefUpdateEvent is forward-declared in CommonLibF4 but not defined
// Based on usage patterns (PlayerCharacter is a BSTEventSource for it),
// it likely contains the target reference handle
namespace RE
{
    class PickRefUpdateEvent
    {
    public:
        ObjectRefHandle target;  // The reference under the crosshair
    };
}

class CrosshairAccessibility :
    public RE::BSTEventSink<RE::PickRefUpdateEvent>
{
public:
    static CrosshairAccessibility* GetSingleton();

    void Register();
    void Unregister();

    // Event handler
    RE::BSEventNotifyControl ProcessEvent(
        const RE::PickRefUpdateEvent& a_event,
        RE::BSTEventSource<RE::PickRefUpdateEvent>* a_source) override;

private:
    CrosshairAccessibility() = default;
    ~CrosshairAccessibility() override = default;
    CrosshairAccessibility(const CrosshairAccessibility&) = delete;
    CrosshairAccessibility(CrosshairAccessibility&&) = delete;
    CrosshairAccessibility& operator=(const CrosshairAccessibility&) = delete;
    CrosshairAccessibility& operator=(CrosshairAccessibility&&) = delete;

    void AnnounceTarget(RE::TESObjectREFR* a_ref);
    void SpeakText(const std::string& a_text);
    std::string GetActivateText(RE::TESObjectREFR* a_ref);

    // Track last announced to avoid repeating (atomic for thread safety)
    std::atomic<RE::TESFormID> m_lastAnnouncedFormID{0};
    std::atomic<std::uint64_t> m_lastTargetTime{0};  // When we last had a valid target
    static constexpr std::uint64_t kResetDelayMs = 500;  // Reset tracking after 500ms with no target
    bool m_registered = false;
};
