#pragma once

#include "pch.h"
#include <RE/M/MainMenu.h>

// Screen reader accessibility for the in-game Mod Manager (Bethesda.net mod
// browser / library).
//
// The UI is Main_ModManager.swf, loaded by MainMenu as the content of the
// private Loader `ModManager_Loader` and added under `root.ModManager_mc`. It
// is NOT a separate RE::IMenu; it lives inside the MainMenu movie and is active
// when MainMenu's currentState == "BETHESDANET_STATE".
//
// Integration: driven per-frame from MainMenuHook::CheckStateChanges while
// ActivePanel == ActivePanel::ModManager. Observation only; no input injected.
//
// Two list strategies are required: the Browse view uses a custom
// ModCategoryList (horizontal, data in dataObj.entries, selectedIndex is uint),
// while the Details / Library / Account Settings views use standard
// BSScrollingList (selectedIndex:int + entryList).
class ModManagerAccessibility
{
public:
	static ModManagerAccessibility* GetSingleton();

	// Called once when the Mod Manager becomes active. Clears cached state.
	void OnOpened();

	// Called every frame while the Mod Manager is active.
	void CheckState(RE::MainMenu* a_menu);

	// Called when the host menu closes. Clears cached state.
	void Reset();

	bool IsOpen() const { return m_isOpen; }

private:
	ModManagerAccessibility() = default;
	~ModManagerAccessibility() = default;
	ModManagerAccessibility(const ModManagerAccessibility&) = delete;
	ModManagerAccessibility(ModManagerAccessibility&&) = delete;
	ModManagerAccessibility& operator=(const ModManagerAccessibility&) = delete;
	ModManagerAccessibility& operator=(ModManagerAccessibility&&) = delete;

	// The mutually-exclusive top-level views, in detection priority order.
	enum class View
	{
		None,
		Login,            // in-manager re-login (LoginHolder_mc)
		Details,          // DetailsPage_mc
		AccountSettings,  // AccountSettingsPage_mc
		Search,           // SearchPage_mc
		Library,          // LibraryPage_mc
		Browse            // ListsHolder_mc (default)
	};

	// Resolve the Main_ModManager instance (loader content) via the
	// ModManager_mc display list. Logs the first failure once.
	bool GetModRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& a_out);

	// True if a_val looks like the Main_ModManager root (has a known page).
	static bool IsModRoot(Scaleform::GFx::Value& a_val);

	// Determine the active view from the page clips' `.visible` flags.
	View        DetectView(Scaleform::GFx::Value& a_root);
	const char* ViewName(View a_view);

	// Browse view (custom ModCategoryList). Reads whichever of List1_mc /
	// List2_mc currently holds a selection. Returns true with the entry speech,
	// the list id (1/2) and the global entry index (for change detection).
	bool ReadBrowseSelection(
		Scaleform::GFx::Value& a_root,
		std::string&           a_outSpeech,
		int&                   a_outListId,
		int&                   a_outIndex);

	// Standard BSScrollingList readers (Details / Library / Account Settings).
	std::int32_t GetListSelectedIndex(Scaleform::GFx::Value& a_root, std::initializer_list<const char*> a_listPath);
	std::string  GetListEntryText(Scaleform::GFx::Value& a_root, std::initializer_list<const char*> a_listPath, std::int32_t a_index);

	// Build the spoken string for a browse/library mod entry data object
	// (title + author + status flags).
	std::string BuildModEntrySpeech(Scaleform::GFx::Value& a_entry);

	// On entering the Details view, summarise the mod (title + author).
	std::string BuildDetailsSummary(Scaleform::GFx::Value& a_root);

	// Scan the Details OptionsList for an inline download-progress string
	// ("Downloading... N%"). Returns "" if none.
	std::string GetDownloadProgressText(Scaleform::GFx::Value& a_root);

	void SpeakText(const std::string& a_text);

	bool         m_isOpen = false;
	View         m_lastView = View::None;
	std::int32_t m_lastSelection = -1;
	int          m_lastBrowseListId = -1;
	std::string  m_lastDownloadText;
	std::string  m_lastSpokenText;
	bool         m_loadingAnnounced = false;

	static constexpr const char* MOD_HOLDER_PATH = "root.ModManager_mc";
};
