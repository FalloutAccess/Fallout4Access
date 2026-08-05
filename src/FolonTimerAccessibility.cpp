#include "FolonTimerAccessibility.h"
#include "SpeechManager.h"
#include <Scaleform/G/GFx_Value.h>

using Scaleform::GFx::Value;

FolonTimerAccessibility* FolonTimerAccessibility::GetSingleton()
{
	static FolonTimerAccessibility singleton;
	return &singleton;
}

std::string FolonTimerAccessibility::ReadTimerText()
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return "";
	}

	auto menu = ui->GetMenu(kMenuName);
	if (!menu || !menu->uiMovie) {
		return "";
	}

	// The DLL drives a def_text clip: it calls root.UITimer.Timer_Text.set_text,
	// which writes the string into the clip's inner `tf` TextField. The movie
	// root is exposed as either `root` or `root1` depending on how the menu was
	// loaded, so try a few candidate paths.
	static constexpr const char* kPaths[] = {
		"root.UITimer.Timer_Text.tf.text",
		"root1.UITimer.Timer_Text.tf.text",
		"root.UITimer.Timer_Text.text",
		"root1.UITimer.Timer_Text.text",
	};

	for (const char* path : kPaths) {
		Value value;
		if (menu->uiMovie->GetVariable(&value, path) && value.IsString()) {
			const char* text = value.GetString();
			if (text && text[0] != '\0') {
				return text;
			}
		}
	}

	return "";
}

void FolonTimerAccessibility::OnTimerMenuOpened()
{
	m_active = true;
	REX::INFO("FolonTimerAccessibility: timer widget opened");

	// The DLL populates the text a frame or two after the menu opens, so defer
	// the appear announcement to let Scaleform fill it in.
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
				taskInterface2->AddTask([this]() {
					if (!m_active) {
						return;
					}
					std::string text = ReadTimerText();
					std::string announcement = "Timer started";
					if (!text.empty()) {
						announcement += ", " + text;
					}
					SpeechManager::GetSingleton()->Speak(announcement);
				});
			}
		});
	}
}

void FolonTimerAccessibility::OnTimerMenuClosed()
{
	if (!m_active) {
		return;
	}
	m_active = false;
	REX::INFO("FolonTimerAccessibility: timer widget closed");
	SpeechManager::GetSingleton()->Speak("Timer ended");
}

bool FolonTimerAccessibility::AnnounceCurrentTime()
{
	if (!m_active) {
		return false;
	}

	std::string text = ReadTimerText();
	if (text.empty()) {
		// Timer is up but we couldn't read the value (e.g. path mismatch on this
		// build). Still report that a timer is running rather than going silent.
		SpeechManager::GetSingleton()->Speak("Timer");
		return true;
	}

	SpeechManager::GetSingleton()->Speak("Timer, " + text);
	return true;
}
