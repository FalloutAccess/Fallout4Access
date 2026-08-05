#pragma once

#include "pch.h"
#include <string>

// Screen reader accessibility for the in-game Creations store ("MarketplaceMenu"
// on the next-gen / 1.11.221 runtime).
//
// The store is a C++-driven widget tree, not a Scaleform UI we can scrape: there
// is no entryList/selectedIndex and the rendered text fields have dynamic names.
// Instead we read the focused Creation straight from the game's C++ objects,
// which is resolution-, aspect-ratio-, and language-independent.
//
// Mechanism (all anchors resolve via address-library IDs, no hardcoded
// single-version offsets):
//   - Hook UI_detailsPage vtable (REL::ID 4829987) slot 3 = Update(float,...).
//     This method consumes the focused content every frame the detail panel is
//     alive, under both mouse and keyboard/controller navigation.
//   - The UI_detailsPage instance holds the focused MarketplaceContent* at
//     +0x88. We dedupe on that pointer and announce only when it changes.
//   - MarketplaceContent fields (verified offsets): title +0x20, price (credits)
//     +0xEC, owned +0xFB, installed +0xFE, downloading +0xFF, bundle +0x109.
class MarketplaceMenuAccessibility
{
public:
	static MarketplaceMenuAccessibility* GetSingleton();

	// Install the UI_detailsPage vtable hook. Idempotent; safe to call whenever
	// the Creations menus open.
	void Install();

	// Announce the store name and reset dedupe state (called when MarketplaceMenu
	// opens).
	void OnMenuOpen();

	// Reset dedupe state (called when MarketplaceMenu closes).
	void OnMenuClose();

private:
	MarketplaceMenuAccessibility() = default;
	~MarketplaceMenuAccessibility() = default;
	MarketplaceMenuAccessibility(const MarketplaceMenuAccessibility&) = delete;
	MarketplaceMenuAccessibility(MarketplaceMenuAccessibility&&) = delete;
	MarketplaceMenuAccessibility& operator=(const MarketplaceMenuAccessibility&) = delete;
	MarketplaceMenuAccessibility& operator=(MarketplaceMenuAccessibility&&) = delete;

	// UI_detailsPage::Update(this, float delta, void* p3, longlong* p4) — slot 3.
	// Fires while the full item-details page is open.
	static void HookedDetailUpdate(void* a_this, float a_delta, void* a_p3, void* a_p4);

	// UI_ScrollingList::setSelectedIndex(this, int newIndex, void* p3, void* p4)
	// — slot 23. Fires on every browse focus move (within a shelf and between
	// shelves). `this` is always the active shelf.
	static void HookedSetSelectedIndex(void* a_this, int a_newIndex, void* a_p3, void* a_p4);

	// Read the focused content off a_detailsPage (+0x88) and announce if changed.
	void OnDetailUpdate(void* a_detailsPage);

	// Read the focused card's content off the active shelf and announce if changed.
	void OnShelfSelect(void* a_scrollingList);

	// Shared dedupe + speak for a focused card. a_title is the localized name read
	// off the card widget (UI_listEntry+0xB0); a_content supplies the status flags.
	void AnnounceContent(const std::string& a_title, std::uintptr_t a_content);

	// Build the spoken string: localized title + one short status (no description).
	std::string BuildContentSpeech(const std::string& a_title, std::uintptr_t a_content);

	void SpeakText(const std::string& a_text);

	bool           m_installed = false;
	std::uintptr_t m_lastContent = 0;      // last focused MarketplaceContent* announced
	std::uintptr_t m_lastDetailSwept = 0;  // TEMP: last content the detail page was dumped for
	std::string    m_lastSpoken;

	// MarketplaceContent field offsets (verified, 1.11.221; resolve identically
	// on other AE builds via the struct layout, not raw image offsets).
	// NB: the localized *title* is NOT in this struct — it's cached on the card
	// widget (kEntryTitle). This struct holds description/author/IDs + status flags.
	static constexpr std::ptrdiff_t kFocusedContent = 0x88;  // on UI_detailsPage
	static constexpr std::ptrdiff_t kPriceCredits = 0xEC;    // int32
	static constexpr std::ptrdiff_t kOwned = 0xFB;           // bool
	static constexpr std::ptrdiff_t kInstalled = 0xFE;       // bool
	static constexpr std::ptrdiff_t kDownloading = 0xFF;     // bool
	static constexpr std::ptrdiff_t kIsBundle = 0x109;       // bool

	// Browse-shelf (UI_ScrollingList) field offsets.
	static constexpr std::ptrdiff_t kSelIndex = 0x98;      // int32 focused card index
	static constexpr std::ptrdiff_t kEntriesBegin = 0xC8;  // UI_listEntry** begin
	static constexpr std::ptrdiff_t kEntriesEnd = 0xD0;    // UI_listEntry** end
	static constexpr std::ptrdiff_t kEntryContent = 0x70;  // MarketplaceContent* on UI_listEntry
	static constexpr std::ptrdiff_t kEntryTitle = 0xB0;    // std::string localized title on UI_listEntry

	// UI_detailsPage vtable address-library ID, slot index of Update.
	static constexpr std::uint64_t kDetailsPageVtableID = 4829987;
	static constexpr std::size_t   kUpdateSlot = 3;

	// UI_ScrollingList vtable ID + setSelectedIndex slot; UI_listEntry vtable ID
	// (to validate that a shelf's entries are real cards, not the category /
	// search lists which are also UI_ScrollingList).
	static constexpr std::uint64_t kScrollListVtableID = 4830050;
	static constexpr std::size_t   kSetSelectedIndexSlot = 23;
	static constexpr std::uint64_t kListEntryVtableID = 4830141;

	static inline REL::THookVFT<void(void*, float, void*, void*)>* s_updateHook = nullptr;
	static inline REL::THookVFT<void(void*, int, void*, void*)>*   s_selectHook = nullptr;

	// Resolved UI_listEntry vtable address; validates that a shelf's entries are
	// real cards before reading their content pointer.
	static inline std::uintptr_t s_listEntryVtableAddr = 0;
};
