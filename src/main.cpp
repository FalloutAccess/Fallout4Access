#include "pch.h"
#include "AutoAimAccessibility.h"
#include "AutoWalk.h"
#include "BarterMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "CrosshairAccessibility.h"
#include "DialogueMenuHook.h"
#include "ExamineMenuHook.h"
#include "HUDMessageHook.h"
#include "ItemStatFormatter.h"
#include "LevelUpMenuHook.h"
#include "LoadingMenuHook.h"
#include "MenuAccessibility.h"
#include "MessageBoxMenuHook.h"
#include "ObjectScanner.h"
#include "SpeechManager.h"
#include "VATSCallbackHook.h"
#include "VATSMenuHook.h"
#include <SRAL.h>

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
        // Game data is loaded - initialize components that need PlayerCharacter
        AutoWalk::GetSingleton()->Initialize();
        AutoAimAccessibility::GetSingleton()->Initialize();
        CrosshairAccessibility::GetSingleton()->Register();

        // Load damage type translations
        ItemStatFormatter::LoadTranslations();
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    // Register for F4SE messages (data loaded, etc.)
    auto* messaging = F4SE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(OnF4SEMessage);
    }

    // Initialize SRAL, excluding SAPI to prefer NVDA/JAWS
    REX::INFO("Initializing SRAL...");
    if (SRAL_Initialize(SRAL_ENGINE_SAPI)) {
        REX::INFO("SRAL initialized successfully");
    } else {
        REX::ERROR("SRAL initialization failed");
    }

    // Initialize speech manager (creates dedicated speech log)
    SpeechManager::GetSingleton()->Initialize();

    // Speak test message via centralized manager
    if (SRAL_GetCurrentEngine() != 0) {
        SpeechManager::GetSingleton()->Speak("Fallout 4 accessibility plugin loaded successfully");
    } else {
        REX::WARN("No speech engine available");
    }

    // Register menu accessibility event handler
    MenuAccessibility::GetSingleton()->Register();

    // Register object scanner for gameplay
    ObjectScanner::GetSingleton()->Register();

    // Install HUD message hook for item pickups, location discoveries, etc.
    HUDMessageHook::GetSingleton()->Install();

    // Install ExamineMenu hook for workbench accessibility
    ExamineMenuHook::GetSingleton()->Install();

    // Install VATSMenu hooks for VATS accessibility
    VATSMenuHook::GetSingleton()->Install();
    VATSCallbackHook::GetSingleton()->Install();

    // Install ContainerMenu hook for looting accessibility
    ContainerMenuAccessibility::GetSingleton()->Install();

    // Install BarterMenu hook for trading accessibility
    BarterMenuAccessibility::GetSingleton()->Install();

    // Install LevelUpMenu hook for perk selection accessibility
    LevelUpMenuHook::GetSingleton()->Install();

    // Install MessageBoxMenu hook for dialog accessibility
    MessageBoxMenuHook::GetSingleton()->Install();

    // Install DialogueMenu hook for NPC conversation accessibility
    DialogueMenuHook::GetSingleton()->Install();

    // Install LoadingMenu hook for loading screen accessibility
    LoadingMenuHook::GetSingleton()->Install();

    // Note: CrosshairAccessibility is registered in OnF4SEMessage when game data is ready
    // because it needs PlayerCharacter to be available

    return true;
}
