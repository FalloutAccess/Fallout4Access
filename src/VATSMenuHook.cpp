#include "VATSMenuHook.h"
#include "SpeechManager.h"
#include "VATSMenuAccessibility.h"
#include <Scaleform/G/GFx_Value.h>

using Scaleform::GFx::Value;

VATSMenuHook* VATSMenuHook::GetSingleton()
{
	static VATSMenuHook singleton;
	return &singleton;
}

void VATSMenuHook::Install()
{
	if (m_installed) {
		REX::DEBUG("VATSMenuHook: Already installed");
		return;
	}

	// Hook VATSMenu::AdvanceMovie (vtable index 4)
	// VTABLE::VATSMenu[0] is the primary vtable (IMenu/GameMenuBase)
	static REL::THookVFT<void(RE::IMenu*, float, std::uint64_t)> hook(
		VTABLE::VATSMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	if (hook.Enable()) {
		s_advanceMovieHook = &hook;
		m_installed = true;
		REX::INFO("VATSMenuHook: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("VATSMenuHook: Failed to install AdvanceMovie hook");
	}
}

void VATSMenuHook::ResetState()
{
	// NOTE: deliberately do NOT clear m_lastTargetName here. Carrying the last
	// announced name across VATS sessions suppresses the stale enemy name the
	// HUD meter still shows on re-entry (after a kill / leaving VATS); we only
	// announce once the meter actually updates to the genuinely new target.
	m_lastBodyPart = 0xFFFFFFFF;
	m_frameDelay = FRAME_DELAY_AFTER_OPEN;
}

void VATSMenuHook::HookedAdvanceMovie(RE::IMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	// Call original function first to let the game update state
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	// Then check for state changes
	auto* hook = GetSingleton();
	if (hook->m_menuOpen) {
		hook->CheckStateChanges(a_this);
	}
}

void VATSMenuHook::CheckStateChanges(RE::IMenu* a_menu)
{
	if (!a_menu)
		return;

	// Wait for frame delay after menu open
	if (m_frameDelay > 0) {
		m_frameDelay--;
		return;
	}

	// Check target changes (most important)
	CheckTargetChange(a_menu);

	// Check body part selection changes
	CheckBodyPartChange(a_menu);
}

void VATSMenuHook::CheckTargetChange(RE::IMenu* a_menu)
{
	// The HUD enemy meter lags behind / persists across VATS sessions, so on
	// re-entry it can still show the previous enemy. Because m_lastTargetName is
	// carried across sessions (see ResetState), a stale name equal to the last
	// one we announced is suppressed here; we only speak once the meter updates
	// to a genuinely different target.
	std::string currentName = GetCurrentTargetName();
	if (currentName.empty() || currentName == m_lastTargetName) {
		return;
	}

	m_lastTargetName = currentName;
	AnnounceTarget(a_menu, currentName);
}

void VATSMenuHook::AnnounceTarget(RE::IMenu* a_menu, const std::string& a_name)
{
	m_lastBodyPart = 0xFFFFFFFF;  // Reset body part on target change

	std::string announcement = a_name;

	// Decorations come from the HUD enemy health meter.
	if (IsTargetLegendary()) {
		announcement += ", Legendary";
	}

	// Dangerous status (skull icon - much higher level)
	if (IsTargetDangerous()) {
		announcement += ", Dangerous";
	}

	float healthPercent = GetTargetHealthPercent();
	if (healthPercent > 0.0f) {
		announcement += ", " + std::to_string(static_cast<int>(healthPercent)) + " percent health";
	}

	SpeechManager::GetSingleton()->Speak(announcement);

	// Also announce the current body part for the new target.
	std::uint32_t currentPart = GetCurrentBodyPart(a_menu);
	if (currentPart != 0xFFFFFFFF) {
		m_lastBodyPart = currentPart;
		VATSMenuAccessibility::GetSingleton()->AnnounceBodyPartChange(currentPart);
	}
}

void VATSMenuHook::CheckBodyPartChange(RE::IMenu* a_menu)
{
	std::uint32_t currentPart = GetCurrentBodyPart(a_menu);

	if (currentPart != 0xFFFFFFFF && currentPart != m_lastBodyPart) {
		m_lastBodyPart = currentPart;
		VATSMenuAccessibility::GetSingleton()->AnnounceBodyPartChange(currentPart);
	}
}

std::string VATSMenuHook::GetCurrentTargetName()
{
	// Enemy name from HUD's EnemyHealthMeter_mc.DisplayText_tf - the name shown
	// at the top of the screen during VATS.
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		auto hudMenu = ui->GetMenu<RE::HUDMenu>();
		if (hudMenu && hudMenu->uiMovie) {
			Scaleform::GFx::Value nameValue;
			if (hudMenu->uiMovie->GetVariable(&nameValue, "root.TopCenterGroup_mc.EnemyHealthMeter_mc.DisplayText_tf.text")) {
				if (nameValue.IsString()) {
					const char* name = nameValue.GetString();
					if (name && name[0] != '\0') {
						return std::string(name);
					}
				}
			}
		}
	}

	return "";
}

std::uint32_t VATSMenuHook::GetCurrentBodyPart(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return 0xFFFFFFFF;

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, SELECTED_PART_PATH)) {
		if (value.IsUInt()) {
			return value.GetUInt();
		} else if (value.IsInt()) {
			auto intVal = value.GetInt();
			if (intVal >= 0) {
				return static_cast<std::uint32_t>(intVal);
			}
		} else if (value.IsNumber()) {
			auto numVal = value.GetNumber();
			if (numVal >= 0) {
				return static_cast<std::uint32_t>(numVal);
			}
		}
	}

	return 0xFFFFFFFF;
}

