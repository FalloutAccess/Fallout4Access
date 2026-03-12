#pragma once

#include "pch.h"
#include <RE/E/ExamineConfirmMenu.h>

// Hooks ExamineConfirmMenu::AdvanceMovie to announce crafting confirmations
// This menu appears when building, scrapping, or encountering repair failures at workbenches
class ExamineConfirmMenuHook
{
public:
	static ExamineConfirmMenuHook* GetSingleton();

	// Install the vtable hook (called once at plugin startup)
	void Install();

	// Reset state tracking (call when menu opens)
	void ResetState();

	// Called by MenuAccessibility when menu opens/closes
	void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
	ExamineConfirmMenuHook() = default;
	~ExamineConfirmMenuHook() = default;
	ExamineConfirmMenuHook(const ExamineConfirmMenuHook&) = delete;
	ExamineConfirmMenuHook(ExamineConfirmMenuHook&&) = delete;
	ExamineConfirmMenuHook& operator=(const ExamineConfirmMenuHook&) = delete;
	ExamineConfirmMenuHook& operator=(ExamineConfirmMenuHook&&) = delete;

	// The hooked AdvanceMovie function
	static void HookedAdvanceMovie(RE::ExamineConfirmMenu* a_this, float a_timeDelta, std::uint64_t a_time);

	// Check for content and announce it
	void CheckContent(RE::ExamineConfirmMenu* a_menu);

	// Scaleform accessors
	std::string GetQuestionText(RE::IMenu* a_menu);
	std::string GetComponentList(RE::IMenu* a_menu);
	std::string GetComponentEntries(Scaleform::GFx::Value& a_sourceEntry);

	// Utility
	void SpeakText(const std::string& a_text);

	// State
	bool                          m_installed = false;
	bool                          m_menuOpen = false;
	bool                          m_announced = false;
	std::int32_t                  m_framesSinceOpen = 0;
	static constexpr std::int32_t MAX_RETRY_FRAMES = 10;

	// Scaleform paths (from SWF analysis)
	static constexpr const char* QUESTION_TEXT_PATH = "root.Menu_mc.Confirm_mc.ConfirmQuestion_tf.text";
	static constexpr const char* CONFIRM_PANEL_PATH = "root.Menu_mc.Confirm_mc";

	// The vtable hook
	static inline REL::THookVFT<void(RE::ExamineConfirmMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
