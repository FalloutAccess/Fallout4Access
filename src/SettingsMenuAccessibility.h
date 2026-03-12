#pragma once

#include "pch.h"

class SettingsMenuAccessibility
{
public:
	static SettingsMenuAccessibility* GetSingleton();

	// Called from MenuAccessibility on up/down navigation
	void CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called from MenuAccessibility on left/right navigation
	void CheckValueChange(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called from MenuAccessibility on Enter key press
	void OnEnterPressed(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called from MenuAccessibility on Escape key press
	void OnEscapePressed();

	// Called when parent menu closes
	void Reset();

	bool IsOpen() const { return m_isOpen; }
	bool IsInOptionsView() const { return m_inOptionsView; }

private:
	SettingsMenuAccessibility() = default;

	RE::IMenu*   GetActiveMenu(bool a_mainMenuOpen, bool a_pauseMenuOpen);
	const char*  GetCurrentListPath();  // Returns OPTIONS_LIST_PATH or CONTROLS_LIST_PATH based on category
	std::int32_t GetSelectedIndex(RE::IMenu* a_menu, const char* a_listPath);
	std::string  GetSelectedItemText(RE::IMenu* a_menu, const char* a_listPath);
	std::string  GetSettingsOptionValueText(RE::IMenu* a_menu, int a_index);
	std::string  GetControlsBindingText(RE::IMenu* a_menu, int a_index);
	bool         IsSettingsPanelVisible(RE::IMenu* a_menu);
	void         SpeakText(const std::string& a_text);
	std::string  CleanLocalizationString(const std::string& a_text);

	bool        m_isOpen = false;
	bool        m_inOptionsView = false;  // true when user has entered the options list
	int         m_lastCategorySelection = -1;
	int         m_lastOptionSelection = -1;
	std::string m_lastAnnouncedValue;  // Track last announced value for change detection

	// Scaleform paths for settings UI
	static constexpr const char* SETTINGS_PANEL_PATH = "root.Menu_mc.SettingsPanel_mc";
	static constexpr const char* CATEGORY_LIST_PATH = "root.Menu_mc.SettingsPanel_mc.SettingsList_mc";
	static constexpr const char* OPTIONS_LIST_PATH = "root.Menu_mc.OptionsPanel_mc.Fader_mc.List_mc";
	static constexpr const char* CONTROLS_LIST_PATH = "root.Menu_mc.ControlsPanel_mc.Fader_mc.List_mc";

	// Category indices
	static constexpr int CATEGORY_GAMEPLAY = 0;
	static constexpr int CATEGORY_CONTROLS = 1;
	static constexpr int CATEGORY_DISPLAY = 2;
	static constexpr int CATEGORY_AUDIO = 3;
};
