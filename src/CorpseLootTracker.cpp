#include "CorpseLootTracker.h"

CorpseLootTracker* CorpseLootTracker::GetSingleton()
{
    static CorpseLootTracker singleton;
    return &singleton;
}

void CorpseLootTracker::MarkLooted(std::uint32_t a_formID)
{
    RE::BSAutoLock lock(m_lock);
    if (m_lootedCorpses.insert(a_formID).second) {
        REX::INFO("CorpseLootTracker: Marked {:08X} as looted", a_formID);
    }
}

bool CorpseLootTracker::IsLooted(std::uint32_t a_formID) const
{
    RE::BSAutoLock lock(m_lock);
    return m_lootedCorpses.contains(a_formID);
}

void CorpseLootTracker::Clear()
{
    RE::BSAutoLock lock(m_lock);
    m_lootedCorpses.clear();
    REX::INFO("CorpseLootTracker: Cleared all looted state");
}

void CorpseLootTracker::Register()
{
    auto* eventSource = RE::TESContainerChangedEvent::GetEventSource();
    if (eventSource) {
        eventSource->RegisterSink(this);
        REX::INFO("CorpseLootTracker: Registered for TESContainerChangedEvent");
    } else {
        REX::ERROR("CorpseLootTracker: Failed to get TESContainerChangedEvent source");
    }
}

RE::BSEventNotifyControl CorpseLootTracker::ProcessEvent(
    const RE::TESContainerChangedEvent& a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*)
{
    // Only care about items moving TO the player
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event.newContainerFormID != player->GetFormID()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Check if the source container is a dead actor (corpse)
    auto* form = RE::TESForm::GetFormByID(a_event.oldContainerFormID);
    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
    if (!ref || !ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = ref->As<RE::Actor>();
    if (actor && actor->IsDead(false)) {
        MarkLooted(a_event.oldContainerFormID);
    }

    return RE::BSEventNotifyControl::kContinue;
}

void CorpseLootTracker::OnSave(const F4SE::SerializationInterface* a_intfc)
{
    auto* tracker = GetSingleton();
    RE::BSAutoLock lock(tracker->m_lock);

    if (!a_intfc->OpenRecord(kRecordType, kRecordVersion)) {
        REX::ERROR("CorpseLootTracker: Failed to open save record");
        return;
    }

    std::uint32_t count = static_cast<std::uint32_t>(tracker->m_lootedCorpses.size());
    a_intfc->WriteRecordData(count);

    for (auto formID : tracker->m_lootedCorpses) {
        a_intfc->WriteRecordData(formID);
    }

    REX::INFO("CorpseLootTracker: Saved {} looted corpses", count);
}

void CorpseLootTracker::OnLoad(const F4SE::SerializationInterface* a_intfc)
{
    auto* tracker = GetSingleton();
    RE::BSAutoLock lock(tracker->m_lock);
    tracker->m_lootedCorpses.clear();

    std::uint32_t type, version, length;
    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type != kRecordType) {
            continue;
        }

        if (version != kRecordVersion) {
            REX::WARN("CorpseLootTracker: Unknown record version {}", version);
            continue;
        }

        std::uint32_t count = 0;
        a_intfc->ReadRecordData(count);

        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t formID = 0;
            a_intfc->ReadRecordData(formID);

            auto resolved = a_intfc->ResolveFormID(formID);
            if (resolved.has_value()) {
                tracker->m_lootedCorpses.insert(resolved.value());
            }
        }

        REX::INFO("CorpseLootTracker: Loaded {} looted corpses ({} resolved)",
            count, tracker->m_lootedCorpses.size());
    }
}

void CorpseLootTracker::OnRevert(const F4SE::SerializationInterface*)
{
    GetSingleton()->Clear();
}
