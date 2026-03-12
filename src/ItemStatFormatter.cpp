#include "ItemStatFormatter.h"
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/B/BSFixedString.h>
#include <RE/B/BSScaleformManager.h>
#include <RE/B/BSScaleformTranslator.h>
#include <RE/S/Setting.h>
#include <Shlobj.h>  // For SHGetFolderPathA
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string_view>

std::vector<ItemStatFormatter::FormattedStat> ItemStatFormatter::ParseInfoObj(
	const Scaleform::GFx::Value& a_infoObj)
{
	std::vector<FormattedStat> stats;

	if (!a_infoObj.IsArray()) {
		return stats;
	}

	std::uint32_t count = a_infoObj.GetArraySize();
	stats.reserve(count);

	for (std::uint32_t i = 0; i < count; ++i) {
		Scaleform::GFx::Value entry;
		if (!a_infoObj.GetElement(i, &entry) || !entry.IsObject()) {
			continue;
		}

		auto stat = ParseStatEntry(entry);
		// Include stats with values, or descriptions with labels
		if (!stat.value.empty() || (stat.isDescription && !stat.label.empty())) {
			stats.push_back(std::move(stat));
		}
	}

	return stats;
}

ItemStatFormatter::FormattedStat ItemStatFormatter::ParseStatEntry(
	const Scaleform::GFx::Value& a_entry)
{
	FormattedStat result;

	Scaleform::GFx::Value labelVal, valueVal, showAsDescVal;

	// Get label text
	std::string rawLabel;
	if (a_entry.GetMember("text", &labelVal) && labelVal.IsString()) {
		rawLabel = labelVal.GetString();
	}

	// Check if description (effect text)
	if (a_entry.GetMember("showAsDescription", &showAsDescVal) &&
		showAsDescVal.IsBoolean() && showAsDescVal.GetBoolean()) {
		result.isDescription = true;
		result.label = TranslateLabel(rawLabel);
		return result;
	}

	// Get value
	if (a_entry.GetMember("value", &valueVal)) {
		if (valueVal.IsString()) {
			// String value (e.g., "$FAST", "$SLOW" for melee speed)
			result.value = TranslateLabel(valueVal.GetString());
		} else if (valueVal.IsNumber() || valueVal.IsInt() || valueVal.IsUInt()) {
			double numValue = 0.0;
			if (valueVal.IsNumber())
				numValue = valueVal.GetNumber();
			else if (valueVal.IsInt())
				numValue = static_cast<double>(valueVal.GetInt());
			else if (valueVal.IsUInt())
				numValue = static_cast<double>(valueVal.GetUInt());

			// Skip zero values (they typically indicate no data)
			if (std::abs(numValue) > 0.001) {
				result.value = FormatNumericValue(numValue, rawLabel);
			}
		}
	}

	// Check for damageType (used for resistance stats on armor)
	Scaleform::GFx::Value damageTypeVal;
	if (a_entry.GetMember("damageType", &damageTypeVal)) {
		if (damageTypeVal.IsInt()) {
			result.damageType = damageTypeVal.GetInt();
		} else if (damageTypeVal.IsUInt()) {
			result.damageType = static_cast<std::int32_t>(damageTypeVal.GetUInt());
		} else if (damageTypeVal.IsNumber()) {
			result.damageType = static_cast<std::int32_t>(damageTypeVal.GetNumber());
		}
		REX::DEBUG("ItemStatFormatter: label='{}' value='{}' damageType={}", rawLabel, result.value, result.damageType);
	} else {
		// Log when damageType is missing for resistance-like stats
		if (rawLabel.find("$dr") != std::string::npos || rawLabel.find("$DR") != std::string::npos ||
			rawLabel.find("$er") != std::string::npos || rawLabel.find("$ER") != std::string::npos ||
			rawLabel.find("$rr") != std::string::npos || rawLabel.find("$RR") != std::string::npos) {
			REX::DEBUG("ItemStatFormatter: resistance stat '{}' missing damageType", rawLabel);
		}
	}

	// Translate label
	result.label = TranslateLabel(rawLabel);

	return result;
}

std::string ItemStatFormatter::FormatNumericValue(double a_value, const std::string& a_label)
{
	char buf[64];

	// Weight uses 1 decimal place, everything else is integer
	if (a_label == "$wt" || a_label == "$Weight" || a_label == "$Wt" ||
		a_label == "Weight" || a_label == "Wt") {
		snprintf(buf, sizeof(buf), "%.1f", a_value);
	} else {
		snprintf(buf, sizeof(buf), "%.0f", a_value);
	}

	return buf;
}

