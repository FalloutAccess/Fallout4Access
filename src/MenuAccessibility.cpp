#include "MenuAccessibility.h"
#include "MainMenuHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include "SettingsMenuAccessibility.h"
#include "SaveLoadMenuAccessibility.h"
#include "DialogueMenuAccessibility.h"
#include "SPECIALMenuAccessibility.h"
#include "TerminalMenuAccessibility.h"
#include "VATSMenuAccessibility.h"
#include <SRAL.h>
#include <RE/M/MessageBoxMenu.h>
#include <RE/M/MessageBoxData.h>

MenuAccessibility* MenuAccessibility::GetSingleton()
{
    static MenuAccessibility singleton;
    return &singleton;
}

void MenuAccessibility::Register()
{
    if (auto ui = RE::UI::GetSingleton()) {
        ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
        REX::INFO("MenuAccessibility registered for menu events");
    } else {
        REX::ERROR("Failed to get UI singleton for MenuAccessibility registration");
    }
}

RE::BSEventNotifyControl MenuAccessibility::ProcessEvent(
    const RE::MenuOpenCloseEvent& a_event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    REX::INFO("Menu event: {} {}", a_event.menuName.c_str(), a_event.opening ? "opened" : "closed");

    if (a_event.opening) {
        OnMenuOpened(a_event.menuName);
    } else {
        OnMenuClosed(a_event.menuName);
    }

    return RE::BSEventNotifyControl::kContinue;
}

bool MenuAccessibility::ShouldHandleEvent(const RE::InputEvent*)
{
    // Only handle events when a tracked menu is open
    return m_mainMenuOpen || m_pauseMenuOpen || m_dialogueMenuOpen || m_specialMenuOpen || m_terminalMenuOpen || m_vatsMenuOpen || m_pipBoyMenuOpen || m_examineMenuOpen;
}

