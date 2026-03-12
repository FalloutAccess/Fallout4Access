#pragma once

#include "pch.h"

class SPECIALMenuAccessibility
{
public:
	static SPECIALMenuAccessibility* GetSingleton();

	// Called from MenuAccessibility on up/down navigation
	void CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called from MenuAccessibility on left/right navigation
	void CheckValueChange(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called when parent menu closes
	void Reset();

	bool IsOpen() const { return m_isOpen; }

private:
	SPECIALMenuAccessibility() = default;

	RE::IMenu*   GetActiveMenu(bool a_mainMenuOpen, bool a_pauseMenuOpen);
	bool         IsSPECIALMenuVisible(RE::IMenu* a_menu);
	std::int32_t GetSelectedIndex(RE::IMenu* a_menu);
	std::int32_t GetRemainingPoints(RE::IMenu* a_menu);
	std::string  GetAttributeName(RE::IMenu* a_menu, int a_index);
	std::int32_t GetAttributeValue(RE::IMenu* a_menu, int a_index);
	std::string  GetAttributeDescription(RE::IMenu* a_menu, int a_index);
	std::string  BuildAttributeAnnouncement(const std::string& a_name, int a_value,
		 int a_remainingPoints, const std::string& a_description);
	void         SpeakText(const std::string& a_text);
	std::string  CleanLocalizationString(const std::string& a_text);

	bool         m_isOpen = false;
	bool         m_menuAnnounced = false;
	std::int32_t m_lastSelection = -1;
	std::int32_t m_lastAnnouncedValue = -1;

	// Scaleform paths for SPECIALMenu
	static constexpr const char* SPECIAL_LIST_PATH = "root.Menu_mc.List_mc";
	static constexpr const char* POINTS_COUNTER_PATH = "root.Menu_mc.PointCounter_tf.text";
	static constexpr const char* DESCRIPTION_PATH = "root.Menu_mc.Description_tf.text";
};
