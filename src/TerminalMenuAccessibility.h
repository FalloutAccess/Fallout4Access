#pragma once

#include "pch.h"
#include <RE/T/TerminalMenu.h>

// Handles initial terminal menu announcement when opened
// State tracking (mode changes, selection, text) is handled by TerminalMenuHook
class TerminalMenuAccessibility
{
public:
	static TerminalMenuAccessibility* GetSingleton();

	void OnTerminalMenuOpened();
	void OnTerminalMenuClosed();

private:
	TerminalMenuAccessibility() = default;
	~TerminalMenuAccessibility() = default;
	TerminalMenuAccessibility(const TerminalMenuAccessibility&) = delete;
	TerminalMenuAccessibility(TerminalMenuAccessibility&&) = delete;
	TerminalMenuAccessibility& operator=(const TerminalMenuAccessibility&) = delete;
	TerminalMenuAccessibility& operator=(TerminalMenuAccessibility&&) = delete;

	// Text retrieval for initial announcement
	std::string GetHeaderText(RE::IMenu* a_menu);
	std::string GetWelcomeText(RE::IMenu* a_menu);

	void SpeakText(const std::string& a_text, bool a_interrupt = false);

	bool m_isOpen = false;

	// Scaleform paths for initial announcement
	static constexpr const char* HEADER_TEXT_PATH = "root.TerminalBase_mc.strHeaderText";
	static constexpr const char* WELCOME_TEXT_PATH = "root.TerminalBase_mc.strWelcomeText";
};
