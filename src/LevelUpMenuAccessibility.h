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
    struct RankInfo {
        int viewedRank = 0;      // Currently viewed rank (1-indexed)
        int maxRank = 0;         // Total number of ranks
        int ownedRanks = 0;      // Number of owned ranks (Full stars)
        bool isViewedOwned = false;  // Is the viewed rank owned?
    };

    // Read star states from StarHolder_mc to determine viewed rank
    RankInfo GetRankInfoFromStars(RE::IMenu* a_menu);

    // Utilities
    void SpeakText(const std::string& a_text);

    // State tracking
    bool m_isOpen = false;
    bool m_menuAnnounced = false;
    std::uint32_t m_lastRow = 0xFFFFFFFF;
    std::uint32_t m_lastCol = 0xFFFFFFFF;

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
};
