#include "AutoWalk.h"
#include <RE/C/COMMAND_TYPE.h>
#include <SRAL.h>
#include <cmath>

static AutoWalk* s_singleton = nullptr;

AutoWalk* AutoWalk::GetSingleton()
{
    if (!s_singleton) {
        s_singleton = new AutoWalk();
    }
    return s_singleton;
}

void AutoWalk::WalkTo(RE::TESObjectREFR* a_target, float a_stopDistance,
                      std::function<void()> a_onArrival)
{
    if (!a_target || a_target->IsDeleted() || a_target->IsDisabled()) {
        REX::WARN("AutoWalk: Invalid target");
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Check if player has AI process
    if (!player->currentProcess || !player->currentProcess->high) {
        REX::WARN("AutoWalk: Player has no AI process");
        return;
    }

    m_targetPos = a_target->GetPosition();
    RE::NiPoint3 playerPos = player->GetPosition();

    float distance = playerPos.GetDistance(m_targetPos);

    if (distance <= a_stopDistance) {
        // Already close enough
        REX::INFO("AutoWalk: Already within {} units, calling arrival callback", a_stopDistance);
        if (a_onArrival) a_onArrival();
        return;
    }

    // Store state
    m_isWalking = true;
    m_targetRef = a_target;
    m_stopDistance = a_stopDistance;
    m_onArrival = a_onArrival;

    // Enable AI-driven movement (equivalent to Game.SetPlayerAIDriven(true))
    player->SetAIControlledPackage(true);
    player->aiControlledToPos = true;

    // Set the pathing destination using queuedTargetLoc
    player->queuedTargetLoc.location = m_targetPos;
    player->queuedTargetLoc.world = nullptr;  // Same worldspace
    player->queuedTargetLoc.interior = player->GetParentCell();
    player->queuedTargetLoc.isValid = true;

    // Also set in HighProcessData for pathing
    auto* highData = player->currentProcess->high;
    highData->pathingDesiredPosition = m_targetPos;
    highData->inCommandState = true;
    highData->doingCommand = true;
    highData->commandType = RE::COMMAND_TYPE::kMove;

    REX::INFO("AutoWalk: AI walking to {} (distance: {}, stop at: {})",
              a_target->GetDisplayFullName(), distance, a_stopDistance);
    REX::INFO("AutoWalk: Set queuedTargetLoc.isValid={}, commandType=kMove",
              player->queuedTargetLoc.isValid);
}

void AutoWalk::Stop()
{
    if (!m_isWalking) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        // Clear command state
        if (player->currentProcess && player->currentProcess->high) {
            auto* highData = player->currentProcess->high;
            highData->inCommandState = false;
            highData->doingCommand = false;
            highData->commandType = RE::COMMAND_TYPE::kNone;
        }

        // Clear queuedTargetLoc
        player->queuedTargetLoc.isValid = false;

        // Restore player control (equivalent to Game.SetPlayerAIDriven(false))
        player->SetAIControlledPackage(false);
        player->aiControlledToPos = false;

        // Cancel any active package
        player->EndInterruptPackage(false);
    }

    m_isWalking = false;
    m_targetRef = nullptr;
    m_onArrival = nullptr;

    REX::INFO("AutoWalk: Stopped");
}

void AutoWalk::Update()
{
    if (!m_isWalking) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        Stop();
        return;
    }

    // Check if target is still valid
    if (!m_targetRef || m_targetRef->IsDeleted() || m_targetRef->IsDisabled()) {
        REX::WARN("AutoWalk: Target became invalid, stopping");
        Stop();
        return;
    }

    // Calculate distance to target using cached position
    RE::NiPoint3 playerPos = player->GetPosition();
    float distance = playerPos.GetDistance(m_targetPos);

    // Check arrival
    if (distance <= m_stopDistance + ARRIVAL_TOLERANCE) {
        OnArrived();
        return;
    }

    // AI handles movement automatically - no manual intervention needed
}

void AutoWalk::OnArrived()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        // Clear command state
        if (player->currentProcess && player->currentProcess->high) {
            auto* highData = player->currentProcess->high;
            highData->inCommandState = false;
            highData->doingCommand = false;
            highData->commandType = RE::COMMAND_TYPE::kNone;
        }

        // Clear queuedTargetLoc
        player->queuedTargetLoc.isValid = false;

        // Restore player control
        player->SetAIControlledPackage(false);
        player->aiControlledToPos = false;
    }

    auto callback = m_onArrival;  // Copy before clearing state

    m_isWalking = false;
    m_targetRef = nullptr;
    m_onArrival = nullptr;

    REX::INFO("AutoWalk: Arrived");

    if (callback) {
        callback();
    }
}