bool VATSMenuHook::IsTargetLegendary()
{
	// Check HUD's EnemyHealthMeter_mc.LegendaryIcon_mc.visible
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		auto hudMenu = ui->GetMenu<RE::HUDMenu>();
		if (hudMenu && hudMenu->uiMovie) {
			Scaleform::GFx::Value visibleValue;
			if (hudMenu->uiMovie->GetVariable(&visibleValue, "root.TopCenterGroup_mc.EnemyHealthMeter_mc.LegendaryIcon_mc.visible")) {
				if (visibleValue.IsBoolean()) {
					return visibleValue.GetBoolean();
				}
			}
		}
	}
	return false;
}

bool VATSMenuHook::IsTargetDangerous()
{
	// Check HUD's EnemyHealthMeter_mc.SkullIcon_mc.visible (enemy much higher level)
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		auto hudMenu = ui->GetMenu<RE::HUDMenu>();
		if (hudMenu && hudMenu->uiMovie) {
			Scaleform::GFx::Value visibleValue;
			if (hudMenu->uiMovie->GetVariable(&visibleValue, "root.TopCenterGroup_mc.EnemyHealthMeter_mc.SkullIcon_mc.visible")) {
				if (visibleValue.IsBoolean()) {
					return visibleValue.GetBoolean();
				}
			}
		}
	}
	return false;
}

float VATSMenuHook::GetTargetHealthPercent()
{
	// Get health from HUD's EnemyHealthMeter_mc.MeterBar_mc.Percent
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		auto hudMenu = ui->GetMenu<RE::HUDMenu>();
		if (hudMenu && hudMenu->uiMovie) {
			Scaleform::GFx::Value percentValue;
			if (hudMenu->uiMovie->GetVariable(&percentValue, "root.TopCenterGroup_mc.EnemyHealthMeter_mc.MeterBar_mc.Percent")) {
				if (percentValue.IsNumber()) {
					// Returns 0.0 to 1.0, convert to percentage
					return static_cast<float>(percentValue.GetNumber() * 100.0);
				}
			}
		}
	}

	return 0.0f;
}
