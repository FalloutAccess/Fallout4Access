#include "SettingsMenuAccessibility.h"
#include "SpeechManager.h"
#include <SRAL.h>

SettingsMenuAccessibility* SettingsMenuAccessibility::GetSingleton()
{
	static SettingsMenuAccessibility singleton;
	return &singleton;
}

void SettingsMenuAccessibility::Reset()
{
	m_isOpen = false;
	m_inOptionsView = false;
	m_lastCategorySelection = -1;
	m_lastOptionSelection = -1;
	m_lastAnnouncedValue.clear();
}

RE::IMenu* SettingsMenuAccessibility::GetActiveMenu(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return nullptr;

	if (a_mainMenuOpen) {
		if (auto menu = ui->GetMenu("MainMenu"sv)) {
			return menu.get();
		}
	} else if (a_pauseMenuOpen) {
		if (auto menu = ui->GetMenu("PauseMenu"sv)) {
			return menu.get();
		}
	}
	return nullptr;
}

const char* SettingsMenuAccessibility::GetCurrentListPath()
{
	if (m_lastCategorySelection == CATEGORY_CONTROLS) {
		return CONTROLS_LIST_PATH;
	}
	return OPTIONS_LIST_PATH;
}

bool SettingsMenuAccessibility::IsSettingsPanelVisible(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value panel;
	if (!a_menu->uiMovie->GetVariable(&panel, SETTINGS_PANEL_PATH)) {
		return false;
	}

	if (!panel.IsObject()) {
		return false;
	}

	Scaleform::GFx::Value visible;
	if (!panel.GetMember("visible", &visible) || !visible.IsBoolean()) {
		return false;
	}

	return visible.GetBoolean();
}

std::int32_t SettingsMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu, const char* a_listPath)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	std::string           indexPath = std::string(a_listPath) + ".selectedIndex";
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

std::string SettingsMenuAccessibility::GetSelectedItemText(RE::IMenu* a_menu, const char* a_listPath)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	auto selectedIndex = GetSelectedIndex(a_menu, a_listPath);
	if (selectedIndex < 0) {
		return "";
	}

	std::string           entryListPath = std::string(a_listPath) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return "";
	}

	if (static_cast<std::uint32_t>(selectedIndex) >= entryList.GetArraySize()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(selectedIndex), &entry) || !entry.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value textValue;
	if (entry.GetMember("text", &textValue) && textValue.IsString()) {
		return textValue.GetString();
	}

	return "";
}

std::string SettingsMenuAccessibility::GetSettingsOptionValueText(RE::IMenu* a_menu, int a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return "";
	}

	const char*           listPath = GetCurrentListPath();
	std::string           entryListPath = std::string(listPath) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return "";
	}

	// Get movieType to determine how to format value (0=scrollbar, 1=stepper, 2=checkbox)
	Scaleform::GFx::Value movieType;
	if (!entry.GetMember("movieType", &movieType)) {
		return "";
	}

	int type = -1;
	if (movieType.IsNumber()) {
		type = static_cast<int>(movieType.GetNumber());
	} else if (movieType.IsUInt()) {
		type = static_cast<int>(movieType.GetUInt());
	} else if (movieType.IsInt()) {
		type = movieType.GetInt();
	}

	// Try to get value from rendered clip first (most up-to-date)
	// Get the clipIndex for this entry
	Scaleform::GFx::Value clipIndex;
	int                   clipIdx = -1;
	if (entry.GetMember("clipIndex", &clipIndex)) {
		if (clipIndex.IsNumber()) {
			clipIdx = static_cast<int>(clipIndex.GetNumber());
		} else if (clipIndex.IsInt()) {
			clipIdx = clipIndex.GetInt();
		} else if (clipIndex.IsUInt()) {
			clipIdx = static_cast<int>(clipIndex.GetUInt());
		}
	}

	// Try to get the rendered clip and read its value property
	double clipValue = -1;
	bool   gotClipValue = false;
	if (clipIdx >= 0) {
		Scaleform::GFx::Value list;
		if (a_menu->uiMovie->GetVariable(&list, listPath)) {
			Scaleform::GFx::Value args[1];
			args[0] = static_cast<std::uint32_t>(clipIdx);
			Scaleform::GFx::Value renderedClip;
			if (list.Invoke("GetClipByIndex", &renderedClip, args, 1) && renderedClip.IsObject()) {
				Scaleform::GFx::Value clipValueProp;
				if (renderedClip.GetMember("value", &clipValueProp) && clipValueProp.IsNumber()) {
					clipValue = clipValueProp.GetNumber();
					gotClipValue = true;
				}
			}
		}
	}

	// Fall back to entry.value if we couldn't get from clip
	double valueNum = 0;
	if (gotClipValue) {
		valueNum = clipValue;
	} else {
		Scaleform::GFx::Value value;
		if (entry.GetMember("value", &value) && value.IsNumber()) {
			valueNum = value.GetNumber();
		}
	}

	switch (type) {
		case 0: {  // Scrollbar - percentage
			int percent = static_cast<int>(valueNum * 100);
			return std::to_string(percent) + "%";
		}
		case 1: {  // Stepper - get option text from options array
			Scaleform::GFx::Value options;
			if (entry.GetMember("options", &options) && options.IsArray()) {
				int idx = static_cast<int>(valueNum);
				if (idx >= 0 && static_cast<std::uint32_t>(idx) < options.GetArraySize()) {
					Scaleform::GFx::Value optionText;
					if (options.GetElement(static_cast<std::uint32_t>(idx), &optionText) && optionText.IsString()) {
						return CleanLocalizationString(optionText.GetString());
					}
				}
			}
			break;
		}
		case 2: {  // Checkbox - ON/OFF
			bool isOn = valueNum != 0;
			return isOn ? "On" : "Off";
		}
		default:
			break;
	}

	return "";
}