void MenuAccessibility::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    if (!a_event || !a_event->QJustPressed()) {
        return;
    }

    // Check for navigation keys
    auto idCode = a_event->QIDCode();
    auto device = a_event->device.get();

    bool isNavigationKey = false;
    bool isPipBoyItemNavKey = false;  // Up/Down only for Pip-Boy item navigation
    bool isPipBoyPageNavKey = false;  // W/S for Pip-Boy page switching
    bool isPipBoyNavUp = false;       // True if navigating up, false if down

    // Keyboard navigation keys (Windows Virtual Key codes)
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        // Up/Down arrows - navigation for all menus, item nav for Pip-Boy
        if (idCode == 0x26) {  // Up arrow
            isNavigationKey = true;
            isPipBoyItemNavKey = true;
            isPipBoyNavUp = true;
        }
        if (idCode == 0x28) {  // Down arrow
            isNavigationKey = true;
            isPipBoyItemNavKey = true;
            isPipBoyNavUp = false;
        }
        // W/S - navigation for most menus, but PAGE switching for Pip-Boy
        if (idCode == 0x57 || idCode == 0x53) {
            isNavigationKey = true;
            isPipBoyPageNavKey = true;
        }
    }
    // Gamepad: DPad Up/Down
    else if (device == RE::INPUT_DEVICE::kGamepad) {
        if (idCode == 0x0001) {  // DPad Up
            isNavigationKey = true;
            isPipBoyItemNavKey = true;
            isPipBoyNavUp = true;
        }
        if (idCode == 0x0002) {  // DPad Down
            isNavigationKey = true;
            isPipBoyItemNavKey = true;
            isPipBoyNavUp = false;
        }
    }

    if (isNavigationKey) {
        // Delay the check to next frame so the game has time to update the selection
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this, isPipBoyItemNavKey, isPipBoyPageNavKey, isPipBoyNavUp]() {
                // Main/Pause menus: Need keystroke-triggered checks because selectedIndex
                // doesn't update without keyboard focus on the Scaleform list
                if (m_mainMenuOpen || m_pauseMenuOpen) {
                    MainMenuHook::GetSingleton()->CheckMainListSelection();
                }
                // Settings/SaveLoad: Keystroke-triggered checks for selection
                SettingsMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                SaveLoadMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                if (m_dialogueMenuOpen) {
                    DialogueMenuAccessibility::GetSingleton()->CheckSelection();
                }
                if (m_specialMenuOpen) {
                    SPECIALMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                }
                // Terminal/VATS: AdvanceMovie hooks handle state tracking per-frame
                if (m_pipBoyMenuOpen) {
                    if (isPipBoyItemNavKey) {
                        // Up/Down navigates items within a list
                        PipBoyMenuAccessibility::GetSingleton()->OnNavigate(isPipBoyNavUp);
                    } else if (isPipBoyPageNavKey) {
                        // W/S switches pages in Pip-Boy
                        PipBoyMenuAccessibility::GetSingleton()->CheckPageChange();
                    }
                }
                // Confirmation dialog: MainMenuHook handles this per-frame
            });
        }
    }

    // Handle left/right arrows for settings value changes
    // Also handle A/D keys which are used for left/right in menus
    bool isValueChangeKey = false;
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        isValueChangeKey = (idCode == 0x25 || idCode == 0x27 ||  // Left, Right arrows
                            idCode == 0x41 || idCode == 0x44);    // A, D keys
    } else if (device == RE::INPUT_DEVICE::kGamepad) {
        isValueChangeKey = (idCode == 0x0004 || idCode == 0x0008);  // DPad Left, Right
    }

    if (isValueChangeKey) {
        REX::DEBUG("Value change key pressed: device={} idCode=0x{:X}", static_cast<int>(device), idCode);
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            // Double-defer to give the game time to update the value in Scaleform
            taskInterface->AddTask([this]() {
                if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
                    taskInterface2->AddTask([this]() {
                        SettingsMenuAccessibility::GetSingleton()->CheckValueChange(m_mainMenuOpen, m_pauseMenuOpen);
                        if (m_specialMenuOpen) {
                            SPECIALMenuAccessibility::GetSingleton()->CheckValueChange(m_mainMenuOpen, m_pauseMenuOpen);
                        }
                        // VATS: AdvanceMovie hook handles target cycling per-frame
                    });
                }
            });
        }
    }

    // Pip-Boy tab keys (Q/E and Left/Right) - switches tabs within a page
    // Note: W/S switches pages (handled above), Q/E switches tabs
    bool isPipBoyTabKey = false;
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        isPipBoyTabKey = (idCode == 0x51 || idCode == 0x45 ||   // Q, E
                          idCode == 0x25 || idCode == 0x27);    // Left, Right arrows
    } else if (device == RE::INPUT_DEVICE::kGamepad) {
        // LB/RB for tabs, D-pad left/right also for tabs
        isPipBoyTabKey = (idCode == 0x0100 || idCode == 0x0200 ||  // LB, RB
                          idCode == 0x0004 || idCode == 0x0008);    // D-pad Left, Right
    }

    if (isPipBoyTabKey && m_pipBoyMenuOpen) {
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                PipBoyMenuAccessibility::GetSingleton()->CheckTabChange();
            });
        }
    }

    // Debug logging for Pip-Boy input
    if (m_pipBoyMenuOpen) {
        REX::DEBUG("PipBoy key press: device={} idCode=0x{:X}", static_cast<int>(device), idCode);
    }

    // For terminal menu, log key presses for debugging
    if (m_terminalMenuOpen) {
        REX::DEBUG("Terminal key press: device={} idCode=0x{:X}", static_cast<int>(device), idCode);
    }

    // Check for Accept keys (Enter=0x0D, E=0x45 - Windows virtual key codes)
    bool isAcceptKey = false;
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        isAcceptKey = (idCode == 0x0D || idCode == 0x45);  // Enter, E
    } else if (device == RE::INPUT_DEVICE::kGamepad) {
        isAcceptKey = (idCode == 0x1000);  // A button
    }

    if (isAcceptKey) {
        // Terminal: AdvanceMovie hook handles mode changes per-frame
        // MainMenu/PauseMenu: AdvanceMovie hooks handle confirm dialog detection per-frame

        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                SettingsMenuAccessibility::GetSingleton()->OnEnterPressed(m_mainMenuOpen, m_pauseMenuOpen);
            });
        }
    }

    // Check for Back/Cancel keys (Escape=0x1B, Tab=0x09 - Windows virtual key codes)
    bool isBackKey = false;
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        isBackKey = (idCode == 0x1B || idCode == 0x09);  // Escape, Tab
    } else if (device == RE::INPUT_DEVICE::kGamepad) {
        isBackKey = (idCode == 0x2000);  // B button
    }

    if (isBackKey) {
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                SettingsMenuAccessibility::GetSingleton()->OnEscapePressed();
            });
        }
    }

    // VATS-specific key handlers
    if (m_vatsMenuOpen) {
        // Q key (queue attack) - VK_Q = 0x51
        bool isQueueKey = false;
        if (device == RE::INPUT_DEVICE::kKeyboard) {
            isQueueKey = (idCode == 0x51);  // Q key
        } else if (device == RE::INPUT_DEVICE::kGamepad) {
            isQueueKey = (idCode == 0x0100);  // R2/RT trigger (queue in VATS)
        }

        if (isQueueKey) {
            if (auto* taskInterface = F4SE::GetTaskInterface()) {
                taskInterface->AddTask([]() {
                    VATSMenuAccessibility::GetSingleton()->CheckQueuedShots();
                });
            }
        }

        // Space key (critical) - VK_SPACE = 0x20
        bool isCriticalKey = false;
        if (device == RE::INPUT_DEVICE::kKeyboard) {
            isCriticalKey = (idCode == 0x20);  // Space key
        } else if (device == RE::INPUT_DEVICE::kGamepad) {
            isCriticalKey = (idCode == 0x4000);  // X button (critical in VATS)
        }

        if (isCriticalKey) {
            if (auto* taskInterface = F4SE::GetTaskInterface()) {
                taskInterface->AddTask([]() {
                    VATSMenuAccessibility::GetSingleton()->CheckCriticalCharge();
                });
            }
        }
    }

}

