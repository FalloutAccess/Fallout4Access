# Plan: Workshop Object Scanner

## Goal
When in workshop mode, the object scanner should show all placed settlement objects that can be scrapped/moved, instead of normal gameplay objects.

## Approach

Add a `Workshop` scan category that uses `Workshop::IsLocationWithinBuildableArea` to filter objects within the settlement bounds. Auto-switch to this category when workshop opens, restore previous category when it closes.

### Key difference from normal scanning
Normal scanning uses `IsInteractable()` which requires activation text — this excludes most workshop objects (walls, floors, decorations, etc.). Workshop scanning skips that check and instead filters by buildable area + having a display name.

## Files to modify

### 1. `src/ObjectScanner.h`
- Add `Workshop` to `ScanCategory` enum (before `COUNT`)
- Add member: `ScanCategory m_preWorkshopCategory` to restore after workshop closes
- Add member: `bool m_workshopModeActive = false`
- Add methods: `void SetWorkshopMode(bool active)`, `void ScanWorkshopObjects()`

### 2. `src/ObjectScanner.cpp`

**`ShouldHandleEvent`**: Remove the `ui->menuMode` early-return when workshop is open (already works since workshop doesn't set menuMode, but also allow scanner when workshop is active).

**`ScanObjects()`**: Add Workshop category dispatch at top:
```cpp
if (m_currentCategory == ScanCategory::Workshop) {
    ScanWorkshopObjects();
    return;
}
```

**New `ScanWorkshopObjects()`**:
- Get player position
- Find nearest workshop via `RE::Workshop::FindNearestValidWorkshop(*player)`
- Iterate all refs in loaded cells (same grid pattern as `ScanObjects`)
- Filter: skip player, skip deleted/disabled, skip NPCs (kACHR)
- Filter: `RE::Workshop::IsLocationWithinBuildableArea(*workshop, objPos)`
- Filter: must have a display name (`GetDisplayFullName()`)
- Sort by distance (same pattern as existing)

**`GetCategoryName()`**: Add `case ScanCategory::Workshop: return "Workshop objects";`

**`MatchesCategory()`**: Not used for Workshop (ScanWorkshopObjects handles its own filtering)

**`SetWorkshopMode(bool active)`**:
- If activating: save current category to `m_preWorkshopCategory`, switch to `ScanCategory::Workshop`, clear scan results
- If deactivating: restore `m_preWorkshopCategory`, clear scan results

### 3. `src/WorkshopMenuAccessibility.cpp`

**`OnWorkshopMenuOpened()`**: Add `ObjectScanner::GetSingleton()->SetWorkshopMode(true);`

**`OnWorkshopMenuClosed()`**: Add `ObjectScanner::GetSingleton()->SetWorkshopMode(false);`

## Verification
1. Build with `xmake build`
2. In-game: enter workshop mode at a settlement
3. Use scanner keys (Page Up/Down) — should list placed settlement objects (turrets, walls, beds, crops, etc.)
4. Press Numpad 5 to rescan
5. Exit workshop — scanner should restore previous category
6. Use scanner outside workshop — should work normally as before
