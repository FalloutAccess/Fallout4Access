#pragma once

#include "pch.h"
#include <string>

// Centralized utility for localization string handling
// Uses game's BSScaleformTranslator to look up $KEY strings
class LocalizationHelper
{
public:
    // Translate a $KEY string using game's translation system
    // Falls back to CleanString() if translation not found
    static std::string Translate(const std::string& a_key);

    // Clean a localization string by removing $ prefix and replacing _/- with spaces
    static std::string CleanString(const std::string& a_text);

private:
    // Convert wide string to UTF-8 narrow string
    static std::string WideToNarrow(const std::wstring& a_wide);
};
