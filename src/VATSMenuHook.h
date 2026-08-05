#pragma once

#include "pch.h"

// Hooks VATSMenu::AdvanceMovie to check for state changes every frame.
// Tracks the aimed target (by name) and the selected body part.
class VATSMenuHook
{
public:
	static VATSMenuHook* GetSingleton();

	// Install the vtable hook (only installs once, stays active)
	void Install();

	// Check if hook is currently active
	bool IsInstalled() const { return m_installed; }

	// Reset state tracking (call when menu opens)
	void ResetState();

	// Called by VATSMenuAccessibility when menu opens/closes
	void SetMenuOpen(bool a_open) { m_menuOpen = a_open; }

private:
	VATSMenuHook() = default;
	~VATSMenuHook() = default;
	VATSMenuHook(const VATSMenuHook&) = delete;
	VATSMenuHook(VATSMenuHook&&) = delete;
	VATSMenuHook& operator=(const VATSMenuHook&) = delete;
	VATSMenuHook& operator=(VATSMenuHook&&) = delete;

	// The hooked AdvanceMovie function
	static void HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time);

	// State checking - called after original AdvanceMovie
	void CheckStateChanges(RE::IMenu* a_menu);
	void CheckTargetChange(RE::IMenu* a_menu);
	void CheckBodyPartChange(RE::IMenu* a_menu);

	// Current target name from the HUD enemy health meter (the name shown at the
	// top of the screen during VATS).
	std::string GetCurrentTargetName();

	// Build and speak a target announcement (name + HUD decorations) and
	// announce the new target's body part.
	void AnnounceTarget(RE::IMenu* a_menu, const std::string& a_name);

	std::uint32_t GetCurrentBodyPart(RE::IMenu* a_menu);

	// Target decorations, read from the HUD enemy health meter
	float GetTargetHealthPercent();
	bool  IsTargetLegendary();
	bool  IsTargetDangerous();

	// State
	bool          m_installed = false;
	bool          m_menuOpen = false;
	int           m_frameDelay = 0;
	std::string   m_lastTargetName;  // carried across sessions to suppress stale HUD names
	std::uint32_t m_lastBodyPart = 0xFFFFFFFF;

	// Frame delay after menu open for Scaleform/HUD to populate
	static constexpr int FRAME_DELAY_AFTER_OPEN = 3;

	// Scaleform path for the selected body part
	static constexpr const char* SELECTED_PART_PATH = "root.MenuInstance.SelectedPart";

	// The vtable hook
	static inline REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)>* s_advanceMovieHook = nullptr;
};
