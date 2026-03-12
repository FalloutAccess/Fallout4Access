#include "SleepWaitMenuAccessibility.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>

SleepWaitMenuAccessibility* SleepWaitMenuAccessibility::GetSingleton()
{
	static SleepWaitMenuAccessibility singleton;
	return &singleton;
}

void SleepWaitMenuAccessibility::Install()
{
	if (m_installed) {
		return;
	}

	// Hook SleepWaitMenu::AdvanceMovie (vtable index 4)
	// No RE::SleepWaitMenu header exists, so use RE::IMenu* as this pointer
	static REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)> sleepWaitHook(
		VTABLE::SleepWaitMenu[0],
		4,
		HookedSleepWaitAdvanceMovie);
	if (sleepWaitHook.Enable()) {
		s_sleepWaitHook = &sleepWaitHook;
		REX::INFO("SleepWaitMenuAccessibility: SleepWaitMenu hook installed");
	} else {
		REX::ERROR("SleepWaitMenuAccessibility: Failed to install SleepWaitMenu hook");
	}

	// SitWaitMenu has no hour picker (different SWF) - no AdvanceMovie hook needed

	m_installed = true;
}

void SleepWaitMenuAccessibility::OnSleepWaitMenuOpened()
{
	REX::INFO("SleepWaitMenuAccessibility: SleepWaitMenu opened");

	m_sleepWaitMenuOpen = true;
	m_lastHoursText.clear();
	m_frameDelay = FRAME_DELAY_AFTER_OPEN;

	// Defer 1 frame to let Scaleform populate
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_sleepWaitMenuOpen)
				return;

			auto ui = RE::UI::GetSingleton();
			if (!ui)
				return;

			auto menuPtr = ui->GetMenu("SleepWaitMenu"sv);
			if (!menuPtr || !menuPtr->uiMovie)
				return;
			auto* menu = menuPtr.get();

			// Read and announce the initial state
			auto prompt = GetPromptText(menu);
			auto hours = GetHoursText(menu);
			auto time = GetCurrentTimeText(menu);

			std::string announcement;
			if (!prompt.empty()) {
				announcement = prompt;
			}
			if (!hours.empty()) {
				if (!announcement.empty())
					announcement += " ";
				announcement += hours;
			}
			if (!time.empty()) {
				if (!announcement.empty())
					announcement += ". ";
				announcement += "Current time: " + time;
			}

			if (!announcement.empty()) {
				SpeakText(announcement);
			}

			// Store initial hours for change detection
			Scaleform::GFx::Value hoursVal;
			if (menu->uiMovie->GetVariable(&hoursVal, HOURS_VALUE_PATH) && hoursVal.IsString()) {
				m_lastHoursText = hoursVal.GetString();
			}
		});
	}
}

void SleepWaitMenuAccessibility::OnSleepWaitMenuClosed()
{
	REX::INFO("SleepWaitMenuAccessibility: SleepWaitMenu closed");
	m_sleepWaitMenuOpen = false;
}

void SleepWaitMenuAccessibility::OnSitWaitMenuOpened()
{
	REX::INFO("SleepWaitMenuAccessibility: SitWaitMenu opened");

	// SitWaitMenu is a minimal menu with just a "Wait" button - no hour picker
	// Just announce it opened
	SpeakText("Wait");
}

void SleepWaitMenuAccessibility::OnSitWaitMenuClosed()
{
	REX::INFO("SleepWaitMenuAccessibility: SitWaitMenu closed");
}

void SleepWaitMenuAccessibility::HookedSleepWaitAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	if (s_sleepWaitHook) {
		(*s_sleepWaitHook)(a_this, a_timeDelta, a_time);
	}

	auto* hook = GetSingleton();
	if (hook->m_sleepWaitMenuOpen) {
		hook->CheckStateChanges(a_this);
	}
}

void SleepWaitMenuAccessibility::CheckStateChanges(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return;

	// Wait a few frames after open for Scaleform to resolve localization
	if (m_frameDelay > 0) {
		m_frameDelay--;
		return;
	}

	Scaleform::GFx::Value hoursVal;
	if (!a_menu->uiMovie->GetVariable(&hoursVal, HOURS_VALUE_PATH) || !hoursVal.IsString()) {
		return;
	}

	std::string currentHours = hoursVal.GetString();
	if (currentHours != m_lastHoursText) {
		m_lastHoursText = currentHours;

		// Read the label too (singular "hour" vs plural "hours")
		Scaleform::GFx::Value labelVal;
		std::string           label;
		if (a_menu->uiMovie->GetVariable(&labelVal, HOURS_LABEL_PATH) && labelVal.IsString()) {
			label = LocalizationHelper::Translate(labelVal.GetString());
			// Remove trailing period for cleaner speech
			if (!label.empty() && label.back() == '.') {
				label.pop_back();
			}
		}

		std::string announcement = currentHours;
		if (!label.empty()) {
			announcement += " " + label;
		}
		SpeakText(announcement);
	}
}

std::string SleepWaitMenuAccessibility::GetHoursText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	Scaleform::GFx::Value hoursVal, labelVal;
	std::string           result;

	if (a_menu->uiMovie->GetVariable(&hoursVal, HOURS_VALUE_PATH) && hoursVal.IsString()) {
		result = hoursVal.GetString();
	}

	if (a_menu->uiMovie->GetVariable(&labelVal, HOURS_LABEL_PATH) && labelVal.IsString()) {
		std::string label = LocalizationHelper::Translate(labelVal.GetString());
		if (!label.empty() && label.back() == '.') {
			label.pop_back();
		}
		if (!result.empty() && !label.empty()) {
			result += " " + label;
		}
	}

	return result;
}

std::string SleepWaitMenuAccessibility::GetCurrentTimeText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	Scaleform::GFx::Value val;
	if (a_menu->uiMovie->GetVariable(&val, CURRENT_TIME_PATH) && val.IsString()) {
		return val.GetString();
	}
	return "";
}

std::string SleepWaitMenuAccessibility::GetPromptText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	Scaleform::GFx::Value val;
	if (a_menu->uiMovie->GetVariable(&val, PROMPT_PATH) && val.IsString()) {
		return LocalizationHelper::Translate(val.GetString());
	}
	return "";
}

void SleepWaitMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
