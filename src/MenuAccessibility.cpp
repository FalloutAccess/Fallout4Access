#include "MenuAccessibility.h"
#include "ContextualInfoAnnouncer.h"
#include "MainMenuHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include "SettingsMenuAccessibility.h"
#include "SaveLoadMenuAccessibility.h"
#include "DialogueMenuHook.h"
#include "SPECIALMenuAccessibility.h"
#include "TerminalMenuAccessibility.h"
#include "VATSMenuAccessibility.h"
#include "BarterMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "LevelUpMenuAccessibility.h"
#include "MessageBoxMenuHook.h"
#include "PipBoyMapPage.h"
#include <SRAL.h>
#include <REX/W32/USER32.h>

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
    return m_mainMenuOpen || m_pauseMenuOpen || m_dialogueMenuOpen || m_specialMenuOpen || m_terminalMenuOpen || m_vatsMenuOpen || m_pipBoyMenuOpen || m_examineMenuOpen || m_powerArmorModMenuOpen || m_cookingMenuOpen || m_robotModMenuOpen || m_levelUpMenuOpen;
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
    if (device == RE::INPUT_DEVICE::kKeyboard) {
        if (idCode == 0x26 || idCode == 0x28) {  // Up/Down arrows
            isNavigationKey = true;
        }
        if (idCode == 0x57 || idCode == 0x53) {  // W/S
            isNavigationKey = true;
        }
    }
    // Gamepad: DPad Up/Down
    else if (device == RE::INPUT_DEVICE::kGamepad) {
        if (idCode == 0x0001 || idCode == 0x0002) {  // DPad Up/Down
            isNavigationKey = true;
        }
    }

    if (isNavigationKey) {
        // Delay the check to next frame so the game has time to update the selection
        if (auto* taskInterface = F4SE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                // Main/Pause menus: Need keystroke-triggered checks because selectedIndex
                // doesn't update without keyboard focus on the Scaleform list
                if (m_mainMenuOpen || m_pauseMenuOpen) {
                    MainMenuHook::GetSingleton()->CheckMainListSelection();
                }
                // Settings/SaveLoad: Keystroke-triggered checks for selection
                SettingsMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                SaveLoadMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                // DialogueMenu: AdvanceMovie hook handles selection tracking per-frame
                if (m_specialMenuOpen) {
                    SPECIALMenuAccessibility::GetSingleton()->CheckSelection(m_mainMenuOpen, m_pauseMenuOpen);
                }
                // Terminal/VATS/PipBoy: AdvanceMovie hooks handle state tracking per-frame
                // Confirmation dialog: MainMenuHook handles this per-frame
            });
        }
    }

    // F key to switch between container/vendor and player inventory
    if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x46) {  // F key
        if (m_containerMenuOpen) {
            ContainerMenuAccessibility::GetSingleton()->SwitchSides();
        }
        if (m_barterMenuOpen) {
            BarterMenuAccessibility::GetSingleton()->SwitchSides();
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

    // PipBoy page/tab changes are detected per-frame via AdvanceMovie hook.
    // Only scanner keys for Map page remain as key-press-driven (custom accessibility actions).

    // Scanner keys for Pip-Boy Map page navigation (Windows VK codes)
    // These keys are only handled on the Map page (page 3)
    if (m_pipBoyMenuOpen && device == RE::INPUT_DEVICE::kKeyboard) {
        // Check if we're on the map page
        auto* mapPage = PipBoyMenuAccessibility::GetSingleton()->GetMapPage();

        // Only handle scanner keys if map page is active and we have a valid page pointer
        bool onMapPage = (mapPage != nullptr);

        // Verify we're actually on the map page by checking current page
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            auto menu = ui->GetMenu("PipboyMenu"sv);
            if (menu) {
                auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
                if (pipboyMenu && !pipboyMenu->dataObj.IsUndefined() && pipboyMenu->dataObj.IsObject()) {
                    Scaleform::GFx::Value pageValue;
                    if (pipboyMenu->dataObj.GetMember("CurrentPage", &pageValue)) {
                        int pageNum = 0;
                        if (pageValue.IsInt()) pageNum = pageValue.GetInt();
                        else if (pageValue.IsNumber()) pageNum = static_cast<int>(pageValue.GetNumber());
                        else if (pageValue.IsUInt()) pageNum = static_cast<int>(pageValue.GetUInt());
                        onMapPage = (pageNum == 3);  // MAP page is 3
                    }
                }
            }
        }

        if (onMapPage && mapPage) {
            bool shiftHeld = REX::W32::GetKeyState(REX::W32::VK_SHIFT) < 0;

            // Page Down (0x22 / VK_NEXT) - Next marker
            if (idCode == 0x22) {
                if (auto* taskInterface = F4SE::GetTaskInterface()) {
                    taskInterface->AddTask([mapPage]() {
                        mapPage->OnNextMarker();
                    });
                }
            }
            // Page Up (0x21 / VK_PRIOR) - Previous marker
            else if (idCode == 0x21) {
                if (auto* taskInterface = F4SE::GetTaskInterface()) {
                    taskInterface->AddTask([mapPage]() {
                        mapPage->OnPreviousMarker();
                    });
                }
            }
            // Home (0x24 / VK_HOME) - Announce details or set reference point
            else if (idCode == 0x24) {
                if (shiftHeld) {
                    // Shift+Home: Set current marker as distance reference point
                    if (auto* taskInterface = F4SE::GetTaskInterface()) {
                        taskInterface->AddTask([mapPage]() {
                            mapPage->OnSetReferencePoint();
                        });
                    }
                } else {
                    // Home: Announce full marker details
                    if (auto* taskInterface = F4SE::GetTaskInterface()) {
                        taskInterface->AddTask([mapPage]() {
                            mapPage->OnAnnounceDetails();
                        });
                    }
                }
            }
            // End (0x23 / VK_END) - Cycle filter
            else if (idCode == 0x23) {
                if (auto* taskInterface = F4SE::GetTaskInterface()) {
                    taskInterface->AddTask([mapPage]() {
                        mapPage->OnCycleFilter();
                    });
                }
            }
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

    // H key - contextual info (combat stats in menus for now, extensible for menu-specific info later)
    if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x48) {  // VK_H
        REX::INFO("MenuAccessibility: H pressed, announcing combat stats");
        ContextualInfoAnnouncer::GetSingleton()->AnnounceCombatStats();
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
        REX::INFO("MessageBoxMenu opened");
        MessageBoxMenuHook::GetSingleton()->ResetState();
        MessageBoxMenuHook::GetSingleton()->SetMenuOpen(true);
    } else if (a_menuName == "DialogueMenu"sv) {
        m_dialogueMenuOpen = true;
        DialogueMenuHook::GetSingleton()->OnDialogueMenuOpened();
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
    } else if (a_menuName == "PowerArmorModMenu"sv) {
        m_powerArmorModMenuOpen = true;
        REX::INFO("PowerArmorModMenu opened");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuOpened();
    } else if (a_menuName == "CookingMenu"sv) {
        m_cookingMenuOpen = true;
        REX::INFO("CookingMenu opened");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuOpened();
    } else if (a_menuName == "RobotModMenu"sv) {
        m_robotModMenuOpen = true;
        REX::INFO("RobotModMenu opened");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuOpened();
    } else if (a_menuName == "ContainerMenu"sv) {
        m_containerMenuOpen = true;
        REX::INFO("ContainerMenu opened");
        ContainerMenuAccessibility::GetSingleton()->OnContainerMenuOpened();
    } else if (a_menuName == "BarterMenu"sv) {
        m_barterMenuOpen = true;
        REX::INFO("BarterMenu opened");
        BarterMenuAccessibility::GetSingleton()->OnBarterMenuOpened();
    } else if (a_menuName == "LevelUpMenu"sv) {
        m_levelUpMenuOpen = true;
        REX::INFO("LevelUpMenu opened");
        LevelUpMenuAccessibility::GetSingleton()->OnLevelUpMenuOpened();
    } else if (a_menuName == "LoadingMenu"sv) {
        m_loadingMenuOpen = true;
        REX::INFO("LoadingMenu opened");
        LoadingMenuHook::GetSingleton()->Install();
        LoadingMenuHook::GetSingleton()->ResetState();
        LoadingMenuHook::GetSingleton()->SetMenuOpen(true);
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
        REX::INFO("MessageBoxMenu closed");
        MessageBoxMenuHook::GetSingleton()->SetMenuOpen(false);
    } else if (a_menuName == "DialogueMenu"sv) {
        m_dialogueMenuOpen = false;
        DialogueMenuHook::GetSingleton()->OnDialogueMenuClosed();
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
    } else if (a_menuName == "PowerArmorModMenu"sv) {
        m_powerArmorModMenuOpen = false;
        REX::INFO("PowerArmorModMenu closed");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuClosed();
    } else if (a_menuName == "CookingMenu"sv) {
        m_cookingMenuOpen = false;
        REX::INFO("CookingMenu closed");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuClosed();
    } else if (a_menuName == "RobotModMenu"sv) {
        m_robotModMenuOpen = false;
        REX::INFO("RobotModMenu closed");
        ExamineMenuAccessibility::GetSingleton()->OnExamineMenuClosed();
    } else if (a_menuName == "ContainerMenu"sv) {
        m_containerMenuOpen = false;
        REX::INFO("ContainerMenu closed");
        ContainerMenuAccessibility::GetSingleton()->OnContainerMenuClosed();
    } else if (a_menuName == "BarterMenu"sv) {
        m_barterMenuOpen = false;
        REX::INFO("BarterMenu closed");
        BarterMenuAccessibility::GetSingleton()->OnBarterMenuClosed();
    } else if (a_menuName == "LevelUpMenu"sv) {
        m_levelUpMenuOpen = false;
        REX::INFO("LevelUpMenu closed");
        LevelUpMenuAccessibility::GetSingleton()->OnLevelUpMenuClosed();
    } else if (a_menuName == "LoadingMenu"sv) {
        m_loadingMenuOpen = false;
        REX::INFO("LoadingMenu closed");
        LoadingMenuHook::GetSingleton()->SetMenuOpen(false);
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

// NOTE: ScheduleMessageBoxRead, SpeakMessageBox, SpeakMessageBoxFromScaleform removed
// MessageBox accessibility is now handled by MessageBoxMenuHook per-frame

// NOTE: IsMenuPanelVisible and TryAnnounceMenu removed
// Panel visibility and menu announcements are now handled by MainMenuHook per-frame
