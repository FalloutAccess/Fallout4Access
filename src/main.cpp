#include "pch.h"
#include "MenuAccessibility.h"
#include <SRAL.h>

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

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

    return true;
}
