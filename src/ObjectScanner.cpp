#include "ObjectScanner.h"
#include "AutoWalk.h"
#include "SpeechManager.h"
#include <SRAL.h>
#include <algorithm>
#include <cmath>
#include <REX/W32/USER32.h>
#include <RE/P/PipboyDataManager.h>
#include <RE/P/PowerArmor.h>
#include <RE/T/TESFurniture.h>

// Static storage for singleton
static RE::PlayerControlsData s_dummyData{};
static ObjectScanner* s_singleton = nullptr;

ObjectScanner::ObjectScanner() :
    RE::PlayerInputHandler(s_dummyData)
{
}

ObjectScanner* ObjectScanner::GetSingleton()
{
    if (!s_singleton) {
        s_singleton = new ObjectScanner();
    }
    return s_singleton;
}

void ObjectScanner::Register()
{
    if (auto* taskInterface = F4SE::GetTaskInterface()) {
        taskInterface->AddTask([]() {
            auto* scanner = GetSingleton();
            if (scanner->m_registered) {
                return;
            }

            if (auto* playerControls = RE::PlayerControls::GetSingleton()) {
                playerControls->RegisterHandler(scanner);
                scanner->m_registered = true;
                REX::INFO("ObjectScanner registered with PlayerControls");
            } else {
                REX::WARN("ObjectScanner: PlayerControls not available yet");
            }
        });
    }
}

bool ObjectScanner::ShouldHandleEvent(const RE::InputEvent* a_event)
{
    // Check AutoWalk arrival on every input event (frequent update)
    AutoWalk::GetSingleton()->Update();

    if (!a_event) return false;

    auto* ui = RE::UI::GetSingleton();
    if (ui && ui->menuMode) {
        return false;
    }

    return true;
}

void ObjectScanner::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    if (!a_event || !a_event->QJustPressed()) {
        return;
    }

    auto device = a_event->device.get();
    auto idCode = a_event->QIDCode();

    if (device != RE::INPUT_DEVICE::kKeyboard) {
        return;
    }

    // Cancel walking on WASD keys (DirectX scan codes)
    if (AutoWalk::GetSingleton()->IsWalking()) {
        // WASD scan codes: W=0x11, A=0x1E, S=0x1F, D=0x20
        // Also Escape=0x01 and Space=0x39
        if (idCode == 0x11 || idCode == 0x1E || idCode == 0x1F || idCode == 0x20 ||
            idCode == 0x01 || idCode == 0x39) {
            AutoWalk::GetSingleton()->Stop();
            SpeakText("Stopped");
            return;
        }
    }

    if (idCode == NUMPAD_5) {
        REX::INFO("ObjectScanner: Numpad 5 pressed, scanning...");
        ScanAndAnnounce();
    } else if (idCode == PAGE_DOWN) {
        REX::INFO("ObjectScanner: Page Down pressed");
        SelectNext();
    } else if (idCode == PAGE_UP) {
        REX::INFO("ObjectScanner: Page Up pressed");
        SelectPrevious();
    } else if (idCode == HOME) {
        // Check if Shift is held using REX::W32 (returns negative if key is down)
        bool shiftHeld = REX::W32::GetKeyState(REX::W32::VK_SHIFT) < 0;

        if (shiftHeld) {
            // Toggle: if already walking, stop; otherwise start walking
            if (AutoWalk::GetSingleton()->IsWalking()) {
                REX::INFO("ObjectScanner: Shift+Home pressed, stopping walk");
                AutoWalk::GetSingleton()->Stop();
                SpeakText("Stopped");
            } else {
                REX::INFO("ObjectScanner: Shift+Home pressed, walking to object");
                WalkToCurrentObject();
            }
        } else {
            REX::INFO("ObjectScanner: Home pressed");
            LookAtCurrentObject();
            AnnounceCurrentObject();
        }
    } else if (idCode == END_KEY) {
        REX::INFO("ObjectScanner: End pressed, cycling category");
        CycleCategory();
    }
}

