#pragma once
#include "pch.h"
#include <Scaleform/G/GFx_Value.h>
#include <string>
#include <vector>

// Generic stat formatter - iterates InfoObj and converts to speech-friendly format
// No hard-coded stat labels - handles any stat type the game provides
class ItemStatFormatter {
public:
    struct FormattedStat {
        std::string label;      // Translated label (e.g., "Damage", "Weight")
        std::string value;      // Formatted value (e.g., "45", "5.5", "Medium")
        bool isDescription;     // true if this is effect text, not a stat
        std::int32_t damageType; // -1 = not resistance, 0=phys, 1=energy, 2=rad, 3=poison

        FormattedStat() : isDescription(false), damageType(-1) {}
    };

    // Load plugin's translation file for damage type names
    // Call once at plugin startup after game is initialized
    static void LoadTranslations();

    // Parse InfoObj array into formatted stats (generic iteration)
    // Returns stats in the order they appear in InfoObj
    static std::vector<FormattedStat> ParseInfoObj(const Scaleform::GFx::Value& a_infoObj);

    // Build announcement suffix from stats (e.g., ", Damage 45, Value 120, Weight 5.5")
    // a_maxEffects limits how many description/effect entries to include
    static std::string BuildStatAnnouncement(const std::vector<FormattedStat>& a_stats,
                                              size_t a_maxEffects = 2);

private:
    static FormattedStat ParseStatEntry(const Scaleform::GFx::Value& a_entry);
    static std::string FormatNumericValue(double a_value, const std::string& a_label);
    static std::string TranslateLabel(const std::string& a_label);
    static std::string CleanLabel(const std::string& a_label);
    static std::string WideToNarrow(const std::wstring& a_wide);
};
