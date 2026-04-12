#include "MainMenuHook.h"
#include "LocalizationHelper.h"
#include "MCMAccessibility.h"
#include "SaveLoadMenuAccessibility.h"
#include "SettingsMenuAccessibility.h"
#include "SpeechManager.h"
#include <RE/P/PauseMenu.h>
#include <SRAL.h>

MainMenuHook* MainMenuHook::GetSingleton()
{
	static MainMenuHook singleton;
	return &singleton;
}

void MainMenuHook::Install()
{
	if (m_installed) {
		REX::DEBUG("MainMenuHook: Already installed");
		return;
	}

	// Hook MainMenu::AdvanceMovie (vtable index 4)
	// VTABLE::MainMenu[0] is the primary vtable (IMenu/GameMenuBase)
	static REL::THookVFT<void(RE::MainMenu*, float, std::uint64_t)> hook(
		VTABLE::MainMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	// Hook PauseMenu::AdvanceMovie (same vtable index)
	// PauseMenu uses the same SWF (MainMenu.swf) so the same hook function works
	static REL::THookVFT<void(RE::PauseMenu*, float, std::uint64_t)> pauseHook(
		VTABLE::PauseMenu[0],
		4,  // AdvanceMovie is at index 4
		reinterpret_cast<void (*)(RE::PauseMenu*, float, std::uint64_t)>(HookedAdvanceMovie));

	bool mainSuccess = hook.Enable();
	bool pauseSuccess = pauseHook.Enable();

	if (mainSuccess) {
		s_advanceMovieHook = &hook;
		REX::INFO("MainMenuHook: MainMenu AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("MainMenuHook: Failed to install MainMenu AdvanceMovie hook");
	}

	if (pauseSuccess) {
		s_pauseMenuAdvanceMovieHook = &pauseHook;
		REX::INFO("MainMenuHook: PauseMenu AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("MainMenuHook: Failed to install PauseMenu AdvanceMovie hook");
	}

	m_installed = mainSuccess || pauseSuccess;
}

void MainMenuHook::Uninstall()
{
	if (!m_installed) {
		return;
	}

	if (s_advanceMovieHook && s_advanceMovieHook->Disable()) {
		REX::INFO("MainMenuHook: MainMenu hook uninstalled");
	}

	if (s_pauseMenuAdvanceMovieHook && s_pauseMenuAdvanceMovieHook->Disable()) {
		REX::INFO("MainMenuHook: PauseMenu hook uninstalled");
	}

	m_installed = false;
}

void MainMenuHook::SetMainMenuOpen(bool a_open)
{
	m_mainMenuOpen = a_open;
	if (!a_open) {
		// Menu closed, reset panel handlers
		SettingsMenuAccessibility::GetSingleton()->Reset();
		SaveLoadMenuAccessibility::GetSingleton()->Reset();
		MCMAccessibility::GetSingleton()->Reset();
	}
}

void MainMenuHook::SetPauseMenuOpen(bool a_open)
{
	m_pauseMenuOpen = a_open;
	if (!a_open) {
		// Menu closed, reset panel handlers
		SettingsMenuAccessibility::GetSingleton()->Reset();
		SaveLoadMenuAccessibility::GetSingleton()->Reset();
		MCMAccessibility::GetSingleton()->Reset();
	}
}

void MainMenuHook::ResetState()
{
	m_lastPanel = ActivePanel::None;
	m_lastSelection = -1;
	m_lastConfirmText.clear();
	m_splashAnnounced = false;
	m_menuNameAnnounced = false;
}

void MainMenuHook::HookedAdvanceMovie(RE::MainMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	auto* hook = GetSingleton();

	// Call original function first to let the game update state
	// Use the appropriate hook based on which menu is open
	if (hook->m_pauseMenuOpen && s_pauseMenuAdvanceMovieHook) {
		(*s_pauseMenuAdvanceMovieHook)(reinterpret_cast<RE::PauseMenu*>(a_this), a_timeDelta, a_time);
	} else if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	// Then check for state changes
	if (hook->m_mainMenuOpen || hook->m_pauseMenuOpen) {
		hook->CheckStateChanges(a_this);
	}
}

void MainMenuHook::CheckStateChanges(RE::MainMenu* a_menu)
{
	if (!a_menu)
		return;

	// Detect which panel is currently active
	auto currentPanel = DetectActivePanel(a_menu);

	// Check for panel change
	if (currentPanel != m_lastPanel) {
		REX::INFO("MainMenuHook: Panel changed {} -> {}",
			static_cast<int>(m_lastPanel), static_cast<int>(currentPanel));
		m_lastPanel = currentPanel;
		m_lastSelection = -1;  // Reset selection BEFORE announcing to avoid double-speak
		OnPanelChanged(a_menu, currentPanel);
		return;
	}

	// MCM overlays the pause menu — its own handler owns every per-frame state
	// check and must suppress the underlying main-list poll, otherwise the mod
	// list behind it gets spoken on top of MCM navigation.
	if (currentPanel == ActivePanel::MCM) {
		MCMAccessibility::GetSingleton()->CheckState(a_menu);
		return;
	}

	// Always check main list selection regardless of detected panel
	// The main list selection only changes when user navigates it, not when in subpanels
	// This avoids issues with unreliable panel visibility detection
	CheckMainListSelectionFromClips(a_menu);

	// Panel-specific state checks (for overlays like confirm dialog)
	if (currentPanel == ActivePanel::Confirm) {
		OnConfirmDialog(a_menu);
	}
}

MainMenuHook::ActivePanel MainMenuHook::DetectActivePanel(RE::MainMenu* a_menu)
{
	// MCM is checked first: it overlays the pause menu and its own conflict
	// popup is drawn inside MCM's clips, not the base game's ConfirmPanel_mc,
	// so placing MCM above Confirm here is safe (see plan note on priority).
	if (MCMAccessibility::IsMCMVisible(a_menu)) {
		return ActivePanel::MCM;
	}

	bool confirmVis = IsPanelVisible(a_menu, CONFIRM_PANEL_PATH);
	bool settingsVis = IsPanelVisible(a_menu, SETTINGS_PANEL_PATH);
	bool saveloadVis = IsPanelVisible(a_menu, SAVELOAD_PANEL_PATH);

	// Check panels in priority order (overlays first)
	if (confirmVis) {
		return ActivePanel::Confirm;
	}
	if (settingsVis) {
		return ActivePanel::Settings;
	}
	if (saveloadVis) {
		return ActivePanel::SaveLoad;
	}

	// For main panel, use selection index as the indicator since the List_mc
	// doesn't have a standard "visible" property but selectedIndex works
	// If we can read a valid selection, the main list is active
	auto selection = GetSelectedIndex(a_menu);
	if (selection >= 0) {
		return ActivePanel::MainList;
	}

	// No overlay panels and no selection = splash screen or transitioning
	// Check if we're in a transitional state by seeing if the list exists
	if (a_menu && a_menu->uiMovie) {
		Scaleform::GFx::Value list;
		if (a_menu->uiMovie->GetVariable(&list, MAIN_LIST_PATH) && list.IsObject()) {
			// List exists but no selection = splash screen (press any key state)
			return ActivePanel::SplashScreen;
		}
	}

	return ActivePanel::None;
}

void MainMenuHook::OnPanelChanged(RE::MainMenu* a_menu, ActivePanel a_newPanel)
{
	switch (a_newPanel) {
		case ActivePanel::SplashScreen:
			OnSplashScreen(a_menu);
			break;

		case ActivePanel::MainList:
			// Note: Keystroke handler calls CheckMainListSelection() for navigation
			// because selectedIndex doesn't update without keyboard focus

			// Announce menu name when main list becomes active
			if (!m_menuNameAnnounced) {
				m_menuNameAnnounced = true;
				if (m_mainMenuOpen) {
					// Try to get localized "Main Menu" from game
					std::string menuName = LocalizationHelper::Translate("$MAIN MENU");
					if (menuName == "MAIN MENU") {
						menuName = "Main Menu";  // Use mixed case for speech
					}
					SpeakText(menuName);
				} else if (m_pauseMenuOpen) {
					SpeakText("Pause Menu");
				}
			}
			// Announce current selection
			OnMainListSelection(a_menu);
			break;

		case ActivePanel::Settings:
			// Announce "Settings" - using game's localization if available
			{
				std::string settingsName = LocalizationHelper::Translate("$Settings");
				SpeakText(settingsName);
			}
			// Tell settings handler to track its state
			// Note: Handler's CheckSelection will be called by the input handler
			break;

		case ActivePanel::SaveLoad:
			// SaveLoad handler will announce the panel name
			// The CheckSelection will be called by input handler
			break;

		case ActivePanel::Confirm:
			OnConfirmDialog(a_menu);
			break;

		case ActivePanel::MCM:
			MCMAccessibility::GetSingleton()->OnOpened();
			break;

		default:
			break;
	}
}

void MainMenuHook::OnSplashScreen(RE::MainMenu* a_menu)
{
	if (m_splashAnnounced)
		return;

	std::string splashText = GetSplashText(a_menu);
	if (!splashText.empty()) {
		m_splashAnnounced = true;
		SpeakText(LocalizationHelper::CleanString(splashText));
	}
}

void MainMenuHook::OnMainListSelection(RE::MainMenu* a_menu)
{
	auto currentSelection = GetSelectedIndex(a_menu);
	if (currentSelection >= 0 && currentSelection != m_lastSelection) {
		m_lastSelection = currentSelection;
		auto text = GetSelectedItemText(a_menu, currentSelection);
		if (!text.empty()) {
			SpeakText(LocalizationHelper::Translate(text));
		}
	}
}

void MainMenuHook::CheckMainListSelectionFromClips(RE::MainMenu* a_menu)
{
	// Called every frame from AdvanceMovie when on MainList panel
	// Uses clip detection since selectedIndex doesn't update without keyboard focus
	auto currentSelection = GetSelectedIndexFromClips(a_menu);
	if (currentSelection >= 0 && currentSelection != m_lastSelection) {
		m_lastSelection = currentSelection;
		auto text = GetSelectedItemText(a_menu, currentSelection);
		if (!text.empty()) {
			SpeakText(LocalizationHelper::Translate(text));
		}
	}
}

void MainMenuHook::CheckMainListSelection()
{
	// Called from keystroke handler - need to get the menu and check selection
	auto ui = RE::UI::GetSingleton();
	if (!ui) {
		return;
	}

	RE::IMenu* menu = nullptr;
	if (m_mainMenuOpen) {
		menu = ui->GetMenu("MainMenu"sv).get();
	} else if (m_pauseMenuOpen) {
		menu = ui->GetMenu("PauseMenu"sv).get();
	}

	if (!menu || !menu->uiMovie) {
		return;
	}

	auto* mainMenu = static_cast<RE::MainMenu*>(menu);

	// Try to find the selected entry by checking which clip has selected=true
	// This works even when selectedIndex property doesn't update
	auto currentSelection = GetSelectedIndexFromClips(mainMenu);
	if (currentSelection >= 0 && currentSelection != m_lastSelection) {
		m_lastSelection = currentSelection;
		auto text = GetSelectedItemText(mainMenu, currentSelection);
		if (!text.empty()) {
			SpeakText(LocalizationHelper::Translate(text));
		}
	}
}

void MainMenuHook::CheckConfirmDialog()
{
	if (!m_mainMenuOpen && !m_pauseMenuOpen) {
		return;
	}

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	RE::IMenu* menu = nullptr;
	if (m_mainMenuOpen) {
		menu = ui->GetMenu("MainMenu"sv).get();
	} else if (m_pauseMenuOpen) {
		menu = ui->GetMenu("PauseMenu"sv).get();
	}

	if (!menu || !menu->uiMovie)
		return;

	auto* mainMenu = static_cast<RE::MainMenu*>(menu);

	// Check if confirm panel is visible and read its text
	if (IsPanelVisible(mainMenu, CONFIRM_PANEL_PATH)) {
		OnConfirmDialog(mainMenu);
	}
}

std::int32_t MainMenuHook::GetSelectedIndexFromClips(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	// First try the standard selectedIndex
	auto selectedIndex = GetSelectedIndex(a_menu);

	// Don't rely solely on selectedIndex - it doesn't update without focus
	// But if it changed from what we tracked, use it
	if (selectedIndex >= 0 && selectedIndex != m_lastSelection) {
		return selectedIndex;
	}

	// Fallback: Check each entry clip for selected=true
	// The EntryHolder_mc is a protected member, so we must find it via the display list
	// Get the list object first
	Scaleform::GFx::Value list;
	if (!a_menu->uiMovie->GetVariable(&list, MAIN_LIST_PATH) || !list.IsObject()) {
		return -1;
	}

	// Get number of children in the list
	Scaleform::GFx::Value listNumChildren;
	if (!list.GetMember("numChildren", &listNumChildren)) {
		return -1;
	}

	std::uint32_t listChildCount = 0;
	if (listNumChildren.IsInt()) {
		listChildCount = static_cast<std::uint32_t>(listNumChildren.GetInt());
	} else if (listNumChildren.IsNumber()) {
		listChildCount = static_cast<std::uint32_t>(listNumChildren.GetNumber());
	}

	// Find the EntryHolder by iterating through list children
	// EntryHolder is added after border, and contains the entry clips
	Scaleform::GFx::Value entryHolder;
	bool                  foundEntryHolder = false;

	for (std::uint32_t i = 0; i < listChildCount && i < 10; ++i) {
		Scaleform::GFx::Value child;
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<double>(i);
		if (list.Invoke("getChildAt", &child, args, 1) && child.IsObject()) {
			// Check if this child has numChildren (indicating it's a container)
			Scaleform::GFx::Value childNumChildren;
			if (child.GetMember("numChildren", &childNumChildren)) {
				std::uint32_t count = 0;
				if (childNumChildren.IsInt()) {
					count = static_cast<std::uint32_t>(childNumChildren.GetInt());
				} else if (childNumChildren.IsNumber()) {
					count = static_cast<std::uint32_t>(childNumChildren.GetNumber());
				}

				// EntryHolder should have children (the entry clips)
				if (count > 0) {
					// Check if first child has a "selected" property (entry clip indicator)
					Scaleform::GFx::Value firstChild;
					Scaleform::GFx::Value firstArgs[1];
					firstArgs[0] = 0.0;
					if (child.Invoke("getChildAt", &firstChild, firstArgs, 1) && firstChild.IsObject()) {
						Scaleform::GFx::Value selectedProp;
						if (firstChild.GetMember("selected", &selectedProp)) {
							entryHolder = child;
							foundEntryHolder = true;
							break;
						}
					}
				}
			}
		}
	}

	if (!foundEntryHolder) {
		return -1;
	}

	// Get number of children in EntryHolder
	Scaleform::GFx::Value numChildren;
	if (!entryHolder.GetMember("numChildren", &numChildren)) {
		return -1;
	}

	std::uint32_t childCount = 0;
	if (numChildren.IsInt()) {
		childCount = static_cast<std::uint32_t>(numChildren.GetInt());
	} else if (numChildren.IsNumber()) {
		childCount = static_cast<std::uint32_t>(numChildren.GetNumber());
	}

	// Check each child for selected property
	for (std::uint32_t i = 0; i < childCount && i < 20; ++i) {
		Scaleform::GFx::Value child;
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<double>(i);
		if (entryHolder.Invoke("getChildAt", &child, args, 1) && child.IsObject()) {
			Scaleform::GFx::Value selected;
			if (child.GetMember("selected", &selected) && selected.IsBoolean()) {
				if (selected.GetBoolean()) {
					// Found selected entry, get its itemIndex
					Scaleform::GFx::Value itemIndex;
					if (child.GetMember("itemIndex", &itemIndex)) {
						if (itemIndex.IsInt()) {
							return itemIndex.GetInt();
						} else if (itemIndex.IsNumber()) {
							return static_cast<std::int32_t>(itemIndex.GetNumber());
						}
					}
					return static_cast<std::int32_t>(i);
				}
			}
		}
	}

	return -1;
}

void MainMenuHook::OnConfirmDialog(RE::MainMenu* a_menu)
{
	std::string confirmText = GetConfirmText(a_menu);

	// Only announce if text changed (avoid repeating every frame)
	if (!confirmText.empty() && confirmText != m_lastConfirmText) {
		m_lastConfirmText = confirmText;

		// Trim whitespace
		confirmText.erase(0, confirmText.find_first_not_of(" \t\n\r"));
		if (!confirmText.empty()) {
			confirmText.erase(confirmText.find_last_not_of(" \t\n\r") + 1);
		}

		if (!confirmText.empty()) {
			// Get localized confirm/cancel text from game
			std::string confirmKey = LocalizationHelper::Translate("$CONFIRM");
			std::string cancelKey = LocalizationHelper::Translate("$CANCEL");

			// Build announcement
			std::string announcement = confirmText;
			announcement += ". ";
			announcement += confirmKey;
			announcement += ", ";
			announcement += cancelKey;

			SpeakText(announcement);
		}
	}
}

bool MainMenuHook::IsPanelVisible(RE::MainMenu* a_menu, const char* a_panelPath)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value panel;
	if (!a_menu->uiMovie->GetVariable(&panel, a_panelPath)) {
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

bool MainMenuHook::IsMainPanelVisible(RE::MainMenu* a_menu)
{
	return IsPanelVisible(a_menu, MAIN_PANEL_PATH);
}

std::int32_t MainMenuHook::GetSelectedIndex(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	std::string           indexPath = std::string(MAIN_LIST_PATH) + ".selectedIndex";
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

std::string MainMenuHook::GetSelectedItemText(RE::MainMenu* a_menu, std::int32_t a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return "";
	}

	std::string           entryListPath = std::string(MAIN_LIST_PATH) + ".entryList";
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

std::string MainMenuHook::GetConfirmText(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value confirmText;
	if (a_menu->uiMovie->GetVariable(&confirmText, CONFIRM_TEXT_PATH) && confirmText.IsString()) {
		return confirmText.GetString();
	}

	return "";
}

std::string MainMenuHook::GetSplashText(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value splashText;
	if (a_menu->uiMovie->GetVariable(&splashText, SPLASH_TEXT_PATH) && splashText.IsString()) {
		return splashText.GetString();
	}

	return "";
}

void MainMenuHook::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