bool ObjectScanner::NeedsRescan()
{
    if (m_scannedObjects.empty()) {
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    float distMoved = player->GetPosition().GetDistance(m_lastScanPosition);
    return distMoved > RESCAN_DISTANCE_THRESHOLD;
}

void ObjectScanner::ScanIfNeeded()
{
    if (NeedsRescan()) {
        REX::INFO("ObjectScanner: Auto-rescanning (player moved or no prior scan)");
        ScanObjects();
    }
}

void ObjectScanner::CycleCategory()
{
    int next = (static_cast<int>(m_currentCategory) + 1) % static_cast<int>(ScanCategory::COUNT);
    m_currentCategory = static_cast<ScanCategory>(next);

    // Clear cached objects to force rescan with new filter
    m_scannedObjects.clear();
    m_currentIndex = -1;

    SpeakText(GetCategoryName(m_currentCategory));
}

const char* ObjectScanner::GetCategoryName(ScanCategory a_category)
{
    switch (a_category) {
        case ScanCategory::All: return "All objects";
        case ScanCategory::NPCs: return "NPCs";
        case ScanCategory::Doors: return "Doors";
        case ScanCategory::Containers: return "Containers";
        case ScanCategory::Activators: return "Activators";
        case ScanCategory::QuestMarkers: return "Quest Markers";
        case ScanCategory::Corpses: return "Corpses";
        case ScanCategory::Locations: return "Locations";
        case ScanCategory::Companions: return "Companions";
        case ScanCategory::PowerArmor: return "Power Armor";
        default: return "Unknown";
    }
}

const char* ObjectScanner::GetMarkerTypeName(RE::MARKER_TYPE a_type)
{
    switch (a_type) {
        case RE::MARKER_TYPE::kCave: return "Cave";
        case RE::MARKER_TYPE::kCity: return "City";
        case RE::MARKER_TYPE::kDiamondCity: return "Diamond City";
        case RE::MARKER_TYPE::kEncampment: return "Encampment";
        case RE::MARKER_TYPE::kIndustrial: return "Factory";
        case RE::MARKER_TYPE::kGovtBuilding: return "Government Building";
        case RE::MARKER_TYPE::kMetro: return "Metro Station";
        case RE::MARKER_TYPE::kMilitaryBase: return "Military Base";
        case RE::MARKER_TYPE::kLandmark: return "Landmark";
        case RE::MARKER_TYPE::kOffice: return "Office Building";
        case RE::MARKER_TYPE::kRuinsTown: return "Ruins";
        case RE::MARKER_TYPE::kRuinsUrban: return "Urban Ruins";
        case RE::MARKER_TYPE::kSanctuary: return "Sanctuary";
        case RE::MARKER_TYPE::kSettlement: return "Settlement";
        case RE::MARKER_TYPE::kSewer: return "Sewer";
        case RE::MARKER_TYPE::kVault: return "Vault";
        case RE::MARKER_TYPE::kAirfield: return "Airfield";
        case RE::MARKER_TYPE::kBunkerHill: return "Bunker Hill";
        case RE::MARKER_TYPE::kCamper: return "Camper";
        case RE::MARKER_TYPE::kCar: return "Car";
        case RE::MARKER_TYPE::kChurch: return "Church";
        case RE::MARKER_TYPE::kCountryClub: return "Country Club";
        case RE::MARKER_TYPE::kCustomHouse: return "Custom House";
        case RE::MARKER_TYPE::kDriveIn: return "Drive-In";
        case RE::MARKER_TYPE::kElevatedHighway: return "Highway";
        case RE::MARKER_TYPE::kFaneuilHall: return "Faneuil Hall";
        case RE::MARKER_TYPE::kFarm: return "Farm";
        case RE::MARKER_TYPE::kFillingStation: return "Gas Station";
        case RE::MARKER_TYPE::kForest: return "Forest";
        case RE::MARKER_TYPE::kGoodNeighbor: return "Goodneighbor";
        case RE::MARKER_TYPE::kGraveyard: return "Graveyard";
        case RE::MARKER_TYPE::kHospital: return "Hospital";
        case RE::MARKER_TYPE::kIndustrialDome: return "Industrial Dome";
        case RE::MARKER_TYPE::kIndustrialStacks: return "Industrial Stacks";
        case RE::MARKER_TYPE::kInstitute: return "Institute";
        case RE::MARKER_TYPE::kIrishPride: return "Irish Pride";
        case RE::MARKER_TYPE::kJunkyard: return "Junkyard";
        case RE::MARKER_TYPE::kObservatory: return "Observatory";
        case RE::MARKER_TYPE::kPier: return "Pier";
        case RE::MARKER_TYPE::kPondOrLake: return "Pond";
        case RE::MARKER_TYPE::kQuarry: return "Quarry";
        case RE::MARKER_TYPE::kRadioactiveArea: return "Radioactive Area";
        case RE::MARKER_TYPE::kRadioTower: return "Radio Tower";
        case RE::MARKER_TYPE::kSalem: return "Salem";
        case RE::MARKER_TYPE::kSchool: return "School";
        case RE::MARKER_TYPE::kShipwreck: return "Shipwreck";
        case RE::MARKER_TYPE::kSubmarine: return "Submarine";
        case RE::MARKER_TYPE::kSwanPond: return "Swan Pond";
        case RE::MARKER_TYPE::kTown: return "Town";
        case RE::MARKER_TYPE::kBOS: return "Brotherhood of Steel";
        case RE::MARKER_TYPE::kBrownstone: return "Brownstone";
        case RE::MARKER_TYPE::kBunker: return "Bunker";
        case RE::MARKER_TYPE::kCastle: return "Castle";
        case RE::MARKER_TYPE::kSkyscraper: return "Skyscraper";
        case RE::MARKER_TYPE::kLibertalia: return "Libertalia";
        case RE::MARKER_TYPE::kLowrise: return "Low-Rise";
        case RE::MARKER_TYPE::kMinutemen: return "Minutemen";
        case RE::MARKER_TYPE::kPoliceStation: return "Police Station";
        case RE::MARKER_TYPE::kPrydwen: return "Prydwen";
        case RE::MARKER_TYPE::kRailroadFaction: return "Railroad";
        case RE::MARKER_TYPE::kRailroad: return "Railroad";
        case RE::MARKER_TYPE::kSatellite: return "Satellite";
        case RE::MARKER_TYPE::kSentinel: return "Sentinel";
        case RE::MARKER_TYPE::kUSSConstitution: return "USS Constitution";
        case RE::MARKER_TYPE::kMechanist: return "Mechanist";
        case RE::MARKER_TYPE::kRaiderSettlement: return "Raider Settlement";
        case RE::MARKER_TYPE::kVassalSettlement: return "Vassal Settlement";
        case RE::MARKER_TYPE::kPotentialVassalSettlement: return "Potential Settlement";
        case RE::MARKER_TYPE::kBottlingPlant: return "Bottling Plant";
        case RE::MARKER_TYPE::kGalactic: return "Galactic Zone";
        case RE::MARKER_TYPE::kHub: return "Hub";
        case RE::MARKER_TYPE::kKiddieKingdom: return "Kiddie Kingdom";
        case RE::MARKER_TYPE::kMonorail: return "Monorail";
        case RE::MARKER_TYPE::kRides: return "Rides";
        case RE::MARKER_TYPE::kSafari: return "Safari";
        case RE::MARKER_TYPE::kWildWest: return "Wild West";
        case RE::MARKER_TYPE::kPOI: return "Point of Interest";
        case RE::MARKER_TYPE::kDisciples: return "Disciples";
        case RE::MARKER_TYPE::kOperators: return "Operators";
        case RE::MARKER_TYPE::kPack: return "Pack";
        case RE::MARKER_TYPE::kDoor: return "Door";
        case RE::MARKER_TYPE::kCountEditor: return "Editor Marker";
        case RE::MARKER_TYPE::kQuest: return "Quest";
        case RE::MARKER_TYPE::kQuestDoor: return "Quest Door";
        case RE::MARKER_TYPE::kQuestMultiple: return "Quest";
        case RE::MARKER_TYPE::kPlayerSet: return "Custom Marker";
        case RE::MARKER_TYPE::kPlayerLocation: return "Player Location";
        case RE::MARKER_TYPE::kPowerArmorLocation: return "Power Armor";
        case RE::MARKER_TYPE::kDogmeat: return "Dogmeat";
        case RE::MARKER_TYPE::kTeammate: return "Companion";
        default: return "Location";
    }
}

bool ObjectScanner::MatchesCategory(RE::TESObjectREFR* a_ref, ScanCategory a_category)
{
    if (a_category == ScanCategory::All) return true;
    if (!a_ref) return false;

    // These categories use separate scanning methods
    if (a_category == ScanCategory::QuestMarkers) return false;
    if (a_category == ScanCategory::Locations) return false;
    if (a_category == ScanCategory::Companions) return false;
    if (a_category == ScanCategory::PowerArmor) return false;

    // Check for Actor (NPC or Corpse)
    if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        if (auto* actor = a_ref->As<RE::Actor>()) {
            bool isDead = actor->IsDead(false);
            if (a_category == ScanCategory::NPCs) {
                return !isDead;  // NPCs are living actors only
            }
            if (a_category == ScanCategory::Corpses) {
                return isDead;   // Corpses are dead actors only
            }
        }
        return false;
    }

    // Get base object for other types
    auto* baseObj = a_ref->GetObjectReference();
    if (!baseObj) return false;

    auto formType = baseObj->GetFormType();

    switch (a_category) {
        case ScanCategory::Doors:
            return formType == RE::ENUM_FORM_ID::kDOOR;

        case ScanCategory::Containers:
            return formType == RE::ENUM_FORM_ID::kCONT;

        case ScanCategory::Activators:
            return formType == RE::ENUM_FORM_ID::kACTI ||
                   formType == RE::ENUM_FORM_ID::kTACT ||
                   formType == RE::ENUM_FORM_ID::kTERM ||
                   formType == RE::ENUM_FORM_ID::kFURN;

        default:
            return false;
    }
}