std::string ItemStatFormatter::TranslateLabel(const std::string& a_label)
{
	if (a_label.empty())
		return "";

	auto* sfManager = RE::BSScaleformManager::GetSingleton();
	if (!sfManager) {
		return CleanLabel(a_label);
	}

	auto* translator = sfManager->GetTranslator();
	if (!translator) {
		return CleanLabel(a_label);
	}

	std::wstring         wideKey(a_label.begin(), a_label.end());
	RE::BSFixedStringWCS lookupKey(wideKey.c_str());

	auto& translationMap = translator->translator.translationMap;
	auto  it = translationMap.find(lookupKey);

	if (it != translationMap.end()) {
		std::wstring wideResult(it->second.c_str());
		return WideToNarrow(wideResult);
	}

	return CleanLabel(a_label);
}

std::string ItemStatFormatter::CleanLabel(const std::string& a_label)
{
	if (a_label.empty())
		return "";

	std::string result = a_label;

	// Remove $ prefix
	if (result[0] == '$') {
		result = result.substr(1);
	}

	// Replace underscores/hyphens with spaces
	for (char& c : result) {
		if (c == '_' || c == '-')
			c = ' ';
	}

	return result;
}

std::string ItemStatFormatter::WideToNarrow(const std::wstring& a_wide)
{
	if (a_wide.empty())
		return "";

	std::string result;
	result.reserve(a_wide.size() * 3);

	for (wchar_t wc : a_wide) {
		if (wc < 0x80) {
			result += static_cast<char>(wc);
		} else if (wc < 0x800) {
			result += static_cast<char>(0xC0 | (wc >> 6));
			result += static_cast<char>(0x80 | (wc & 0x3F));
		} else {
			result += static_cast<char>(0xE0 | (wc >> 12));
			result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
			result += static_cast<char>(0x80 | (wc & 0x3F));
		}
	}

	return result;
}

// Helper function to read sLanguage from Fallout4.ini
static std::string GetLanguageFromINI()
{
	// Get path: Documents\My Games\Fallout4\Fallout4.ini
	char docPath[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docPath))) {
		std::string iniPath = std::string(docPath) + "\\My Games\\Fallout4\\Fallout4.ini";

		std::ifstream file(iniPath);
		if (file.is_open()) {
			std::string line;
			while (std::getline(file, line)) {
				// Look for sLanguage=XX in [General] section
				if (line.find("sLanguage=") == 0) {
					std::string lang = line.substr(10);  // After "sLanguage="
					// Trim whitespace/newlines
					while (!lang.empty() && (lang.back() == '\r' || lang.back() == '\n' || lang.back() == ' ')) {
						lang.pop_back();
					}
					if (!lang.empty()) {
						// Convert to lowercase for consistency
						std::transform(lang.begin(), lang.end(), lang.begin(), ::tolower);
						return lang;
					}
				}
			}
		}
	}
	return "en";  // Default fallback
}

// Load plugin's translation file based on game language
void ItemStatFormatter::LoadTranslations()
{
	auto* sfManager = RE::BSScaleformManager::GetSingleton();
	if (!sfManager) {
		REX::WARN("ItemStatFormatter: Failed to get BSScaleformManager");
		return;
	}

	auto* translator = sfManager->GetTranslator();
	if (!translator) {
		REX::WARN("ItemStatFormatter: Failed to get translator");
		return;
	}

	// Read language directly from INI file
	std::string lang = GetLanguageFromINI();
	REX::INFO("ItemStatFormatter: Detected language '{}' from Fallout4.ini", lang);

	// Load our translation file
	std::string path = std::format("Interface\\Translations\\fallout4access_{}.txt", lang);
	REX::INFO("ItemStatFormatter: Loading translations from '{}'", path);
	translator->AddTranslationsFile(path.c_str());
	REX::INFO("ItemStatFormatter: Loaded translations");
}

