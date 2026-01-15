#include "pch.h"
#include "AutoWalk.h"
#include "CrosshairAccessibility.h"
#include "MenuAccessibility.h"
#include "ObjectScanner.h"
#include <SRAL.h>

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
        // Game data is loaded - initialize components that need PlayerCharacter
        AutoWalk::GetSingleton()->Initialize();
        CrosshairAccessibility::GetSingleton()->Register();
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

    // Speak test message
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak("Fallout 4 accessibility plugin loaded successfully", false);
        REX::INFO("Spoke loading message");
    } else {
        REX::WARN("No speech engine available");
    }

    // Register menu accessibility event handler
    MenuAccessibility::GetSingleton()->Register();

    // Register object scanner for gameplay
    ObjectScanner::GetSingleton()->Register();

    // Note: CrosshairAccessibility is registered in OnF4SEMessage when game data is ready
    // because it needs PlayerCharacter to be available

    return true;
}