bool ObjectScanner::IsInteractable(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    // Skip disabled/deleted
    if (a_ref->IsDeleted() || a_ref->IsDisabled()) return false;

    // Check for actors (living NPCs and corpses)
    if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        if (auto* actor = a_ref->As<RE::Actor>()) {
            // For dead actors (corpses), exclude if already looted
            if (actor->IsDead(false)) {
                if (IsContainerEmpty(a_ref)) {
                    return false;  // Skip empty corpses
                }
            }
        }
        return true;  // Living NPCs and non-empty corpses are interactable
    }

    auto* baseObj = a_ref->GetObjectReference();
    if (!baseObj) return false;

    // Doors are always interactable (skip activation check)
    // This ensures load doors are included
    if (baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
        return true;
    }

    // Check if object is activatable (has activation prompt)
    // This filters out static objects like fence posts, rocks, etc.
    RE::BSString activateText;
    if (!baseObj->GetActivateText(a_ref, activateText)) {
        return false;  // No activation = not interactable
    }

    return true;
}

bool ObjectScanner::IsContainerEmpty(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return true;

    // Get the runtime inventory list (tracks actual contents after looting)
    RE::BGSInventoryList* inventoryList = a_ref->inventoryList;

    // If no inventory list exists, container is empty
    if (!inventoryList) return true;

    // Check if the data array is empty
    if (inventoryList->data.empty()) return true;

    // Count actual items (entries may exist but have 0 count after looting)
    std::uint32_t totalItems = 0;
    for (const auto& item : inventoryList->data) {
        totalItems += item.GetCount();
    }

    return totalItems == 0;
}

