#pragma once

#include "pch.h"

// Handles accessibility announcements for the LevelUpMenu (perk selection grid)
// Works with LevelUpMenuHook which provides AdvanceMovie hooks and platform hotpatch
class LevelUpMenuAccessibility
{
public:
	static LevelUpMenuAccessibility* GetSingleton();

	// Lifecycle (called from MenuAccessibility)
	void OnLevelUpMenuOpened();
	void OnLevelUpMenuClosed();
	void Reset();

	// Called by LevelUpMenuHook when selection changes
	void AnnounceSelection(std::uint32_t a_row, std::uint32_t a_col);

	// Called when user confirms perk selection
	void AnnounceConfirmation(bool a_confirming);

	// Announce current player level and XP % toward next level.
	// Reads root.Menu_mc.playerLevel for the level; piggybacks PipboyMenu's
	// dataObj (if it's still open beneath) for the XP %, since LevelUpMenu
	// itself doesn't expose the Meter_mc internals. Falls back to
	// level-only when XP% isn't reachable.
	bool AnnounceLevelAndXP();

	// Fallout London (PRKF) replaces this menu with a list-based perks / skills /
	// traits screen. Polled every frame by LevelUpMenuHook when that variant is
	// detected. Announces list navigation, mode / SPECIAL context, perk rank /
	// eligibility / requirements / description, and the learn / info dialogs.
	void CheckPRKFSelection(RE::IMenu* a_menu);

private:
	LevelUpMenuAccessibility() = default;
	~LevelUpMenuAccessibility() = default;
	LevelUpMenuAccessibility(const LevelUpMenuAccessibility&) = delete;
	LevelUpMenuAccessibility(LevelUpMenuAccessibility&&) = delete;
	LevelUpMenuAccessibility& operator=(const LevelUpMenuAccessibility&) = delete;
	LevelUpMenuAccessibility& operator=(LevelUpMenuAccessibility&&) = delete;

	// Build announcement string for current perk
	std::string BuildSelectionAnnouncement(RE::IMenu* a_menu, std::uint32_t a_row, std::uint32_t a_col);

	// Build menu open announcement with level and perk points
	std::string BuildMenuOpenAnnouncement(RE::IMenu* a_menu);

	// Get SPECIAL column name from column index (0-6)
	const char* GetColumnName(std::uint32_t a_col);

	// Rank info from reading star states
	struct RankInfo
	{
		int  viewedRank = 0;         // Currently viewed rank (1-indexed)
		int  maxRank = 0;            // Total number of ranks
		int  ownedRanks = 0;         // Number of owned ranks (Full stars)
		bool isViewedOwned = false;  // Is the viewed rank owned?
	};

	// Read star states from StarHolder_mc to determine viewed rank
	RankInfo GetRankInfoFromStars(RE::IMenu* a_menu);

	// --- Fallout London (PRKF) list-based perk/skill/trait menu ---

	// Which sub-view of the PRKF menu currently has focus.
	enum class PRKFView
	{
		None,
		Perks,
		Traits,
		Skills,
		TagSkills,
		Intense,  // Intense Training SPECIAL sub-list
		Confirm,  // Learn perk/skill/trait Yes/No dialog
		Info      // Perk ranks info window (OK)
	};

	// Resolve the focused view and, for list views, the list path + selected index.
	PRKFView DeterminePRKFView(RE::IMenu* a_menu, std::string& a_listPath, std::int32_t& a_index);
	// Spoken context prefix for a view (e.g. "Perks", or the confirm prompt).
	std::string BuildPRKFContext(RE::IMenu* a_menu, PRKFView a_view);
	// Spoken item for the current selection within a view.
	std::string BuildPRKFItem(RE::IMenu* a_menu, PRKFView a_view, const std::string& a_listPath, std::int32_t a_index);

