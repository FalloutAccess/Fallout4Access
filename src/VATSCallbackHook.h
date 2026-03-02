#pragma once

#include "pch.h"
#include <Scaleform/G/GFx_FunctionHandler.h>
#include <chrono>

// Hooks VATSMenu's SWFToCodeFunctionHandler::Call to intercept AS-to-C++ callbacks
// Specifically targets CycleTarget callback fired when A/D keys cycle targets
class VATSCallbackHook
{
public:
    static VATSCallbackHook* GetSingleton();

    // Install/uninstall the vtable hook
    void Install();
    void Uninstall();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset duplicate announcement tracking (call when menu opens)
    void ResetState()
    {
        m_lastAnnouncedTargetName.clear();
        m_lastAnnouncedTime = {};
    }

private:
    VATSCallbackHook() = default;
    ~VATSCallbackHook() = default;
    VATSCallbackHook(const VATSCallbackHook&) = delete;
    VATSCallbackHook(VATSCallbackHook&&) = delete;
    VATSCallbackHook& operator=(const VATSCallbackHook&) = delete;
    VATSCallbackHook& operator=(VATSCallbackHook&&) = delete;

    // The hooked Call function (uses RE::IMenu* since VATSMenu has no dedicated header)
    static void HookedCall(RE::IMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params);

    // Callback handler - called after original when CycleTarget detected
    void HandlePotentialTargetChange();

    // Build and speak target announcement
    void AnnounceCurrentTarget(RE::IMenu* a_menu);

    // Function ID for CycleTarget callback (discovered at runtime)
    // Set to -1 initially, populated when callback signature matches
    std::int32_t m_cycleTargetID = -1;

    // State
    bool m_installed = false;
    std::string m_lastAnnouncedTargetName;  // For duplicate detection
    std::chrono::steady_clock::time_point m_lastAnnouncedTime;  // For time-based dedup

    // The vtable hook (uses RE::IMenu* since VATSMenu has no dedicated header)
    static inline REL::HookVFT<void(RE::IMenu*, const Scaleform::GFx::FunctionHandler::Params&)>* s_callHook = nullptr;
};
