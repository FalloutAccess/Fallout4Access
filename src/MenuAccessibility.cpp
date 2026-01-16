#include "MenuAccessibility.h"
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
    return m_mainMenuOpen || m_pauseMenuOpen || m_dialogueMenuOpen || m_specialMenuOpen || m_terminalMenuOpen || m_vatsMenuOpen || m_pipBoyMenuOpen;
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
                CheckAndSpeakSelection();
                SettingsMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                SaveLoadMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                if (m_dialogueMenuOpen) {
                    DialogueMenuAccessibility::GetSingleton()->CheckSelection();
                }
                if (m_specialMenuOpen) {
                    SPECIALMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                }
                // Terminal: AdvanceMovie hook handles state tracking per-frame
                if (m_vatsMenuOpen) {
                    VATSMenuAccessibility::GetSingleton()->CheckSelection();
                }
                if (m_pipBoyMenuOpen) {
                    if (isPipBoyItemNavKey) {
                        // Up/Down navigates items within a list
                        PipBoyMenuAccessibility::GetSingleton()->OnNavigate(isPipBoyNavUp);
                    } else if (isPipBoyPageNavKey) {
                        // W/S switches pages in Pip-Boy
                        PipBoyMenuAccessibility::GetSingleton()->CheckPageChange();
                    }
                }
                CheckForConfirmationDialog();
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
                        if (m_vatsMenuOpen) {
                            // A/D keys cycle targets in VATS
                            VATSMenuAccessibility::GetSingleton()->CheckTargetChange();
                        }
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

        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                SettingsMenuAccessibility::GetSingleton()->OnEnterPressed(m_mainMenuOpen, m_pauseMenuOpen);
                CheckForConfirmationDialog();
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
        m_mainMenuAnnounced = false;
        m_lastMainMenuSelection = -1;

        // Check for splash screen "Press any button to start" text
        // TODO: This text is announced as soon as MainMenu opens, which happens while the
        // logo/intro video is still playing. Ideally we should wait until the video ends
        // and the splash screen is actually visible before announcing.
        if (auto menu = RE::UI::GetSingleton()->GetMenu("MainMenu"sv)) {
            if (menu->uiMovie) {
                Scaleform::GFx::Value splashText;
                if (menu->uiMovie->GetVariable(&splashText, "root.Menu_mc.SplashScreenHolder_mc.SplashScreenText_tf.text")) {
                    if (splashText.IsString()) {
                        std::string text = splashText.GetString();
                        if (!text.empty()) {
                            SpeakText(CleanLocalizationString(text));
                        }
                    }
                }
            }
        }

        // Menu will be announced when content becomes available (handles splash screen/video)
        TryAnnounceMenu("MainMenu", "Main Menu", MAIN_MENU_LIST_PATH);
    } else if (a_menuName == "PauseMenu"sv) {
        m_pauseMenuOpen = true;
        m_pauseMenuAnnounced = false;
        m_lastPauseMenuSelection = -1;

        // Try to announce if menu is already ready, otherwise will announce on first content detection
        TryAnnounceMenu("PauseMenu", "Pause Menu", PAUSE_MENU_LIST_PATH);
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
    }
}

void MenuAccessibility::OnMenuClosed(const RE::BSFixedString& a_menuName)
{
    if (a_menuName == "MainMenu"sv) {
        m_mainMenuOpen = false;
        m_mainMenuAnnounced = false;
        m_lastMainMenuSelection = -1;
        SettingsMenuAccessibility::GetSingleton()->Reset();
        SaveLoadMenuAccessibility::GetSingleton()->Reset();
    } else if (a_menuName == "PauseMenu"sv) {
        m_pauseMenuOpen = false;
        m_pauseMenuAnnounced = false;
        m_lastPauseMenuSelection = -1;
        SettingsMenuAccessibility::GetSingleton()->Reset();
        SaveLoadMenuAccessibility::GetSingleton()->Reset();
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
    }
}

