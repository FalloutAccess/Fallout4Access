#pragma once

#include "pch.h"
#include <string>

// Contextual information announcer for accessibility plugin
// Collects and formats player stats for speech output
//
// Usage:
//   ContextualInfoAnnouncer::GetSingleton()->AnnounceCombatStats();
//   ContextualInfoAnnouncer::GetSingleton()->AnnounceMenuContext("PipboyMenu");  // Future
//
// Design:
// - Pure data collection and formatting (no input handling)
// - Called from ObjectScanner (gameplay) or MenuAccessibility (menus)
// - Uses SpeechManager for output
class ContextualInfoAnnouncer
{
public:
    static ContextualInfoAnnouncer* GetSingleton();

    // Combat stats announcement (Health, AP, weapon, ammo)
    // Called from ObjectScanner on H key press during gameplay
    void AnnounceCombatStats();

    // Menu-specific contextual announcements
    // Called from MenuAccessibility on H key press in menus
    // Currently a placeholder - menu announcements to be implemented later
    void AnnounceMenuContext(const std::string& a_menuName);

private:
    ContextualInfoAnnouncer() = default;
    ~ContextualInfoAnnouncer() = default;
    ContextualInfoAnnouncer(const ContextualInfoAnnouncer&) = delete;
    ContextualInfoAnnouncer(ContextualInfoAnnouncer&&) = delete;
    ContextualInfoAnnouncer& operator=(const ContextualInfoAnnouncer&) = delete;
    ContextualInfoAnnouncer& operator=(ContextualInfoAnnouncer&&) = delete;

    // Data collection helpers
    struct CombatStats {
        float healthCurrent = 0.0f;
        float healthMax = 0.0f;
        float apCurrent = 0.0f;
        float apMax = 0.0f;
        float rads = 0.0f;               // Current radiation level
        std::string weaponName;
        std::uint32_t clipAmmo = 0;      // Ammo in current magazine
        std::uint32_t reserveAmmo = 0;   // Ammo in inventory
        bool hasWeapon = false;
        bool isRangedWeapon = false;     // True if weapon uses ammo
    };

    CombatStats GatherCombatStats();
    std::string FormatCombatStatsAnnouncement(const CombatStats& a_stats);

    void SpeakText(const std::string& a_text);
};
