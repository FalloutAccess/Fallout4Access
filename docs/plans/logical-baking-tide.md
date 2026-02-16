# Plan: Add Locked Door Subcategory and Door Status Announcements

## Summary
Add a "Locked" subcategory filter for doors and announce door status only when relevant (locked or closed).

## Files to Modify
- `src/ObjectScanner.h` - Add helper method declarations
- `src/ObjectScanner.cpp` - Implement helpers, subcategory logic, and announcements

## Implementation

### 1. Add Helper Method Declarations (ObjectScanner.h)

After line 81 (`IsContainerEmpty`), add:
```cpp
bool IsDoorLocked(RE::TESObjectREFR* a_ref);
bool IsDoorOpen(RE::TESObjectREFR* a_ref);
```

### 2. Implement Helper Methods (ObjectScanner.cpp)

Add after `IsContainerEmpty()` implementation (around line 566):

```cpp
bool ObjectScanner::IsDoorLocked(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    RE::REFR_LOCK* lock = a_ref->GetLock();
    if (!lock) return false;

    RE::LOCK_LEVEL lockLevel = lock->GetLockLevel(a_ref);
    return lockLevel != RE::LOCK_LEVEL::kUnlocked;
}

bool ObjectScanner::IsDoorOpen(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return false;

    RE::BGSOpenCloseForm::OPEN_STATE state = RE::BGSOpenCloseForm::GetOpenState(a_ref);
    return state == RE::BGSOpenCloseForm::OPEN_STATE::kOpen ||
           state == RE::BGSOpenCloseForm::OPEN_STATE::kOpening;
}
```

### 3. Modify HasSubcategories() (line 256-260)

Add Doors to the check:
```cpp
bool ObjectScanner::HasSubcategories(ScanCategory a_category)
{
    return a_category == ScanCategory::Activators ||
           a_category == ScanCategory::Containers ||
           a_category == ScanCategory::Doors;
}
```

### 4. Modify GetSubcategoryName() (line 262-281)

Add Doors case after Containers (only "Locked" subcategory, no "Unlocked"):
```cpp
const char* ObjectScanner::GetSubcategoryName(ScanSubcategory a_subcategory)
{
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
            case ScanSubcategory::TypeB: return "All";  // No separate unlocked filter
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
```

### 5. Modify MatchesSubcategory() (line 321-347)

Add Doors case after Containers (around line 334):
```cpp
// Door subcategories: TypeA=Locked only
if (m_currentCategory == ScanCategory::Doors) {
    if (a_subcategory == ScanSubcategory::TypeA) {
        return IsDoorLocked(a_ref);  // Locked filter
    }
    return true;  // All or TypeB shows all doors
}
```

### 6. Add Door Status in ScanAndAnnounce() (line 1253-1261)

After container empty check, add door status (only if locked or closed):
```cpp
// Add empty indicator for containers
if (!firstObj.isVirtual && firstObj.ref) {
    auto* baseObj = firstObj.ref->GetObjectReference();
    if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
        if (IsContainerEmpty(firstObj.ref)) {
            objName += " (empty)";
        }
    }
    // Add status for doors (only if locked or closed)
    else if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
        bool isLocked = IsDoorLocked(firstObj.ref);
        bool isOpen = IsDoorOpen(firstObj.ref);
        // Only add suffix if there's something notable (locked or closed)
        if (isLocked || !isOpen) {
            std::string status;
            if (isLocked) status = "locked";
            if (!isOpen) {
                if (!status.empty()) status += ", ";
                status += "closed";
            }
            objName += " (" + status + ")";
        }
    }
}
```

### 7. Add Door Status in AnnounceCurrentObject() (line 1335-1343)

Same pattern as above:
```cpp
// Add empty indicator for containers
if (!obj.isVirtual && obj.ref) {
    auto* baseObj = obj.ref->GetObjectReference();
    if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kCONT) {
        if (IsContainerEmpty(obj.ref)) {
            announcement += " (empty)";
        }
    }
    // Add status for doors (only if locked or closed)
    else if (baseObj && baseObj->GetFormType() == RE::ENUM_FORM_ID::kDOOR) {
        bool isLocked = IsDoorLocked(obj.ref);
        bool isOpen = IsDoorOpen(obj.ref);
        // Only add suffix if there's something notable (locked or closed)
        if (isLocked || !isOpen) {
            std::string status;
            if (isLocked) status = "locked";
            if (!isOpen) {
                if (!status.empty()) status += ", ";
                status += "closed";
            }
            announcement += " (" + status + ")";
        }
    }
}
```

## Announcement Examples
- "Door to Diamond City (locked, closed), 150 units" - locked and closed
- "Door to Street (locked), 50 units" - locked but open
- "Exit (closed), 30 units" - unlocked but closed
- "Cellar Door, 75 units" - unlocked and open (no suffix)

## CommonLibF4 APIs Used
- `TESObjectREFR::GetLock()` → `REFR_LOCK*`
- `REFR_LOCK::GetLockLevel(ref)` → `LOCK_LEVEL` enum
- `LOCK_LEVEL::kUnlocked` (-1) means not locked
- `BGSOpenCloseForm::GetOpenState(ref)` → `OPEN_STATE` enum
- `OPEN_STATE::kOpen`, `kOpening`, `kClosed`, `kClosing`

## Verification
1. Build: `xmake build`
2. Deploy: `xmake install-mo2`
3. In-game testing:
   - Use numpad to cycle to Doors category
   - Press End to cycle subcategories (All → Locked → All)
   - Verify locked+closed doors announce "(locked, closed)"
   - Verify locked+open doors announce "(locked)"
   - Verify unlocked+closed doors announce "(closed)"
   - Verify unlocked+open doors have no suffix