// Returns localized damage type name using plugin's translation file
// Falls back to English if translation not found
static std::string GetDamageTypeName(std::int32_t damageType)
{
	// Fallout 4 has 6 damage types (NUM_DAMAGE_TYPES = 6)
	// damageType values from InfoObj based on testing:
	// 1 = physical/ballistic (shield icon)
	// 2 = poison
	// 3 = fire
	// 4 = energy (lightning icon)
	// 5 = cryo/frost
	// 6 = radiation (radiation icon)

	const char* key = nullptr;
	const char* fallback = nullptr;

	switch (damageType) {
		case 1:
			key = "$F4A_dmgtype_ballistic";
			fallback = "ballistic";
			break;
		case 2:
			key = "$F4A_dmgtype_poison";
			fallback = "poison";
			break;
		case 3:
			key = "$F4A_dmgtype_fire";
			fallback = "fire";
			break;
		case 4:
			key = "$F4A_dmgtype_energy";
			fallback = "energy";
			break;
		case 5:
			key = "$F4A_dmgtype_cryo";
			fallback = "cryo";
			break;
		case 6:
			key = "$F4A_dmgtype_radiation";
			fallback = "radiation";
			break;
		default:
			return "";
	}

	// Look up translation
	auto* sfManager = RE::BSScaleformManager::GetSingleton();
	if (sfManager) {
		auto* translator = sfManager->GetTranslator();
		if (translator) {
			std::wstring         wideKey(key, key + strlen(key));
			RE::BSFixedStringWCS lookupKey(wideKey.c_str());

			auto& translationMap = translator->translator.translationMap;
			REX::DEBUG("GetDamageTypeName: Looking up key '{}', map size={}", key, translationMap.size());

			auto it = translationMap.find(lookupKey);

			if (it != translationMap.end()) {
				std::wstring wideResult(it->second.c_str());
				REX::DEBUG("GetDamageTypeName: Found translation for '{}', wide len={}", key, wideResult.length());
				// Convert wide string to UTF-8
				std::string result;
				for (wchar_t wc : wideResult) {
					if (wc < 0x80) {
						result += static_cast<char>(wc);
					} else if (wc < 0x800) {
						result += static_cast<char>(0xC0 | (wc >> 6));
						result += static_cast<char>(0x80 | (wc & 0x3F));
					} else {
						result += static_cast<char>(0xE0 | (wc >> 12));
						result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
						result += static_cast<char>(0x80 | (wc & 0x3F));
					}
				}
				REX::DEBUG("GetDamageTypeName: Returning '{}' for key '{}'", result, key);
				return result;
			} else {
				REX::DEBUG("GetDamageTypeName: Key '{}' NOT found in translation map", key);
			}
		} else {
			REX::DEBUG("GetDamageTypeName: translator is null");
		}
	} else {
		REX::DEBUG("GetDamageTypeName: sfManager is null");
	}

	REX::DEBUG("GetDamageTypeName: Using fallback '{}' for damageType={}", fallback, damageType);
	return fallback ? fallback : "";
}

std::string ItemStatFormatter::BuildStatAnnouncement(
	const std::vector<FormattedStat>& a_stats, size_t a_maxEffects)
{
	// Note: a_maxEffects is ignored - we always limit descriptions to 1
	// to avoid duplicate English/Russian effect text
	(void)a_maxEffects;

	std::string result;
	size_t      effectCount = 0;
	bool        announcedDamageResistLabel = false;

	for (const auto& stat : a_stats) {
		if (stat.isDescription) {
			// Effect/description text - only read FIRST effect
			// UI shows only one, but InfoObj may contain duplicates (e.g., English + Russian)
			if (effectCount >= 1)
				continue;
			if (!stat.label.empty()) {
				result += ", " + stat.label;
				effectCount++;
			}
		} else if (!stat.value.empty() && !stat.label.empty()) {
			// Check if this is a damage/resistance stat with type info
			if (stat.damageType >= 0) {
				std::string typeName = GetDamageTypeName(stat.damageType);
				if (!typeName.empty()) {
					if (!announcedDamageResistLabel) {
						// First stat with type: "Сопр. урону 50 баллистический" or "Урон 14 энергия"
						result += ", " + stat.label + " " + stat.value + " " + typeName;
						announcedDamageResistLabel = true;
					} else {
						// Subsequent: "30 энергия"
						result += ", " + stat.value + " " + typeName;
					}
				} else {
					result += ", " + stat.label + " " + stat.value;
				}
			} else {
				// Normal stat with label and value
				result += ", " + stat.label + " " + stat.value;
			}
		} else if (!stat.value.empty()) {
			// Value only (rare case)
			result += ", " + stat.value;
		}
	}

	return result;
}
