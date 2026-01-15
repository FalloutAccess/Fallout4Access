#include "ObjectScanner.h"
#include "AutoWalk.h"
#include <SRAL.h>
#include <algorithm>
#include <cmath>
#include <REX/W32/USER32.h>

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
        case ScanCategory::Activators: return "Activators";
        case ScanCategory::QuestMarkers: return "Quest Markers";
        default: return "Unknown";
    }
}

bool ObjectScanner::MatchesCategory(RE::TESObjectREFR* a_ref, ScanCategory a_category)
{
    if (a_category == ScanCategory::All) return true;
    if (!a_ref) return false;

    // Quest markers are virtual - never match physical refs
    if (a_category == ScanCategory::QuestMarkers) return false;

    // Check for Actor (NPC)
    if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        return a_category == ScanCategory::NPCs;
    }

    // Get base object for other types
    auto* baseObj = a_ref->GetObjectReference();
    if (!baseObj) return false;

    auto formType = baseObj->GetFormType();

    switch (a_category) {
        case ScanCategory::Doors:
            return formType == RE::ENUM_FORM_ID::kDOOR;

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

    // Check for dead actors
    if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        if (auto* actor = a_ref->As<RE::Actor>()) {
            if (actor->IsDead(false)) return false;
        }
    }

    // Check if object is activatable (has activation prompt)
    // This filters out static objects like fence posts, rocks, etc.
    auto* baseObj = a_ref->GetObjectReference();
    if (baseObj) {
        RE::BSString activateText;
        if (!baseObj->GetActivateText(a_ref, activateText)) {
            return false;  // No activation = not interactable
        }
    }

    return true;
}

void ObjectScanner::ScanObjects()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        REX::WARN("ObjectScanner: No player singleton");
        return;
    }

    // Quest markers use a different scanning method
    if (m_currentCategory == ScanCategory::QuestMarkers) {
        ScanQuestMarkers();
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

    // Debug: Log playerMarkerPath status
    REX::INFO("ObjectScanner: playerMarkerPath is {}", player->playerMarkerPath ? "valid" : "null");
    if (player->playerMarkerPath) {
        auto& end = player->playerMarkerPath->end;
        REX::INFO("ObjectScanner: playerMarkerPath->end = ({}, {}, {})", end.x, end.y, end.z);
    }

    REX::INFO("ObjectScanner: Player has {} objectives", player->objectives.size());

    // Get displayed objectives and find their target references via quest aliases
    for (const auto& instancedObj : player->objectives) {
        if (!instancedObj.objective) continue;

        // Only include displayed (active) objectives
        if (instancedObj.enstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) {
            continue;
        }

        auto* quest = instancedObj.objective->ownerQuest;
        if (!quest) continue;

        // Get objective display text
        const char* text = instancedObj.objective->displayText.c_str();
        if (!text || text[0] == '\0') {
            text = "Quest Objective";
        }

        REX::INFO("ObjectScanner: Processing objective '{}' from quest '{}' with {} aliases",
            text, quest->GetFormEditorID() ? quest->GetFormEditorID() : "unknown",
            quest->aliases.size());

        ScannedObject obj;
        obj.ref = nullptr;
        obj.name = text;
        obj.isVirtual = true;
        obj.virtualPos = {0.0f, 0.0f, 0.0f};

        // Try to find a valid reference from quest aliases
        // First pass: look for aliases marked as QuestObject (actual targets)
        // Second pass: look for any valid non-Player alias
        for (int pass = 0; pass < 2 && obj.isVirtual; pass++) {
            for (const auto* alias : quest->aliases) {
                if (!alias) continue;

                // Always skip Player alias - it's never the target
                if (alias->aliasName == "Player") continue;

                // First pass: only check aliases with QuestObject flag
                if (pass == 0 && !alias->IsQuestObject()) continue;

                RE::ObjectRefHandle handle;
                quest->GetAliasedRef(&handle, alias->aliasID);

                bool handleValid = handle ? true : false;
                REX::DEBUG("ObjectScanner: Pass {}: Alias '{}' (ID {}) QuestObj:{} handle:{}",
                    pass, alias->aliasName.c_str(), alias->aliasID,
                    alias->IsQuestObject(), handleValid);

                if (handle) {
                    auto refPtr = handle.get();
                    if (refPtr && refPtr.get()) {
                        auto* ref = refPtr.get();

                        // Skip if it's the player
                        if (ref == RE::PlayerCharacter::GetSingleton()) continue;

                        const char* refName = ref->GetDisplayFullName();
                        REX::INFO("ObjectScanner: Alias '{}' points to ref '{}' (deleted:{}, disabled:{})",
                            alias->aliasName.c_str(),
                            refName ? refName : "null",
                            ref->IsDeleted(), ref->IsDisabled());

                        if (!ref->IsDeleted() && !ref->IsDisabled()) {
                            // Found a valid reference - store it directly
                            obj.ref = ref;
                            obj.isVirtual = false;
                            obj.virtualPos = ref->GetPosition();
                            REX::INFO("ObjectScanner: Using alias '{}' as quest target", alias->aliasName.c_str());
                            break;
                        }
                    }
                }
            }
        }

        // If no alias ref found, try playerMarkerPath as fallback (requires V.A.N.S.)
        if (obj.isVirtual && player->playerMarkerPath) {
            RE::NiPoint3 markerPos = player->playerMarkerPath->end;
            if (IsValidPosition(markerPos)) {
                obj.virtualPos = markerPos;
                REX::INFO("ObjectScanner: Using V.A.N.S. path for quest marker position");
            }
        }

        m_scannedObjects.push_back(obj);
        REX::INFO("ObjectScanner: Added quest objective: '{}' (has ref: {}, has valid pos: {})",
            text, obj.ref != nullptr, IsValidPosition(obj.virtualPos));
    }

    if (!m_scannedObjects.empty()) {
        m_currentIndex = 0;
    }

    REX::INFO("ObjectScanner: Found {} quest markers", m_scannedObjects.size());
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

    if (dist >= 0) {
        announcement += firstObj.name + ", " + std::to_string(static_cast<int>(dist)) + " units";
    } else {
        announcement += firstObj.name;
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
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), false);
        REX::INFO("ObjectScanner spoke: {}", a_text);
    }
}