void MenuAccessibility::OnMenuOpened(const RE::BSFixedString& a_menuName)
{
    // Register input handler on first menu open (MenuControls not available at plugin load)
    if (!m_inputHandlerRegistered) {
        if (auto* menuControls = RE::MenuControls::GetSingleton()) {
            menuControls->handlers.push_back(this);
            m_inputHandlerRegistered = true;
            REX::INFO("MenuAccessibility registered for input events (deferred)");
        }
    }

    if (a_menuName == "MainMenu"sv) {
        m_mainMenuOpen = true;
        REX::INFO("MainMenu opened");
        // MainMenuHook handles splash screen, menu name, list selection, and confirmation dialogs
        MainMenuHook::GetSingleton()->Install();
        MainMenuHook::GetSingleton()->ResetState();
        MainMenuHook::GetSingleton()->SetMainMenuOpen(true);
    } else if (a_menuName == "PauseMenu"sv) {
        m_pauseMenuOpen = true;
        REX::INFO("PauseMenu opened");
        // MainMenuHook handles the same SWF (MainMenu.swf) for PauseMenu
        MainMenuHook::GetSingleton()->Install();
        MainMenuHook::GetSingleton()->ResetState();
        MainMenuHook::GetSingleton()->SetPauseMenuOpen(true);
    } else if (a_menuName == "MessageBoxMenu"sv) {
        m_messageBoxOpen = true;
        m_messageBoxSpoken = false;
        // Schedule retries - MessageBoxData may take several frames to initialize
        ScheduleMessageBoxRead(10);
    } else if (a_menuName == "DialogueMenu"sv) {
        m_dialogueMenuOpen = true;
        DialogueMenuAccessibility::GetSingleton()->OnDialogueMenuOpened();
    } else if (a_menuName == "SPECIALMenu"sv) {
        m_specialMenuOpen = true;
        REX::INFO("SPECIALMenu opened");
        // Trigger initial selection check to announce menu and first item
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([]() {
                SPECIALMenuAccessibility::GetSingleton()->CheckSelection(false, false);
            });
        }
    } else if (a_menuName == "TerminalMenu"sv) {
        m_terminalMenuOpen = true;
        REX::INFO("TerminalMenu opened");
        TerminalMenuAccessibility::GetSingleton()->OnTerminalMenuOpened();
    } else if (a_menuName == "VATSMenu"sv) {
        m_vatsMenuOpen = true;
        REX::INFO("VATSMenu opened");
        VATSMenuAccessibility::GetSingleton()->OnVATSMenuOpened();
    } else if (a_menuName == "HUDMenu"sv) {
        m_hudMenuOpen = true;
        HUDNotificationAccessibility::GetSingleton()->OnHUDMenuOpened();
    } else if (a_menuName == "PipboyMenu"sv) {
        m_pipBoyMenuOpen = true;
        REX::INFO("PipboyMenu opened");
        PipBoyMenuAccessibility::GetSingleton()->OnPipBoyMenuOpened();
    } else if (a_menuName == "ExamineMenu"sv) {
        m_examineMenuOpen = true;
        REX::INFO("ExamineMenu opened");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuOpened();
    }
}