void MenuAccessibility::CheckAndSpeakSelection()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    if (m_mainMenuOpen) {
        // Try to announce menu name first if not yet announced
        TryAnnounceMenu("MainMenu", "Main Menu", MAIN_MENU_LIST_PATH);

        if (auto menu = ui->GetMenu("MainMenu"sv)) {
            auto currentSelection = GetSelectedIndex(menu.get(), MAIN_MENU_LIST_PATH);
            REX::INFO("MainMenu selection: current={} last={}", currentSelection, m_lastMainMenuSelection);
            if (currentSelection >= 0 && currentSelection != m_lastMainMenuSelection) {
                m_lastMainMenuSelection = currentSelection;
                auto text = GetSelectedItemText(menu.get(), MAIN_MENU_LIST_PATH);
                if (!text.empty()) {
                    SpeakText(CleanLocalizationString(text));
                }
            }
        }
    }

    if (m_pauseMenuOpen) {
        // Try to announce menu name first if not yet announced
        TryAnnounceMenu("PauseMenu", "Pause Menu", PAUSE_MENU_LIST_PATH);

        if (auto menu = ui->GetMenu("PauseMenu"sv)) {
            auto currentSelection = GetSelectedIndex(menu.get(), PAUSE_MENU_LIST_PATH);
            REX::INFO("PauseMenu selection: current={} last={}", currentSelection, m_lastPauseMenuSelection);
            if (currentSelection >= 0 && currentSelection != m_lastPauseMenuSelection) {
                m_lastPauseMenuSelection = currentSelection;
                auto text = GetSelectedItemText(menu.get(), PAUSE_MENU_LIST_PATH);
                if (!text.empty()) {
                    SpeakText(CleanLocalizationString(text));
                }
            }
        }
    }
}

std::int32_t MenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu, const std::string& a_listPath)
{
    if (!a_menu || !a_menu->uiMovie) {
        return -1;
    }

    std::string indexPath = a_listPath + ".selectedIndex";
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

std::string MenuAccessibility::GetSelectedItemText(RE::IMenu* a_menu, const std::string& a_listPath)
{
    if (!a_menu || !a_menu->uiMovie) {
        return "";
    }

    auto selectedIndex = GetSelectedIndex(a_menu, a_listPath);
    if (selectedIndex < 0) {
        return "";
    }

    // Get entryList array
    std::string entryListPath = a_listPath + ".entryList";
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

std::string MenuAccessibility::CleanLocalizationString(const std::string& a_text)
{
    if (a_text.empty()) {
        return a_text;
    }

    std::string result = a_text;

    // Remove $ prefix used for localization keys
    if (result[0] == '$') {
        result = result.substr(1);
    }

    // Replace underscores and hyphens with spaces for better speech
    for (char& c : result) {
        if (c == '_' || c == '-') {
            c = ' ';
        }
    }

    return result;
}

void MenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), a_interrupt);
        REX::INFO("Spoke: {}", a_text);
    }
}

// TODO: Find a way to read the confirmation dialog automatically when it appears,
// not just when user presses a key. May need to hook into Scaleform events or
// use a frame callback to poll for dialog visibility changes.
void MenuAccessibility::CheckForConfirmationDialog()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    // Check both MainMenu and PauseMenu for confirmation dialogs
    RE::IMenu* menu = nullptr;
    if (m_mainMenuOpen) {
        if (auto m = ui->GetMenu("MainMenu"sv)) {
            menu = m.get();
        }
    } else if (m_pauseMenuOpen) {
        if (auto m = ui->GetMenu("PauseMenu"sv)) {
            menu = m.get();
        }
    }

    if (!menu || !menu->uiMovie) return;

    // Check if ConfirmPanel is visible
    Scaleform::GFx::Value confirmPanel;
    if (!menu->uiMovie->GetVariable(&confirmPanel, "root.Menu_mc.ConfirmPanel_mc")) return;
    if (confirmPanel.IsUndefined() || !confirmPanel.IsObject()) return;

    Scaleform::GFx::Value visible;
    if (!confirmPanel.GetMember("visible", &visible) || !visible.IsBoolean() || !visible.GetBoolean()) {
        return;
    }

    // Get confirmation text
    Scaleform::GFx::Value confirmText;
    if (!menu->uiMovie->GetVariable(&confirmText, "root.Menu_mc.ConfirmPanel_mc.Text_mc.TextPulse_mc.textField.text")) return;
    if (!confirmText.IsString()) return;

    std::string text = confirmText.GetString();

    // Trim whitespace
    text.erase(0, text.find_first_not_of(" \t\n\r"));
    if (!text.empty()) {
        text.erase(text.find_last_not_of(" \t\n\r") + 1);
    }

    // Always speak if we have text (user requested no duplicate check)
    if (!text.empty()) {
        REX::INFO("Confirmation dialog: '{}'", text);
        SpeakText(text + ". Press Enter to confirm, Escape to cancel.");
    }
}

