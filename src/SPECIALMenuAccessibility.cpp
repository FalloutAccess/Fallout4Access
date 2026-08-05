#include "SPECIALMenuAccessibility.h"
#include "SpeechManager.h"

SPECIALMenuAccessibility* SPECIALMenuAccessibility::GetSingleton()
{
	static SPECIALMenuAccessibility singleton;
	return &singleton;
}

void SPECIALMenuAccessibility::Reset()
{
	m_isOpen = false;
	m_menuAnnounced = false;
	m_lastSelection = -1;
	m_lastAnnouncedValue = -1;
}

RE::IMenu* SPECIALMenuAccessibility::GetActiveMenu([[maybe_unused]] bool a_mainMenuOpen, [[maybe_unused]] bool a_pauseMenuOpen)
{
	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return nullptr;

	// SPECIALMenu is a standalone menu, not embedded in MainMenu/PauseMenu
	if (auto menu = ui->GetMenu("SPECIALMenu"sv)) {
		return menu.get();
	}

	return nullptr;
}

bool SPECIALMenuAccessibility::IsSPECIALMenuVisible(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	// Check if the list component exists and has valid data
	Scaleform::GFx::Value list;
	if (!a_menu->uiMovie->GetVariable(&list, SPECIAL_LIST_PATH)) {
		return false;
	}

	if (!list.IsObject() && !list.IsDisplayObject()) {
		return false;
	}

	// Check for entryList to verify menu is populated
	Scaleform::GFx::Value entryList;
	if (!list.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return false;
	}

	return entryList.GetArraySize() > 0;
}

std::int32_t SPECIALMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	std::string           indexPath = std::string(SPECIAL_LIST_PATH) + ".selectedIndex";
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

std::int32_t SPECIALMenuAccessibility::GetRemainingPoints(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	Scaleform::GFx::Value pointsText;
	if (a_menu->uiMovie->GetVariable(&pointsText, POINTS_COUNTER_PATH)) {
		if (pointsText.IsString()) {
			std::string text = pointsText.GetString();
			// Parse the number from the text
			try {
				return std::stoi(text);
			} catch (...) {
				return -1;
			}
		} else if (pointsText.IsInt()) {
			return pointsText.GetInt();
		} else if (pointsText.IsNumber()) {
			return static_cast<std::int32_t>(pointsText.GetNumber());
		}
	}

	return -1;
}

std::string SPECIALMenuAccessibility::GetAttributeName(RE::IMenu* a_menu, int a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return "";
	}

	std::string           entryListPath = std::string(SPECIAL_LIST_PATH) + ".entryList";
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

	return "";
}

std::int32_t SPECIALMenuAccessibility::GetAttributeValue(RE::IMenu* a_menu, int a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return -1;
	}

	// Index 0 is the name entry, no value
	if (a_index == 0) {
		return -1;
	}

	std::string           entryListPath = std::string(SPECIAL_LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return -1;
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return -1;
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return -1;
	}

	Scaleform::GFx::Value valueNum;
	if (entry.GetMember("value", &valueNum)) {
		if (valueNum.IsInt()) {
			return valueNum.GetInt();
		} else if (valueNum.IsNumber()) {
			return static_cast<std::int32_t>(valueNum.GetNumber());
		} else if (valueNum.IsUInt()) {
			return static_cast<std::int32_t>(valueNum.GetUInt());
		}
	}

	return -1;
}

std::string SPECIALMenuAccessibility::GetAttributeDescription(RE::IMenu* a_menu, [[maybe_unused]] int a_index)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	// Read description from the Description_tf text field (shows current selection's description)
	Scaleform::GFx::Value descValue;
	if (a_menu->uiMovie->GetVariable(&descValue, DESCRIPTION_PATH)) {
		if (descValue.IsString()) {
			return descValue.GetString();
		}
	}

	return "";
}

std::string SPECIALMenuAccessibility::BuildAttributeAnnouncement(
	const std::string& a_name, int a_value, int a_remainingPoints, const std::string& a_description)
{
	std::string announcement = CleanLocalizationString(a_name);

	if (a_value >= 0) {
		announcement += ", " + std::to_string(a_value);
	}

	if (a_remainingPoints >= 0) {
		announcement += ", " + std::to_string(a_remainingPoints);
		announcement += (a_remainingPoints == 1) ? " point remaining" : " points remaining";
	}

	if (!a_description.empty()) {
		announcement += ". " + CleanLocalizationString(a_description);
	}

	return announcement;
}

void SPECIALMenuAccessibility::CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu || !menu->uiMovie) {
		if (m_isOpen) {
			Reset();
		}
		return;
	}

	// Check if SPECIAL menu is visible
	if (!IsSPECIALMenuVisible(menu)) {
		if (m_isOpen) {
			Reset();
		}
		return;
	}

	// Menu just opened
	if (!m_isOpen) {
		m_isOpen = true;
		REX::INFO("SPECIALMenu detected as open");
	}

	// Announce menu name once
	if (!m_menuAnnounced) {
		m_menuAnnounced = true;
		SpeakText("Special Character Creation");
	}

	auto selectedIndex = GetSelectedIndex(menu);
	if (selectedIndex < 0) {
		return;
	}

	// Check if selection changed
	if (selectedIndex == m_lastSelection) {
		return;
	}

	m_lastSelection = selectedIndex;

	// Get attribute info
	auto name = GetAttributeName(menu, selectedIndex);
	auto value = GetAttributeValue(menu, selectedIndex);
	auto remainingPoints = GetRemainingPoints(menu);
	auto description = GetAttributeDescription(menu, selectedIndex);

	// Update tracked value for value change detection
	m_lastAnnouncedValue = value;

	if (selectedIndex == 0) {
		// Name entry
		std::string announcement = "Name Entry";
		if (!name.empty()) {
			announcement += ": " + name;
		}
		if (!description.empty()) {
			announcement += ". " + CleanLocalizationString(description);
		}
		SpeakText(announcement);
	} else {
		// SPECIAL attribute - full announcement
		auto announcement = BuildAttributeAnnouncement(name, value, remainingPoints, description);
		if (!announcement.empty()) {
			SpeakText(announcement);
		}
	}
}

void SPECIALMenuAccessibility::CheckValueChange(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	if (!m_isOpen) {
		return;
	}

	// Only process value changes for SPECIAL attributes (indices 1-7), not name entry
	if (m_lastSelection <= 0) {
		return;
	}

	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu || !menu->uiMovie) {
		return;
	}

	auto currentValue = GetAttributeValue(menu, m_lastSelection);
	auto remainingPoints = GetRemainingPoints(menu);

	// Check if value actually changed
	if (currentValue == m_lastAnnouncedValue) {
		return;
	}

	m_lastAnnouncedValue = currentValue;

	// Announce new value and remaining points
	std::string announcement = std::to_string(currentValue);
	if (remainingPoints >= 0) {
		announcement += ", " + std::to_string(remainingPoints);
		announcement += (remainingPoints == 1) ? " point remaining" : " points remaining";
	}

	SpeakText(announcement);
}

void SPECIALMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}

std::string SPECIALMenuAccessibility::CleanLocalizationString(const std::string& a_text)
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
