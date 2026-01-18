#pragma once

#include "pch.h"
#include <vector>
#include <string>

enum class ScanCategory {
    All,
    NPCs,
    Doors,
    Containers,
    Activators,
    QuestMarkers,
    Corpses,
    Locations,
    Companions,
    PowerArmor,
    COUNT  // For cycling
};

class ObjectScanner : public RE::PlayerInputHandler
{
public:
    static ObjectScanner* GetSingleton();

    void Register();

    // BSInputEventUser overrides
    bool ShouldHandleEvent(const RE::InputEvent* a_event) override;
    void OnButtonEvent(const RE::ButtonEvent* a_event) override;

private:
    ObjectScanner();
    ~ObjectScanner() override = default;
    ObjectScanner(const ObjectScanner&) = delete;
    ObjectScanner(ObjectScanner&&) = delete;
    ObjectScanner& operator=(const ObjectScanner&) = delete;
    ObjectScanner& operator=(ObjectScanner&&) = delete;

    // Scanning
    void ScanObjects();
    void ScanQuestMarkers();
    void ScanCompassMarkers();
    void ScanCompanions();
    void ScanPowerArmor();
    void ScanAndAnnounce();
    bool NeedsRescan();
    void ScanIfNeeded();

    // Navigation
    void SelectNext();
    void SelectPrevious();
    void AnnounceCurrentObject();
    void LookAtCurrentObject();
    void WalkToCurrentObject();

    // Categories
    void CycleCategory();
    bool MatchesCategory(RE::TESObjectREFR* a_ref, ScanCategory a_category);
    bool IsInteractable(RE::TESObjectREFR* a_ref);
    bool IsContainerEmpty(RE::TESObjectREFR* a_ref);
    const char* GetCategoryName(ScanCategory a_category);
    const char* GetMarkerTypeName(RE::MARKER_TYPE a_type);

    // Scanned object storage (declared early for use in helpers)
    struct ScannedObject {
        RE::TESObjectREFR* ref;
        std::string name;
        RE::NiPoint3 virtualPos{0.0f, 0.0f, 0.0f};  // Used for quest markers without refs
        bool isVirtual = false;  // True for quest markers
        std::optional<RE::MARKER_TYPE> markerType;  // For location markers (nullopt if not a location)
    };

    // Helpers
    float GetDistanceToObject(RE::TESObjectREFR* a_ref);
    float GetDistanceToPosition(const RE::NiPoint3& a_pos);
    bool IsValidPosition(const RE::NiPoint3& a_pos);
    float GetDistanceToScannedObject(const ScannedObject& a_obj);
    void SpeakNoObjectsNearby();
    void SpeakText(const std::string& a_text);

    std::vector<ScannedObject> m_scannedObjects;
    std::int32_t m_currentIndex = -1;

    // Position tracking for auto-rescan
    RE::NiPoint3 m_lastScanPosition{0.0f, 0.0f, 0.0f};

    // Category filtering
    ScanCategory m_currentCategory = ScanCategory::All;

    bool m_registered = false;

    // Constants
    static constexpr float RESCAN_DISTANCE_THRESHOLD = 100.0f;

    // Key codes (from BS_BUTTON_CODE.h)
    static constexpr std::uint32_t NUMPAD_5 = 0x65;
    static constexpr std::uint32_t PAGE_UP = 0x21;
    static constexpr std::uint32_t PAGE_DOWN = 0x22;
    static constexpr std::uint32_t HOME = 0x24;
    static constexpr std::uint32_t END_KEY = 0x23;
};
