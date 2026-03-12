#include "SaveLoadMenuAccessibility.h"
#include "SpeechManager.h"
#include <SRAL.h>

SaveLoadMenuAccessibility* SaveLoadMenuAccessibility::GetSingleton()
{
	static SaveLoadMenuAccessibility singleton;
	return &singleton;
}

void SaveLoadMenuAccessibility::Reset()
{
	m_isOpen = false;
	m_isSaveMode = false;
	m_panelAnnounced = false;
	m_lastSelection = -1;
}

RE::IMenu* SaveLoadMenuAccessibility::GetActiveMenu(bool a_mainMenuOpen, bool a_pauseMenuOpen)
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

bool SaveLoadMenuAccessibility::IsSaveLoadPanelVisible(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value holder;
	if (!a_menu->uiMovie->GetVariable(&holder, SAVELOAD_HOLDER_PATH)) {
		return false;
	}

	if (!holder.IsObject()) {
		return false;
	}

	Scaleform::GFx::Value visible;
	if (!holder.GetMember("visible", &visible) || !visible.IsBoolean()) {
		return false;
	}

	return visible.GetBoolean();
}

bool SaveLoadMenuAccessibility::IsSaveMode(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value isSaving;
	if (a_menu->uiMovie->GetVariable(&isSaving, IS_SAVING_PATH)) {
		if (isSaving.IsBoolean()) {
			return isSaving.GetBoolean();
		}
	}
	return false;
}

std::int32_t SaveLoadMenuAccessibility::GetSelectedIndex(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	std::string           indexPath = std::string(SAVELOAD_LIST_PATH) + ".selectedIndex";
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

std::uint32_t SaveLoadMenuAccessibility::GetEntryListSize(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return 0;
	}

	std::string           entryListPath = std::string(SAVELOAD_LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) && entryList.IsArray()) {
		return entryList.GetArraySize();
	}
	return 0;
}

SaveLoadMenuAccessibility::SaveEntryInfo SaveLoadMenuAccessibility::GetSaveEntryInfo(RE::IMenu* a_menu, int a_index)
{
	SaveEntryInfo info;

	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return info;
	}

	std::string           entryListPath = std::string(SAVELOAD_LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return info;
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return info;
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return info;
	}

	// Extract text (location name)
	Scaleform::GFx::Value textValue;
	if (entry.GetMember("text", &textValue) && textValue.IsString()) {
		info.text = textValue.GetString();
	}

	// Extract name (character/save name)
	Scaleform::GFx::Value nameValue;
	if (entry.GetMember("name", &nameValue) && nameValue.IsString()) {
		info.name = nameValue.GetString();
	}

	// Extract playTime
	Scaleform::GFx::Value playTimeValue;
	if (entry.GetMember("playTime", &playTimeValue) && playTimeValue.IsString()) {
		info.playTime = playTimeValue.GetString();
	}

	// Extract level
	Scaleform::GFx::Value levelValue;
	if (entry.GetMember("level", &levelValue)) {
		if (levelValue.IsInt()) {
			info.level = levelValue.GetInt();
		} else if (levelValue.IsNumber()) {
			info.level = static_cast<int>(levelValue.GetNumber());
		}
	}

	// Extract corrupt flag
	Scaleform::GFx::Value corruptValue;
	if (entry.GetMember("corrupt", &corruptValue) && corruptValue.IsBoolean()) {
		info.corrupt = corruptValue.GetBoolean();
	}

	// Extract obsolete flag
	Scaleform::GFx::Value obsoleteValue;
	if (entry.GetMember("obsolete", &obsoleteValue) && obsoleteValue.IsBoolean()) {
		info.obsolete = obsoleteValue.GetBoolean();
	}

	// Check if this is the "New Save" slot (in save mode, index 0 with empty name)
	if (m_isSaveMode && a_index == 0 && info.name.empty() && info.level == 0) {
		info.isNewSaveSlot = true;
	}

	return info;
}

