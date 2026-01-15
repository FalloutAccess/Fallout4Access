#include "TerminalMenuAccessibility.h"
#include <SRAL.h>
#include <thread>
#include <chrono>

TerminalMenuAccessibility* TerminalMenuAccessibility::GetSingleton()
{
    static TerminalMenuAccessibility singleton;
    return &singleton;
}

void TerminalMenuAccessibility::OnTerminalMenuOpened()
{
    m_isOpen = true;
    m_menuAnnounced = false;
    m_lastMode = RE::TerminalMenu::Mode::kInit;
    m_lastSelection = -1;
    m_lastDisplayText.clear();
    m_pollCounter = 0;
    REX::INFO("TerminalMenuAccessibility: menu opened, m_isOpen={}", m_isOpen);

    // Defer announcement to next frame so Scaleform has time to populate
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([this]() {
            if (!m_isOpen) return;

            auto ui = RE::UI::GetSingleton();
            if (!ui) return;

            auto menu = ui->GetMenu("TerminalMenu"sv);
            if (!menu || !menu->uiMovie) return;

            // Get terminal header and welcome text
            std::string header = GetHeaderText(menu.get());
            std::string welcome = GetWelcomeText(menu.get());

            std::string announcement;
            if (!header.empty()) {
                announcement = "Terminal: " + header;
            } else {
                announcement = "Terminal";
            }

            SpeakText(announcement);

            if (!welcome.empty()) {
                SpeakText(welcome);
            }

            m_menuAnnounced = true;

            // Check initial mode and announce
            auto mode = GetCurrentMode(menu.get());
            m_lastMode = mode;
            AnnounceCurrentState(menu.get(), mode, true);

            // Start continuous polling to catch mode changes from Enter key
            StartContinuousPolling();
        });
    }
}

void TerminalMenuAccessibility::ScheduleDelayedChecks(int a_count)
{
    // Legacy method - now just triggers time-based polling
    StartContinuousPolling();
}

void TerminalMenuAccessibility::StartContinuousPolling()
{
    // No longer used - see OnAcceptPressed for the new approach
}

void TerminalMenuAccessibility::ContinuePolling()
{
    // No longer used
}

void TerminalMenuAccessibility::ScheduleNextPollCheck()
{
    // No longer used
}

void TerminalMenuAccessibility::OnAcceptPressed()
{
    if (!m_isOpen) return;

    // Only prepare for new text if we're in list mode (selecting an item)
    // If we're already in text mode, Enter goes back to menu - don't clear
    if (m_lastMode == RE::TerminalMenu::Mode::kList) {
        REX::DEBUG("OnAcceptPressed: in list mode, preparing for text change");
        // Clear the last display text so we detect the new text
        m_lastDisplayText.clear();

        // Use a background thread with real sleeps to check for changes
        std::thread([this]() {
            // Check every 100ms for up to 3 seconds
            for (int i = 0; i < 30 && m_isOpen; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                if (!m_isOpen) break;

                // Schedule a task to check state on the main thread
                if (auto* taskInterface = F4SE::GetTaskInterface()) {
                    taskInterface->AddTask([this]() {
                        if (m_isOpen) {
                            CheckStateChange();
                        }
                    });
                }
            }
        }).detach();
    } else {
        REX::DEBUG("OnAcceptPressed: in mode {}, not starting text polling", static_cast<int>(m_lastMode));
    }
}

void TerminalMenuAccessibility::OnTerminalMenuClosed()
{
    m_isOpen = false;
    REX::INFO("TerminalMenu closed");
    Reset();
}

void TerminalMenuAccessibility::Reset()
{
    m_menuAnnounced = false;
    m_lastMode = RE::TerminalMenu::Mode::kInit;
    m_lastSelection = -1;
    m_lastDisplayText.clear();
    m_pollCounter = 0;
}

void TerminalMenuAccessibility::ScheduleStateCheck()
{
    // Schedule a few checks to catch delayed state changes
    ScheduleDelayedChecks(5);
}

void TerminalMenuAccessibility::CheckStateChange()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    auto menu = ui->GetMenu("TerminalMenu"sv);
    if (!menu || !menu->uiMovie) return;

    auto mode = GetCurrentMode(menu.get());

    // Check for mode change
    if (mode != m_lastMode) {
        REX::INFO("Terminal mode changed: {} -> {}",
            static_cast<int>(m_lastMode), static_cast<int>(mode));

        m_lastMode = mode;
        m_lastSelection = -1;  // Reset selection on mode change
        m_lastDisplayText.clear();

        // Announce new mode state
        AnnounceCurrentState(menu.get(), mode, true);

        // If entering text mode and no text was announced, schedule a retry
        if ((mode == RE::TerminalMenu::Mode::kText || mode == RE::TerminalMenu::Mode::kLogin)
            && m_lastDisplayText.empty()) {
            REX::DEBUG("Text mode entered but no text yet, will retry");
            // Schedule another check after a short delay
            ScheduleStateCheck();
        }
        return;
    }

    // ALSO check for displayText changes regardless of mode
    // This catches cases where text is populated before mode officially changes
    std::string displayText = GetDisplayText(menu.get());
    if (!displayText.empty() && displayText != m_lastDisplayText) {
        REX::INFO("Display text changed (mode={}): '{}'",
            static_cast<int>(mode), displayText.substr(0, 50));
        m_lastDisplayText = displayText;
        SpeakText(displayText);
        return;
    }

    // Check for changes within current mode (selection changes in list mode)
    AnnounceCurrentState(menu.get(), mode, false);
}