void MenuAccessibility::OnMenuClosed(const RE::BSFixedString& a_menuName)
{
    if (a_menuName == "MainMenu"sv) {
        m_mainMenuOpen = false;
        // MainMenuHook handles resetting panel handlers via SetMainMenuOpen(false)
        MainMenuHook::GetSingleton()->SetMainMenuOpen(false);
    } else if (a_menuName == "PauseMenu"sv) {
        m_pauseMenuOpen = false;
        // MainMenuHook handles resetting panel handlers via SetPauseMenuOpen(false)
        MainMenuHook::GetSingleton()->SetPauseMenuOpen(false);
    } else if (a_menuName == "MessageBoxMenu"sv) {
        m_messageBoxOpen = false;
        m_messageBoxSpoken = false;
    } else if (a_menuName == "DialogueMenu"sv) {
        m_dialogueMenuOpen = false;
        DialogueMenuAccessibility::GetSingleton()->OnDialogueMenuClosed();
    } else if (a_menuName == "SPECIALMenu"sv) {
        m_specialMenuOpen = false;
        SPECIALMenuAccessibility::GetSingleton()->Reset();
        REX::INFO("SPECIALMenu closed");
    } else if (a_menuName == "TerminalMenu"sv) {
        m_terminalMenuOpen = false;
        TerminalMenuAccessibility::GetSingleton()->OnTerminalMenuClosed();
    } else if (a_menuName == "VATSMenu"sv) {
        m_vatsMenuOpen = false;
        VATSMenuAccessibility::GetSingleton()->OnVATSMenuClosed();
    } else if (a_menuName == "HUDMenu"sv) {
        m_hudMenuOpen = false;
        HUDNotificationAccessibility::GetSingleton()->OnHUDMenuClosed();
    } else if (a_menuName == "PipboyMenu"sv) {
        m_pipBoyMenuOpen = false;
        PipBoyMenuAccessibility::GetSingleton()->OnPipBoyMenuClosed();
    } else if (a_menuName == "ExamineMenu"sv) {
        m_examineMenuOpen = false;
        REX::INFO("ExamineMenu closed");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuClosed();
    }
}

// NOTE: CheckAndSpeakSelection, GetSelectedIndex, GetSelectedItemText, CleanLocalizationString removed
// Main/Pause menu list selection is now handled by MainMenuHook per-frame
// Use LocalizationHelper::CleanString() for string cleaning

void MenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}

// NOTE: CheckForConfirmationDialog and DumpScaleformChildren removed
// Confirmation dialogs are now detected per-frame by MainMenuHook

void MenuAccessibility::ScheduleMessageBoxRead(int a_maxRetries)
{
    // Use a background thread with real sleep delays
    // F4SE task interface executes tasks too quickly for proper delays
    std::thread([this, a_maxRetries]() {
        for (int i = 0; i < a_maxRetries && m_messageBoxOpen; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            if (!m_messageBoxOpen) break;

            // Schedule the actual check on the main thread (Scaleform requires main thread)
            if (auto* taskInterface = F4SE::GetTaskInterface()) {
                taskInterface->AddTask([this, i]() {
                    if (!m_messageBoxOpen) return;
                    if (m_messageBoxSpoken) return;  // Already spoken

                    auto ui = RE::UI::GetSingleton();
                    if (!ui) return;

                    auto menu = ui->GetMenu("MessageBoxMenu"sv);
                    if (!menu || !menu->uiMovie) return;

                    // Try multiple Scaleform paths to find the right one
                    const char* listPaths[] = {
                        "root.List_mc.entryList",
                        "root.Menu_mc.List_mc.entryList",
                        "root.MessageBox_mc.List_mc.entryList",
                        "root.MenuObj.List_mc.entryList"
                    };

                    for (const char* path : listPaths) {
                        Scaleform::GFx::Value entryList;
                        if (menu->uiMovie->GetVariable(&entryList, path)) {
                            REX::DEBUG("MessageBoxMenu: path '{}' exists, IsArray={}, size={}",
                                path, entryList.IsArray(),
                                entryList.IsArray() ? entryList.GetArraySize() : 0);
                            if (entryList.IsArray() && entryList.GetArraySize() > 0) {
                                REX::DEBUG("MessageBoxMenu: Scaleform data ready after {} attempts at '{}'", i + 1, path);
                                SpeakMessageBoxFromScaleform(menu.get());
                                m_messageBoxSpoken = true;
                                return;
                            }
                        }
                    }

                    // Log what we can find at root level
                    if (i == 0) {
                        Scaleform::GFx::Value root;
                        if (menu->uiMovie->GetVariable(&root, "root")) {
                            REX::DEBUG("MessageBoxMenu: root exists, IsObject={}", root.IsObject());
                        }
                    }
                });
            }
        }

        // Final warning if we didn't find it
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (m_messageBoxOpen && !m_messageBoxSpoken) {
            REX::WARN("MessageBoxMenu: gave up waiting for currentMessage after {} retries", a_maxRetries);
        }
    }).detach();
}