std::string SaveLoadMenuAccessibility::FormatPlayTime(const std::string& a_playTime)
{
	if (a_playTime.empty()) {
		return "";
	}

	// Parse the format: "days.hours.minutes.seconds"
	int days = 0, hours = 0, minutes = 0, seconds = 0;

	// Split by '.'
	std::vector<std::string> parts;
	std::string              current;
	for (char c : a_playTime) {
		if (c == '.') {
			parts.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}
	if (!current.empty()) {
		parts.push_back(current);
	}

	// Parse parts (expecting days.hours.minutes.seconds)
	try {
		if (parts.size() >= 1)
			days = std::stoi(parts[0]);
		if (parts.size() >= 2)
			hours = std::stoi(parts[1]);
		if (parts.size() >= 3)
			minutes = std::stoi(parts[2]);
		if (parts.size() >= 4)
			seconds = std::stoi(parts[3]);
	} catch (...) {
		// If parsing fails, return raw string
		return a_playTime;
	}

	// Build human-readable string
	std::string result;

	if (days > 0) {
		result += std::to_string(days) + (days == 1 ? " day" : " days");
	}

	if (hours > 0) {
		if (!result.empty())
			result += ", ";
		result += std::to_string(hours) + (hours == 1 ? " hour" : " hours");
	}

	if (minutes > 0) {
		if (!result.empty())
			result += ", ";
		result += std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
	}

	// Only show seconds if less than an hour of playtime
	if (days == 0 && hours == 0 && seconds > 0) {
		if (!result.empty())
			result += ", ";
		result += std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
	}

	// Handle edge case of no playtime
	if (result.empty()) {
		result = "0 minutes";
	}

	return result;
}

std::string SaveLoadMenuAccessibility::BuildSaveAnnouncement(const SaveEntryInfo& a_info)
{
	std::string announcement;

	// Handle "New Save" slot specially
	if (a_info.isNewSaveSlot) {
		return "New Save";
	}

	// Start with warnings if applicable
	if (a_info.corrupt) {
		announcement += "Corrupted. ";
	}
	if (a_info.obsolete) {
		announcement += "Obsolete. ";
	}

	// Character name and level
	if (!a_info.name.empty()) {
		announcement += CleanLocalizationString(a_info.name);
		if (a_info.level > 0) {
			announcement += ", Level " + std::to_string(a_info.level);
		}
	}

	// Location
	if (!a_info.text.empty()) {
		if (!announcement.empty()) {
			announcement += ". ";
		}
		announcement += CleanLocalizationString(a_info.text);
	}

	// Playtime
	if (!a_info.playTime.empty()) {
		std::string formattedTime = FormatPlayTime(a_info.playTime);
		if (!formattedTime.empty()) {
			if (!announcement.empty()) {
				announcement += ". ";
			}
			announcement += "Playtime: " + formattedTime;
		}
	}

	return announcement;
}

void SaveLoadMenuAccessibility::CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen)
{
	auto menu = GetActiveMenu(a_mainMenuOpen, a_pauseMenuOpen);
	if (!menu || !menu->uiMovie) {
		return;
	}

	// Check if save/load panel is visible
	if (!IsSaveLoadPanelVisible(menu)) {
		if (m_isOpen) {
			Reset();
		}
		return;
	}

	// Panel just became visible
	if (!m_isOpen) {
		m_isOpen = true;
		m_isSaveMode = IsSaveMode(menu);
		m_panelAnnounced = false;
		m_lastSelection = -1;
	}

	// Announce panel opening if not yet announced
	if (!m_panelAnnounced) {
		// Check if list is populated before announcing
		auto listSize = GetEntryListSize(menu);
		auto selectedIndex = GetSelectedIndex(menu);

		if (listSize > 0 && selectedIndex >= 0) {
			m_panelAnnounced = true;
			std::string panelName = m_isSaveMode ? "Save Game" : "Load Game";
			SpeakText(panelName);
		}
	}

	// Check for selection changes
	auto selectedIndex = GetSelectedIndex(menu);
	if (selectedIndex >= 0 && selectedIndex != m_lastSelection) {
		m_lastSelection = selectedIndex;

		auto        saveInfo = GetSaveEntryInfo(menu, selectedIndex);
		std::string announcement = BuildSaveAnnouncement(saveInfo);

		if (!announcement.empty()) {
			SpeakText(announcement);
		}
	}
}

void SaveLoadMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}

std::string SaveLoadMenuAccessibility::CleanLocalizationString(const std::string& a_text)
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
