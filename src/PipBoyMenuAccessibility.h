#pragma once

#include "PipBoyPageBase.h"
#include "pch.h"
#include <memory>

class PipBoyInventoryPage;
class PipBoyDataPage;
class PipBoyMapPage;
class PipBoyStatsPage;
class PipBoyRadioPage;

// Main coordinator for Pip-Boy menu accessibility
// Manages page lifecycle and routes input to active page.
// Page/tab changes are detected per-frame via AdvanceMovie hook polling.
class PipBoyMenuAccessibility
{
public:
	static PipBoyMenuAccessibility* GetSingleton();

	// Lifecycle methods (called by MenuAccessibility)
	void OnPipBoyMenuOpened();
	void OnPipBoyMenuClosed();

	// Per-frame state check (called from PipBoyCallbackHook::HookedAdvanceMovie)
	// Detects page changes, tab changes, and delegates to active page
	void CheckSelection();

	void Reset();

	// Getter for map page (needed by MenuAccessibility for scanner keys)
	PipBoyMapPage* GetMapPage() { return m_mapPage.get(); }

	// H-key entry point. Announces whatever the Pipboy bottom bar would
	// render for the current page: carry weight + caps on INV, level +
	// XP % on STAT. Silent on DATA/MAP/RADIO (the game's bottom bar
	// doesn't surface player stats there either).
	void AnnouncePageStats();

private:
	PipBoyMenuAccessibility();
	~PipBoyMenuAccessibility() = default;
	PipBoyMenuAccessibility(const PipBoyMenuAccessibility&) = delete;
	PipBoyMenuAccessibility(PipBoyMenuAccessibility&&) = delete;
	PipBoyMenuAccessibility& operator=(const PipBoyMenuAccessibility&) = delete;
	PipBoyMenuAccessibility& operator=(PipBoyMenuAccessibility&&) = delete;

	// Page management
	enum class Page
	{
		STAT = 0,
		INV = 1,
		DATA = 2,
		MAP = 3,
		RADIO = 4
	};

	Page            GetCurrentPage(RE::IMenu* a_menu);
	PipBoyPageBase* GetActivePage();
	void            SwitchToPage(Page a_newPage);
	std::string     GetLocalizedPageName(Page a_page);

	// Tab change detection (called per-frame from CheckSelection)
	void CheckTabChange();

	// Announce current player level and XP % toward next level.
	// Reads PipboyMenu::dataObj (XPLevel, XPProgressPct) — same source the
	// game's Pipboy_BottomBar uses to render "LEVEL N" and the XP meter.
	// Private because the H-key entry point is AnnouncePageStats; callers
	// outside the class should go through that.
	void AnnounceLevelAndXP();

	// Utilities
	void SpeakText(const std::string& a_text, bool a_interrupt = false);

	// State
	bool m_isOpen = false;
	Page m_currentPage = Page::STAT;

	// Page handlers
	std::unique_ptr<PipBoyInventoryPage> m_inventoryPage;
	std::unique_ptr<PipBoyDataPage>      m_dataPage;
	std::unique_ptr<PipBoyMapPage>       m_mapPage;
	std::unique_ptr<PipBoyStatsPage>     m_statsPage;
	std::unique_ptr<PipBoyRadioPage>     m_radioPage;
};