void MenuAccessibility::SpeakMessageBox(RE::IMenu* a_menu)
{
    REX::DEBUG("SpeakMessageBox: entering");

    if (!a_menu) {
        REX::DEBUG("SpeakMessageBox: menu is null");
        return;
    }

    auto* msgBoxMenu = static_cast<RE::MessageBoxMenu*>(a_menu);
    if (!msgBoxMenu->currentMessage) {
        REX::WARN("MessageBoxMenu has no currentMessage");
        return;
    }

    auto* msgData = msgBoxMenu->currentMessage;
    REX::DEBUG("SpeakMessageBox: msgData at {:p}", static_cast<void*>(msgData));

    // Check if buttonText array is populated - if empty, data may not be fully ready
    // Use capacity check as a safer way to verify the array is initialized
    if (msgData->buttonText.empty()) {
        REX::DEBUG("SpeakMessageBox: buttonText is empty, data may not be ready");
        return;
    }

    REX::DEBUG("SpeakMessageBox: buttonText has {} entries", msgData->buttonText.size());

    // Build the message to speak
    std::string speechText;

    // Add header if present - check c_str() is not null
    REX::DEBUG("SpeakMessageBox: checking headerText");
    if (msgData->headerText.size() > 0 && msgData->headerText.c_str() != nullptr) {
        speechText += msgData->headerText.c_str();
        speechText += ". ";
    }

    // Add body text
    REX::DEBUG("SpeakMessageBox: checking bodyText");
    if (msgData->bodyText.size() > 0 && msgData->bodyText.c_str() != nullptr) {
        speechText += msgData->bodyText.c_str();
    }

    // Add button options
    REX::DEBUG("SpeakMessageBox: adding button options");
    speechText += ". ";
    for (std::uint32_t i = 0; i < msgData->buttonText.size(); ++i) {
        if (i > 0) {
            speechText += ", ";
        }
        if (msgData->buttonText[i].c_str() != nullptr) {
            speechText += msgData->buttonText[i].c_str();
        }
    }

    REX::INFO("MessageBox: header='{}' body='{}' buttons={}",
        msgData->headerText.c_str() ? msgData->headerText.c_str() : "(null)",
        msgData->bodyText.c_str() ? msgData->bodyText.c_str() : "(null)",
        msgData->buttonText.size());

    if (!speechText.empty()) {
        SpeakText(LocalizationHelper::CleanString(speechText));
    }
}

void MenuAccessibility::SpeakMessageBoxFromScaleform(RE::IMenu* a_menu)
{
    if (!a_menu || !a_menu->uiMovie) {
        return;
    }

    std::string speechText;

    // Get body text from Body_tf.text
    Scaleform::GFx::Value bodyText;
    if (a_menu->uiMovie->GetVariable(&bodyText, "root.Body_tf.text") && bodyText.IsString()) {
        std::string body = bodyText.GetString();
        if (!body.empty()) {
            speechText += body;
        }
    }

    // Get button list from List_mc.entryList
    Scaleform::GFx::Value entryList;
    if (a_menu->uiMovie->GetVariable(&entryList, "root.List_mc.entryList") && entryList.IsArray()) {
        std::uint32_t count = entryList.GetArraySize();
        if (count > 0) {
            speechText += ". ";
            for (std::uint32_t i = 0; i < count; ++i) {
                Scaleform::GFx::Value entry;
                if (entryList.GetElement(i, &entry) && entry.IsObject()) {
                    Scaleform::GFx::Value textValue;
                    if (entry.GetMember("text", &textValue) && textValue.IsString()) {
                        if (i > 0) {
                            speechText += ", ";
                        }
                        speechText += textValue.GetString();
                    }
                }
            }
        }
    }

    REX::INFO("MessageBox (Scaleform): body='{}' buttons found", speechText);

    if (!speechText.empty()) {
        SpeakText(LocalizationHelper::CleanString(speechText));
    }
}

// NOTE: IsMenuPanelVisible and TryAnnounceMenu removed
// Panel visibility and menu announcements are now handled by MainMenuHook per-frame
