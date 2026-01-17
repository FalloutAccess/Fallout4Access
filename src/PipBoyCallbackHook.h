#pragma once

#include "pch.h"
#include "ItemIdentity.h"
#include <RE/P/PipboyMenu.h>
#include <Scaleform/G/GFx_FunctionHandler.h>
#include <REL/Hook.h>
#include <chrono>

// Hooks PipboyMenu's SWFToCodeFunctionHandler::Call to intercept AS-to-C++ callbacks
// This allows us to receive real-time selection updates from the Pip-Boy UI
class PipBoyCallbackHook
{
public:
    static PipBoyCallbackHook* GetSingleton();

    // Install/uninstall the vtable hook
    void Install();
    void Uninstall();

    // Check if hook is currently active
    bool IsInstalled() const { return m_installed; }

    // Reset duplicate announcement tracking (call when menu opens)
    void ResetLastAnnounced()
    {
        m_lastAnnouncedItemName.clear();
        m_lastAnnouncedWeight = 0.0f;
        m_lastAnnouncedValue = 0.0f;
        m_lastAnnouncedQuestIndex = -1;
        m_lastAnnouncedTime = {};
    }

private:
    PipBoyCallbackHook() = default;
    ~PipBoyCallbackHook() = default;
    PipBoyCallbackHook(const PipBoyCallbackHook&) = delete;
    PipBoyCallbackHook(PipBoyCallbackHook&&) = delete;
    PipBoyCallbackHook& operator=(const PipBoyCallbackHook&) = delete;
    PipBoyCallbackHook& operator=(PipBoyCallbackHook&&) = delete;

    // The hooked Call function
    static void HookedCall(RE::PipboyMenu* a_this, const Scaleform::GFx::FunctionHandler::Params& a_params);

    // Callback handlers
    void HandleInvItemSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);
    void HandleQuestSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);
    void HandleWorkshopSelection(const Scaleform::GFx::FunctionHandler::Params& a_params);

    // Extract and announce inventory item using page reference and InfoObj array
    void AnnounceInventoryItem(const Scaleform::GFx::Value& a_pageRef, const Scaleform::GFx::Value& a_infoObj);

    // Extract and announce quest from callback params
    void AnnounceQuest(const Scaleform::GFx::Value& a_questEntry);

    // Extract and announce workshop from callback params
    void AnnounceWorkshop(const Scaleform::GFx::Value& a_workshopEntry);

    // Utility
    void SpeakText(const std::string& a_text);
    std::string CleanLocalizationString(const std::string& a_text);
    std::string TranslateKey(const std::string& a_key);
    std::string WideToNarrow(const std::wstring& a_wide);

    // Function IDs (populated on first hook call by examining known callbacks)
    // These are the integer IDs mapped from AS function names via MapCodeMethodToASFunction
    std::int32_t m_invItemSelectionID = -1;
    std::int32_t m_questSelectionID = -1;
    std::int32_t m_workshopSelectionID = -1;

    // State
    bool m_installed = false;
    std::string m_lastAnnouncedItemName;     // For duplicate detection
    float m_lastAnnouncedWeight = 0.0f;      // For stale InfoObj detection
    float m_lastAnnouncedValue = 0.0f;       // For stale InfoObj detection
    std::int32_t m_lastAnnouncedQuestIndex = -1;
    std::chrono::steady_clock::time_point m_lastAnnouncedTime;  // For time-based dedup

    // The vtable hook
    static inline REL::HookVFT<void(RE::PipboyMenu*, const Scaleform::GFx::FunctionHandler::Params&)>* s_callHook = nullptr;
};
