#pragma once

#include "pch.h"
#include <RE/B/BGSSoundDescriptorForm.h>
#include <chrono>
#include <mutex>

// Provides audio hit marker feedback for combat accessibility:
// - Plays different sounds for normal hit, critical hit, sneak attack, and killing blow
// - Enforces cooldown timer to prevent rapid-fire weapon audio spam
// - Listens to TESHitEvent (player hits enemy) and TESDeathEvent (kill confirmation)
class HitMarkerAudioFeedback :
    public RE::BSTEventSink<RE::TESHitEvent>,
    public RE::BSTEventSink<RE::TESDeathEvent>
{
public:
    static HitMarkerAudioFeedback* GetSingleton();

    void Register();

private:
    HitMarkerAudioFeedback() = default;
    ~HitMarkerAudioFeedback() override = default;
    HitMarkerAudioFeedback(const HitMarkerAudioFeedback&) = delete;
    HitMarkerAudioFeedback(HitMarkerAudioFeedback&&) = delete;
    HitMarkerAudioFeedback& operator=(const HitMarkerAudioFeedback&) = delete;
    HitMarkerAudioFeedback& operator=(HitMarkerAudioFeedback&&) = delete;

    // BSTEventSink<TESHitEvent>
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent& a_event,
        RE::BSTEventSource<RE::TESHitEvent>* a_source) override;

    // BSTEventSink<TESDeathEvent>
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent& a_event,
        RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;

    // Kill is only triggered by TESDeathEvent to avoid double-play with TESHitEvent kFatal
    enum class HitType { Normal, Critical, Sneak, Kill };

    HitType CategorizeHit(const RE::TESHitEvent& a_event);
    void PlayHitSound(HitType a_type);
    bool CanPlaySound();
    void UpdateCooldown();
    void CacheSoundForms();

    // Cached SNDR forms from fallout4access.esp
    RE::BGSSoundDescriptorForm* m_normalHitSound = nullptr;
    RE::BGSSoundDescriptorForm* m_criticalHitSound = nullptr;
    RE::BGSSoundDescriptorForm* m_sneakAttackSound = nullptr;
    RE::BGSSoundDescriptorForm* m_killSound = nullptr;
    std::once_flag m_formsOnce;

    // Cooldown state (protected by m_lock)
    std::chrono::steady_clock::time_point m_lastSoundTime{};
    mutable RE::BSSpinLock m_lock;

    static constexpr std::chrono::milliseconds COOLDOWN_MS{ 150 };
};
