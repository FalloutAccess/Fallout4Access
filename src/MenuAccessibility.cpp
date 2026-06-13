#include "MenuAccessibility.h"
#include "DialogueMenuHook.h"
#include "ExamineConfirmMenuHook.h"
#include "LevelUpMenuAccessibility.h"
#include "MCMKeybindDispatcher.h"
#include "MainMenuHook.h"
#include "MessageBoxMenuHook.h"
#include "SPECIALMenuAccessibility.h"
#include "SaveLoadMenuAccessibility.h"
#include "SettingsMenuAccessibility.h"
#include "SpeechManager.h"
#include "TerminalMenuAccessibility.h"
#include "VATSMenuAccessibility.h"
#include <SRAL.h>

MenuAccessibility* MenuAccessibility::GetSingleton()
{
	static MenuAccessibility singleton;
	return &singleton;
}

bool MenuAccessibility::Register()
{
	if (m_registered) {
		return true;
	}
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		// UI singleton not constructed yet (early OG preloader load). The caller
		// retries on a later F4SE message, so don't log — it would be a false alarm.
		return false;
	}
	ui->RegisterSink<RE::MenuOpenCloseEvent>(this);
	m_registered = true;
	REX::INFO("MenuAccessibility registered for menu events");

	// If a menu was already open when we registered, its open event was missed.
	// This happens on OG: the xSE PluginPreloader delays our setup until after the
	// main menu is already showing, so the MainMenu open event fired before our
	// sink existed. Replay it now so the menu installs its hook and announces.
	if (ui->GetMenuOpen("MainMenu")) {
		REX::INFO("MenuAccessibility: MainMenu already open at registration; syncing state");
		OnMenuOpened("MainMenu");
	}

	return true;
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
	return m_mainMenuOpen || m_pauseMenuOpen || m_dialogueMenuOpen || m_specialMenuOpen || m_terminalMenuOpen || m_vatsMenuOpen || m_pipBoyMenuOpen || m_examineMenuOpen || m_powerArmorModMenuOpen || m_cookingMenuOpen || m_robotModMenuOpen || m_containerMenuOpen || m_barterMenuOpen || m_levelUpMenuOpen || m_workshopMenuOpen || m_bookMenuOpen || m_sleepWaitMenuOpen || m_sitWaitMenuOpen;
}

bool MenuAccessibility::IsKeybindDispatchContext() const
{
	return m_pipBoyMenuOpen ||
	       m_containerMenuOpen ||
	       m_barterMenuOpen ||
	       m_levelUpMenuOpen ||
	       m_workshopMenuOpen;
}

