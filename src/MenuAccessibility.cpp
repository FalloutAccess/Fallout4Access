#include "MenuAccessibility.h"
#include "SettingsMenuAccessibility.h"
#include "SaveLoadMenuAccessibility.h"
#include "DialogueMenuAccessibility.h"
#include "SPECIALMenuAccessibility.h"
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
    return m_mainMenuOpen || m_pauseMenuOpen || m_dialogueMenuOpen || m_specialMenuOpen;
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

    // Keyboard navigation keys (Windows Virtual Key codes)
    // VK_UP=0x26, VK_DOWN=0x28, W=0x57, S=0x53
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        isNavigationKey = (idCode == 0x26 || idCode == 0x28 ||  // Up, Down arrows
                          idCode == 0x57 || idCode == 0x53);    // W, S
    }
    // Gamepad: DPad Up=0x0001, DPad Down=0x0002
    else if (device == RE::INPUT_DEVICE::kGamepad) {
        isNavigationKey = (idCode == 0x0001 || idCode == 0x0002);
    }

    if (isNavigationKey) {
        // Delay the check to next frame so the game has time to update the selection
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                CheckAndSpeakSelection();
                SettingsMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                SaveLoadMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                if (m_dialogueMenuOpen) {
                    DialogueMenuAccessibility::GetSingleton()->CheckSelection();
                }
                if (m_specialMenuOpen) {
                    SPECIALMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
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
                    });
                }
            });
        }
    }

    // Check for Enter key (0x0D) - may show dialogs, enter settings options, etc.
    if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x0D) {
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                SettingsMenuAccessibility::GetSingleton()->OnEnterPressed(m_mainMenuOpen, m_pauseMenuOpen);
                CheckForConfirmationDialog();
            });
        }
    }

    // Check for Escape key (0x1B) - may exit settings options view
    if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x1B) {
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([]() {
                SettingsMenuAccessibility::GetSingleton()->OnEscapePressed();
            });
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
        if (auto menu = RE::UI::GetSingleton()->GetMenu("MessageBoxMenu"sv)) {
            SpeakMessageBox(menu.get());
        }
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
    } else if (a_menuName == "DialogueMenu"sv) {
        m_dialogueMenuOpen = false;
        DialogueMenuAccessibility::GetSingleton()->OnDialogueMenuClosed();
    } else if (a_menuName == "SPECIALMenu"sv) {
        m_specialMenuOpen = false;
        SPECIALMenuAccessibility::GetSingleton()->Reset();
        REX::INFO("SPECIALMenu closed");
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

void MenuAccessibility::SpeakMessageBox(RE::IMenu* a_menu)
{
    if (!a_menu) {
        return;
    }

    auto* msgBoxMenu = static_cast<RE::MessageBoxMenu*>(a_menu);
    if (!msgBoxMenu->currentMessage) {
        REX::WARN("MessageBoxMenu has no currentMessage");
        return;
    }

    auto* msgData = msgBoxMenu->currentMessage;

    // Build the message to speak
    std::string speechText;

    // Add header if present
    if (msgData->headerText.size() > 0) {
        speechText += msgData->headerText.c_str();
        speechText += ". ";
    }

    // Add body text
    if (msgData->bodyText.size() > 0) {
        speechText += msgData->bodyText.c_str();
    }

    // Add button options
    if (!msgData->buttonText.empty()) {
        speechText += ". ";
        for (std::uint32_t i = 0; i < msgData->buttonText.size(); ++i) {
            if (i > 0) {
                speechText += ", ";
            }
            speechText += msgData->buttonText[i].c_str();
        }
    }

    REX::INFO("MessageBox: header='{}' body='{}' buttons={}",
        msgData->headerText.c_str(),
        msgData->bodyText.c_str(),
        msgData->buttonText.size());

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