void ObjectScanner::ScanObjects()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    // These categories use different scanning methods
    if (m_currentCategory == ScanCategory::QuestMarkers) {
        ScanQuestMarkers();
        return;
    }
    if (m_currentCategory == ScanCategory::Locations) {
        ScanCompassMarkers();
        return;
    }
    if (m_currentCategory == ScanCategory::Companions) {
        ScanCompanions();
        return;
    }
    if (m_currentCategory == ScanCategory::PowerArmor) {
        ScanPowerArmor();
        return;
    }

    auto* cell = player->GetParentCell();
    if (!cell) {
        REX::WARN("ObjectScanner: Player has no parent cell");
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();

    // Store scan position for rescan detection
    m_lastScanPosition = playerPos;

    // Clear previous scan
    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Temporary struct for sorting
    struct TempObject {
        RE::TESObjectREFR* ref;
        std::string name;
        float distance;
    };
    std::vector<TempObject> tempObjects;

    for (auto& refPtr : cell->references) {
        auto* ref = refPtr.get();
        if (!ref) continue;
        if (ref == player) continue;

        // Filter by interactability and category
        if (!IsInteractable(ref)) continue;
        if (!MatchesCategory(ref, m_currentCategory)) continue;

        RE::NiPoint3 objPos = ref->GetPosition();
        float distSq = playerPos.GetSquaredDistance(objPos);

        const char* name = ref->GetDisplayFullName();
        if (!name || name[0] == '\0') continue;

        tempObjects.push_back({ref, name, std::sqrt(distSq)});
    }

    // Sort by distance (closest first)
    std::sort(tempObjects.begin(), tempObjects.end(),
        [](const TempObject& a, const TempObject& b) {
            return a.distance < b.distance;
        });

    // Store in member vector
    for (const auto& obj : tempObjects) {
        m_scannedObjects.push_back({obj.ref, obj.name});
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Scanned {} {}",
        m_scannedObjects.size(), GetCategoryName(m_currentCategory));
}

