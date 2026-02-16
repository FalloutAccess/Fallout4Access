# Plan: Unlooted Corpse Tracking

## Goal

Track which corpses the player has "viewed" (opened container menu or looted via quick-loot). All corpses always appear in the scanner — the player uses **subcategory cycling** (End key) to filter between All / Unlooted / Looted.

## New File: `src/CorpseLootTracker.h` + `src/CorpseLootTracker.cpp`

Singleton class that tracks viewed/looted corpse FormIDs.

### Event Sink: `BSTEventSink<TESContainerChangedEvent>`

Detects item transfers from dead actors to player (covers quick-loot widget):

```cpp
ProcessEvent(const TESContainerChangedEvent& event) {
    if (event.newContainerFormID != playerFormID) return kContinue;

    auto* form = RE::TESForm::GetFormByID(event.oldContainerFormID);
    auto* ref = form ? form->As<RE::TESObjectREFR>() : nullptr;
    if (!ref || !ref->Is(kACHR)) return kContinue;

    if (auto* actor = ref->As<RE::Actor>(); actor && actor->IsDead(false))
        MarkLooted(event.oldContainerFormID);

    return kContinue;
}
```

### Public API

```cpp
void MarkLooted(std::uint32_t formID);
bool IsLooted(std::uint32_t formID) const;
void Clear();                               // On save revert
void Register();                            // Register event sink
static void OnSave(const F4SE::SerializationInterface*);
static void OnLoad(const F4SE::SerializationInterface*);
static void OnRevert(const F4SE::SerializationInterface*);
```

### State

```cpp
std::unordered_set<std::uint32_t> m_lootedCorpses;  // FormIDs
mutable BSSpinLock m_lock;                            // Thread safety
```

### F4SE Cosave

- Unique ID: `'F4AC'`
- Record type: `'CLOT'`, version 1
- Data: `uint32_t count` + array of `uint32_t formID`
- On load: `ResolveFormID()` to remap across load orders
- On revert: `Clear()` the set

## Modified: `src/ObjectScanner.cpp`

### 1. Remove corpse filtering from `IsInteractable()` (line ~536-544)

```cpp
// BEFORE:
if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
    if (auto* actor = a_ref->As<RE::Actor>()) {
        if (actor->IsDead(false)) {
            if (IsContainerEmpty(a_ref)) {
                return false;  // Skip empty corpses
            }
        }
    }
    return true;
}

// AFTER: Remove empty-inventory check for dead actors.
// Living actors are unaffected. MatchesCategory() still separates NPCs vs Corpses.
// Looted/unlooted filtering moves to MatchesSubcategory() via CorpseLootTracker.
if (a_ref->Is(RE::ENUM_FORM_ID::kACHR)) {
    return true;
}
```

### 2. Add Corpses to `HasSubcategories()` (line ~261)

```cpp
bool ObjectScanner::HasSubcategories(ScanCategory a_category)
{
    return a_category == ScanCategory::Activators ||
           a_category == ScanCategory::Containers ||
           a_category == ScanCategory::Doors ||
           a_category == ScanCategory::Corpses;  // NEW
}
```

### 3. Add Corpses subcategory names in `GetSubcategoryName()` (line ~268)

Add before the existing `else` block:

```cpp
} else if (m_currentCategory == ScanCategory::Corpses) {
    switch (a_subcategory) {
        case ScanSubcategory::All: return "All";
        case ScanSubcategory::TypeA: return "Unlooted";
        case ScanSubcategory::TypeB: return "Looted";
        default: return "Unknown";
    }
}
```

### 4. Add Corpses subcategory filtering in `MatchesSubcategory()` (line ~334)

Add after the Containers block:

```cpp
// Corpse subcategories: TypeA=Unlooted, TypeB=Looted
if (m_currentCategory == ScanCategory::Corpses) {
    bool isLooted = CorpseLootTracker::GetSingleton()->IsLooted(a_ref->GetFormID());
    switch (a_subcategory) {
        case ScanSubcategory::TypeA: return !isLooted;  // Unlooted
        case ScanSubcategory::TypeB: return isLooted;   // Looted
        default: return true;
    }
}
```

## Modified: `src/ContainerMenuAccessibility.cpp`

In `OnContainerMenuOpened()`, inside the existing task lambda (which already has the `ContainerMenu*`), add corpse detection:

```cpp
// After getting containerMenu, before AnnounceContainerOpened:
auto refPtr = containerMenu->containerRef.get();
if (auto* ref = refPtr.get()) {
    if (ref->Is(RE::ENUM_FORM_ID::kACHR)) {
        if (auto* actor = ref->As<RE::Actor>(); actor && actor->IsDead(false)) {
            CorpseLootTracker::GetSingleton()->MarkLooted(ref->GetFormID());
        }
    }
}
```

## Modified: `src/main.cpp`

```cpp
#include "CorpseLootTracker.h"

// In F4SE_PLUGIN_LOAD — register cosave:
auto* serialization = F4SE::GetSerializationInterface();
if (serialization) {
    serialization->SetUniqueID('F4AC');
    serialization->SetSaveCallback(CorpseLootTracker::OnSave);
    serialization->SetLoadCallback(CorpseLootTracker::OnLoad);
    serialization->SetRevertCallback(CorpseLootTracker::OnRevert);
}

// In OnF4SEMessage (kGameDataReady) — register event sink:
CorpseLootTracker::GetSingleton()->Register();
```

## Files Summary

| File | Action | Changes |
|------|--------|---------|
| `src/CorpseLootTracker.h` | **New** | Singleton, event sink, API, cosave callbacks |
| `src/CorpseLootTracker.cpp` | **New** | Implementation |
| `src/main.cpp` | **Modify** | Register cosave + event sink |
| `src/ObjectScanner.cpp` | **Modify** | Remove corpse filter from IsInteractable; add subcategories |
| `src/ContainerMenuAccessibility.cpp` | **Modify** | Mark corpse as looted on menu open |

## Verification

1. **Build**: `xmake build` compiles cleanly
2. **Test: Subcategory cycling**
   - Switch to Corpses category (Shift+PageDown)
   - Press End to cycle: "All" → "Unlooted" → "Looted" → "All"
3. **Test: Unlooted detection**
   - Kill an enemy, scan Corpses → corpse appears in both "All" and "Unlooted"
   - Open corpse's container menu, close it
   - Scan again → corpse now appears in "All" and "Looted", NOT in "Unlooted"
4. **Test: Quick-loot detection**
   - Kill another enemy, take an item via quick-loot (no full menu open)
   - Scan → corpse now in "Looted" subcategory
5. **Test: Save persistence**
   - Save game, reload → looted state preserved
6. **Test: New game/revert**
   - Load a different save → looted set clears
