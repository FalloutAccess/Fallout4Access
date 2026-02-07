#include "ObjectScanner.h"
#include "AutoAimAccessibility.h"
#include "AutoWalk.h"
#include "ContextualInfoAnnouncer.h"
#include "CorpseLootTracker.h"
#include "SpeechManager.h"
#include <SRAL.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <REX/W32/USER32.h>
#include <RE/P/PipboyDataManager.h>
#include <RE/P/PowerArmor.h>
#include <RE/T/TES.h>
#include <RE/T/TESFurniture.h>
#include <RE/W/Workshop.h>
#include <RE/E/ExtraTeleport.h>
#include <RE/G/GridCellArray.h>

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

void ObjectScanner::SetWorkshopMode(bool a_active)
{
    if (a_active && !m_workshopModeActive) {
        m_preWorkshopCategory = m_currentCategory;
        m_currentCategory = ScanCategory::Workshop;
        m_currentSubcategory = ScanSubcategory::All;
        m_scannedObjects.clear();
        m_currentIndex = -1;
        m_workshopModeActive = true;
        REX::INFO("ObjectScanner: Workshop mode activated");
    } else if (!a_active && m_workshopModeActive) {
        m_currentCategory = m_preWorkshopCategory;
        m_currentSubcategory = ScanSubcategory::All;
        m_scannedObjects.clear();
        m_currentIndex = -1;
        m_workshopModeActive = false;
        REX::INFO("ObjectScanner: Workshop mode deactivated");
    }
}