std::string SettingsMenuAccessibility::GetControlsBindingText(RE::IMenu* a_menu, int a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return "";
	}

	std::string           entryListPath = std::string(CONTROLS_LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return "";
	}

	// Controls entries have PCKeyName for keyboard bindings
	Scaleform::GFx::Value pcKeyName;
	if (entry.GetMember("PCKeyName", &pcKeyName) && pcKeyName.IsString()) {
		return pcKeyName.GetString();
	}

	// Or buttonName for gamepad
	Scaleform::GFx::Value buttonName;
	if (entry.GetMember("buttonName", &buttonName) && buttonName.IsString()) {
		return buttonName.GetString();
	}

	return "";
}

void SettingsMenuAccessibility::CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu || !menu->uiMovie) {
		return;
	}

	// When in options view, don't check settings panel visibility
	// (the panel becomes "invisible" when options are focused)
	if (!m_inOptionsView) {
		// Check if settings panel is visible
		if (!IsSettingsPanelVisible(menu)) {
			if (m_isOpen) {
				Reset();
			}
			return;
		}

		// Settings panel just opened
		if (!m_isOpen) {
			m_isOpen = true;
			SpeakText("Settings");
		}
	}

	if (m_inOptionsView) {
		// We're in the options list - check option selection
		const char* listPath = GetCurrentListPath();
		auto        optionIndex = GetSelectedIndex(menu, listPath);

		if (optionIndex >= 0 && optionIndex != m_lastOptionSelection) {
			m_lastOptionSelection = optionIndex;
			auto name = GetSelectedItemText(menu, listPath);

			// Get value based on category type
			std::string value;
			if (m_lastCategorySelection == CATEGORY_CONTROLS) {
				value = GetControlsBindingText(menu, optionIndex);
			} else {
				value = GetSettingsOptionValueText(menu, optionIndex);
			}

			// Store value for change detection
			m_lastAnnouncedValue = value;

			if (!name.empty()) {
				std::string announcement = CleanLocalizationString(name);
				if (!value.empty()) {
					announcement += ": " + value;
				}
				SpeakText(announcement);
			}
		}
	} else {
		// We're in the category list - check category selection
		auto categoryIndex = GetSelectedIndex(menu, CATEGORY_LIST_PATH);

		if (categoryIndex >= 0 && categoryIndex != m_lastCategorySelection) {
			m_lastCategorySelection = categoryIndex;
			auto text = GetSelectedItemText(menu, CATEGORY_LIST_PATH);
			if (!text.empty()) {
				SpeakText(CleanLocalizationString(text));
			}
		}
	}
}

void SettingsMenuAccessibility::CheckValueChange(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	if (!m_isOpen || !m_inOptionsView) {
		return;
	}

	// Don't handle value changes for Controls (key bindings use Enter for remapping)
	if (m_lastCategorySelection == CATEGORY_CONTROLS) {
		return;
	}

	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu) {
		return;
	}

	const char* listPath = GetCurrentListPath();
	auto        optionIndex = GetSelectedIndex(menu, listPath);
	if (optionIndex >= 0) {
		auto currentValue = GetSettingsOptionValueText(menu, optionIndex);

		// Announce if different from last announced value
		if (!currentValue.empty() && currentValue != m_lastAnnouncedValue) {
			m_lastAnnouncedValue = currentValue;
			SpeakText(currentValue);
		}
	}
}

void SettingsMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}

std::string SettingsMenuAccessibility::CleanLocalizationString(const std::string& a_text)
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

void SettingsMenuAccessibility::OnEnterPressed(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	if (!m_isOpen) {
		return;
	}

	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu || !menu->uiMovie) {
		return;
	}

	if (!m_inOptionsView) {
		// Entering the options view from category view
		m_inOptionsView = true;
		m_lastOptionSelection = -1;  // Reset so first option will be announced
		m_lastAnnouncedValue.clear();

		// Announce the first option
		const char* listPath = GetCurrentListPath();
		auto        optionIndex = GetSelectedIndex(menu, listPath);
		if (optionIndex >= 0) {
			m_lastOptionSelection = optionIndex;
			auto name = GetSelectedItemText(menu, listPath);

			// Get value based on category type
			std::string value;
			if (m_lastCategorySelection == CATEGORY_CONTROLS) {
				value = GetControlsBindingText(menu, optionIndex);
			} else {
				value = GetSettingsOptionValueText(menu, optionIndex);
			}

			// Store value for change detection
			m_lastAnnouncedValue = value;

			if (!name.empty()) {
				std::string announcement = CleanLocalizationString(name);
				if (!value.empty()) {
					announcement += ": " + value;
				}
				SpeakText(announcement);
			}
		}
	} else {
		// Already in options view - Enter may toggle checkboxes or cycle steppers
		// Check for value change and announce if different
		CheckValueChange(a_mainMenuOpen, a_pauseMenuOpen);
	}
}

void SettingsMenuAccessibility::OnEscapePressed()
{
	if (!m_isOpen) {
		return;
	}

	if (m_inOptionsView) {
		// Going back to category view
		m_inOptionsView = false;
		m_lastOptionSelection = -1;
		m_lastAnnouncedValue.clear();
	}
}
