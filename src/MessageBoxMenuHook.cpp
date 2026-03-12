#include "MessageBoxMenuHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"

MessageBoxMenuHook* MessageBoxMenuHook::GetSingleton()
{
	static MessageBoxMenuHook singleton;
	return &singleton;
}

void MessageBoxMenuHook::Install()
{
	if (m_installed) {
		return;
	}

	// Hook MessageBoxMenu::AdvanceMovie (vtable index 4)
	static REL::THookVFT<void(RE::MessageBoxMenu*, float, std::uint64_t)> hook(
		VTABLE::MessageBoxMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	if (hook.Enable()) {
		s_advanceMovieHook = &hook;
		m_installed = true;
		REX::INFO("MessageBoxMenuHook: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("MessageBoxMenuHook: Failed to install AdvanceMovie hook");
	}
}

void MessageBoxMenuHook::ResetState()
{
	m_lastSelection = -1;
	m_buttonCount = 0;
	m_initialAnnouncementDone = false;
	m_framesSinceOpen = 0;
	m_lastBodyText.clear();
	m_lastButtonLabels.clear();
}

void MessageBoxMenuHook::HookedAdvanceMovie(RE::MessageBoxMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	// Call original function first to let the game update state
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	auto* hook = GetSingleton();
	if (!hook->m_menuOpen)
		return;

	hook->m_framesSinceOpen++;

	// Stop polling after max frames if we already announced once
	if (hook->m_framesSinceOpen > MAX_RETRY_FRAMES && hook->m_initialAnnouncementDone) {
		hook->CheckContentChanges(a_this);
		return;
	}

	hook->CheckContentChanges(a_this);
}

void MessageBoxMenuHook::CheckContentChanges(RE::MessageBoxMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return;

	// Read current state from Scaleform
	std::string  currentBodyText = GetBodyText(a_menu);
	std::int32_t currentButtonCount = GetButtonCount(a_menu);
	std::int32_t currentSelection = GetSelectedIndex(a_menu);

	// Build current button label list
	std::vector<std::string> currentButtonLabels;
	currentButtonLabels.reserve(currentButtonCount);
	for (std::int32_t i = 0; i < currentButtonCount; ++i) {
		currentButtonLabels.emplace_back(GetButtonText(a_menu, i));
	}

	// Check if content changed (body text or button labels)
	bool contentChanged = (currentBodyText != m_lastBodyText) ||
	                      (currentButtonLabels != m_lastButtonLabels);

	if (contentChanged) {
		// Content changed (or first population) — announce full state
		std::string announcement;

		if (!currentBodyText.empty()) {
			announcement = LocalizationHelper::Translate(currentBodyText);
		}

		if (currentButtonCount > 0) {
			if (!announcement.empty()) {
				announcement += ". ";
			}
			std::string buttonWord = (currentButtonCount == 1) ? LocalizationHelper::Translate("$F4A_button") : LocalizationHelper::Translate("$F4A_buttons");
			announcement += std::to_string(currentButtonCount) + " " + buttonWord;

			if (currentSelection >= 0 && currentSelection < currentButtonCount) {
				const std::string& buttonText = currentButtonLabels[currentSelection];
				if (!buttonText.empty()) {
					announcement += ". " + LocalizationHelper::Translate(buttonText);
				}
			}
		}

		if (!announcement.empty()) {
			SpeakText(announcement);
			m_initialAnnouncementDone = true;
		} else if (m_framesSinceOpen >= MAX_RETRY_FRAMES) {
			m_initialAnnouncementDone = true;
		}

		// Update cached state
		m_lastBodyText = currentBodyText;
		m_lastButtonLabels = currentButtonLabels;
		m_lastSelection = currentSelection;
		m_buttonCount = currentButtonCount;
	} else if (currentSelection >= 0 && currentSelection != m_lastSelection) {
		// Content unchanged, but selection changed — announce just the button
		m_lastSelection = currentSelection;

		if (currentSelection < currentButtonCount) {
			const std::string& buttonText = currentButtonLabels[currentSelection];
			if (!buttonText.empty()) {
				SpeakText(LocalizationHelper::Translate(buttonText));
			}
		}
	}
}

std::string MessageBoxMenuHook::GetBodyText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, BODY_TEXT_PATH)) {
		if (value.IsString()) {
			return value.GetString();
		}
	}

	return "";
}

std::int32_t MessageBoxMenuHook::GetButtonCount(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return 0;
	}

	std::string           entryListPath = std::string(LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str())) {
		if (entryList.IsArray()) {
			return static_cast<std::int32_t>(entryList.GetArraySize());
		}
	}

	return 0;
}

std::int32_t MessageBoxMenuHook::GetSelectedIndex(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return -1;
	}

	std::string           indexPath = std::string(LIST_PATH) + ".selectedIndex";
	Scaleform::GFx::Value indexValue;
	if (a_menu->uiMovie->GetVariable(&indexValue, indexPath.c_str())) {
		if (indexValue.IsInt()) {
			return indexValue.GetInt();
		} else if (indexValue.IsNumber()) {
			return static_cast<std::int32_t>(indexValue.GetNumber());
		} else if (indexValue.IsUInt()) {
			return static_cast<std::int32_t>(indexValue.GetUInt());
		}
	}

	return -1;
}

std::string MessageBoxMenuHook::GetButtonText(RE::IMenu* a_menu, std::int32_t a_index)
{
	if (!a_menu || !a_menu->uiMovie || a_index < 0) {
		return "";
	}

	std::string           entryListPath = std::string(LIST_PATH) + ".entryList";
	Scaleform::GFx::Value entryList;
	if (!a_menu->uiMovie->GetVariable(&entryList, entryListPath.c_str()) || !entryList.IsArray()) {
		return "";
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value textValue;
	if (entry.GetMember("text", &textValue) && textValue.IsString()) {
		return textValue.GetString();
	}

	return "";
}

void MessageBoxMenuHook::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
