#pragma once

#include "pch.h"
#include <RE/M/MainMenu.h>

// Provides screen reader accessibility for the Mod Configuration Menu (MCM).
// MCM is a separate F4SE plugin (f4mcm) that injects MCM.swf into MainMenu.swf,
// which serves as the movie for both MainMenu and PauseMenu. MCM does not
// register its own RE::IMenu; it hosts inside the PauseMenu (or MainMenu) movie.
//
// Integration: driven per-frame from MainMenuHook::CheckStateChanges while
// ActivePanel == ActivePanel::MCM. Reads Scaleform state from
// root.mcm_loader.content.mcmMenu; no input is dispatched.
class MCMAccessibility
{
public:
	static MCMAccessibility* GetSingleton();

	// Fast-path visibility probe used by MainMenuHook::DetectActivePanel.
	// Reads root.mcm_loader.content.mcmMenu.visible; returns false on any
	// failure (including MCM not loaded, path missing, or visible==false).
	static bool IsMCMVisible(RE::MainMenu* a_menu);

	// Called once when MCM becomes the active panel. Announces the menu name
	// and clears cached state so the first selection is announced.
	void OnOpened();

	// Called every frame from MainMenuHook::CheckStateChanges while MCM is
	// the active panel. Handles iMode transitions, pane focus tracking,
	// selection announcements, and hint text changes.
	void CheckState(RE::MainMenu* a_menu);

	// Called from MainMenuHook::Set{Main,Pause}MenuOpen(false) when the host
	// menu closes. Clears all cached state.
	void Reset();

	bool IsOpen() const { return m_isOpen; }

private:
	MCMAccessibility() = default;
	~MCMAccessibility() = default;
	MCMAccessibility(const MCMAccessibility&) = delete;
	MCMAccessibility(MCMAccessibility&&) = delete;
	MCMAccessibility& operator=(const MCMAccessibility&) = delete;
	MCMAccessibility& operator=(MCMAccessibility&&) = delete;

	// Which pane currently owns logical focus. MCM does not expose this
	// directly; inferred from the right-pane selectedIndex transitioning
	// between -1 and >= 0.
	enum class Pane
	{
		Left,
		Right
	};

	// Strategy for reading iMode. Probed once per session on first call,
	// then latched to avoid re-testing every frame.
	enum class IModeStrategy
	{
		Unknown,
		Getter,         // mcmRoot.GetMember("iMode") returned a number
		ClipVisibility  // fall back to visibility checks on sub-panels
	};

	// MCM_Menu.iMode values (matches MCM_Menu.as lines 66-72).
	static constexpr std::int32_t MCM_MAIN_MODE = 0;
	static constexpr std::int32_t MCM_REMAP_MODE = 1;
	static constexpr std::int32_t MCM_CONFLICT_MODE = 2;
	static constexpr std::int32_t MCM_DD_MODE = 3;
	static constexpr std::int32_t MCM_POSITIONER_MODE = 4;
	static constexpr std::int32_t MCM_POSITIONER_CONFIRM_MODE = 5;
	static constexpr std::int32_t MCM_TEXTINPUT_MODE = 6;

	// SettingsOptionItem.movieType constants (matches SettingsOptionItem.as lines 26-41).
	static constexpr std::int32_t MOVIETYPE_SCROLLBAR = 0;
	static constexpr std::int32_t MOVIETYPE_STEPPER = 1;
	static constexpr std::int32_t MOVIETYPE_SWITCHER = 2;
	static constexpr std::int32_t MOVIETYPE_SECTION = 3;
	static constexpr std::int32_t MOVIETYPE_EMPTY_LINE = 4;
	static constexpr std::int32_t MOVIETYPE_DROPDOWN = 5;
	static constexpr std::int32_t MOVIETYPE_TEXT = 6;
	static constexpr std::int32_t MOVIETYPE_BUTTON = 7;
	static constexpr std::int32_t MOVIETYPE_KEYINPUT = 8;
	static constexpr std::int32_t MOVIETYPE_IMAGE = 9;
	static constexpr std::int32_t MOVIETYPE_HOTKEY = 10;
	static constexpr std::int32_t MOVIETYPE_DD_FILES = 11;
	static constexpr std::int32_t MOVIETYPE_POSITIONER = 12;
	static constexpr std::int32_t MOVIETYPE_TEXTINPUT_INT = 13;
	static constexpr std::int32_t MOVIETYPE_TEXTINPUT_FLOAT = 14;
	static constexpr std::int32_t MOVIETYPE_TEXTINPUT_STRING = 15;

