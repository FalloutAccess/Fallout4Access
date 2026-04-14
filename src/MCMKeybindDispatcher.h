#pragma once

#include "pch.h"
#include <string>
#include <unordered_map>

// Reads MCM keybind assignments from our mod's settings INI files and
// dispatches them ourselves. We use MCM's `keyinput` control type (not
// `hotkey`), which stores the pressed key as a "keycode,modifiers" string
// in the mod's settings.ini and skips MCM's built-in conflict detection.
//
// Why roll our own dispatch:
//   1. MCM's keybind handler is gated by `numPauseGame == 0` (f4mcm/src/
//      MCMInput.cpp:87), so MCM-managed keybinds never fire in pausing
//      menus (Pip-Boy, Container, Barter).
//   2. MCM's `hotkey` type triggers conflict warnings against game controls
//      even when our bindings are only meant to fire in menu contexts.
//   Using `keyinput` + our dispatcher sidesteps both. The trade-off is we
//   handle dispatch in both gameplay (ObjectScanner::OnButtonEvent) and
//   menu contexts (MenuAccessibility::OnButtonEvent).
//
// File layout (standard MCM convention):
//   Defaults: Data\MCM\Config\fallout4access\settings.ini (ships with mod)
//   User:     Data\MCM\Settings\fallout4access.ini         (written by MCM)
// Both have a [Keybinds] section with `action_id=keycode,modifiers` lines.
// User file takes precedence per key; missing keys fall back to defaults.
class MCMKeybindDispatcher
{
public:
	static MCMKeybindDispatcher* GetSingleton();

	// (Re)load bindings from defaults + user-override INIs.
	// Call at plugin startup and on menu opens so MCM-UI rebinds apply
	// without a game restart.
	void LoadFromFile();

	// If a_event (a just-pressed keyboard event) matches one of our
	// configured keybinds, dispatch the action and return true.
	// Caller should skip this when MCM itself would handle the event
	// (i.e. non-pausing contexts). Use from both gameplay and menu
	// input handlers — MCM's `numPauseGame == 0` gate means MCM never
	// double-fires with us in pausing menus.
	bool TryDispatch(const RE::ButtonEvent* a_event);

private:
	MCMKeybindDispatcher() = default;
	~MCMKeybindDispatcher() = default;
	MCMKeybindDispatcher(const MCMKeybindDispatcher&) = delete;
	MCMKeybindDispatcher(MCMKeybindDispatcher&&) = delete;
	MCMKeybindDispatcher& operator=(const MCMKeybindDispatcher&) = delete;
	MCMKeybindDispatcher& operator=(MCMKeybindDispatcher&&) = delete;

	// (keycode, modifiers) packed into a single uint64 for hashing.
	//   low 32 bits: keycode
	//   next 8 bits: modifier bitfield (1=shift, 2=ctrl, 4=alt) per MCM's convention
	static std::uint64_t PackKey(std::uint32_t a_keycode, std::uint8_t a_modifiers)
	{
		return static_cast<std::uint64_t>(a_keycode) | (static_cast<std::uint64_t>(a_modifiers) << 32);
	}

	// Read the [Keybinds] section of `a_iniPath` and merge into `m_bindings`.
	// Silently no-ops if the file or section doesn't exist.
	void MergeFromIni(const char* a_iniPath);

	// Parse a "keycode,modifiers" value string into components.
	// Returns false on malformed input.
	static bool ParseKeyValue(const std::string& a_value, std::uint32_t& a_outKeycode, std::uint8_t& a_outModifiers);

	std::unordered_map<std::uint64_t, std::string> m_bindings;
};
