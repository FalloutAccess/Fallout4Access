#pragma once

#include "PipBoyPageBase.h"
#include <string>
#include <vector>

// Handles the MAP page of the Pip-Boy
// Tabs: World Map(0), Local Map(1)
// Uses scanner keys (Page Up/Down, Home, End) for keyboard-based marker navigation
class PipBoyMapPage : public PipBoyPageBase
{
public:
	PipBoyMapPage() = default;
	~PipBoyMapPage() override = default;

	// PipBoyPageBase interface
	void OnPageActivated() override;
	void OnPageDeactivated() override;
	void CheckSelection() override;
	void CheckTabChange() override;
	void Reset() override;

	// Scanner key handlers (called from MenuAccessibility)
	void OnNextMarker();         // Page Down
	void OnPreviousMarker();     // Page Up
	void OnAnnounceDetails();    // Home
	void OnSetReferencePoint();  // Shift+Home
	void OnCycleFilter();        // End
	void OnActivateMarker();     // MCM-bound: fast travel to the selected marker

private:
	// Map marker data structure
	struct MapMarker
	{
		std::string   name;
		std::uint32_t type = 0;
		std::uint32_t arrayIndex = 0;
		float         x = 0, y = 0;
		float         distance = 0;
		std::string   direction;
		bool          undiscovered = false;
		bool          cleared = false;
		bool          isSettlement = false;
		std::uint32_t population = 0;
		std::uint32_t happiness = 0;
	};

	// Marker filter categories
	enum class MarkerFilter
	{
		All = 0,
		Quests,       // type 78-80 (quest markers)
		Settlements,  // type 13 or workshopOwned
		Discovered,   // !undiscovered
		Undiscovered  // undiscovered
	};

	// Core functionality
	void BuildMarkerList();
	void SelectMarkerAtIndex(std::int32_t a_index);
	void CenterOnCurrentMarker();  // Centers map and sets visual selection (Home key)

	// Fast-travel completion. Keyboard scanner nav never moves the OS cursor, so
	// the game's cursor-driven selectedMarkerInfo doesn't point at our marker.
	// After CenterOnCurrentMarker() repositions the cursor, this re-derives the
	// game's selection (RefreshSelectedMarker) on a later frame, verifies it
	// matches our target, then fires onMarkerActivated so the vanilla
	// confirm/undiscovered/hardcore dialog appears. Falls back to a cursor-free
	// host FastTravel call if the cursor approach can't lock on.
	void ActivateSelectedMarkerDeferred(std::uint32_t a_arrayIndex, int a_attemptsRemaining);
	void FastTravelDirect(std::uint32_t a_arrayIndex);

	// Number of extra frames ActivateSelectedMarkerDeferred retries the cursor →
	// selection lock-on before falling back to a direct host FastTravel call.
	static constexpr int MARKER_ACTIVATE_MAX_ATTEMPTS = 3;

	void        AnnounceCurrentMarker(bool a_fullDetails = false, bool a_interrupt = false);
	std::string GetMarkerTypeName(std::uint32_t a_type);
	std::string GetDirectionString(float a_dx, float a_dy);
	bool        MarkerMatchesFilter(const MapMarker& a_marker);
	void        AnnounceFilterChange();
	const char* GetFilterName(MarkerFilter a_filter);
	std::string GetLocalizedTabName(std::int32_t a_tabIndex);

	// Embedded message box (fast travel confirmation, marker management)
	void CheckMessageBox();

	// Distance and direction calculation
	void CalculateDistanceAndDirection(MapMarker& a_marker);
	void RecalculateAllDistances();

	// State
	std::vector<MapMarker>    m_markers;            // All markers from Scaleform
	std::vector<std::int32_t> m_filteredIndices;    // Indices into m_markers matching current filter
	std::int32_t              m_currentIndex = -1;  // Current position in filtered list
	MarkerFilter              m_filter = MarkerFilter::All;
	bool                      m_isLocalMap = false;

	// Distance reference point
	bool        m_useCustomReference = false;        // True if using marker as reference
	float       m_referenceX = 0, m_referenceY = 0;  // Reference point coordinates
	std::string m_referenceName;                     // Name of reference marker

	// Player position cache (updated on activation)
	float m_playerX = 0, m_playerY = 0;

	// Game selection tracking (polls uiSelectedMarkerIndex)
	std::int32_t m_lastGameSelectedIndex = -1;

	// Ignore CheckSelection writeback for a few frames after a keyboard scanner
	// nav, giving the game's FindClosestMarkerIndex time to react to the newly
	// positioned cursor. Without this the poll reads stale selectedMarkerInfo
	// and snaps m_currentIndex back to the previous marker.
	std::int32_t m_scannerNavSuppressFrames = 0;

	// Embedded message box state (PipboyMessageBox.swf loaded into MessageHolder_mc)
	bool                     m_messageBoxOpen = false;
	std::int32_t             m_messageBoxLastSelection = -1;
	std::string              m_messageBoxLastBodyText;
	std::vector<std::string> m_messageBoxLastButtonLabels;
};