void ObjectScanner::ScanQuestMarkers()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();
    m_lastScanPosition = playerPos;

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Get PipboyDataManager for the authoritative quest marker data
    auto* pipboyData = RE::PipboyDataManager::GetSingleton();

    // Use worldQuestMarkers which contains the currently tracked quest marker(s)
    // This is what the game uses to show markers on the Pip-Boy map
    if (pipboyData) {
        auto& worldMarkers = pipboyData->mapData.worldQuestMarkers;
        REX::INFO("ObjectScanner: worldQuestMarkers has {} entries", worldMarkers.size());

        for (const auto& [refHandle, pipboyObj] : worldMarkers) {
            if (!refHandle) continue;

            auto refPtr = refHandle.get();
            if (!refPtr || !refPtr.get()) continue;

            auto* ref = refPtr.get();
            if (ref->IsDeleted() || ref->IsDisabled()) continue;

            // Get position from the reference
            RE::NiPoint3 pos = ref->GetPosition();
            if (!IsValidPosition(pos)) continue;

            // Try to find which objective this marker belongs to
            // by checking if any quest's aliases point to this reference
            std::string markerName;
            for (const auto& instancedObj : player->objectives) {
                if (!instancedObj.objective) continue;
                if (instancedObj.enstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) continue;

                auto* quest = instancedObj.objective->ownerQuest;
                if (!quest) continue;

                // Check if any alias in this quest points to our marker ref
                bool foundMatch = false;
                for (const auto* alias : quest->aliases) {
                    if (!alias) continue;

                    RE::ObjectRefHandle aliasHandle;
                    quest->GetAliasedRef(&aliasHandle, alias->aliasID);

                    if (aliasHandle) {
                        auto aliasRefPtr = aliasHandle.get();
                        if (aliasRefPtr && aliasRefPtr.get() == ref) {
                            // Found the objective that owns this marker
                            const char* objText = instancedObj.objective->displayText.c_str();
                            if (objText && objText[0] != '\0') {
                                markerName = objText;
                            }
                            foundMatch = true;
                            REX::INFO("ObjectScanner: Matched marker to objective '{}'", markerName);
                            break;
                        }
                    }
                }
                if (foundMatch) break;
            }

            // Fall back to reference name if no objective match found
            if (markerName.empty()) {
                const char* name = ref->GetDisplayFullName();
                markerName = (name && name[0] != '\0') ? name : "Quest Marker";
                REX::INFO("ObjectScanner: No objective match, using ref name '{}'", markerName);
            }

            ScannedObject obj;
            obj.ref = ref;
            obj.name = markerName;
            obj.isVirtual = false;
            obj.virtualPos = pos;

            m_scannedObjects.push_back(obj);
            REX::INFO("ObjectScanner: Added quest marker: '{}' at ({}, {}, {})",
                markerName, pos.x, pos.y, pos.z);
        }
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} quest markers", m_scannedObjects.size());
}

void ObjectScanner::ScanCompassMarkers()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    auto* pipboyData = RE::PipboyDataManager::GetSingleton();
    if (!pipboyData) {
        REX::WARN("ObjectScanner: No PipboyDataManager singleton");
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();
    m_lastScanPosition = playerPos;

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Check if player is in an interior cell - compass doesn't show exterior locations there
    auto* playerCell = player->GetParentCell();
    if (!playerCell) {
        REX::WARN("ObjectScanner: Player has no parent cell");
        return;
    }

    if (playerCell->IsInterior()) {
        REX::INFO("ObjectScanner: Player is in interior cell, no exterior locations on compass");
        SpeakText("No Locations nearby");
        return;
    }

    // Get the player's worldspace for filtering
    auto* playerWorldspace = playerCell->IsExterior() ? playerCell->worldSpace : nullptr;

    // Get compass range from game setting (default 3000 if not found)
    float compassRange = 3000.0f;
    auto* gameSettings = RE::GameSettingCollection::GetSingleton();
    if (gameSettings) {
        auto* setting = gameSettings->GetSetting("fHUDCompassLocationMaxDist");
        if (setting) {
            compassRange = setting->GetFloat();
            REX::INFO("ObjectScanner: Using compass range from game setting: {:.0f}", compassRange);
        }
    }

    // Temporary struct for sorting by distance
    struct TempMarker {
        RE::TESObjectREFR* ref;
        std::string name;
        float distance;
    };
    std::vector<TempMarker> tempMarkers;

    // Iterate through travelLocationsMarkers
    for (const auto& [markerData, pipboyObj] : pipboyData->mapData.travelLocationsMarkers) {
        if (!markerData) continue;

        // Check if location is discovered (flag 0x02 = Can Travel To)
        bool isDiscovered = (markerData->flags & 0x02) != 0;

        // Get the marker name - use "Unknown Location" if not discovered
        std::string name;
        if (isDiscovered) {
            const char* fullName = markerData->GetFullName();
            if (fullName && fullName[0] != '\0') {
                name = fullName;
            }
        }
        if (name.empty()) {
            name = "Unknown Location";
        }

        // Find the corresponding ref by matching marker data
        RE::TESObjectREFR* foundRef = nullptr;
        RE::NiPoint3 pos;

        for (const auto& [formId, refHandle] : pipboyData->mapData.travelLocationRefrHandles) {
            if (!refHandle) continue;
            auto refPtr = refHandle.get();
            if (!refPtr) continue;
            auto* ref = refPtr.get();
            if (!ref || ref->IsDeleted() || ref->IsDisabled()) continue;

            // Check if this ref's marker data matches
            auto* refMarkerData = ref->GetMapMarkerData();
            if (refMarkerData == markerData) {
                foundRef = ref;
                pos = ref->GetPosition();
                break;
            }
        }

        if (!foundRef) continue;

        // Check if location is in the same worldspace as the player
        auto* refCell = foundRef->GetParentCell();
        if (refCell && playerWorldspace) {
            auto* refWorldspace = refCell->IsExterior() ? refCell->worldSpace : nullptr;
            if (refWorldspace != playerWorldspace) {
                continue;  // Different worldspace, skip
            }
        }

        float dist = playerPos.GetDistance(pos);

        // Only include markers within compass range
        if (dist > compassRange) {
            continue;
        }

        tempMarkers.push_back({foundRef, name, dist});
        REX::INFO("ObjectScanner: Found location '{}' discovered={} dist={:.0f}",
            name, isDiscovered, dist);
    }

    // Sort by distance (closest first)
    std::sort(tempMarkers.begin(), tempMarkers.end(),
        [](const TempMarker& a, const TempMarker& b) {
            return a.distance < b.distance;
        });

    // Store in member vector
    for (const auto& marker : tempMarkers) {
        ScannedObject obj;
        obj.ref = marker.ref;
        obj.name = marker.name;
        obj.isVirtual = false;
        m_scannedObjects.push_back(obj);
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} compass locations within {} units",
        m_scannedObjects.size(), static_cast<int>(compassRange));
}