bool ObjectScanner::ShouldHandleEvent(const RE::InputEvent* a_event)
{
    // Check AutoWalk arrival on every input event (frequent update)
    AutoWalk::GetSingleton()->Update();

    // Check for auto-aim on ironsights
    AutoAimAccessibility::GetSingleton()->Update();

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

    // Check modifier keys using REX::W32 (returns negative if key is down)
    bool shiftHeld = REX::W32::GetKeyState(REX::W32::VK_SHIFT) < 0;
    bool ctrlHeld = REX::W32::GetKeyState(REX::W32::VK_CONTROL) < 0;

    if (idCode == NUMPAD_5) {
        REX::INFO("ObjectScanner: Numpad 5 pressed, scanning...");
        ScanAndAnnounce();
    } else if (idCode == PAGE_DOWN) {
        if (shiftHeld) {
            REX::INFO("ObjectScanner: Shift+Page Down pressed, next category");
            NextCategory();
        } else {
            REX::INFO("ObjectScanner: Page Down pressed");
            SelectNext();
        }
    } else if (idCode == PAGE_UP) {
        if (shiftHeld) {
            REX::INFO("ObjectScanner: Shift+Page Up pressed, previous category");
            PreviousCategory();
        } else {
            REX::INFO("ObjectScanner: Page Up pressed");
            SelectPrevious();
        }
    } else if (idCode == HOME) {
        if (ctrlHeld && shiftHeld) {
            REX::INFO("ObjectScanner: Ctrl+Shift+Home pressed, debug info");
            DebugAnnounceCurrentObject();
        } else if (shiftHeld) {
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
        REX::INFO("ObjectScanner: End pressed, cycling subcategory");
        CycleSubcategory();
    } else if (idCode == H_KEY) {
        // Skip if workshop menu is open — it handles H key for settlement stats
        auto* ui = RE::UI::GetSingleton();
        if (ui && ui->GetMenu("WorkshopMenu"sv)) {
            return;
        }
        REX::INFO("ObjectScanner: H pressed, announcing combat stats");
        ContextualInfoAnnouncer::GetSingleton()->AnnounceCombatStats();
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

void ObjectScanner::NextCategory()
{
    ScanCategory startCategory = m_currentCategory;
    int categoryCount = static_cast<int>(ScanCategory::COUNT);

    // Try each category until we find one with objects (or cycle back to start)
    for (int i = 0; i < categoryCount; ++i) {
        int next = (static_cast<int>(m_currentCategory) + 1) % categoryCount;
        m_currentCategory = static_cast<ScanCategory>(next);
        m_currentSubcategory = ScanSubcategory::All;

        // Skip Workshop category when not in workshop mode
        if (m_currentCategory == ScanCategory::Workshop && !m_workshopModeActive) {
            continue;
        }

        // Clear and rescan
        m_scannedObjects.clear();
        m_currentIndex = -1;
        ScanObjects();

        // If we found objects or we're back to start, stop
        if (!m_scannedObjects.empty() || m_currentCategory == startCategory) {
            break;
        }
    }

    SpeakText(GetCategoryName(m_currentCategory));
}

void ObjectScanner::PreviousCategory()
{
    ScanCategory startCategory = m_currentCategory;
    int categoryCount = static_cast<int>(ScanCategory::COUNT);

    // Try each category until we find one with objects (or cycle back to start)
    for (int i = 0; i < categoryCount; ++i) {
        int prev = static_cast<int>(m_currentCategory) - 1;
        if (prev < 0) {
            prev = categoryCount - 1;
        }
        m_currentCategory = static_cast<ScanCategory>(prev);
        m_currentSubcategory = ScanSubcategory::All;

        // Skip Workshop category when not in workshop mode
        if (m_currentCategory == ScanCategory::Workshop && !m_workshopModeActive) {
            continue;
        }

        // Clear and rescan
        m_scannedObjects.clear();
        m_currentIndex = -1;
        ScanObjects();

        // If we found objects or we're back to start, stop
        if (!m_scannedObjects.empty() || m_currentCategory == startCategory) {
            break;
        }
    }

    SpeakText(GetCategoryName(m_currentCategory));
}

const char* ObjectScanner::GetCategoryName(ScanCategory a_category)
{
    switch (a_category) {
        case ScanCategory::All: return "All objects";
        case ScanCategory::NPCs: return "NPCs";
        case ScanCategory::Doors: return "Doors";
        case ScanCategory::Containers: return "Containers";
        case ScanCategory::Items: return "Items";
        case ScanCategory::Activators: return "Activators";
        case ScanCategory::QuestMarkers: return "Quest Markers";
        case ScanCategory::Corpses: return "Corpses";
        case ScanCategory::Locations: return "Locations";
        case ScanCategory::Companions: return "Companions";
        case ScanCategory::PowerArmor: return "Power Armor";
        case ScanCategory::Workshop: return "Workshop objects";
        default: return "Unknown";
    }
}

void ObjectScanner::CycleSubcategory()
{
    // Only cycle if current category has subcategories
    if (!HasSubcategories(m_currentCategory)) {
        SpeakText("No subcategories");
        return;
    }

    // Cycle through subcategories
    // TypeA/TypeB meaning depends on category (see GetSubcategoryName)
    int next = (static_cast<int>(m_currentSubcategory) + 1) % static_cast<int>(ScanSubcategory::COUNT);
    m_currentSubcategory = static_cast<ScanSubcategory>(next);

    // Clear cached objects to force rescan with new filter
    m_scannedObjects.clear();
    m_currentIndex = -1;

    SpeakText(GetSubcategoryName(m_currentSubcategory));
}

bool ObjectScanner::HasSubcategories(ScanCategory a_category)
{
    return a_category == ScanCategory::Activators ||
           a_category == ScanCategory::Containers ||
           a_category == ScanCategory::Doors ||
           a_category == ScanCategory::Corpses;
}

const char* ObjectScanner::GetSubcategoryName(ScanSubcategory a_subcategory)
{
    // Return context-aware names based on current category
    if (m_currentCategory == ScanCategory::Containers) {
        switch (a_subcategory) {
            case ScanSubcategory::All: return "All";
            case ScanSubcategory::TypeA: return "Non-empty";
            case ScanSubcategory::TypeB: return "Empty";
            default: return "Unknown";
        }
    } else if (m_currentCategory == ScanCategory::Doors) {
        switch (a_subcategory) {
            case ScanSubcategory::All: return "All";
            case ScanSubcategory::TypeA: return "Locked";
            case ScanSubcategory::TypeB: return "Cell doors";
            default: return "Unknown";
        }
    } else if (m_currentCategory == ScanCategory::Corpses) {
        switch (a_subcategory) {
            case ScanSubcategory::All: return "All";
            case ScanSubcategory::TypeA: return "Unlooted";
            case ScanSubcategory::TypeB: return "Looted";
            default: return "Unknown";
        }
    } else {
        // Activators (default)
        switch (a_subcategory) {
            case ScanSubcategory::All: return "All";
            case ScanSubcategory::TypeA: return "Furniture";
            case ScanSubcategory::TypeB: return "Other";
            default: return "Unknown";
        }
    }
}

bool ObjectScanner::IsSittableFurniture(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    auto* baseObj = a_ref->GetObjectReference();
    if (!baseObj) return false;

    // Must be furniture
    if (baseObj->GetFormType() != RE::ENUM_FORM_ID::kFURN) return false;

    auto* furniture = static_cast<RE::TESFurniture*>(baseObj);

    // Skip workbenches - they're not sittable furniture
    if (furniture->wbData.type != RE::WorkbenchData::Type::kNone) {
        return false;
    }

    // Animation type flags (from xEdit/Creation Kit documentation)
    constexpr std::uint32_t kAnimSit = 0x01;    // Bit 0: Sit animation
    constexpr std::uint32_t kAnimSleep = 0x02;  // Bit 1: Sleep/Lay animation

    // Check furniture markers for sit or sleep animations
    for (const auto& marker : furniture->markersArray) {
        if (marker.allowedAnimations & (kAnimSit | kAnimSleep)) {
            return true;
        }
    }

    // Also check entry point data for sit/sleep flags
    for (const auto& entry : furniture->entryPointDataArray) {
        if (entry.flags & (kAnimSit | kAnimSleep)) {
            return true;
        }
    }

    return false;
}

bool ObjectScanner::MatchesSubcategory(RE::TESObjectREFR* a_ref, ScanSubcategory a_subcategory)
{
    if (a_subcategory == ScanSubcategory::All) return true;
    if (!a_ref) return false;

    // Container subcategories: TypeA=NonEmpty, TypeB=Empty
    if (m_currentCategory == ScanCategory::Containers) {
        bool isEmpty = IsContainerEmpty(a_ref);
        switch (a_subcategory) {
            case ScanSubcategory::TypeA: return !isEmpty;  // Non-empty
            case ScanSubcategory::TypeB: return isEmpty;   // Empty
            default: return true;
        }
    }

    // Door subcategories: TypeA=Locked, TypeB=Cell doors
    if (m_currentCategory == ScanCategory::Doors) {
        switch (a_subcategory) {
            case ScanSubcategory::TypeA: return IsDoorLocked(a_ref);
            case ScanSubcategory::TypeB: return IsCellDoor(a_ref);
            default: return true;
        }
    }

    // Corpse subcategories: TypeA=Unlooted, TypeB=Looted
    if (m_currentCategory == ScanCategory::Corpses) {
        bool isLooted = CorpseLootTracker::GetSingleton()->IsLooted(a_ref->GetFormID());
        switch (a_subcategory) {
            case ScanSubcategory::TypeA: return !isLooted;  // Unlooted
            case ScanSubcategory::TypeB: return isLooted;   // Looted
            default: return true;
        }
    }

    // Activator subcategories: TypeA=Furniture, TypeB=Other
    if (m_currentCategory == ScanCategory::Activators) {
        bool isFurniture = IsSittableFurniture(a_ref);
        switch (a_subcategory) {
            case ScanSubcategory::TypeA: return isFurniture;
            case ScanSubcategory::TypeB: return !isFurniture;
            default: return true;
        }
    }

    return true;
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

        case ScanCategory::Items:
            // Takeable items - weapons, armor, ammo, aid, misc, books, keys, notes
            return formType == RE::ENUM_FORM_ID::kWEAP ||
                   formType == RE::ENUM_FORM_ID::kARMO ||
                   formType == RE::ENUM_FORM_ID::kAMMO ||
                   formType == RE::ENUM_FORM_ID::kALCH ||
                   formType == RE::ENUM_FORM_ID::kMISC ||
                   formType == RE::ENUM_FORM_ID::kBOOK ||
                   formType == RE::ENUM_FORM_ID::kKEYM ||
                   formType == RE::ENUM_FORM_ID::kNOTE ||
                   formType == RE::ENUM_FORM_ID::kINGR;

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
    // All actors are interactable - looted/unlooted filtering is handled
    // by MatchesSubcategory() via CorpseLootTracker
    if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        return true;
    }

    auto* baseObj = a_ref->GetObjectReference();
    if (!baseObj) return false;

    // Doors are always interactable (skip activation check)
    // This ensures load doors are included
    if (baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
        return true;
    }

    // Skip objects the game marks as non-activatable by the player
    // (e.g. invisible sound emitters, mod-internal markers)
    if (auto* keywordForm = baseObj->As<RE::BGSKeywordForm>()) {
        static RE::BGSKeyword* blockActivationKW = nullptr;
        static bool kwLookedUp = false;
        if (!kwLookedUp) {
            blockActivationKW = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("BlockPlayerActivation");
            kwLookedUp = true;
        }
        if (blockActivationKW && keywordForm->HasKeyword(blockActivationKW)) {
            return false;
        }
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

    // Check runtime inventory list first (reflects actual state after looting)
    RE::BGSInventoryList* inventoryList = a_ref->inventoryList;
    if (inventoryList) {
        if (inventoryList->data.empty()) return true;

        std::uint32_t totalItems = 0;
        for (const auto& item : inventoryList->data) {
            totalItems += item.GetCount();
        }
        return totalItems == 0;
    }

    // Runtime inventory not yet created - fall back to base form's TESContainer.
    // Actors (TESNPC -> TESActorBase -> TESContainer) define default items on
    // their base form. The runtime inventoryList is lazily created, so corpses
    // killed off-screen or never accessed may not have one yet.
    auto* baseObj = a_ref->GetObjectReference();
    if (baseObj) {
        if (auto* actorBase = baseObj->As<RE::TESActorBase>()) {
            return actorBase->numContainerObjects == 0;
        }
    }

    return true;
}

bool ObjectScanner::IsDoorLocked(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    RE::REFR_LOCK* lock = a_ref->GetLock();
    if (!lock) return false;

    // Bit 0 of flags indicates the lock is currently engaged
    // GetLockLevel() returns difficulty, not locked state
    // When you pick a lock, SetLocked(false) clears this flag
    constexpr std::uint8_t kLockedFlag = 0x01;
    return (lock->flags & kLockedFlag) != 0;
}

bool ObjectScanner::IsDoorOpen(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    RE::BGSOpenCloseForm::OPEN_STATE state = RE::BGSOpenCloseForm::GetOpenState(a_ref);
    return state == RE::BGSOpenCloseForm::OPEN_STATE::kOpen ||
           state == RE::BGSOpenCloseForm::OPEN_STATE::kOpening;
}

bool ObjectScanner::IsCellDoor(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    auto* extraList = a_ref->extraList.get();
    if (!extraList) return false;

    auto* extraTeleport = extraList->GetByType<RE::ExtraTeleport>();
    return extraTeleport && extraTeleport->teleportData;
}

std::string ObjectScanner::GetDoorDestinationName(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return {};

    auto* extraList = a_ref->extraList.get();
    if (!extraList) return {};

    auto* extraTeleport = extraList->GetByType<RE::ExtraTeleport>();
    if (!extraTeleport || !extraTeleport->teleportData) return {};

    auto* teleportData = extraTeleport->teleportData;

    // Try transition cell name first
    if (auto* destCell = teleportData->transitionCell) {
        const char* cellName = destCell->GetFullName();
        if (cellName && cellName[0] != '\0') {
            return cellName;
        }
    }

    // Fallback: resolve linked door and get its parent cell name
    if (teleportData->linkedDoor) {
        auto destDoorPtr = teleportData->linkedDoor.get();
        if (destDoorPtr) {
            auto* destDoor = destDoorPtr.get();
            if (destDoor) {
                auto* parentCell = destDoor->GetParentCell();
                if (parentCell) {
                    const char* cellName = parentCell->GetFullName();
                    if (cellName && cellName[0] != '\0') {
                        return cellName;
                    }
                }
            }
        }
    }

    return {};
}

void ObjectScanner::ScanObjects()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    // These categories use different scanning methods
    if (m_currentCategory == ScanCategory::Workshop) {
        ScanWorkshopObjects();
        return;
    }
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
        float zDiff;  // Vertical difference from player (positive = above)
    };
    std::vector<TempObject> tempObjects;
    float playerZ = playerPos.z;

    // Lambda to scan a single cell's references
    auto scanCell = [&](RE::TESObjectCELL* cell) {
        if (!cell) return;

        // Only scan fully attached cells
        if (cell->cellState == RE::TESObjectCELL::CELL_STATE::kAttached) {
            for (auto& refPtr : cell->references) {
                auto* ref = refPtr.get();
                if (!ref) continue;
                if (ref == player) continue;

                // Filter by interactability, category, and subcategory
                if (!IsInteractable(ref)) continue;
                if (!MatchesCategory(ref, m_currentCategory)) continue;
                if (!MatchesSubcategory(ref, m_currentSubcategory)) continue;

                RE::NiPoint3 objPos = ref->GetPosition();
                float distSq = playerPos.GetSquaredDistance(objPos);
                float zDiff = objPos.z - playerZ;

                const char* name = ref->GetDisplayFullName();
                if (!name || name[0] == '\0') continue;

                // For cell doors, append destination name
                std::string objName = name;
                auto* baseObj = ref->GetObjectReference();
                if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
                    std::string destName = GetDoorDestinationName(ref);
                    if (!destName.empty()) {
                        objName += ", to " + destName;
                    }
                }

                tempObjects.push_back({ref, objName, std::sqrt(distSq), zDiff});
            }
        }
    };

    // Get TES singleton for cell grid access
    auto* tes = RE::TES::GetSingleton();
    if (!tes) {
        REX::WARN("ObjectScanner: No TES singleton");
        return;
    }

    // Check if player is in an interior
    if (tes->interiorCell) {
        // Interior: scan single cell
        scanCell(tes->interiorCell);
        REX::INFO("ObjectScanner: Scanned interior cell");
    } else {
        // Exterior: scan all loaded grid cells
        auto* gridCells = tes->gridCells;
        if (gridCells) {
            std::uint32_t dimension = gridCells->dimension;
            std::uint32_t cellsScanned = 0;

            for (std::uint32_t x = 0; x < dimension; ++x) {
                for (std::uint32_t y = 0; y < dimension; ++y) {
                    auto* gridCell = gridCells->Get(x, y);
                    if (gridCell && gridCell->cell) {
                        scanCell(gridCell->cell);
                        cellsScanned++;
                    }
                }
            }
            REX::INFO("ObjectScanner: Scanned {} exterior cells ({}x{} grid)",
                cellsScanned, dimension, dimension);
        } else {
            // Fallback to parent cell if grid not available
            auto* cell = player->GetParentCell();
            if (cell) {
                scanCell(cell);
                REX::INFO("ObjectScanner: Fallback to parent cell scan");
            } else {
                REX::WARN("ObjectScanner: No cells available to scan");
                return;
            }
        }
    }

    // Sort by: same-level objects first, then by distance
    std::sort(tempObjects.begin(), tempObjects.end(),
        [](const TempObject& a, const TempObject& b) {
            bool aSameLevel = std::abs(a.zDiff) <= ELEVATION_THRESHOLD;
            bool bSameLevel = std::abs(b.zDiff) <= ELEVATION_THRESHOLD;
            // Prefer same-level objects
            if (aSameLevel != bSameLevel) {
                return aSameLevel;  // Same-level objects come first
            }
            // Within same elevation group, sort by distance
            return a.distance < b.distance;
        });

    // Store in member vector
    for (const auto& obj : tempObjects) {
        m_scannedObjects.push_back({obj.ref, obj.name});
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} {}",
        m_scannedObjects.size(), GetCategoryName(m_currentCategory));
}

