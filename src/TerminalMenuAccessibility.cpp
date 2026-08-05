#include "TerminalMenuAccessibility.h"
#include "SpeechManager.h"
#include "TerminalMenuHook.h"

TerminalMenuAccessibility* TerminalMenuAccessibility::GetSingleton()
{
	static TerminalMenuAccessibility singleton;
	return &singleton;
}

void TerminalMenuAccessibility::OnTerminalMenuOpened()
{
	m_isOpen = true;
	REX::INFO("TerminalMenuAccessibility: menu opened");

	// Install the AdvanceMovie hook (only installs once, stays active)
	TerminalMenuHook::GetSingleton()->Install();

	// Tell the hook the menu is open and reset its state
	TerminalMenuHook::GetSingleton()->SetMenuOpen(true);
	TerminalMenuHook::GetSingleton()->ResetState();

	// Defer initial announcement to next frame so Scaleform has time to populate
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_isOpen)
				return;

			auto ui = RE::UI::GetSingleton();
			if (!ui)
				return;

			auto menu = ui->GetMenu("TerminalMenu"sv);
			if (!menu || !menu->uiMovie)
				return;

			// Announce terminal header and welcome text
			std::string header = GetHeaderText(menu.get());
			std::string welcome = GetWelcomeText(menu.get());

			std::string announcement;
			if (!header.empty()) {
				announcement = "Terminal: " + header;
			} else {
				announcement = "Terminal";
			}

			SpeakText(announcement);

			if (!welcome.empty()) {
				SpeakText(welcome);
			}

			// The AdvanceMovie hook will handle all subsequent state tracking
		});
	}
}

void TerminalMenuAccessibility::OnTerminalMenuClosed()
{
	m_isOpen = false;
	REX::INFO("TerminalMenuAccessibility: menu closed");

	// Tell the hook the menu is closed
	TerminalMenuHook::GetSingleton()->SetMenuOpen(false);
}

std::string TerminalMenuAccessibility::GetHeaderText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, HEADER_TEXT_PATH)) {
		if (value.IsString()) {
			return value.GetString();
		}
	}

	return "";
}

std::string TerminalMenuAccessibility::GetWelcomeText(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return "";
	}

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, WELCOME_TEXT_PATH)) {
		if (value.IsString()) {
			return value.GetString();
		}
	}

	return "";
}

void TerminalMenuAccessibility::SpeakText(const std::string& a_text, bool a_interrupt)
{
	SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}