void ObjectScanner::ScanCompanions()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    auto* cell = player->GetParentCell();
    if (!cell) {
        REX::WARN("ObjectScanner: Player has no parent cell");
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();
    m_lastScanPosition = playerPos;

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Temporary struct for sorting by distance
    struct TempCompanion {
        RE::Actor* actor;
        std::string name;
        float distance;
    };
    std::vector<TempCompanion> tempCompanions;

    // Scan all actors in the cell and check if they're following the player
    for (auto& refPtr : cell->references) {
        auto* ref = refPtr.get();
        if (!ref || ref == player) continue;
        if (ref->IsDeleted() || ref->IsDisabled()) continue;

        // Check if this is an actor
        if (!ref->Is(RE::ENUM_FORM_ID::kACHR)) continue;

        auto* actor = ref->As<RE::Actor>();
        if (!actor) continue;
        if (actor->IsDead(false)) continue;  // Skip dead actors

        // Check if this actor is following the player (companion check)
        if (!actor->IsFollowing()) continue;

        const char* name = actor->GetDisplayFullName();
        if (!name || name[0] == '\0') continue;

        float dist = playerPos.GetDistance(actor->GetPosition());

        tempCompanions.push_back({actor, name, dist});
        REX::INFO("ObjectScanner: Found companion '{}' dist={:.0f}", name, dist);
    }

    // Sort by distance (closest first)
    std::sort(tempCompanions.begin(), tempCompanions.end(),
        [](const TempCompanion& a, const TempCompanion& b) {
            return a.distance < b.distance;
        });

    // Store in member vector
    for (const auto& companion : tempCompanions) {
        ScannedObject obj;
        obj.ref = companion.actor;
        obj.name = companion.name;
        obj.isVirtual = false;
        m_scannedObjects.push_back(obj);
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} companions", m_scannedObjects.size());
}