	// Left-pane filterFlag values (UniversalListEntry.as lines 24-55).
	static constexpr std::int32_t FILTER_MOD_HEADER = 1;
	static constexpr std::int32_t FILTER_PAGE = 2;

	// Resolve root.mcm_loader.content.mcmMenu with primary + fallback paths.
	// Returns true on success with out filled. Logs the first failure at
	// REX::INFO via a std::once_flag; subsequent failures are silent.
	bool GetMCMRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& out);

	// Read iMode via the latched strategy. Returns MCM_MAIN_MODE on any
	// read failure so row polling can proceed.
	std::int32_t GetIMode(const Scaleform::GFx::Value& a_mcmRoot);

	// selectedIndex readers for the two list panes. Return -1 on any
	// read failure or if the list is not currently hosting a selection.
	std::int32_t GetLeftSelectedIndex(Scaleform::GFx::Value& a_mcmRoot);
	std::int32_t GetRightSelectedIndex(Scaleform::GFx::Value& a_mcmRoot);

	// Build the speech string for a left-pane entry at a_index.
	// Handles filterFlag 1 (mod header), filterFlag 2 (page), and catch-all.
	std::string BuildLeftEntrySpeech(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index);

	// Build the speech string for a right-pane entry at a_index. For
	// movieTypes whose live value lives in the rendered clip, walks to the
	// clip via GetClipByIndex and reads OptionItem state there.
	std::string BuildRightEntrySpeech(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index);

	// Lower-level variant that returns the label and value parts separately
	// so CheckState can announce just the value when the user changes it
	// without moving rows (e.g. pressing left/right on a slider). Both
	// out_label and out_valueText are cleared first. a_valueText is empty
	// for rows that have no distinct value (SECTION, TEXT, EMPTY_LINE, IMAGE).
	void BuildRightEntryParts(
		Scaleform::GFx::Value& a_mcmRoot,
		std::int32_t           a_index,
		std::string&           out_label,
		std::string&           out_valueText);

	// Read the rendered right-pane clip at a_clipIdx and pull live value
	// text from its OptionItem. a_movieType and a_entry are used to pick
	// the right field and to resolve dropdown/stepper options arrays.
	std::string GetRenderedRightValueText(
		Scaleform::GFx::Value& a_rightList,
		std::int32_t           a_clipIdx,
		std::int32_t           a_movieType,
		Scaleform::GFx::Value& a_entry);

	// Find the nearest MOVIETYPE_SECTION entry at or before a_index in the
	// right pane's entryList. Returns the section text (translated), or ""
	// if no section header precedes the entry.
	std::string FindSectionForIndex(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index);

	// Read configPanel_mc.hint_tf.text. Returns empty string on failure.
	std::string GetHintText(Scaleform::GFx::Value& a_mcmRoot);

	// Speak via SpeechManager (interrupt=false per CLAUDE.md).
	void SpeakText(const std::string& a_text);

	// Strip the trailing ')' MCM appends in Option_ButtonMapping.RefreshText,
	// trim whitespace, and return "unbound" for empty or "None" inputs.
	static std::string FormatKeyBinding(const std::string& a_raw);

	// State
	bool          m_isOpen = false;
	Pane          m_focusedPane = Pane::Left;
	std::int32_t  m_lastLeftIndex = -1;
	std::int32_t  m_lastRightIndex = -1;
	std::int32_t  m_lastIMode = -1;  // -1 = unknown / first frame
	IModeStrategy m_imodeReadStrategy = IModeStrategy::Unknown;
	std::string   m_lastHint;
	std::string   m_lastSpokenText;
	std::string   m_lastRightValueText;  // just the value part of the focused right row (for value-change detection without row change)
	std::string   m_lastSectionName;     // nearest preceding section header in the right pane (for group-change announcements)

	// Scaleform paths
	static constexpr const char* MCM_ROOT_PATH = "root.mcm_loader.content.mcmMenu";
	static constexpr const char* MCM_LOADER_CONTENT_PATH = "root.mcm_loader.content";
};
