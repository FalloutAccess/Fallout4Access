# Plan: Add Door Proxy and Local Quest Markers to Scanner

## Problem

When the player is in an interior cell and quest objectives are elsewhere, the game shows markers on exit doors instead of the actual objectives. Currently `ScanQuestMarkers()` only scans `worldQuestMarkers`, missing:

1. **Door proxy markers** - exit doors when objective is outside current cell
2. **Local quest markers** - objectives within the current interior

## Data Structures

| Structure | Type | Purpose |
|-----------|------|---------|
| `worldQuestMarkers` | `BSTHashMap<ObjectRefHandle, PipboyObject*>` | Already scanned |
| `doorMarkers` | `BSTArray<PipboyObject*>` | UI objects for doors |
| `doorRefHandles` | `BSTArray<ObjectRefHandle>` | Door references (parallel to doorMarkers) |
| `localQuestMarkers` | `BSTHashMap<ObjectRefHandle, PipboyObject*>` | Interior quest markers |

## Implementation

### File: `src/ObjectScanner.cpp`

Modify `ScanQuestMarkers()` to scan three sources:

#### 1. Add door proxy markers (lines ~730-815)

```cpp
// Scan door proxy markers (exit doors when objective is outside)
auto& doorHandles = pipboyData->mapData.doorRefHandles;
REX::INFO("ObjectScanner: doorRefHandles has {} entries", doorHandles.size());

for (std::uint32_t i = 0; i < doorHandles.size(); ++i) {
    auto handle = doorHandles[i];
    if (!handle) continue;

    auto refPtr = handle.get();
    if (!refPtr || !refPtr.get()) continue;

    auto* ref = refPtr.get();
    if (ref->IsDeleted() || ref->IsDisabled()) continue;

    RE::NiPoint3 pos = ref->GetPosition();
    if (!IsValidPosition(pos)) continue;

    const char* name = ref->GetDisplayFullName();
    std::string markerName = (name && name[0] != '\0') ? name : "Exit";
    markerName += " (Quest)";  // Indicate this is a quest-related door

    ScannedObject obj;
    obj.ref = ref;
    obj.name = markerName;
    obj.isVirtual = false;
    obj.virtualPos = pos;

    m_scannedObjects.push_back(obj);
    REX::INFO("ObjectScanner: Added door marker: '{}'", markerName);
}
```

#### 2. Add local quest markers

```cpp
// Scan local quest markers (objectives in current interior)
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

    // Use same objective-matching logic as worldQuestMarkers
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
}
```

#### 3. Refactor objective name lookup

Extract the objective-matching logic (lines 757-804) into a helper function to avoid code duplication:

```cpp
std::string ObjectScanner::FindObjectiveName(RE::TESObjectREFR* ref, RE::PlayerCharacter* player)
```

### Order of Priority

Scan in this order (first match wins for duplicate refs):
1. **Door markers** - highest priority when in interior
2. **Local quest markers** - objectives in current interior
3. **World quest markers** - objectives in world (existing)

Add duplicate detection to avoid listing the same marker twice.

## Verification

1. Build: `xmake build`
2. Test in-game:
   - Enter an interior (e.g., a building)
   - Have an active quest with objective outside
   - Switch to Quest Markers category
   - Verify exit door appears with "(Quest)" suffix
   - Verify objectives inside the interior also appear