void MenuAccessibility::DumpScaleformChildren(Scaleform::GFx::Value& a_obj, const std::string& a_path, int a_depth)
{
    if (a_depth > 3) return;  // Limit depth to avoid excessive logging

    if (!a_obj.IsObject() && !a_obj.IsDisplayObject()) {
        return;
    }

    // Try to get numChildren (for display objects)
    Scaleform::GFx::Value numChildren;
    if (a_obj.GetMember("numChildren", &numChildren) && numChildren.IsNumber()) {
        int count = static_cast<int>(numChildren.GetNumber());
        REX::INFO("{} has {} children", a_path, count);
    }

    // Look for specific properties that might indicate a confirmation dialog
    const char* propsToCheck[] = { "visible", "text", "htmlText", "label", "ConfirmText_tf", "CancelText_tf" };
    for (const char* prop : propsToCheck) {
        Scaleform::GFx::Value propVal;
        if (a_obj.GetMember(prop, &propVal)) {
            if (propVal.IsString()) {
                REX::INFO("{}[{}] = '{}'", a_path, prop, propVal.GetString());
            } else if (propVal.IsBoolean()) {
                REX::INFO("{}[{}] = {}", a_path, prop, propVal.GetBoolean() ? "true" : "false");
            }
        }
    }

    // Check for named children that might be confirmation-related
    const char* childNames[] = {
        "Menu_mc", "ConfirmPanel_mc", "Confirm_mc", "MessageBox_mc",
        "PopupPanel_mc", "BottomBar_mc", "ButtonBar_mc", "ConfirmText_tf",
        "BodyText_tf", "HeaderText_tf", "QuestionText_tf", "Body_tf",
        "Header_tf", "Message_tf", "Text_tf", "Question_tf", "strMessage",
        "strHeader", "textField", "label"
    };

    for (const char* childName : childNames) {
        Scaleform::GFx::Value child;
        if (a_obj.GetMember(childName, &child)) {
            if (!child.IsUndefined()) {
                std::string childPath = a_path + "." + childName;
                REX::INFO("Found child: {}", childPath);
                DumpScaleformChildren(child, childPath, a_depth + 1);
            }
        }
    }
}

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
        SpeakText(CleanLocalizationString(speechText));
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
        SpeakText(CleanLocalizationString(speechText));
    }
}

bool MenuAccessibility::IsMenuPanelVisible(RE::IMenu* a_menu, const std::string& a_panelPath)
{
    if (!a_menu || !a_menu->uiMovie) {
        return false;
    }

    Scaleform::GFx::Value panel;
    if (!a_menu->uiMovie->GetVariable(&panel, a_panelPath.c_str())) {
        return false;
    }

    if (panel.IsUndefined() || !panel.IsObject()) {
        return false;
    }

    Scaleform::GFx::Value visible;
    if (!panel.GetMember("visible", &visible) || !visible.IsBoolean()) {
        return false;
    }

    return visible.GetBoolean();
}

void MenuAccessibility::TryAnnounceMenu(const std::string& a_menuName, const std::string& a_announcement, const std::string& a_listPath)
{
    // Check if already announced
    bool* announced = nullptr;
    if (a_menuName == "MainMenu") {
        announced = &m_mainMenuAnnounced;
    } else if (a_menuName == "PauseMenu") {
        announced = &m_pauseMenuAnnounced;
    }

    if (!announced || *announced) {
        return;  // Already announced or unknown menu
    }

    auto ui = RE::UI::GetSingleton();
    if (!ui) {
        return;
    }

    auto menu = ui->GetMenu(a_menuName.c_str());
    if (!menu) {
        return;
    }

    // Check if the menu has valid content (selected index >= 0 means list is populated)
    auto selectedIndex = GetSelectedIndex(menu.get(), a_listPath);
    if (selectedIndex >= 0) {
        // Menu has content, announce it
        *announced = true;
        REX::INFO("{} is ready (selectedIndex={}), announcing", a_menuName, selectedIndex);
        SpeakText(a_announcement);
    }
}