void TerminalMenuAccessibility::AnnounceCurrentState(RE::IMenu* a_menu, RE::TerminalMenu::Mode a_mode, bool a_modeChanged)
{
    switch (a_mode) {
        case RE::TerminalMenu::Mode::kHack: {
            if (a_modeChanged) {
                std::string attempts = GetHackingAttemptsText(a_menu);
                if (!attempts.empty()) {
                    SpeakText("Hacking mode. " + attempts);
                } else {
                    SpeakText("Hacking mode");
                }
            }
            // Check if attempts changed (after a guess)
            std::string displayText = GetDisplayText(a_menu);
            if (!displayText.empty() && displayText != m_lastDisplayText) {
                m_lastDisplayText = displayText;
                if (!a_modeChanged) {
                    // Announce new attempts after a guess
                    std::string attempts = GetHackingAttemptsText(a_menu);
                    if (!attempts.empty()) {
                        SpeakText(attempts);
                    }
                }
            }
            break;
        }
        case RE::TerminalMenu::Mode::kText:
        case RE::TerminalMenu::Mode::kLogin: {
            std::string displayText = GetDisplayText(a_menu);
            REX::DEBUG("kText mode: displayText='{}', lastDisplayText='{}', modeChanged={}",
                displayText.substr(0, 50), m_lastDisplayText.substr(0, 50), a_modeChanged);
            if (!displayText.empty() && displayText != m_lastDisplayText) {
                m_lastDisplayText = displayText;
                SpeakText(displayText);
            }
            break;
        }
        case RE::TerminalMenu::Mode::kList: {
            auto currentSelection = GetSelectedIndex(a_menu);
            if (currentSelection >= 0 && currentSelection != m_lastSelection) {
                m_lastSelection = currentSelection;

                std::string itemText = GetSelectedItemText(a_menu);
                if (!itemText.empty()) {
                    SpeakText(itemText);
                }
            }
            break;
        }
        default:
            break;
    }
}

void TerminalMenuAccessibility::CheckSelection()
{
    // This is now only called from arrow key navigation as a backup
    // The polling mechanism handles automatic announcements
    CheckStateChange();
}

RE::TerminalMenu::Mode TerminalMenuAccessibility::GetCurrentMode(RE::IMenu* a_menu)
{
    if (!a_menu) {
        return RE::TerminalMenu::Mode::kInit;
    }

    auto* termMenu = static_cast<RE::TerminalMenu*>(a_menu);
    return termMenu->mode.get();
}

std::string TerminalMenuAccessibility::GetHeaderText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, HEADER_TEXT_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::string TerminalMenuAccessibility::GetWelcomeText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    Scaleform::GFx::Value value;
    if (a_menu->uiMovie->GetVariable(&value, WELCOME_TEXT_PATH)) {
        if (value.IsString()) {
            return value.GetString();
        }
    }

    return "";
}

std::string TerminalMenuAccessibility::GetDisplayText(RE::IMenu* a_menu)
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

std::int32_t TerminalMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu)
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
        }
    }

    return -1;
}

std::string TerminalMenuAccessibility::GetSelectedItemText(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    auto selectedIndex = GetSelectedIndex(a_menu);
    if (selectedIndex < 0) {
        return "";
    }

    // Get entryList array
    std::string entryListPath = std::string(MENU_LIST_PATH) + ".entryList";
    Scaleform::GFx::Value entryList;
    if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
        return "";
    }

    if (static_cast<std::uint32_t>(selectedIndex) >= entryList.GetArraySize()) {
        return "";
    }

    // Get the selected entry
    Scaleform::GFx::Value entry;
    if (!entryList.GetElement(static_cast<std::uint32_t>(selectedIndex), &entry) || !entry.IsObject()) {
        return "";
    }

    // Get the text property
    Scaleform::GFx::Value textValue;
    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    // Try label as fallback
    if (entry.GetMember("label", &textValue) && textValue.IsString()) {
        return textValue.GetString();
    }

    return "";
}

std::string TerminalMenuAccessibility::GetHackingAttemptsText(RE::IMenu* a_menu)
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

void TerminalMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), a_interrupt);
        REX::INFO("Spoke: {}", a_text);
    }
}
