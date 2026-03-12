#pragma once

#include "pch.h"

class VATSMenuAccessibility
{
public:
	static VATSMenuAccessibility* GetSingleton();

	// Lifecycle (called from MenuAccessibility)
	void OnVATSMenuOpened();
	void OnVATSMenuClosed();
	void Reset();

	// Called by VATSMenuHook when body part changes
	void AnnounceBodyPartChange(std::uint32_t a_partIndex);

	// Input-triggered checks (called from MenuAccessibility)
	void CheckQueuedShots();     // Q key (queue attack)
	void CheckCriticalCharge();  // Periodic critical charge check

private:
	VATSMenuAccessibility() = default;
	~VATSMenuAccessibility() = default;
	VATSMenuAccessibility(const VATSMenuAccessibility&) = delete;
	VATSMenuAccessibility(VATSMenuAccessibility&&) = delete;
	VATSMenuAccessibility& operator=(const VATSMenuAccessibility&) = delete;
	VATSMenuAccessibility& operator=(VATSMenuAccessibility&&) = delete;

	// Scaleform data extraction
	std::string   GetTargetName();
	std::string   GetTargetLevel(RE::IMenu* a_menu);
	std::uint32_t GetSelectedPartIndex(RE::IMenu* a_menu);
	std::string   GetPartName(RE::IMenu* a_menu, std::uint32_t a_partIndex);
	std::string   GetPartHitChance(RE::IMenu* a_menu, std::uint32_t a_partIndex);
	std::string   GetPartHealth(RE::IMenu* a_menu, std::uint32_t a_partIndex);
	std::uint32_t GetPartCount(RE::IMenu* a_menu);
	int           GetQueuedShotCount();
	float         GetAPRemaining();
	float         GetCriticalChargePercent();

	// Helper to navigate to a part's AnimationInstance
	bool GetPartAnimationInstance(RE::IMenu* a_menu, std::uint32_t a_partIndex, Scaleform::GFx::Value* a_outAnim);

	// Announcement builders
	std::string BuildFullAnnouncement(RE::IMenu* a_menu, std::uint32_t a_partIndex);
	std::string BuildPartAnnouncement(RE::IMenu* a_menu, std::uint32_t a_partIndex);

	// Utilities
	void SpeakText(const std::string& a_text);

	// State tracking
	bool          m_isOpen = false;
	bool          m_menuAnnounced = false;
	std::string   m_lastTargetName;
	std::uint32_t m_lastSelectedPart = 0xFFFFFFFF;
	int           m_lastQueuedShotCount = 0;
	int           m_lastCriticalPercent = -1;

	// Scaleform paths for VATSMenu
	// VATSMenu uses "root.MenuInstance" as the main container
	static constexpr const char* MENU_INSTANCE_PATH = "root.MenuInstance";
	static constexpr const char* SELECTED_PART_PATH = "root.MenuInstance.SelectedPart";
	static constexpr const char* PART_INFOS_PATH = "root.MenuInstance.PartInfos";
	static constexpr const char* TARGET_LEVEL_PATH = "root.MenuInstance.ResistancesInstance.Level.text";
	static constexpr const char* RESISTANCES_PATH = "root.MenuInstance.ResistancesInstance";
};
