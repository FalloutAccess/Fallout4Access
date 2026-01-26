#include "AutoAimAccessibility.h"
#include "SpeechManager.h"
#include <RE/P/ProcessLists.h>
#include <cmath>

static AutoAimAccessibility* s_singleton = nullptr;

AutoAimAccessibility* AutoAimAccessibility::GetSingleton()
{
    if (!s_singleton) {
        s_singleton = new AutoAimAccessibility();
    }
    return s_singleton;
}

void AutoAimAccessibility::Initialize()
{
    if (m_initialized) return;

    m_initialized = true;
    REX::INFO("AutoAimAccessibility: Initialized");
}

void AutoAimAccessibility::Update()
{
    if (!m_enabled || !m_initialized) return;

    // Skip if in menu mode
    auto* ui = RE::UI::GetSingleton();
    if (ui && ui->menuMode) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Get current gun state from player's ActorState
    RE::GUN_STATE currentGunState = player->gunState;

    // Check for transition from non-sighted to sighted state
    bool wasSighted = (m_lastGunState == RE::GUN_STATE::kSighted ||
                       m_lastGunState == RE::GUN_STATE::kFireSighted);
    bool isSighted = (currentGunState == RE::GUN_STATE::kSighted ||
                      currentGunState == RE::GUN_STATE::kFireSighted);

    // Just entered ironsights
    if (!wasSighted && isSighted) {
        OnEnterIronsights();
    }
    // Still in ironsights - continue tracking
    else if (isSighted && m_isTracking) {
        UpdateTracking();
    }
    // Just exited ironsights
    else if (wasSighted && !isSighted) {
        StopTracking();
    }

    m_lastGunState = currentGunState;
}

void AutoAimAccessibility::OnEnterIronsights()
{
    RE::Actor* nearestHostile = FindNearestHostileActor();

    if (nearestHostile) {
        LockTarget(nearestHostile);
        RotateToTarget(nearestHostile);
        AnnounceTarget(nearestHostile);
        m_isTracking = true;
        m_trackingFrameCount = 0;
    }
    // If no enemies found, remain silent per plan
}

RE::Actor* AutoAimAccessibility::FindNearestHostileActor()
{
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return nullptr;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nullptr;

    // Get hostile actors from ProcessLists
    RE::BSScrapArray<RE::ActorHandle> hostileActors;
    bool hasHostiles = processLists->AreHostileActorsNear(&hostileActors);

    if (!hasHostiles || hostileActors.empty()) {
        return nullptr;
    }

    RE::Actor* nearestActor = nullptr;
    float nearestDistance = MAX_TARGET_DISTANCE;

    for (const auto& handle : hostileActors) {
        if (!handle) continue;

        auto actorPtr = handle.get();
        if (!actorPtr) continue;

        RE::Actor* actor = actorPtr.get();
        if (!IsValidTarget(actor)) continue;

        float distance = CalculateDistanceToActor(actor);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestActor = actor;
        }
    }

    return nearestActor;
}

bool AutoAimAccessibility::IsValidTarget(RE::Actor* a_actor)
{
    if (!a_actor) return false;
    if (a_actor->IsDeleted() || a_actor->IsDisabled()) return false;
    if (a_actor->IsDead(false)) return false;

    // Make sure it's not the player
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (a_actor == player) return false;

    return true;
}

float AutoAimAccessibility::CalculateDistanceToActor(RE::Actor* a_actor)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !a_actor) return MAX_TARGET_DISTANCE + 1.0f;

    return player->GetPosition().GetDistance(a_actor->GetPosition());
}

void AutoAimAccessibility::RotateToTarget(RE::Actor* a_target)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !a_target) return;

    // Get player eye position (aiming origin)
    RE::NiPoint3 eyePos, eyeDir;
    player->GetEyeVector(eyePos, eyeDir, true);

    // Get target center (not feet position)
    RE::NiPoint3 targetCenter;
    a_target->GetObjectCenter(targetCenter);

    // Calculate direction from eye to target center
    RE::NiPoint3 direction = targetCenter - eyePos;

    // Calculate yaw (horizontal rotation) - atan2(x, y) gives angle from north
    float yaw = std::atan2(direction.x, direction.y);

    // Calculate pitch (vertical rotation)
    float horizontalDist = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    float pitch = -std::atan2(direction.z, horizontalDist);

    // Apply rotation: SetAngleOnReference takes (pitch, roll, yaw)
    player->SetAngleOnReference(RE::NiPoint3(pitch, 0.0f, yaw));

    REX::INFO("AutoAimAccessibility: Rotated to target '{}' (yaw={:.2f}, pitch={:.2f})",
              a_target->GetDisplayFullName(), yaw, pitch);
}

void AutoAimAccessibility::AnnounceTarget(RE::Actor* a_target)
{
    if (!a_target) return;

    const char* name = a_target->GetDisplayFullName();
    if (!name || name[0] == '\0') {
        name = "Enemy";
    }

    float distance = CalculateDistanceToActor(a_target);
    int distInt = static_cast<int>(distance);

    std::string announcement = std::string(name) + ", " + std::to_string(distInt) + " units";
    SpeechManager::GetSingleton()->Speak(announcement);
}

void AutoAimAccessibility::LockTarget(RE::Actor* a_target)
{
    if (!a_target) return;
    m_lockedTargetHandle = RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(a_target);
}

void AutoAimAccessibility::StopTracking()
{
    m_lockedTargetHandle = RE::ActorHandle();
    m_isTracking = false;
    m_trackingFrameCount = 0;
}

bool AutoAimAccessibility::IsLockedTargetValid()
{
    auto ptr = m_lockedTargetHandle.get();
    if (!ptr) return false;
    RE::Actor* actor = ptr.get();
    return IsValidTarget(actor);
}

void AutoAimAccessibility::UpdateTracking()
{
    if (!m_isTracking) return;

    // Rate limit: only update every TRACK_UPDATE_INTERVAL frames
    m_trackingFrameCount++;
    if (m_trackingFrameCount % TRACK_UPDATE_INTERVAL != 0) {
        return;
    }

    // Check if target still valid
    if (!IsLockedTargetValid()) {
        SpeechManager::GetSingleton()->Speak("Target lost");
        StopTracking();
        return;
    }

    // Re-aim at target (no announcement - silent tracking)
    auto ptr = m_lockedTargetHandle.get();
    RE::Actor* target = ptr.get();
    RotateToTarget(target);
}
