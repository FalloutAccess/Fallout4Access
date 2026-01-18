#include "TerminalMenuHook.h"
#include "SpeechManager.h"
#include <SRAL.h>

TerminalMenuHook* TerminalMenuHook::GetSingleton()
{
    static TerminalMenuHook singleton;
    return &singleton;
}

void TerminalMenuHook::Install()
{
    if (m_installed) {
        REX::DEBUG("TerminalMenuHook: Already installed");
        return;
    }

    // Hook TerminalMenu::AdvanceMovie (vtable index 4)
    // VTABLE::TerminalMenu[0] is the primary vtable (IMenu/GameMenuBase)
    static REL::HookVFT<void(RE::TerminalMenu*, float, std::uint64_t)> hook(
        VTABLE::TerminalMenu[0],
        4,  // AdvanceMovie is at index 4
        HookedAdvanceMovie
    );

    if (hook.Enable()) {
        s_advanceMovieHook = &hook;
        m_installed = true;
        REX::INFO("TerminalMenuHook: AdvanceMovie hook installed successfully");
    } else {
        REX::ERROR("TerminalMenuHook: Failed to install AdvanceMovie hook");
    }
}

void TerminalMenuHook::Uninstall()
{
    if (!m_installed || !s_advanceMovieHook) {
        return;
    }

    if (s_advanceMovieHook->Disable()) {
        m_installed = false;
        REX::INFO("TerminalMenuHook: Hook uninstalled");
    }
}

void TerminalMenuHook::ResetState()
{
    m_lastMode = RE::TerminalMenu::Mode::kInit;
    m_lastSelection = -1;
    m_lastDisplayText.clear();
}

void TerminalMenuHook::HookedAdvanceMovie(RE::TerminalMenu* a_this, float a_timeDelta, std::uint64_t a_time)
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

void TerminalMenuHook::CheckStateChanges(RE::TerminalMenu* a_menu)
{
    if (!a_menu) return;

    // Get current mode
    auto currentMode = a_menu->mode.get();

    // Check for mode change
    if (currentMode != m_lastMode) {
        REX::INFO("TerminalMenuHook: Mode changed {} -> {}",
            static_cast<int>(m_lastMode), static_cast<int>(currentMode));

        m_lastMode = currentMode;
        m_lastSelection = -1;  // Reset selection on mode change
        m_lastDisplayText.clear();

        AnnounceMode(currentMode);

        // If entering text mode, announce the text
        if (currentMode == RE::TerminalMenu::Mode::kText ||
            currentMode == RE::TerminalMenu::Mode::kLogin) {
            AnnounceDisplayText(a_menu);
        }
        return;
    }

    // Mode-specific state checks
    switch (currentMode) {
        case RE::TerminalMenu::Mode::kList: {
            // Check for selection change
            auto currentSelection = GetSelectedIndex(a_menu);
            if (currentSelection >= 0 && currentSelection != m_lastSelection) {
                m_lastSelection = currentSelection;
                AnnounceSelection(a_menu, currentSelection);
            }
            break;
        }
        case RE::TerminalMenu::Mode::kText:
        case RE::TerminalMenu::Mode::kLogin: {
            // Check for display text change
            std::string displayText = GetDisplayText(a_menu);
            if (!displayText.empty() && displayText != m_lastDisplayText) {
                m_lastDisplayText = displayText;
                SpeakText(displayText);
            }
            break;
        }
        case RE::TerminalMenu::Mode::kHack: {
            // Could check for attempts change here if needed
            break;
        }
        default:
            break;
    }
}

void TerminalMenuHook::AnnounceMode(RE::TerminalMenu::Mode a_mode)
{
    switch (a_mode) {
        case RE::TerminalMenu::Mode::kHack: {
            SpeakText("Hacking mode");
            break;
        }
        case RE::TerminalMenu::Mode::kList: {
            // Don't announce "list mode" - just let selection be announced
            break;
        }
        case RE::TerminalMenu::Mode::kText:
        case RE::TerminalMenu::Mode::kLogin: {
            // Text will be announced separately
            break;
        }
        default:
            break;
    }
}

void TerminalMenuHook::AnnounceSelection(RE::TerminalMenu* a_menu, std::int32_t a_index)
{
    std::string itemText = GetSelectedItemText(a_menu, a_index);
    if (!itemText.empty()) {
        SpeakText(itemText);
    }
}

void TerminalMenuHook::AnnounceDisplayText(RE::TerminalMenu* a_menu)
{
    std::string displayText = GetDisplayText(a_menu);
    if (!displayText.empty() && displayText != m_lastDisplayText) {
        m_lastDisplayText = displayText;
        SpeakText(displayText);
    }
}

std::int32_t TerminalMenuHook::GetSelectedIndex(RE::TerminalMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return -1;
    }

    std::string indexPath = std::string(MENU_LIST_PATH) + ".selectedIndex";
    Scaleform::GFx::Value indexValue;
    if (a_menu->uiMovie->GetVariable(&indexValue, indexPath.c_str())) {
        if (indexValue.IsInt()) {
            return indexValue.GetInt();
        } else if (indexValue.IsNumber()) {
            return static_cast<std::int32_t>(indexValue.GetNumber());
        } else if (indexValue.IsUInt()) {
            return static_cast<std::int32_t>(indexValue.GetUInt());
        }
    }

    return -1;
}

std::string TerminalMenuHook::GetSelectedItemText(RE::TerminalMenu* a_menu, std::int32_t a_index)
{
    if (!a_menu || !a_menu->uiMovie || a_index < 0) {
        return "";
    }

    std::string entryListPath = std::string(MENU_LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
        return "";
    }

    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
        return "";
    }

    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    if (entry.GetMember("label", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    return "";
}

std::string TerminalMenuHook::GetDisplayText(RE::TerminalMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, DISPLAY_TEXT_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::string TerminalMenuHook::GetHackingAttemptsText(RE::TerminalMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, HACKING_ATTEMPTS_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

void TerminalMenuHook::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
