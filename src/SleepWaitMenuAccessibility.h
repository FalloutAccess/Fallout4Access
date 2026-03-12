#pragma once

#include "pch.h"

// Provides accessibility for SleepWaitMenu (beds/standing wait) and SitWaitMenu (furniture wait)
// SleepWaitMenu: AdvanceMovie vtable hook for per-frame hours tracking
// SitWaitMenu: Simple announcement on open (no hour picker in its SWF)
class SleepWaitMenuAccessibility
{
public:
	static SleepWaitMenuAccessibility* GetSingleton();

	// Install vtable hooks (call once at plugin startup)
	void Install();

	// Called by MenuAccessibility when menus open/close
	void OnSleepWaitMenuOpened();
	void OnSleepWaitMenuClosed();
	void OnSitWaitMenuOpened();
	void OnSitWaitMenuClosed();

private:
	SleepWaitMenuAccessibility() = default;
	~SleepWaitMenuAccessibility() = default;
	SleepWaitMenuAccessibility(const SleepWaitMenuAccessibility&) = delete;
	SleepWaitMenuAccessibility(SleepWaitMenuAccessibility&&) = delete;
	SleepWaitMenuAccessibility& operator=(const SleepWaitMenuAccessibility&) = delete;
	SleepWaitMenuAccessibility& operator=(SleepWaitMenuAccessibility&&) = delete;

	// Hooked AdvanceMovie for SleepWaitMenu
	static void HookedSleepWaitAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

	// State checking (called every frame when SleepWaitMenu is open)
	void CheckStateChanges(RE::IMenu* a_menu);

	// Data extraction
	std::string GetHoursText(RE::IMenu* a_menu);
	std::string GetCurrentTimeText(RE::IMenu* a_menu);
	std::string GetPromptText(RE::IMenu* a_menu);

	// Utility
	void SpeakText(const std::string& a_text);

	// State
	bool        m_installed = false;
	bool        m_sleepWaitMenuOpen = false;
	int         m_frameDelay = 0;
	std::string m_lastHoursText;

	// Frame delay to let Scaleform populate after menu open
	static constexpr int FRAME_DELAY_AFTER_OPEN = 3;

	// Scaleform paths (SleepWaitMenu.swf: MainTimeline -> Menu_mc -> children)
	static constexpr const char* PROMPT_PATH = "root.Menu_mc.Function_tf.text";
	static constexpr const char* CURRENT_TIME_PATH = "root.Menu_mc.CurrentTime_tf.text";
	static constexpr const char* HOURS_VALUE_PATH = "root.Menu_mc.Value_tf.text";
	static constexpr const char* HOURS_LABEL_PATH = "root.Menu_mc.HoursLabel_tf.text";

	// Vtable hook (only SleepWaitMenu needs per-frame tracking)
	static inline REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_sleepWaitHook = nullptr;
};
