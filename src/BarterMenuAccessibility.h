#pragma once

#include "ItemIdentity.h"
#include "pch.h"
#include <RE/B/BarterMenu.h>

// Provides accessibility for BarterMenu (trading with vendors)
// Uses AdvanceMovie vtable hook for per-frame state tracking
class BarterMenuAccessibility
{
public:
	static BarterMenuAccessibility* GetSingleton();

	// Called by MenuAccessibility when BarterMenu opens/closes
	void OnBarterMenuOpened();
	void OnBarterMenuClosed();

	// Install the vtable hook (call once at plugin startup)
	void Install();

	// Called by MenuAccessibility when Q key is pressed to switch sides
	void SwitchSides();

	// Announce caps for the currently focused side (H key)
	void AnnounceBarterStatus();

private:
	BarterMenuAccessibility() = default;
	~BarterMenuAccessibility() = default;
	BarterMenuAccessibility(const BarterMenuAccessibility&) = delete;
	BarterMenuAccessibility(BarterMenuAccessibility&&) = delete;
	BarterMenuAccessibility& operator=(const BarterMenuAccessibility&) = delete;
	BarterMenuAccessibility& operator=(BarterMenuAccessibility&&) = delete;

	// The hooked AdvanceMovie function
	static void HookedAdvanceMovie(RE::BarterMenu* a_this, float a_timeDelta, std::uint64_t a_time);

	// The hooked OnButtonEvent function (for keyboard input)
	static void HookedOnButtonEvent(RE::BarterMenu* a_this, const RE::ButtonEvent* a_event);

	// State checking (called every frame when menu is open)
	void CheckStateChanges(RE::BarterMenu* a_menu);

	// Focus tracking (vendor vs player inventory)
	void CheckFocusChange(RE::BarterMenu* a_menu);
	bool IsVendorFocused(RE::BarterMenu* a_menu);

	// Filter/category tracking (left/right arrows change filter)
	void        CheckFilterChange(RE::BarterMenu* a_menu);
	std::string GetCurrentFilterName(RE::BarterMenu* a_menu, bool a_vendorSide);

	// Sort mode tracking
	void        CheckSortChange(RE::BarterMenu* a_menu);
	std::string GetSortButtonLabel(RE::BarterMenu* a_menu);

	// Item selection tracking
	void         CheckItemSelection(RE::BarterMenu* a_menu);
	std::int32_t GetSelectedIndex(RE::BarterMenu* a_menu, bool a_vendorSide);
	ItemIdentity GetItemAtIndex(RE::BarterMenu* a_menu, std::int32_t a_index, bool a_vendorSide);
	std::int32_t GetBarterCount(RE::BarterMenu* a_menu, std::int32_t a_index, bool a_vendorSide);

	// Quantity picker
	void        CheckQuantityMenu(RE::BarterMenu* a_menu);
	bool        IsQuantityMenuOpen(RE::BarterMenu* a_menu);
	std::string GetQuantityValue(RE::BarterMenu* a_menu);

	// Caps tracking
	void        CheckCapsChange(RE::BarterMenu* a_menu);
	std::string GetCapsTransferAmount(RE::BarterMenu* a_menu);
	std::string GetCapsTransferDirection(RE::BarterMenu* a_menu);
	std::string GetCaps(RE::BarterMenu* a_menu, bool a_vendorSide);

	// Announcement helpers
	void        AnnounceBarterOpened(RE::BarterMenu* a_menu);
	void        AnnounceItem(const ItemIdentity& a_item, std::int32_t a_barterCount, bool a_vendorSide);
	void        AnnounceFocusChange(bool a_vendorSide);
	std::string GetVendorName(RE::BarterMenu* a_menu);
	std::string GetItemStats(RE::BarterMenu* a_menu);

	// Utility
	void SpeakText(const std::string& a_text);

	// State tracking
	bool         m_installed = false;
	bool         m_menuOpen = false;
	bool         m_openingAnnouncementDone = false;  // Skip item announcements until vendor name announced
	bool         m_vendorFocused = true;             // true = vendor side, false = player side
	std::int32_t m_lastVendorIndex = -1;
	std::int32_t m_lastPlayerIndex = -1;
	std::string  m_lastVendorFilterName;  // Filter/category name for vendor
	std::string  m_lastPlayerFilterName;  // Filter/category name for player inventory
	std::string  m_lastSortLabel;         // Sort button label (e.g., "SORT (DMG)")
	std::string  m_lastCapsTransfer;      // Last caps transfer amount
	bool         m_quantityMenuOpen = false;
	std::string  m_lastQuantityValue;

	// Scaleform paths (same structure as ContainerMenu)
	static constexpr const char* VENDOR_LIST_PATH = "root.ContainerList_mc";
	static constexpr const char* PLAYER_LIST_PATH = "root.PlayerInventory_mc.PlayerList_mc";
	static constexpr const char* VENDOR_NAME_PATH = "root.ContainerInventory_mc.ContainerListHeader.headerText";
	static constexpr const char* CAPS_TRANSFER_PATH = "root.CapsTransferInfo_mc.TransferCaps_tf.text";

	// The vtable hooks
	static inline REL::THookVFT<void(RE::BarterMenu*, float, std::uint64_t)>*   s_advanceMovieHook = nullptr;
	static inline REL::THookVFT<void(RE::BarterMenu*, const RE::ButtonEvent*)>* s_buttonEventHook = nullptr;
};
