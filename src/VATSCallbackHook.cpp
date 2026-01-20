#include "VATSCallbackHook.h"
#include "VATSMenuHook.h"
#include "SpeechManager.h"
#include <RE/U/UI.h>

VATSCallbackHook* VATSCallbackHook::GetSingleton()
{
    static VATSCallbackHook singleton;
    return &singleton;
}

void VATSCallbackHook::Install()
{
    if (m_installed) {
        REX::DEBUG("VATSCallbackHook: Already installed");
        return;
    }

    // Hook VATSMenu's Call function (vtable index 1, inherited from SWFToCodeFunctionHandler)
    // VTABLE::VATSMenu[0] is the primary vtable (SWFToCodeFunctionHandler/IMenu vtable)
    // Index 1 is the Call virtual function
    static REL::HookVFT<void(RE::IMenu*, const Scaleform::GFx::FunctionHandler::Params&)> hook(
        VTABLE::VATSMenu[0],  // First vtable entry
        1,                     // Call is at index 1
        HookedCall
    );

    if (hook.Enable()) {
        s_callHook = &hook;
        m_installed = true;
        REX::INFO("VATSCallbackHook: Hook installed successfully");
    } else {
        REX::ERROR("VATSCallbackHook: Failed to install hook");
    }
}

void VATSCallbackHook::Uninstall()
{
    if (!m_installed || !s_callHook) {
        return;
    }

    if (s_callHook->Disable()) {
        m_installed = false;
        REX::INFO("VATSCallbackHook: Hook uninstalled");
    }
}

void VATSCallbackHook::HookedCall(RE::IMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params)
{
    auto* hook = GetSingleton();

    // Call original function FIRST to let game update state
    if (s_callHook) {
        (*s_callHook)(a_this, a_params);
    }

    // AFTER original: Check for target change on any callback
    // This is a discovery phase - we check after every callback to see which one causes target changes
    // Later we can optimize to only check on specific funcIDs once we identify CycleTarget
    hook->HandlePotentialTargetChange();
}

void VATSCallbackHook::HandlePotentialTargetChange()
{
    // Get current target name using VATSMenuHook's method
    std::string currentTarget = VATSMenuHook::GetSingleton()->GetCurrentTargetName();

    if (currentTarget.empty()) {
        return;
    }

    // Only announce if target actually changed (different name)
    if (currentTarget == m_lastAnnouncedTargetName) {
        // Same target - never re-announce
        return;
    }

    // NEW target detected - check time-based dedup to prevent rapid-fire
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAnnouncedTime);
    if (elapsed.count() < 200) {
        // Within 200ms of last announcement, skip to prevent spam
        return;
    }

    // Target changed and outside dedup window - announce it
    m_lastAnnouncedTargetName = currentTarget;
    m_lastAnnouncedTime = now;

    // Get menu for level extraction
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("VATSMenu"sv);
    if (!menu) return;

    AnnounceCurrentTarget(menu.get());
}

void VATSCallbackHook::AnnounceCurrentTarget([[maybe_unused]] RE::IMenu* a_menu)
{
    auto* vatsHook = VATSMenuHook::GetSingleton();

    // Reuse all extraction logic from VATSMenuHook
    std::string targetName = vatsHook->GetCurrentTargetName();
    if (targetName.empty()) return;

    float healthPercent = vatsHook->GetTargetHealthPercent();
    bool isLegendary = vatsHook->IsTargetLegendary();
    bool isDangerous = vatsHook->IsTargetDangerous();

    // Build announcement (same format as VATSMenuHook::CheckTargetChange)
    std::string announcement = targetName;

    // Add legendary status
    if (isLegendary) {
        announcement += ", Legendary";
    }

    // Add dangerous status (skull icon - much higher level)
    if (isDangerous) {
        announcement += ", Dangerous";
    }

    // Add health percentage
    if (healthPercent > 0.0f) {
        announcement += ", " + std::to_string(static_cast<int>(healthPercent)) + " percent health";
    }

    SpeechManager::GetSingleton()->Speak(announcement);
}