void ObjectScanner::ScanWorkshopObjects()
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

    // Find the active workshop
    auto* workshop = RE::Workshop::FindNearestValidWorkshop(*player);
    if (!workshop) {
        REX::WARN("ObjectScanner: No valid workshop found nearby");
        return;
    }

    struct TempObject {
        RE::TESObjectREFR* ref;
        std::string name;
        float distance;
        float zDiff;
    };
    std::vector<TempObject> tempObjects;
    float playerZ = playerPos.z;

    auto scanCell = [&](RE::TESObjectCELL* cell) {
        if (!cell) return;
        if (cell->cellState != RE::TESObjectCELL::CELL_STATE::kAttached) return;

        for (auto& refPtr : cell->references) {
            auto* ref = refPtr.get();
            if (!ref) continue;
            if (ref == player) continue;
            if (ref == workshop) continue;
            if (ref->IsDeleted() || ref->IsDisabled()) continue;

            // Skip NPCs/actors
            if (ref->Is(RE::ENUM_FORM_ID::kACHR)) continue;

            RE::NiPoint3 objPos = ref->GetPosition();

            // Must be within workshop buildable area
            if (!RE::Workshop::IsLocationWithinBuildableArea(*workshop, objPos)) continue;

            const char* name = ref->GetDisplayFullName();
            if (!name || name[0] == '\0') continue;

            float distSq = playerPos.GetSquaredDistance(objPos);
            float zDiff = objPos.z - playerZ;

            tempObjects.push_back({ref, name, std::sqrt(distSq), zDiff});
        }
    };

    auto* tes = RE::TES::GetSingleton();
    if (!tes) return;

    if (tes->interiorCell) {
        scanCell(tes->interiorCell);
    } else {
        auto* gridCells = tes->gridCells;
        if (gridCells) {
            std::uint32_t dimension = gridCells->dimension;
            for (std::uint32_t x = 0; x < dimension; ++x) {
                for (std::uint32_t y = 0; y < dimension; ++y) {
                    auto* gridCell = gridCells->Get(x, y);
                    if (gridCell && gridCell->cell) {
                        scanCell(gridCell->cell);
                    }
                }
            }
        } else {
            auto* cell = player->GetParentCell();
            if (cell) {
                scanCell(cell);
            }
        }
    }

    // Sort by same-level first, then distance
    std::sort(tempObjects.begin(), tempObjects.end(),
        [](const TempObject& a, const TempObject& b) {
            bool aSameLevel = std::abs(a.zDiff) <= ELEVATION_THRESHOLD;
            bool bSameLevel = std::abs(b.zDiff) <= ELEVATION_THRESHOLD;
            if (aSameLevel != bSameLevel) {
                return aSameLevel;
            }
            return a.distance < b.distance;
        });

    for (const auto& obj : tempObjects) {
        m_scannedObjects.push_back({obj.ref, obj.name});
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} workshop objects", m_scannedObjects.size());
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

    // Log player position and cell info for debugging
    auto* playerCell = player->GetParentCell();
    REX::INFO("ObjectScanner: Player position: ({:.0f}, {:.0f}, {:.0f})", playerPos.x, playerPos.y, playerPos.z);
    if (playerCell) {
        REX::INFO("ObjectScanner: Player cell: {} (interior={})",
            playerCell->GetFullName() ? playerCell->GetFullName() : "unnamed",
            playerCell->IsInterior());
        if (!playerCell->IsInterior() && playerCell->worldSpace) {
            REX::INFO("ObjectScanner: Player worldspace: {}",
                playerCell->worldSpace->GetFullName() ? playerCell->worldSpace->GetFullName() : "unnamed");
        }
    }

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Track already-added references to avoid duplicates
    std::set<RE::TESObjectREFR*> addedRefs;

    // Get PipboyDataManager for the authoritative quest marker data
    auto* pipboyData = RE::PipboyDataManager::GetSingleton();
    if (!pipboyData) {
        REX::WARN("ObjectScanner: No PipboyDataManager singleton");
        return;
    }

    // 1. Scan local quest markers (objectives in current interior)
    auto& localMarkers = pipboyData->mapData.localQuestMarkers;
    REX::INFO("ObjectScanner: localQuestMarkers has {} entries", localMarkers.size());

    for (const auto& [refHandle, pipboyObj] : localMarkers) {
        if (!refHandle) continue;

        auto refPtr = refHandle.get();
        if (!refPtr || !refPtr.get()) continue;

        auto* ref = refPtr.get();
        if (ref->IsDeleted() || ref->IsDisabled()) continue;

        RE::NiPoint3 pos = ref->GetPosition();
        if (!IsValidPosition(pos)) continue;

        // Use objective-matching logic
        std::string markerName = FindObjectiveName(ref, player);
        if (markerName.empty()) {
            const char* name = ref->GetDisplayFullName();
            markerName = (name && name[0] != '\0') ? name : "Quest Marker";
        }

        ScannedObject obj;
        obj.ref = ref;
        obj.name = markerName;
        obj.isVirtual = false;
        obj.virtualPos = pos;

        m_scannedObjects.push_back(obj);
        addedRefs.insert(ref);
        REX::INFO("ObjectScanner: Added local quest marker: '{}' at ({:.0f}, {:.0f}, {:.0f})",
            markerName, pos.x, pos.y, pos.z);
    }

    // 2. Scan world quest markers (objectives in world/exterior)
    auto& worldMarkers = pipboyData->mapData.worldQuestMarkers;
    REX::INFO("ObjectScanner: worldQuestMarkers has {} entries", worldMarkers.size());

    for (const auto& [refHandle, pipboyObj] : worldMarkers) {
        if (!refHandle) continue;

        auto refPtr = refHandle.get();
        if (!refPtr || !refPtr.get()) continue;

        auto* ref = refPtr.get();
        if (ref->IsDeleted() || ref->IsDisabled()) continue;

        // Skip if already added
        if (addedRefs.contains(ref)) continue;

        // Get position from the reference
        RE::NiPoint3 pos = ref->GetPosition();
        if (!IsValidPosition(pos)) continue;

        // Note: We intentionally do NOT filter by cell or base object here.
        // Quest markers often point to distant/unloaded areas where:
        // - refCell will be NULL (cell not loaded)
        // - baseObj may be NULL
        // The position data is still valid and we want to show these markers.

        // Use objective-matching logic
        std::string markerName = FindObjectiveName(ref, player);
        if (markerName.empty()) {
            const char* name = ref->GetDisplayFullName();
            markerName = (name && name[0] != '\0') ? name : "Quest Marker";
        }

        ScannedObject obj;
        obj.ref = ref;
        obj.name = markerName;
        obj.isVirtual = false;
        obj.virtualPos = pos;

        m_scannedObjects.push_back(obj);
        addedRefs.insert(ref);
        REX::INFO("ObjectScanner: Added world quest marker: '{}' at ({:.0f}, {:.0f}, {:.0f})",
            markerName, pos.x, pos.y, pos.z);
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} quest markers (local={}, world={})",
        m_scannedObjects.size(),
        localMarkers.size(),
        worldMarkers.size());
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
        float zDiff;
    };
    std::vector<TempMarker> tempMarkers;
    float playerZ = playerPos.z;

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

        float zDiff = pos.z - playerZ;
        tempMarkers.push_back({foundRef, name, dist, zDiff});
        REX::INFO("ObjectScanner: Found location '{}' discovered={} dist={:.0f}",
            name, isDiscovered, dist);
    }

    // Sort by: same-level locations first, then by distance
    std::sort(tempMarkers.begin(), tempMarkers.end(),
        [](const TempMarker& a, const TempMarker& b) {
            bool aSameLevel = std::abs(a.zDiff) <= ELEVATION_THRESHOLD;
            bool bSameLevel = std::abs(b.zDiff) <= ELEVATION_THRESHOLD;
            if (aSameLevel != bSameLevel) {
                return aSameLevel;
            }
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

    RE::NiPoint3 playerPos = player->GetPosition();
    m_lastScanPosition = playerPos;

    m_scannedObjects.clear();
    m_currentIndex = -1;

    // Temporary struct for sorting by distance
    struct TempCompanion {
        RE::Actor* actor;
        std::string name;
        float distance;
        float zDiff;
    };
    std::vector<TempCompanion> tempCompanions;
    float playerZ = playerPos.z;

    // Lambda to scan a cell for companions
    auto scanCellForCompanions = [&](RE::TESObjectCELL* cell) {
        if (!cell) return;
        if (cell->cellState != RE::TESObjectCELL::CELL_STATE::kAttached) return;

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

            RE::NiPoint3 actorPos = actor->GetPosition();
            float dist = playerPos.GetDistance(actorPos);
            float zDiff = actorPos.z - playerZ;

            tempCompanions.push_back({actor, name, dist, zDiff});
            REX::INFO("ObjectScanner: Found companion '{}' dist={:.0f}", name, dist);
        }
    };

    // Get TES singleton for cell grid access
    auto* tes = RE::TES::GetSingleton();
    if (!tes) {
        REX::WARN("ObjectScanner: No TES singleton");
        return;
    }

    // Scan appropriate cells
    if (tes->interiorCell) {
        scanCellForCompanions(tes->interiorCell);
    } else if (tes->gridCells) {
        std::uint32_t dimension = tes->gridCells->dimension;
        for (std::uint32_t x = 0; x < dimension; ++x) {
            for (std::uint32_t y = 0; y < dimension; ++y) {
                auto* gridCell = tes->gridCells->Get(x, y);
                if (gridCell && gridCell->cell) {
                    scanCellForCompanions(gridCell->cell);
                }
            }
        }
    }

    // Sort by: same-level companions first, then by distance
    std::sort(tempCompanions.begin(), tempCompanions.end(),
        [](const TempCompanion& a, const TempCompanion& b) {
            bool aSameLevel = std::abs(a.zDiff) <= ELEVATION_THRESHOLD;
            bool bSameLevel = std::abs(b.zDiff) <= ELEVATION_THRESHOLD;
            if (aSameLevel != bSameLevel) {
                return aSameLevel;
            }
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
        float zDiff;
    };
    std::vector<TempPowerArmor> tempPowerArmor;
    float playerZ = playerPos.z;

    // Check lastUsedPowerArmor from PlayerCharacter
    if (player->lastUsedPowerArmor) {
        auto refPtr = player->lastUsedPowerArmor.get();
        if (refPtr) {
            auto* ref = refPtr.get();
            if (ref && !ref->IsDeleted() && !ref->IsDisabled()) {
                const char* name = ref->GetDisplayFullName();
                std::string displayName = (name && name[0] != '\0') ? name : "Power Armor";
                RE::NiPoint3 paPos = ref->GetPosition();
                float dist = playerPos.GetDistance(paPos);
                float zDiff = paPos.z - playerZ;

                tempPowerArmor.push_back({ref, displayName, dist, zDiff});
                REX::INFO("ObjectScanner: Found last used power armor '{}' dist={:.0f}", displayName, dist);
            }
        }
    }

    // Lambda to scan a cell for power armor
    auto* paKeyword = RE::PowerArmor::GetArmorKeyword();
    auto scanCellForPowerArmor = [&](RE::TESObjectCELL* cell) {
        if (!cell) return;
        if (cell->cellState != RE::TESObjectCELL::CELL_STATE::kAttached) return;

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
                    RE::NiPoint3 paPos = ref->GetPosition();
                    float dist = playerPos.GetDistance(paPos);
                    float zDiff = paPos.z - playerZ;

                    tempPowerArmor.push_back({ref, displayName, dist, zDiff});
                    REX::INFO("ObjectScanner: Found power armor frame '{}' dist={:.0f}", displayName, dist);
                }
            }
        }
    };

    // Get TES singleton for cell grid access
    auto* tes = RE::TES::GetSingleton();
    if (tes) {
        if (tes->interiorCell) {
            scanCellForPowerArmor(tes->interiorCell);
        } else if (tes->gridCells) {
            std::uint32_t dimension = tes->gridCells->dimension;
            for (std::uint32_t x = 0; x < dimension; ++x) {
                for (std::uint32_t y = 0; y < dimension; ++y) {
                    auto* gridCell = tes->gridCells->Get(x, y);
                    if (gridCell && gridCell->cell) {
                        scanCellForPowerArmor(gridCell->cell);
                    }
                }
            }
        }
    }

    // Sort by: same-level power armor first, then by distance
    std::sort(tempPowerArmor.begin(), tempPowerArmor.end(),
        [](const TempPowerArmor& a, const TempPowerArmor& b) {
            bool aSameLevel = std::abs(a.zDiff) <= ELEVATION_THRESHOLD;
            bool bSameLevel = std::abs(b.zDiff) <= ELEVATION_THRESHOLD;
            if (aSameLevel != bSameLevel) {
                return aSameLevel;
            }
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

    // Add status suffixes based on object type
    if (!firstObj.isVirtual && firstObj.ref) {
        auto* baseObj = firstObj.ref->GetObjectReference();
        if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
            if (IsContainerEmpty(firstObj.ref)) {
                objName += " (empty)";
            }
        }
        // Add status for doors (locked and/or closed, but skip "closed" for cell doors)
        else if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
            bool isLocked = IsDoorLocked(firstObj.ref);
            bool isOpen = IsDoorOpen(firstObj.ref);
            bool isCellDoor = IsCellDoor(firstObj.ref);
            std::string status;
            if (isLocked) status = "locked";
            if (!isOpen && !isCellDoor) {
                if (!status.empty()) status += ", ";
                status += "closed";
            }
            if (!status.empty()) {
                objName += " (" + status + ")";
            }
        }
        // Add looted indicator for corpses
        else if (firstObj.ref->Is(RE::ENUM_FORM_ID::kACHR)) {
            if (CorpseLootTracker::GetSingleton()->IsLooted(firstObj.ref->GetFormID())) {
                objName += " (looted)";
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
        std::string elevation = GetElevationIndicator(firstObj);
        if (!elevation.empty()) {
            announcement += ", " + elevation;
        }
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

    // Add status suffixes based on object type
    if (!obj.isVirtual && obj.ref) {
        auto* baseObj = obj.ref->GetObjectReference();
        if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
            if (IsContainerEmpty(obj.ref)) {
                announcement += " (empty)";
            }
        }
        // Add status for doors (locked and/or closed, but skip "closed" for cell doors)
        else if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
            bool isLocked = IsDoorLocked(obj.ref);
            bool isOpen = IsDoorOpen(obj.ref);
            bool isCellDoor = IsCellDoor(obj.ref);
            std::string status;
            if (isLocked) status = "locked";
            if (!isOpen && !isCellDoor) {
                if (!status.empty()) status += ", ";
                status += "closed";
            }
            if (!status.empty()) {
                announcement += " (" + status + ")";
            }
        }
        // Add looted indicator for corpses
        else if (obj.ref->Is(RE::ENUM_FORM_ID::kACHR)) {
            if (CorpseLootTracker::GetSingleton()->IsLooted(obj.ref->GetFormID())) {
                announcement += " (looted)";
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
        std::string elevation = GetElevationIndicator(obj);
        if (!elevation.empty()) {
            announcement += ", " + elevation;
        }
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
        // Check base object exists (quest markers may have null base objects)
        if (!obj.ref->GetObjectReference()) {
            REX::WARN("ObjectScanner: Object '{}' has null base object, using position fallback", obj.name);
            targetCenter = obj.ref->GetPosition();
        } else {
            obj.ref->GetObjectCenter(targetCenter);
        }
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

std::string ObjectScanner::FindObjectiveName(RE::TESObjectREFR* a_ref, RE::PlayerCharacter* a_player)
{
    if (!a_ref || !a_player) return "";

    // Try to find which objective this marker belongs to
    // by checking if any quest's aliases point to this reference
    for (const auto& instancedObj : a_player->objectives) {
        if (!instancedObj.objective) continue;
        if (instancedObj.enstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) continue;

        auto* quest = instancedObj.objective->ownerQuest;
        if (!quest) continue;

        // Check if any alias in this quest points to our marker ref
        for (const auto* alias : quest->aliases) {
            if (!alias) continue;

            RE::ObjectRefHandle aliasHandle;
            quest->GetAliasedRef(&aliasHandle, alias->aliasID);

            if (aliasHandle) {
                auto aliasRefPtr = aliasHandle.get();
                if (aliasRefPtr && aliasRefPtr.get() == a_ref) {
                    // Found the objective that owns this marker
                    const char* objText = instancedObj.objective->displayText.c_str();
                    if (objText && objText[0] != '\0') {
                        return objText;
                    }
                }
            }
        }
    }

    return "";
}

float ObjectScanner::GetDistanceToScannedObject(const ScannedObject& a_obj)
{
    if (a_obj.isVirtual) {
        return GetDistanceToPosition(a_obj.virtualPos);
    } else {
        return GetDistanceToObject(a_obj.ref);
    }
}

std::string ObjectScanner::GetElevationIndicator(const ScannedObject& a_obj)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return "";

    float playerZ = player->GetPosition().z;
    float objectZ = 0.0f;

    if (a_obj.isVirtual) {
        if (!IsValidPosition(a_obj.virtualPos)) {
            return "";
        }
        objectZ = a_obj.virtualPos.z;
    } else {
        if (!a_obj.ref || a_obj.ref->IsDeleted() || a_obj.ref->IsDisabled()) {
            return "";
        }
        objectZ = a_obj.ref->GetPosition().z;
    }

    float zDiff = objectZ - playerZ;

    if (zDiff > ELEVATION_THRESHOLD) {
        return "above";
    } else if (zDiff < -ELEVATION_THRESHOLD) {
        return "below";
    }

    return "";
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

    // Check base object exists (quest markers may have null base objects)
    if (!obj.ref->GetObjectReference()) {
        REX::WARN("ObjectScanner: Object '{}' has null base object", obj.name);
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
                // Check base object exists before calling GetObjectCenter
                if (targetRef->GetObjectReference()) {
                    targetRef->GetObjectCenter(targetCenter);
                } else {
                    targetCenter = targetRef->GetPosition();
                }
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

void ObjectScanner::DebugAnnounceCurrentObject()
{
    if (m_scannedObjects.empty() || m_currentIndex < 0) {
        SpeakText("No object selected");
        return;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];

    if (obj.isVirtual) {
        SpeakText("Virtual marker, no reference to inspect");
        REX::INFO("=== DEBUG OBJECT INFO ===");
        REX::INFO("  Virtual quest marker: '{}'", obj.name);
        REX::INFO("  Position: ({:.0f}, {:.0f}, {:.0f})", obj.virtualPos.x, obj.virtualPos.y, obj.virtualPos.z);
        REX::INFO("=========================");
        return;
    }

    if (!obj.ref) {
        SpeakText("Null reference");
        return;
    }

    auto* ref = obj.ref;

    // Gather all debug info
    std::uint32_t refFormID = ref->GetFormID();
    RE::NiPoint3 pos = ref->GetPosition();
    float dist = GetDistanceToObject(ref);

    // Base object info
    auto* baseObj = ref->GetObjectReference();
    std::uint32_t baseFormID = baseObj ? baseObj->GetFormID() : 0;
    const char* baseEditorID = baseObj ? baseObj->GetFormEditorID() : nullptr;
    RE::ENUM_FORM_ID baseFormType = baseObj ? baseObj->GetFormType() : RE::ENUM_FORM_ID::kNONE;

    // Display name
    const char* displayName = ref->GetDisplayFullName();

    // Activation text
    RE::BSString activateText;
    bool hasActivation = baseObj ? baseObj->GetActivateText(ref, activateText) : false;

    // 3D loaded state
    bool is3DLoaded = ref->Get3D() != nullptr;

    // Flags
    bool isDeleted = ref->IsDeleted();
    bool isDisabled = ref->IsDisabled();

    // Cell info
    auto* cell = ref->GetParentCell();
    const char* cellName = cell ? cell->GetFullName() : nullptr;
    bool cellIsInterior = cell ? cell->IsInterior() : false;

    // Log everything in detail
    REX::INFO("=== DEBUG OBJECT INFO ===");
    REX::INFO("  Display Name: '{}'", displayName ? displayName : "(null)");
    REX::INFO("  Ref FormID: {:08X}", refFormID);
    REX::INFO("  Base FormID: {:08X}", baseFormID);
    REX::INFO("  Base EditorID: '{}'", baseEditorID ? baseEditorID : "(null)");
    REX::INFO("  Base Form Type: {}", static_cast<int>(baseFormType));
    REX::INFO("  Position: ({:.0f}, {:.0f}, {:.0f})", pos.x, pos.y, pos.z);
    REX::INFO("  Distance: {:.0f}", dist);
    REX::INFO("  3D Loaded: {}", is3DLoaded);
    REX::INFO("  Deleted: {}, Disabled: {}", isDeleted, isDisabled);
    REX::INFO("  Cell: '{}' (interior={})", cellName ? cellName : "(null)", cellIsInterior);
    REX::INFO("  Has Activation: {} (text='{}')", hasActivation,
        hasActivation ? activateText.c_str() : "");

    // Keywords from base object
    if (baseObj) {
        if (auto* keywordForm = baseObj->As<RE::BGSKeywordForm>()) {
            if (keywordForm->numKeywords > 0 && keywordForm->keywords) {
                std::string kwList;
                for (std::uint32_t i = 0; i < keywordForm->numKeywords; ++i) {
                    auto* kw = keywordForm->keywords[i];
                    if (kw) {
                        if (!kwList.empty()) kwList += ", ";
                        const char* kwEdID = kw->GetFormEditorID();
                        kwList += kwEdID ? kwEdID : std::format("{:08X}", kw->GetFormID());
                    }
                }
                REX::INFO("  Keywords: [{}]", kwList);
            } else {
                REX::INFO("  Keywords: (none)");
            }
        }
    }

    REX::INFO("=========================");

    // Speak a concise summary via screen reader
    std::string summary = "Debug: ";
    summary += displayName ? displayName : "(no name)";
    summary += ", type " + std::to_string(static_cast<int>(baseFormType));
    summary += ", ref " + std::format("{:08X}", refFormID);
    summary += ", base " + std::format("{:08X}", baseFormID);
    if (baseEditorID && baseEditorID[0] != '\0') {
        summary += ", editor ID " + std::string(baseEditorID);
    }
    if (!is3DLoaded) {
        summary += ", no 3D";
    }
    if (hasActivation) {
        summary += ", activate: " + std::string(activateText.c_str());
    } else {
        summary += ", no activation";
    }

    SpeakText(summary);
}

void ObjectScanner::SpeakText(const std::string& a_text)
{
    SpeechManager::GetSingleton()->Speak(a_text);
}