void ObjectScanner::ScanPowerArmor()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    RE::NiPoint3 playerPos = player->GetPosition();
    m_lastScanPosition = playerPos;

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Temporary struct for sorting by distance
    struct TempPowerArmor {
        RE::TESObjectREFR* ref;
        std::string name;
        float distance;
    };
    std::vector<TempPowerArmor> tempPowerArmor;

    // Check lastUsedPowerArmor from PlayerCharacter
    if (player->lastUsedPowerArmor) {
        auto refPtr = player->lastUsedPowerArmor.get();
        if (refPtr) {
            auto* ref = refPtr.get();
            if (ref && !ref->IsDeleted() && !ref->IsDisabled()) {
                const char* name = ref->GetDisplayFullName();
                std::string displayName = (name && name[0] != '\0') ? name : "Power Armor";
                float dist = playerPos.GetDistance(ref->GetPosition());

                tempPowerArmor.push_back({ref, displayName, dist});
                REX::INFO("ObjectScanner: Found last used power armor '{}' dist={:.0f}", displayName, dist);
            }
        }
    }

    // Scan cell for furniture with power armor keyword
    auto* cell = player->GetParentCell();
    if (cell) {
        auto* paKeyword = RE::PowerArmor::GetArmorKeyword();

        for (auto& refPtr : cell->references) {
            auto* ref = refPtr.get();
            if (!ref || ref == player) continue;
            if (ref->IsDeleted() || ref->IsDisabled()) continue;

            auto* baseObj = ref->GetObjectReference();
            if (!baseObj) continue;

            // Check if this is a furniture object (power armor frames are furniture)
            if (baseObj->GetFormType() != RE::ENUM_FORM_ID::kFURN) continue;

            // Cast to TESFurniture to access HasKeyword (via BGSKeywordForm inheritance)
            auto* furniture = static_cast<RE::TESFurniture*>(baseObj);

            // Check if it has the power armor keyword
            if (paKeyword && furniture->HasKeyword(paKeyword)) {
                // Check if already added (avoid duplicates with lastUsedPowerArmor)
                bool alreadyAdded = false;
                for (const auto& existing : tempPowerArmor) {
                    if (existing.ref == ref) {
                        alreadyAdded = true;
                        break;
                    }
                }

                if (!alreadyAdded) {
                    const char* name = ref->GetDisplayFullName();
                    std::string displayName = (name && name[0] != '\0') ? name : "Power Armor Frame";
                    float dist = playerPos.GetDistance(ref->GetPosition());

                    tempPowerArmor.push_back({ref, displayName, dist});
                    REX::INFO("ObjectScanner: Found power armor frame '{}' dist={:.0f}", displayName, dist);
                }
            }
        }
    }

    // Sort by distance (closest first)
    std::sort(tempPowerArmor.begin(), tempPowerArmor.end(),
        [](const TempPowerArmor& a, const TempPowerArmor& b) {
            return a.distance < b.distance;
        });

    // Store in member vector
    for (const auto& pa : tempPowerArmor) {
        ScannedObject obj;
        obj.ref = pa.ref;
        obj.name = pa.name;
        obj.isVirtual = false;
        m_scannedObjects.push_back(obj);
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} power armor frames", m_scannedObjects.size());
}

void ObjectScanner::ScanAndAnnounce()
{
    ScanObjects();

    if (m_scannedObjects.empty()) {
        SpeakNoObjectsNearby();
        return;
    }

    std::string announcement = std::to_string(m_scannedObjects.size()) + " ";
    announcement += GetCategoryName(m_currentCategory);
    announcement += ". ";

    // Announce first object with dynamic distance
    const auto& firstObj = m_scannedObjects[0];
    float dist = GetDistanceToScannedObject(firstObj);

    std::string objName = firstObj.name;

    // Add empty indicator for containers
    if (!firstObj.isVirtual && firstObj.ref) {
        auto* baseObj = firstObj.ref->GetObjectReference();
        if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
            if (IsContainerEmpty(firstObj.ref)) {
                objName += " (empty)";
            }
        }
    }

    // Add marker type for locations
    if (firstObj.markerType.has_value()) {
        objName += ", ";
        objName += GetMarkerTypeName(firstObj.markerType.value());
    }

    if (dist >= 0) {
        announcement += objName + ", " + std::to_string(static_cast<int>(dist)) + " units";
    } else {
        announcement += objName;
    }

    SpeakText(announcement);
}

void ObjectScanner::SelectNext()
{
    ScanIfNeeded();

    if (m_scannedObjects.empty()) {
        SpeakNoObjectsNearby();
        return;
    }

    m_currentIndex++;
    if (m_currentIndex >= static_cast<std::int32_t>(m_scannedObjects.size())) {
        m_currentIndex = 0;
    }

    AnnounceCurrentObject();
}

void ObjectScanner::SelectPrevious()
{
    ScanIfNeeded();

    if (m_scannedObjects.empty()) {
        SpeakNoObjectsNearby();
        return;
    }

    m_currentIndex--;
    if (m_currentIndex < 0) {
        m_currentIndex = static_cast<std::int32_t>(m_scannedObjects.size()) - 1;
    }

    AnnounceCurrentObject();
}

void ObjectScanner::AnnounceCurrentObject()
{
    if (m_scannedObjects.empty() || m_currentIndex < 0) {
        SpeakNoObjectsNearby();
        return;
    }

    if (m_currentIndex >= static_cast<std::int32_t>(m_scannedObjects.size())) {
        m_currentIndex = 0;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];

    // For physical objects, check if still valid
    if (!obj.isVirtual && (!obj.ref || obj.ref->IsDeleted() || obj.ref->IsDisabled())) {
        SpeakText("Object no longer available");
        return;
    }

    // Build announcement with distance
    float dist = GetDistanceToScannedObject(obj);
    std::string announcement = obj.name;

    // Add empty indicator for containers
    if (!obj.isVirtual && obj.ref) {
        auto* baseObj = obj.ref->GetObjectReference();
        if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
            if (IsContainerEmpty(obj.ref)) {
                announcement += " (empty)";
            }
        }
    }

    // Add marker type for locations
    if (obj.markerType.has_value()) {
        announcement += ", ";
        announcement += GetMarkerTypeName(obj.markerType.value());
    }

    if (dist >= 0) {
        announcement += ", " + std::to_string(static_cast<int>(dist)) + " units";
    }

    SpeakText(announcement);
}

