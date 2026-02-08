#include "HitMarkerAudioFeedback.h"
#include <RE/T/TESHitEvent.h>
#include <RE/T/TESDeathEvent.h>
#include <RE/T/TESDataHandler.h>
#include <RE/G/GameScript.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/H/HitData.h>

// SNDR form IDs in fallout4access.esp
static constexpr std::uint32_t kNormalHitFormID   = 0x008ABC;  // F4A_TargetHit
static constexpr std::uint32_t kCriticalHitFormID = 0x009257;  // F4A_TargetHitCrit
static constexpr std::uint32_t kSneakHitFormID    = 0x008ABC;  // F4A_TargetHit (same as normal)
static constexpr std::uint32_t kKillFormID        = 0x0099F2;  // F4A_TargetHitKill
static constexpr const char* kESPName = "fallout4access.esp";

HitMarkerAudioFeedback* HitMarkerAudioFeedback::GetSingleton()
{
    static HitMarkerAudioFeedback singleton;
    return &singleton;
}

void HitMarkerAudioFeedback::Register()
{
    auto* hitSource = RE::TESHitEvent::GetEventSource();
    if (hitSource) {
        hitSource->RegisterSink(static_cast<RE::BSTEventSink<RE::TESHitEvent>*>(this));
        REX::INFO("HitMarkerAudioFeedback: Registered for TESHitEvent");
    } else {
        REX::ERROR("HitMarkerAudioFeedback: Failed to get TESHitEvent source");
    }

    auto* deathSource = RE::TESDeathEvent::GetEventSource();
    if (deathSource) {
        deathSource->RegisterSink(static_cast<RE::BSTEventSink<RE::TESDeathEvent>*>(this));
        REX::INFO("HitMarkerAudioFeedback: Registered for TESDeathEvent");
    } else {
        REX::ERROR("HitMarkerAudioFeedback: Failed to get TESDeathEvent source");
    }
}

RE::BSEventNotifyControl HitMarkerAudioFeedback::ProcessEvent(
    const RE::TESHitEvent& a_event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return RE::BSEventNotifyControl::kContinue;

    // Only care about hits caused by the player
    if (!a_event.cause || a_event.cause.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Skip if target is invalid
    if (!a_event.target) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!CanPlaySound()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    HitType type = CategorizeHit(a_event);
    PlayHitSound(type);
    UpdateCooldown();

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl HitMarkerAudioFeedback::ProcessEvent(
    const RE::TESDeathEvent& a_event,
    RE::BSTEventSource<RE::TESDeathEvent>*)
{
    // Only care about confirmed deaths (dead == true), not the dying phase
    if (!a_event.dead) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return RE::BSEventNotifyControl::kContinue;

    // Only care about kills by the player
    if (!a_event.actorKiller || a_event.actorKiller.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Don't announce player's own death
    if (a_event.actorDying && a_event.actorDying.get() == player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!CanPlaySound()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    PlayHitSound(HitType::Kill);
    UpdateCooldown();

    return RE::BSEventNotifyControl::kContinue;
}

HitMarkerAudioFeedback::HitType HitMarkerAudioFeedback::CategorizeHit(const RE::TESHitEvent& a_event)
{
    if (!a_event.usesHitData) {
        return HitType::Normal;
    }

    const auto& flags = a_event.hitData.flags;

    // kFatal is not checked here - kill sounds come exclusively from TESDeathEvent
    // to avoid double-playing when both events fire for the same kill

    if (flags.any(RE::HitData::Flag::kSneakAttack)) {
        return HitType::Sneak;
    }

    if (flags.any(RE::HitData::Flag::kCritical)) {
        return HitType::Critical;
    }

    return HitType::Normal;
}

void HitMarkerAudioFeedback::PlayHitSound(HitType a_type)
{
    std::call_once(m_formsOnce, [this]() { CacheSoundForms(); });

    RE::BGSSoundDescriptorForm* soundForm = nullptr;
    switch (a_type) {
    case HitType::Normal:   soundForm = m_normalHitSound; break;
    case HitType::Critical: soundForm = m_criticalHitSound; break;
    case HitType::Sneak:    soundForm = m_sneakAttackSound; break;
    case HitType::Kill:     soundForm = m_killSound; break;
    }

    if (!soundForm) {
        return;
    }

    auto* gameVM = RE::GameVM::GetSingleton();
    if (!gameVM) return;
    auto vm = gameVM->GetVM();
    if (!vm) return;

    // Fire-and-forget: script gets player reference internally
    vm->DispatchStaticCall(
        RE::BSFixedString("F4A:SoundHelper"),
        RE::BSFixedString("PlaySound"),
        nullptr,
        static_cast<std::int32_t>(soundForm->GetFormID()));
}

void HitMarkerAudioFeedback::CacheSoundForms()
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    m_normalHitSound = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(kNormalHitFormID, kESPName);
    m_criticalHitSound = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(kCriticalHitFormID, kESPName);
    m_sneakAttackSound = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(kSneakHitFormID, kESPName);
    m_killSound = dataHandler->LookupForm<RE::BGSSoundDescriptorForm>(kKillFormID, kESPName);

    if (!m_normalHitSound) REX::WARN("HitMarkerAudioFeedback: Normal hit SNDR {:06X} not found in {}", kNormalHitFormID, kESPName);
    if (!m_criticalHitSound) REX::WARN("HitMarkerAudioFeedback: Critical hit SNDR {:06X} not found in {}", kCriticalHitFormID, kESPName);
    if (!m_sneakAttackSound) REX::WARN("HitMarkerAudioFeedback: Sneak attack SNDR {:06X} not found in {}", kSneakHitFormID, kESPName);
    if (!m_killSound) REX::WARN("HitMarkerAudioFeedback: Kill SNDR {:06X} not found in {}", kKillFormID, kESPName);
}

bool HitMarkerAudioFeedback::CanPlaySound()
{
    RE::BSAutoLock lock(m_lock);
    auto now = std::chrono::steady_clock::now();
    return (now - m_lastSoundTime) >= COOLDOWN_MS;
}

void HitMarkerAudioFeedback::UpdateCooldown()
{
    RE::BSAutoLock lock(m_lock);
    m_lastSoundTime = std::chrono::steady_clock::now();
}
