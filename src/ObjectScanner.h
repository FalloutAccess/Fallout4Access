#pragma once

#include "pch.h"
#include <string>
#include <vector>

enum class ScanCategory
{
	All,
	NPCs,
	Doors,
	Containers,
	Items,
	Activators,
	QuestMarkers,
	Corpses,
	Locations,
	Companions,
	PowerArmor,
	Workshop,
	COUNT  // For cycling
};

// Subcategories for filtering within a main category
// Generic names that are interpreted based on current category:
// - Activators: TypeA=Furniture, TypeB=Other
// - Containers: TypeA=NonEmpty, TypeB=Empty
// - Doors: TypeA=Locked, TypeB=Cell doors
enum class ScanSubcategory
{
	All,    // No subcategory filtering
	TypeA,  // Category-specific filter A
	TypeB,  // Category-specific filter B
	COUNT
};

class ObjectScanner : public RE::PlayerInputHandler
{
public:
	static ObjectScanner* GetSingleton();

	void Register();

	// Workshop mode integration
	void SetWorkshopMode(bool a_active);

	// BSInputEventUser overrides
	bool ShouldHandleEvent(const RE::InputEvent* a_event) override;
	void OnButtonEvent(const RE::ButtonEvent* a_event) override;

	// Scanner actions (called by KeybindActions from MCM keybinds)
	void ScanAndAnnounce();
	void SelectNext();
	void SelectPrevious();
	void NextCategory();
	void PreviousCategory();
	void CycleSubcategory();
	void AnnounceCurrentObject();
	void LookAtCurrentObject();
	void WalkToCurrentObject();

private:
	ObjectScanner();
	~ObjectScanner() override = default;
	ObjectScanner(const ObjectScanner&) = delete;
	ObjectScanner(ObjectScanner&&) = delete;
	ObjectScanner& operator=(const ObjectScanner&) = delete;
	ObjectScanner& operator=(ObjectScanner&&) = delete;

	// Scanning
	void ScanObjects();
	void ScanWorkshopObjects();
	void ScanQuestMarkers();
	void ScanCompassMarkers();
	void ScanCompanions();
	void ScanPowerArmor();
	bool NeedsRescan();
	void ScanIfNeeded();
	bool MatchesCategory(RE::TESObjectREFR* a_ref, ScanCategory a_category);

	// Subcategories
	bool        HasSubcategories(ScanCategory a_category);
	bool        MatchesSubcategory(RE::TESObjectREFR* a_ref, ScanSubcategory a_subcategory);
	const char* GetSubcategoryName(ScanSubcategory a_subcategory);
	bool        IsSittableFurniture(RE::TESObjectREFR* a_ref);
	bool        IsInteractable(RE::TESObjectREFR* a_ref);
	bool        IsContainerEmpty(RE::TESObjectREFR* a_ref);
	bool        IsDoorLocked(RE::TESObjectREFR* a_ref);
	bool        IsDoorOpen(RE::TESObjectREFR* a_ref);
	bool        IsCellDoor(RE::TESObjectREFR* a_ref);
	std::string GetDoorDestinationName(RE::TESObjectREFR* a_ref);
	const char* GetCategoryName(ScanCategory a_category);
	const char* GetMarkerTypeName(RE::MARKER_TYPE a_type);

	// Scanned object storage (declared early for use in helpers)
	struct ScannedObject
	{
		RE::TESObjectREFR*             ref;
		std::string                    name;
		RE::NiPoint3                   virtualPos{ 0.0f, 0.0f, 0.0f };  // Used for quest markers without refs
		bool                           isVirtual = false;               // True for quest markers
		std::optional<RE::MARKER_TYPE> markerType;                      // For location markers (nullopt if not a location)
	};

	// Debug
	void DebugAnnounceCurrentObject();

	// Helpers
	float       GetDistanceToObject(RE::TESObjectREFR* a_ref);
	float       GetDistanceToPosition(const RE::NiPoint3& a_pos);
	bool        IsValidPosition(const RE::NiPoint3& a_pos);
	float       GetDistanceToScannedObject(const ScannedObject& a_obj);
	std::string GetElevationIndicator(const ScannedObject& a_obj);
	std::string FindObjectiveName(RE::TESObjectREFR* a_ref, RE::PlayerCharacter* a_player);
	void        SpeakNoObjectsNearby();
	void        SpeakText(const std::string& a_text);

	std::vector<ScannedObject> m_scannedObjects;
	std::int32_t               m_currentIndex = -1;

	// Position tracking for auto-rescan
	RE::NiPoint3 m_lastScanPosition{ 0.0f, 0.0f, 0.0f };

	// Category filtering
	ScanCategory    m_currentCategory = ScanCategory::All;
	ScanSubcategory m_currentSubcategory = ScanSubcategory::All;

	// Workshop mode
	bool         m_workshopModeActive = false;
	ScanCategory m_preWorkshopCategory = ScanCategory::All;

	bool m_registered = false;

	// Constants
	static constexpr float RESCAN_DISTANCE_THRESHOLD = 100.0f;
	static constexpr float ELEVATION_THRESHOLD = 256.0f;  // Vertical difference to trigger above/below indicator (~1 floor height)
};