void ObjectScanner::LookAtCurrentObject()
{
    if (m_scannedObjects.empty() || m_currentIndex < 0) {
        return;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Get player eye position (not feet position)
    RE::NiPoint3 eyePos, eyeDir;
    player->GetEyeVector(eyePos, eyeDir, true);

    RE::NiPoint3 targetCenter;

    if (obj.isVirtual) {
        // Virtual object - use stored position
        if (!IsValidPosition(obj.virtualPos)) {
            REX::INFO("ObjectScanner: Cannot look at {} - no position data", obj.name);
            return;
        }
        targetCenter = obj.virtualPos;
    } else {
        // Physical object - check validity and get center
        if (!obj.ref || obj.ref->IsDeleted() || obj.ref->IsDisabled()) {
            return;
        }
        obj.ref->GetObjectCenter(targetCenter);
    }

    // Calculate direction from eye to target
    RE::NiPoint3 direction = targetCenter - eyePos;

    float yaw = std::atan2(direction.x, direction.y);
    float horizontalDist = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    float pitch = -std::atan2(direction.z, horizontalDist);

    player->SetAngleOnReference(RE::NiPoint3(pitch, 0.0f, yaw));

    REX::INFO("ObjectScanner: Looking at {}", obj.name);
}

float ObjectScanner::GetDistanceToObject(RE::TESObjectREFR* a_ref)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !a_ref) return -1.0f;

    return player->GetPosition().GetDistance(a_ref->GetPosition());
}

float ObjectScanner::GetDistanceToPosition(const RE::NiPoint3& a_pos)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return -1.0f;

    if (!IsValidPosition(a_pos)) {
        return -1.0f;
    }

    return player->GetPosition().GetDistance(a_pos);
}

bool ObjectScanner::IsValidPosition(const RE::NiPoint3& a_pos)
{
    return a_pos.x != 0.0f || a_pos.y != 0.0f || a_pos.z != 0.0f;
}

float ObjectScanner::GetDistanceToScannedObject(const ScannedObject& a_obj)
{
    if (a_obj.isVirtual) {
        return GetDistanceToPosition(a_obj.virtualPos);
    } else {
        return GetDistanceToObject(a_obj.ref);
    }
}

void ObjectScanner::SpeakNoObjectsNearby()
{
    std::string msg = "No ";
    msg += GetCategoryName(m_currentCategory);
    msg += " nearby";
    SpeakText(msg);
}

void ObjectScanner::WalkToCurrentObject()
{
    if (m_scannedObjects.empty() || m_currentIndex < 0) {
        SpeakText("No object selected");
        return;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];

    // Handle virtual objects (quest markers without a direct reference)
    if (obj.isVirtual) {
        // Virtual quest markers can't be walked to without a reference
        // The alias scan should have found a reference if one exists
        if (IsValidPosition(obj.virtualPos)) {
            SpeakText("Cannot walk to quest marker: only position known, no reference available");
        } else {
            SpeakText("Cannot walk to quest marker: position unknown");
        }
        return;
    }

    // Handle physical objects
    if (!obj.ref || obj.ref->IsDeleted() || obj.ref->IsDisabled()) {
        SpeakText("Object no longer available");
        return;
    }

    SpeakText("Walking to " + obj.name);

    // Capture reference for arrival callback (avoids stale m_currentIndex)
    RE::TESObjectREFR* targetRef = obj.ref;
    AutoWalk::GetSingleton()->WalkTo(targetRef, 100.0f, [this, targetRef]() {
        // Look at the target on arrival
        if (targetRef && !targetRef->IsDeleted() && !targetRef->IsDisabled()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                RE::NiPoint3 eyePos, eyeDir;
                player->GetEyeVector(eyePos, eyeDir, true);
                RE::NiPoint3 targetCenter;
                targetRef->GetObjectCenter(targetCenter);
                RE::NiPoint3 direction = targetCenter - eyePos;
                float yaw = std::atan2(direction.x, direction.y);
                float horizontalDist = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                float pitch = -std::atan2(direction.z, horizontalDist);
                player->SetAngleOnReference(RE::NiPoint3(pitch, 0.0f, yaw));
            }
        }
        SpeakText("Arrived");
    });
}

void ObjectScanner::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