	// Small Scaleform read helpers. ReadIntVar returns -1 when unreadable
	// (matches selectedIndex "no selection" semantics).
	static std::int32_t ReadIntVar(RE::IMenu* a_menu, const char* a_path);
	static std::string  ReadStringVar(RE::IMenu* a_menu, const char* a_path);
	static bool         ReadBoolVar(RE::IMenu* a_menu, const char* a_path);
	// Collapse newlines to ". " and trim, for speech.
	static std::string CleanForSpeech(const std::string& a_text);

	// Utilities
	void SpeakText(const std::string& a_text);

	// State tracking
	bool          m_isOpen = false;
	bool          m_menuAnnounced = false;
	std::uint32_t m_lastRow = 0xFFFFFFFF;
	std::uint32_t m_lastCol = 0xFFFFFFFF;

	// PRKF (Fallout London) state. m_prkfView stores the last-announced PRKFView
	// (as int); m_prkfKey is the last-announced "view:list:index:item" snapshot;
	// m_prkfPrimed gates the first announcement so it doesn't duplicate the
	// menu-open line.
	int         m_prkfView = 0;
	std::string m_prkfKey;
	bool        m_prkfPrimed = false;

	// SPECIAL names for column announcements (index 0-6)
	static constexpr const char* SPECIAL_NAMES[] = {
		"Strength",
		"Perception",
		"Endurance",
		"Charisma",
		"Intelligence",
		"Agility",
		"Luck"
	};

	// Scaleform paths for LevelUpMenu
	// The menu structure is: root.Menu_mc.GridView_mc (PerkGrid instance)
	// Note: Use lowercase getters where available (private vars are inaccessible)
	static constexpr const char* GRID_VIEW_PATH = "root.Menu_mc.GridView_mc";
	static constexpr const char* SELECTED_PERK_PATH = "root.Menu_mc.GridView_mc.selectedPerkEntry";
	static constexpr const char* SELECTED_PERK_CLIP_PATH = "root.Menu_mc.GridView_mc.selectedPerkClip";
	static constexpr const char* PERK_POINTS_PATH = "root.Menu_mc.perkCount";
	static constexpr const char* PLAYER_LEVEL_PATH = "root.Menu_mc.playerLevel";
	// Description text field - updated when rank changes via Alt/Ctrl
	static constexpr const char* DESCRIPTION_TEXT_PATH = "root.Menu_mc.PerkInfo_mc.DescriptionText_tf.text";
	// Requirements text field - shows "Requires: X" for the viewed rank
	static constexpr const char* REQUIREMENTS_TEXT_PATH = "root.Menu_mc.PerkInfo_mc.Reqs_tf.text";
	// Star holder for reading rank states (Empty/Full/Available)
	static constexpr const char* STAR_HOLDER_PATH = "root.Menu_mc.PerkInfo_mc.StarHolder_mc";

	// Fallout London PRKF menu paths (list-based perks/skills/traits screen).
	// PerkList_mc holds perks (and traits in traits mode); List_mc holds skills
	// (and tag-skills). Description_tf / Reqs_tf are shared text fields the AS3
	// fills for the focused list's selection. The header textField + countField
	// show the points-available label and count.
	static constexpr const char* PRKF_PERKLIST = "root.Menu_mc.PerkList_mc";
	static constexpr const char* PRKF_SKILLLIST = "root.Menu_mc.List_mc";
	static constexpr const char* PRKF_DESC = "root.Menu_mc.Description_tf.text";
	static constexpr const char* PRKF_REQS = "root.Menu_mc.Reqs_tf.text";
	static constexpr const char* PRKF_HEADER = "root.Menu_mc.textField.text";
	static constexpr const char* PRKF_PERKCOUNT = "root.Menu_mc.countField.text";
	static constexpr const char* PRKF_TRAITSMODE = "root.Menu_mc.traitsMode";
	static constexpr const char* PRKF_TAGSKILLSMODE = "root.Menu_mc.tagSkillsMode";
	static constexpr const char* PRKF_CONFIRM = "root.Menu_mc.LearnSkillsConfirmMenu_mc";
	static constexpr const char* PRKF_INFO = "root.Menu_mc.InfoWindow_mc";
	static constexpr const char* PRKF_INTENSE = "root.Menu_mc.Intense_training_menu_mc";
};
