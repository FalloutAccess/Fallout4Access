#pragma once

#include "pch.h"

class SaveLoadMenuAccessibility
{
public:
	static SaveLoadMenuAccessibility* GetSingleton();

	// Called from MenuAccessibility on up/down navigation
	void CheckSelection(bool a_mainMenuOpen, bool a_pauseMenuOpen);

	// Called when parent menu closes
	void Reset();

	bool IsOpen() const { return m_isOpen; }

private:
	SaveLoadMenuAccessibility() = default;

	// Save entry data
	struct SaveEntryInfo
	{
		std::string text;      // Location name
		std::string name;      // Character/save name
		std::string playTime;  // Format: "days.hours.minutes.seconds"
		int         level = 0;
		bool        corrupt = false;
		bool        obsolete = false;
		bool        isNewSaveSlot = false;
	};

	RE::IMenu*    GetActiveMenu(bool a_mainMenuOpen, bool a_pauseMenuOpen);
	bool          IsSaveLoadPanelVisible(RE::IMenu* a_menu);
	bool          IsSaveMode(RE::IMenu* a_menu);
	std::int32_t  GetSelectedIndex(RE::IMenu* a_menu);
	std::uint32_t GetEntryListSize(RE::IMenu* a_menu);
	SaveEntryInfo GetSaveEntryInfo(RE::IMenu* a_menu, int a_index);
	std::string   FormatPlayTime(const std::string& a_playTime);
	std::string   BuildSaveAnnouncement(const SaveEntryInfo& a_info);
	void          SpeakText(const std::string& a_text);
	std::string   CleanLocalizationString(const std::string& a_text);

	bool m_isOpen = false;
	bool m_isSaveMode = false;
	bool m_panelAnnounced = false;
	int  m_lastSelection = -1;

	// Scaleform paths for save/load UI
	static constexpr const char* SAVELOAD_HOLDER_PATH = "root.Menu_mc.SaveLoadHolder_mc";
	static constexpr const char* SAVELOAD_LIST_PATH = "root.Menu_mc.SaveLoadHolder_mc.Panel_mc.List_mc";
	static constexpr const char* IS_SAVING_PATH = "root.Menu_mc.SaveLoadHolder_mc.Panel_mc.isSaving";
};
