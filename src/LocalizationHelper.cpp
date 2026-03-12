#include "LocalizationHelper.h"
#include <RE/B/BSFixedString.h>
#include <RE/B/BSScaleformManager.h>
#include <RE/B/BSScaleformTranslator.h>

std::string LocalizationHelper::Translate(const std::string& a_key)
{
	if (a_key.empty()) {
		return "";
	}

	auto* sfManager = RE::BSScaleformManager::GetSingleton();
	if (!sfManager) {
		return CleanString(a_key);
	}

	auto* translator = sfManager->GetTranslator();
	if (!translator) {
		return CleanString(a_key);
	}

	// Convert narrow string to wide for lookup
	std::wstring         wideKey(a_key.begin(), a_key.end());
	RE::BSFixedStringWCS lookupKey(wideKey.c_str());

	auto& translationMap = translator->translator.translationMap;
	auto  it = translationMap.find(lookupKey);

	if (it != translationMap.end()) {
		std::wstring wideResult(it->second.c_str());
		return WideToNarrow(wideResult);
	}

	return CleanString(a_key);
}

std::string LocalizationHelper::CleanString(const std::string& a_text)
{
	if (a_text.empty()) {
		return a_text;
	}

	std::string result = a_text;

	// Remove $ prefix used for localization keys
	if (result[0] == '$') {
		result = result.substr(1);
	}

	// Replace underscores and hyphens with spaces for better speech
	for (char& c : result) {
		if (c == '_' || c == '-') {
			c = ' ';
		}
	}

	return result;
}

std::string LocalizationHelper::WideToNarrow(const std::wstring& a_wide)
{
	if (a_wide.empty()) {
		return "";
	}

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
