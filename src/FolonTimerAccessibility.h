#pragma once

#include "pch.h"
#include <string>
#include <string_view>

// Fallout London ships an on-screen countdown widget via its FOLON_UITimer
// F4SE plugin (a registered menu whose DLL writes the remaining time to
// root.UITimer.Timer_Text.tf). It is used by many timed quests (bomb defusal,
// escort, escape sequences). Sighted players read the number off the HUD; we
// announce when the timer appears and when it ends, and let the user pull the
// current value on demand via the "Announce Stats" key (default H).
//
// Self-contained singleton: appear/end are driven by MenuAccessibility's
// MenuOpenCloseEvent sink; the on-demand read is invoked from
// KeybindActions::AnnounceStats() when a timer is active.
class FolonTimerAccessibility
{
public:
	static FolonTimerAccessibility* GetSingleton();

	// The menu name FOLON_UITimer registers with. Exposed so MenuAccessibility
	// can match it in its open/close sink.
	static constexpr std::string_view kMenuName{ "FOLON_UITimer" };

	// Driven by MenuAccessibility's menu-open sink.
	void OnTimerMenuOpened();
	void OnTimerMenuClosed();

	// True while the timer widget is on screen.
	bool IsActive() const { return m_active; }

	// Read the current remaining time and speak it. Returns true if a timer was
	// active and something was announced (so callers can fall through to other
	// behaviour when no timer is up). Called from the Announce Stats key.
	bool AnnounceCurrentTime();

private:
	FolonTimerAccessibility() = default;
	~FolonTimerAccessibility() = default;
	FolonTimerAccessibility(const FolonTimerAccessibility&) = delete;
	FolonTimerAccessibility(FolonTimerAccessibility&&) = delete;
	FolonTimerAccessibility& operator=(const FolonTimerAccessibility&) = delete;
	FolonTimerAccessibility& operator=(FolonTimerAccessibility&&) = delete;

	// Read root.UITimer.Timer_Text.tf.text (with fallbacks). Empty if unreadable.
	std::string ReadTimerText();

	bool m_active = false;
};
