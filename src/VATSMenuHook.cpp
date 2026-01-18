#include "VATSMenuHook.h"
#include "SpeechManager.h"
#include "VATSMenuAccessibility.h"
#include <SRAL.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerCamera.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/V/VATS.h>
#include <RE/C/Console.h>

VATSMenuHook* VATSMenuHook::GetSingleton()
{
    static VATSMenuHook singleton;
    return &singleton;
}

void VATSMenuHook::Install()
{
    if (m_installed) {
        REX::DEBUG("VATSMenuHook: Already installed");
        return;
    }

    // Hook VATSMenu::AdvanceMovie (vtable index 4)
    // VTABLE::VATSMenu[0] is the primary vtable (IMenu/GameMenuBase)
    static REL::HookVFT<void(RE::IMenu*, float, std::uint64_t)> hook(
        VTABLE::VATSMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("VATSMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("VATSMenuHook: Failed to install AdvanceMovie hook");
    }
}

void VATSMenuHook::ResetState()
{
    m_lastTargetName.clear();
    m_lastBodyPart = 0xFFFFFFFF;
    m_frameDelay = FRAME_DELAY_AFTER_OPEN;
}

void VATSMenuHook::HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
    // Call original function first to let the game update state
    if (s_advanceMovieHook) {
        (*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
    }

    // Then check for state changes
    auto* hook = GetSingleton();
    if (hook->m_menuOpen) {
        hook->CheckStateChanges(a_this);
    }
}

void VATSMenuHook::CheckStateChanges(RE::IMenu* a_menu)
{
    if (!a_menu) return;

    // Wait for frame delay after menu open
    if (m_frameDelay > 0) {
        m_frameDelay--;
        return;
    }

    // Check target changes (most important)
    CheckTargetChange(a_menu);

    // Check body part selection changes
    CheckBodyPartChange(a_menu);
}

void VATSMenuHook::CheckTargetChange(RE::IMenu* a_menu)
{
    std::string currentTarget = GetCurrentTargetName();

    if (!currentTarget.empty() && currentTarget != m_lastTargetName) {
        m_lastTargetName = currentTarget;
        m_lastBodyPart = 0xFFFFFFFF;  // Reset body part on target change

        // Get target stats
        float healthPercent = GetTargetHealthPercent();
        float distance = GetTargetDistance();
        std::string level = GetTargetLevel(a_menu);

        // Build announcement
        std::string announcement = currentTarget;

        // Add level
        if (!level.empty() && level != "0") {
            announcement += ", Level " + level;
        }

        // Add health percentage
        if (healthPercent > 0.0f) {
            announcement += ", " + std::to_string(static_cast<int>(healthPercent)) + " percent health";
        }

        // Add distance in meters
        if (distance > 0.0f) {
            char distBuf[32];
            snprintf(distBuf, sizeof(distBuf), "%.1f", distance);
            announcement += ", " + std::string(distBuf) + " meters";
        }

        // Speak target announcement
        SpeechManager::GetSingleton()->Speak(announcement);

        // Also announce current body part
        std::uint32_t currentPart = GetCurrentBodyPart(a_menu);
        if (currentPart != 0xFFFFFFFF) {
            m_lastBodyPart = currentPart;
            VATSMenuAccessibility::GetSingleton()->AnnounceBodyPartChange(currentPart);
        }
    }
}

void VATSMenuHook::CheckBodyPartChange(RE::IMenu* a_menu)
{
    std::uint32_t currentPart = GetCurrentBodyPart(a_menu);

    if (currentPart != 0xFFFFFFFF && currentPart != m_lastBodyPart) {
        m_lastBodyPart = currentPart;
        VATSMenuAccessibility::GetSingleton()->AnnounceBodyPartChange(currentPart);
    }
}

std::string VATSMenuHook::GetCurrentTargetName()
{
    // Try multiple sources for the current VATS target
    // Note: cameraTarget points to the PLAYER in VATS, so we skip it here

    auto* player = RE::PlayerCharacter::GetSingleton();

    // Source 1: PlayerCharacter::commandTarget (updates when cycling targets with A/D)
    if (player && player->commandTarget) {
        // Make sure it's not the player
        if (player->commandTarget != player) {
            const char* name = player->commandTarget->GetDisplayFullName();
            if (name && name[0] != '\0') {
                return std::string(name);
            }
        }
    }

    // Source 2: VATS::VATSTarget (VATS-specific)
    auto* vats = RE::VATS::GetSingleton();
    if (vats && vats->VATSTarget) {
        if (vats->VATSTarget.get() != player) {
            const char* name = vats->VATSTarget->GetDisplayFullName();
            if (name && name[0] != '\0') {
                return std::string(name);
            }
        }
    }

    // Source 3: Console pick ref
    auto pickRefHandle = RE::Console::GetPickRef();
    if (pickRefHandle) {
        auto* pickRef = pickRefHandle.get().get();
        if (pickRef && pickRef != player) {
            const char* name = pickRef->GetDisplayFullName();
            if (name && name[0] != '\0') {
                return std::string(name);
            }
        }
    }

    // Skip cameraTarget - it points to the PLAYER in VATS mode!

    return "";
}

std::uint32_t VATSMenuHook::GetCurrentBodyPart(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) return 0xFFFFFFFF;

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, SELECTED_PART_PATH)) {
        if (value.IsUInt()) {
            return value.GetUInt();
        } else if (value.IsInt()) {
            auto intVal = value.GetInt();
            if (intVal >= 0) {
                return static_cast<std::uint32_t>(intVal);
            }
        } else if (value.IsNumber()) {
            auto numVal = value.GetNumber();
            if (numVal >= 0) {
                return static_cast<std::uint32_t>(numVal);
            }
        }
    }

    return 0xFFFFFFFF;
}

