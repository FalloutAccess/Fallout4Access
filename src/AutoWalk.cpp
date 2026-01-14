#include "AutoWalk.h"
#include <SRAL.h>
#include <cmath>
#include <Windows.h>

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

    // Find the GlobalVariables from fallout4access.esp
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        REX::WARN("AutoWalk: TESDataHandler not available");
        return;
    }

    // Try to find our globals by EditorID
    for (auto* global : dataHandler->GetFormArray<RE::TESGlobal>()) {
        if (!global) continue;

        const char* editorID = global->GetFormEditorID();
        if (!editorID) continue;

        if (strcmp(editorID, "F4A_TargetFormID") == 0) {
            m_globalTargetFormID = global;
            REX::INFO("AutoWalk: Found F4A_TargetFormID global");
        }
        else if (strcmp(editorID, "F4A_StopDistance") == 0) {
            m_globalStopDistance = global;
            REX::INFO("AutoWalk: Found F4A_StopDistance global");
        }
    }

    if (!m_globalTargetFormID || !m_globalStopDistance) {
        REX::WARN("AutoWalk: fallout4access.esp not loaded or missing GlobalVariables");
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

    // Set the stop distance global
    m_globalStopDistance->value = a_stopDistance;

    // Set the target FormID global - Papyrus will detect this and start walking
    m_globalTargetFormID->value = static_cast<float>(a_target->GetFormID());

    REX::INFO("AutoWalk: Set target FormID {} for {}, distance: {}",
              a_target->GetFormID(), a_target->GetDisplayFullName(), distance);
}

void AutoWalk::Stop()
{
    if (!m_targetRef) return;

    if (m_initialized && m_globalTargetFormID) {
        // Send stop signal (-1) - Papyrus will detect this and call StopWalking()
        m_globalTargetFormID->value = -1.0f;
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
        SRAL_Speak("Stopped", false);
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
