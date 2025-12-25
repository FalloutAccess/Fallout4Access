#include "ObjectScanner.h"
#include <SRAL.h>
#include <algorithm>
#include <cmath>

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
        REX::INFO("ObjectScanner: Home pressed");
        LookAtCurrentObject();
        AnnounceCurrentObject();
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
        default: return "Unknown";
    }
}

bool ObjectScanner::MatchesCategory(RE::TESObjectREFR* a_ref, ScanCategory a_category)
{
    if (a_category == ScanCategory::All) return true;
    if (!a_ref) return false;

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

    return true;
}

void ObjectScanner::ScanObjects()
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
    float maxDistSq = SCAN_RADIUS * SCAN_RADIUS;

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

        if (distSq > maxDistSq) continue;

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

    REX::INFO("ObjectScanner: Scanned {} {} within {} units",
        m_scannedObjects.size(), GetCategoryName(m_currentCategory), SCAN_RADIUS);
}

void ObjectScanner::ScanAndAnnounce()
{
    ScanObjects();

    if (m_scannedObjects.empty()) {
        std::string msg = "No ";
        msg += GetCategoryName(m_currentCategory);
        msg += " nearby";
        SpeakText(msg);
        return;
    }

    std::string announcement = std::to_string(m_scannedObjects.size()) + " ";
    announcement += GetCategoryName(m_currentCategory);
    announcement += ". ";

    // Announce first object with dynamic distance
    float dist = GetDistanceToObject(m_scannedObjects[0].ref);
    announcement += m_scannedObjects[0].name + ", " + std::to_string(static_cast<int>(dist)) + " units";

    SpeakText(announcement);
}

void ObjectScanner::SelectNext()
{
    ScanIfNeeded();

    if (m_scannedObjects.empty()) {
        std::string msg = "No ";
        msg += GetCategoryName(m_currentCategory);
        msg += " nearby";
        SpeakText(msg);
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
        std::string msg = "No ";
        msg += GetCategoryName(m_currentCategory);
        msg += " nearby";
        SpeakText(msg);
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
        std::string msg = "No ";
        msg += GetCategoryName(m_currentCategory);
        msg += " nearby";
        SpeakText(msg);
        return;
    }

    if (m_currentIndex >= static_cast<std::int32_t>(m_scannedObjects.size())) {
        m_currentIndex = 0;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];

    if (!obj.ref || obj.ref->IsDeleted() || obj.ref->IsDisabled()) {
        SpeakText("Object no longer available");
        return;
    }

    float dist = GetDistanceToObject(obj.ref);
    std::string announcement = obj.name + ", " + std::to_string(static_cast<int>(dist)) + " units";

    SpeakText(announcement);
}

void ObjectScanner::LookAtCurrentObject()
{
    if (m_scannedObjects.empty() || m_currentIndex < 0) {
        return;
    }

    const auto& obj = m_scannedObjects[m_currentIndex];
    if (!obj.ref || obj.ref->IsDeleted() || obj.ref->IsDisabled()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    // Get player eye position (not feet position)
    RE::NiPoint3 eyePos, eyeDir;
    player->GetEyeVector(eyePos, eyeDir, true);

    // Get object center (not base position)
    RE::NiPoint3 targetCenter;
    obj.ref->GetObjectCenter(targetCenter);

    // Calculate direction from eye to object center
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

void ObjectScanner::SpeakText(const std::string& a_text)
{
    if (SRAL_GetCurrentEngine() != 0) {
        SRAL_Speak(a_text.c_str(), false);
        REX::INFO("ObjectScanner spoke: {}", a_text);
    }
}