void MenuAccessibility::OnButtonEvent(const RE::ButtonEvent* a_event)
{
	if (!a_event || !a_event->QJustPressed()) {
		return;
	}

	// Dispatch MCM-configured accessibility keybinds (H, F, scanner keys,
	// etc.) in menus whose contextual handlers in KeybindActions actually
	// do something — see KeybindActions::AnnounceStats / SwitchMenuSide.
	// Other tracked menus (MainMenu, PauseMenu, SaveLoad, Settings, Book,
	// Terminal, Dialogue, VATS, …) are excluded on purpose: none of our
	// actions have a meaningful behavior there, and firing would just
	// produce a spurious "Stats unavailable" or similar.
	if (IsKeybindDispatchContext()) {
		if (MCMKeybindDispatcher::GetSingleton()->TryDispatch(a_event)) {
			return;
		}
	}

	// Use QRawUserEvent() for device-agnostic input (respects game key remaps)
	auto userEvent = std::string(a_event->QRawUserEvent().c_str());

	// Navigation: Up/Down user events (covers arrows, DPad, and any remapped keys)
	// Also check Forward/Back for W/S keys which map to gameplay context
	bool isNavigationKey = (userEvent == "Up" || userEvent == "Down" ||
							userEvent == "Forward" || userEvent == "Back");

	// Value change: Left/Right user events (covers arrows, DPad, and remapped keys)
	// Also check StrafeLeft/StrafeRight for A/D keys in gameplay context
	bool isValueChangeKey = (userEvent == "Left" || userEvent == "Right" ||
							 userEvent == "StrafeLeft" || userEvent == "StrafeRight");

	// Stop in-flight speech for selection/value changes so the next
	// announcement (which arrives 1-2 frames later via the deferred check or
	// a Scaleform callback) plays cleanly. Screen readers only auto-interrupt
	// on a fixed key set; we can't rely on that, especially since menu nav
	// uses arrow/WASD/DPad which the user can remap. Accept/Cancel are
	// intentionally excluded — those are confirmations, not selection churn,
	// so cutting off prior speech there feels jarring.
	if (isNavigationKey || isValueChangeKey) {
		SpeechManager::GetSingleton()->StopSpeech();
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

	if (isValueChangeKey) {
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

	// PipBoy Map page scanner keys (next/prev marker, announce, set reference,
	// cycle filter) are handled via MCM keybinds in KeybindActions. The keybind
	// handlers dispatch to PipBoyMapPage when the map page is active.

	// Accept: user events "Accept" (Enter) and "Activate" (E key)
	bool isAcceptKey = (userEvent == "Accept" || userEvent == "Activate");

	if (isAcceptKey) {
		// Terminal: AdvanceMovie hook handles mode changes per-frame
		// MainMenu/PauseMenu: AdvanceMovie hooks handle confirm dialog detection per-frame

		if (auto* taskInterface = F4SE::GetTaskInterface()) {
			taskInterface->AddTask([this]() {
				SettingsMenuAccessibility::GetSingleton()->OnEnterPressed(m_mainMenuOpen, m_pauseMenuOpen);
			});
		}
	}

	// Cancel: user event "Cancel" (Tab/Escape/B button)
	bool isBackKey = (userEvent == "Cancel");

	if (isBackKey) {
		if (auto* taskInterface = F4SE::GetTaskInterface()) {
			taskInterface->AddTask([this]() {
				SettingsMenuAccessibility::GetSingleton()->OnEscapePressed();
			});
		}
	}

	// VATS-specific key handlers
	if (m_vatsMenuOpen) {
		bool isQueueKey = (userEvent == "VATS");
		if (isQueueKey) {
			if (auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([]() {
					VATSMenuAccessibility::GetSingleton()->CheckQueuedShots();
				});
			}
		}

		bool isCriticalKey = (userEvent == "ApplyCritical");
		if (isCriticalKey) {
			if (auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([]() {
					VATSMenuAccessibility::GetSingleton()->CheckCriticalCharge();
				});
			}
		}
	}

	// F and H key actions are now handled via MCM keybinds (KeybindActions).
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

	if (a_menuName == "BookMenu"sv) {
		m_bookMenuOpen = true;
		REX::INFO("BookMenu opened");
		BookMenuAccessibility::GetSingleton()->OnBookMenuOpened();
	} else if (a_menuName == "MainMenu"sv) {
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
		// Pick up any keybind remaps the user made since last open
		MCMKeybindDispatcher::GetSingleton()->LoadFromFile();
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
	} else if (a_menuName == "ExamineConfirmMenu"sv) {
		REX::INFO("ExamineConfirmMenu opened");
		ExamineConfirmMenuHook::GetSingleton()->ResetState();
		ExamineConfirmMenuHook::GetSingleton()->SetMenuOpen(true);
	} else if (a_menuName == "ContainerMenu"sv) {
		m_containerMenuOpen = true;
		REX::INFO("ContainerMenu opened");
		MCMKeybindDispatcher::GetSingleton()->LoadFromFile();
		ContainerMenuAccessibility::GetSingleton()->OnContainerMenuOpened();
	} else if (a_menuName == "BarterMenu"sv) {
		m_barterMenuOpen = true;
		REX::INFO("BarterMenu opened");
		MCMKeybindDispatcher::GetSingleton()->LoadFromFile();
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
	} else if (a_menuName == "WorkshopMenu"sv) {
		m_workshopMenuOpen = true;
		REX::INFO("WorkshopMenu opened");
		WorkshopMenuAccessibility::GetSingleton()->OnWorkshopMenuOpened();
	} else if (a_menuName == "Workshop_CaravanMenu"sv) {
		m_caravanMenuOpen = true;
		REX::INFO("Workshop_CaravanMenu opened");
		WorkshopCaravanMenuAccessibility::GetSingleton()->OnCaravanMenuOpened();
	} else if (a_menuName == "SleepWaitMenu"sv) {
		m_sleepWaitMenuOpen = true;
		REX::INFO("SleepWaitMenu opened");
		SleepWaitMenuAccessibility::GetSingleton()->OnSleepWaitMenuOpened();
	} else if (a_menuName == "SitWaitMenu"sv) {
		m_sitWaitMenuOpen = true;
		REX::INFO("SitWaitMenu opened");
		SleepWaitMenuAccessibility::GetSingleton()->OnSitWaitMenuOpened();
	}
}

void MenuAccessibility::OnMenuClosed(const RE::BSFixedString& a_menuName)
{
	if (a_menuName == "BookMenu"sv) {
		m_bookMenuOpen = false;
		REX::INFO("BookMenu closed");
		BookMenuAccessibility::GetSingleton()->OnBookMenuClosed();
	} else if (a_menuName == "MainMenu"sv) {
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
	} else if (a_menuName == "ExamineConfirmMenu"sv) {
		REX::INFO("ExamineConfirmMenu closed");
		ExamineConfirmMenuHook::GetSingleton()->SetMenuOpen(false);
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
	} else if (a_menuName == "WorkshopMenu"sv) {
		m_workshopMenuOpen = false;
		REX::INFO("WorkshopMenu closed");
		WorkshopMenuAccessibility::GetSingleton()->OnWorkshopMenuClosed();
	} else if (a_menuName == "Workshop_CaravanMenu"sv) {
		m_caravanMenuOpen = false;
		REX::INFO("Workshop_CaravanMenu closed");
		WorkshopCaravanMenuAccessibility::GetSingleton()->OnCaravanMenuClosed();
	} else if (a_menuName == "SleepWaitMenu"sv) {
		m_sleepWaitMenuOpen = false;
		REX::INFO("SleepWaitMenu closed");
		SleepWaitMenuAccessibility::GetSingleton()->OnSleepWaitMenuClosed();
	} else if (a_menuName == "SitWaitMenu"sv) {
		m_sitWaitMenuOpen = false;
		REX::INFO("SitWaitMenu closed");
		SleepWaitMenuAccessibility::GetSingleton()->OnSitWaitMenuClosed();
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
