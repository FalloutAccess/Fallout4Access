#include "AutoWalk.h"
#include "SpeechManager.h"
#include <Windows.h>
#undef ERROR

static AutoWalk* s_singleton = nullptr;

AutoWalk* AutoWalk::GetSingleton()
{
    if (!s_singleton) {
        s_singleton = new AutoWalk();
    }
    return s_singleton;
}

void AutoWalk::Initialize()
{
    if (m_initialized) return;

    // Find the quest by EditorID
    m_quest = RE::TESForm::GetFormByEditorID<RE::TESQuest>("F4A_AutoWalkQuest");
    if (!m_quest) {
        REX::WARN("AutoWalk: F4A_AutoWalkQuest not found");
        REX::WARN("AutoWalk: Auto-walk feature disabled. Install fallout4access.esp to enable.");
        return;
    }

    m_initialized = true;
    REX::INFO("AutoWalk: Initialized successfully");
}

void AutoWalk::WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance,
                      std::function<void()> a_onArrival)
{
    if (!m_initialized) {
        REX::WARN("AutoWalk: Not initialized (ESP not loaded?)");
        return;
    }

    if (!a_target || a_target->IsDeleted() || a_target->IsDisabled()) {
        REX::WARN("AutoWalk: Invalid target");
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    RE::NiPoint3 targetPos = a_target->GetPosition();
    RE::NiPoint3 playerPos = player->GetPosition();
    float distance = playerPos.GetDistance(targetPos);

    if (distance <= a_stopDistance) {
        // Already close enough
        REX::INFO("AutoWalk: Already within {} units", a_stopDistance);
        if (a_onArrival) a_onArrival();
        return;
    }

    // Store state for arrival callback
    m_targetRef = a_target;
    m_stopDistance = a_stopDistance;
    m_onArrival = a_onArrival;

    // Dispatch to Papyrus with FormID as int32 (no float precision loss)
    auto* gameVM = RE::GameVM::GetSingleton();
    if (!gameVM) return;
    auto vm = gameVM->GetVM();
    if (!vm) return;

    auto& handlePolicy = vm->GetObjectHandlePolicy();
    auto handle = handlePolicy.GetHandleForObject(
        RE::BSScript::GetVMTypeID<RE::TESQuest>(),
        m_quest);

    if (handle == handlePolicy.EmptyHandle()) {
        REX::ERROR("AutoWalk: Failed to get quest VM handle");
        m_targetRef = nullptr;
        m_onArrival = nullptr;
        return;
    }

    bool dispatched = vm->DispatchMethodCall(
        handle,
        RE::BSFixedString("F4A:AutoWalk"),
        RE::BSFixedString("OnWalkToTarget"),
        nullptr,
        static_cast<std::int32_t>(a_target->GetFormID()),
        a_stopDistance);

    if (!dispatched) {
        REX::ERROR("AutoWalk: DispatchMethodCall OnWalkToTarget failed");
        m_targetRef = nullptr;
        m_onArrival = nullptr;
        return;
    }

    const char* name = a_target->GetDisplayFullName();
    REX::INFO("AutoWalk: Dispatched walk to FormID {:08X} for {}, distance: {}",
              a_target->GetFormID(), name ? name : "<unnamed>", distance);
}

void AutoWalk::Stop()
{
    if (!m_targetRef) return;

    if (m_initialized && m_quest) {
        auto* gameVM = RE::GameVM::GetSingleton();
        if (gameVM) {
            auto vm = gameVM->GetVM();
            if (vm) {
                auto& handlePolicy = vm->GetObjectHandlePolicy();
                auto handle = handlePolicy.GetHandleForObject(
                    RE::BSScript::GetVMTypeID<RE::TESQuest>(),
                    m_quest);

                if (handle != handlePolicy.EmptyHandle()) {
                    vm->DispatchMethodCall(
                        handle,
                        RE::BSFixedString("F4A:AutoWalk"),
                        RE::BSFixedString("OnStopWalking"),
                        nullptr);
                }
            }
        }
    }

    m_targetRef = nullptr;
    m_onArrival = nullptr;

    REX::INFO("AutoWalk: Stopped");
}

void AutoWalk::Update()
{
    // Skip if no target
    if (!m_targetRef) return;

    // Check for cancel keys (WASD, Escape, Space) using key state polling
    // This works even when AI-driven movement blocks normal input events
    // Virtual key codes: W=0x57, A=0x41, S=0x53, D=0x44, Escape=0x1B, Space=0x20
    if (GetAsyncKeyState(0x57) & 0x8000 ||  // W
        GetAsyncKeyState(0x41) & 0x8000 ||  // A
        GetAsyncKeyState(0x53) & 0x8000 ||  // S
        GetAsyncKeyState(0x44) & 0x8000 ||  // D
        GetAsyncKeyState(0x1B) & 0x8000 ||  // Escape
        GetAsyncKeyState(0x20) & 0x8000) {  // Space
        REX::INFO("AutoWalk: Cancel key detected, stopping");
        SpeechManager::GetSingleton()->Speak("Stopped");
        Stop();
        return;
    }

    // Check if target is still valid
    if (m_targetRef->IsDeleted() || m_targetRef->IsDisabled()) {
        REX::WARN("AutoWalk: Target became invalid");
        Stop();
        return;
    }

    // Check arrival (Papyrus also checks, but we need to call our callback)
    CheckArrival();
}

void AutoWalk::CheckArrival()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !m_targetRef) return;

    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 targetPos = m_targetRef->GetPosition();
    float distance = playerPos.GetDistance(targetPos);

    if (distance <= m_stopDistance + ARRIVAL_TOLERANCE) {
        REX::INFO("AutoWalk: Arrived (distance: {})", distance);

        auto callback = m_onArrival;  // Copy before clearing state

        // Clear our state (Papyrus handles its own cleanup)
        m_targetRef = nullptr;
        m_onArrival = nullptr;

        if (callback) {
            callback();
        }
    }
}