RE::Actor* VATSMenuHook::GetTargetActor()
{
    // Try multiple sources for the current VATS target
    // Note: cameraTarget points to the PLAYER in VATS, so we skip it here

    auto* player = RE::PlayerCharacter::GetSingleton();

    // Source 1: PlayerCharacter::commandTarget (best source for VATS)
    if (player && player->commandTarget) {
        auto* actor = player->commandTarget->As<RE::Actor>();
        if (actor && actor != player) {  // Make sure it's not the player
            REX::DEBUG("VATSMenuHook: Got target actor from commandTarget");
            return actor;
        }
    }

    // Source 2: VATS::VATSTarget (VATS-specific, should be the enemy)
    auto* vats = RE::VATS::GetSingleton();
    if (vats && vats->VATSTarget) {
        auto* actor = vats->VATSTarget->As<RE::Actor>();
        if (actor && actor != player) {
            REX::DEBUG("VATSMenuHook: Got target actor from VATS::VATSTarget");
            return actor;
        }
    }

    // Source 3: Console pick ref
    auto pickRefHandle = RE::Console::GetPickRef();
    if (pickRefHandle) {
        auto* pickRef = pickRefHandle.get().get();
        if (pickRef) {
            auto* actor = pickRef->As<RE::Actor>();
            if (actor && actor != player) {
                REX::DEBUG("VATSMenuHook: Got target actor from Console pickRef");
                return actor;
            }
        }
    }

    // Skip cameraTarget - it points to the PLAYER in VATS mode!

    REX::DEBUG("VATSMenuHook: No target actor found (excluding player)");
    return nullptr;
}

float VATSMenuHook::GetTargetHealthPercent()
{
    auto* actor = GetTargetActor();
    if (!actor) return 0.0f;

    // Get health values
    auto* avSingleton = RE::ActorValue::GetSingleton();
    if (!avSingleton || !avSingleton->health) {
        REX::DEBUG("VATSMenuHook: ActorValue singleton or health not available");
        return 0.0f;
    }

    float currentHealth = actor->GetActorValue(*avSingleton->health);
    float maxHealth = actor->GetBaseActorValue(*avSingleton->health);

    REX::DEBUG("VATSMenuHook: Health current={}, max={}", currentHealth, maxHealth);

    if (maxHealth <= 0.0f) return 0.0f;

    return (currentHealth / maxHealth) * 100.0f;
}

float VATSMenuHook::GetTargetDistance()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return 0.0f;

    auto* actor = GetTargetActor();
    if (!actor) return 0.0f;

    RE::NiPoint3 playerPos = player->GetPosition();
    RE::NiPoint3 targetPos = actor->GetPosition();

    // GetDistance returns game units, convert to meters (1 meter ≈ 70 units)
    float distanceUnits = playerPos.GetDistance(targetPos);
    REX::DEBUG("VATSMenuHook: Distance {} units = {} meters", distanceUnits, distanceUnits / 70.0f);
    return distanceUnits / 70.0f;
}

std::string VATSMenuHook::GetTargetLevel(RE::IMenu* a_menu)
{
    // Try Scaleform UI first (visible with Awareness perk)
    if (a_menu && a_menu->uiMovie) {
        Scaleform::GFx::Value levelValue;
        if (a_menu->uiMovie->GetVariable(&levelValue, TARGET_LEVEL_PATH)) {
            if (levelValue.IsString()) {
                std::string level = levelValue.GetString();
                if (!level.empty() && level != "0") {
                    return level;
                }
            }
        }
    }

    // Fallback: Get from actor directly
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player && player->commandTarget) {
        auto* actor = player->commandTarget->As<RE::Actor>();
        if (actor) {
            return std::to_string(actor->GetLevel());
        }
    }

    // Fallback: PlayerCamera::cameraTarget
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (camera) {
        auto targetActor = camera->cameraTarget.get();
        if (targetActor) {
            return std::to_string(targetActor->GetLevel());
        }
    }

    return "";
}
