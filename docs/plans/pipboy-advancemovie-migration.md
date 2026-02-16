# PipBoy AdvanceMovie Migration Plan

## Goal
Migrate PipBoy accessibility to detect page/tab changes via AdvanceMovie hook polling instead of key press handlers, and replace hardcoded English names with localized strings from the game's translation system.

## What Changes

### 1. AdvanceMovie Hook Expansion
**Current**: `HookedAdvanceMovie` only checks quest summary state (`Description_tf.visible`).
**New**: Also check `CurrentPage` and `CurrentTab` every frame, announce changes.

This means page/tab changes are detected regardless of input method (keyboard, gamepad, mouse click on header, companion app).

### 2. Remove Key Press Handlers for Page/Tab
**Remove from MenuAccessibility::OnButtonEvent**:
- PipBoy navigation key handling (Up/Down triggering `OnNavigate`, W/S triggering `CheckPageChange`)
- PipBoy tab key handling (Q/E/Left/Right triggering `CheckTabChange`)

**Keep**:
- Map scanner keys (Page Up/Down/Home/End) - these are user-initiated custom actions
- The `ShouldHandleEvent` check for `m_pipBoyMenuOpen` (other menus still need it)

### 3. Localized Names via Translation API
Replace all hardcoded `GetTabName()`/`GetPageName()` switch statements with `LocalizationHelper::Translate("$KEY")` calls.

**Localization keys** (from Translate_en.txt, verified present):
- Pages: `$STAT`, `$INV`, `$DATA`, `$MAP`, `$RADIO`
- Inventory tabs: `$InventoryCategoryWeapons`, `$InventoryCategoryApparel`, `$InventoryCategoryAid`, `$InventoryCategoryMisc`, `$InventoryCategoryJunk`, `$InventoryCategoryMods`, `$InventoryCategoryAmmo`
- Data tabs: `$QUESTS`, `$SANCTUARY`, `$LOG`
- Map tabs: `$World Map`, `$Local Map`
- Stats tabs: `$PipboyConditionCategory`, `$PipboySPECIALCategory`, `$PipboyPerksCategory`

### 4. Remove Dead Code
- `PipBoyMenuAccessibility::CheckSelection()` - no longer called from key handler
- `PipBoyMenuAccessibility::CheckPageChange()` - replaced by AdvanceMovie polling
- `PipBoyMenuAccessibility::CheckTabChange()` - replaced by AdvanceMovie polling
- `PipBoyMenuAccessibility::OnNavigate()` - no longer needed (inventory already callback-driven)
- `PipBoyInventoryPage::OnNavigateUp/Down()` - already empty stubs
- `PipBoyInventoryPage::CheckSelection()` - tab detection moves to AdvanceMovie
- `PipBoyDataPage::CheckSelection()` - tab detection moves to AdvanceMovie

## Files Modified

1. **PipBoyCallbackHook.h/cpp** - Expand AdvanceMovie hook to track page/tab changes
2. **PipBoyMenuAccessibility.h/cpp** - Remove key-press methods, use localized page names
3. **PipBoyInventoryPage.h/cpp** - Remove key-press methods, use localized tab names
4. **PipBoyDataPage.h/cpp** - Remove key-press methods, use localized tab names
5. **PipBoyMapPage.h/cpp** - Use localized tab names (keep scanner key handlers)
6. **PipBoyPageBase.h/cpp** - Add `CheckTabChange()` to AdvanceMovie-driven flow, remove input-driven methods from interface
7. **MenuAccessibility.cpp** - Remove PipBoy key press handling code

## Architecture

### AdvanceMovie Flow (new)
```
HookedAdvanceMovie() called every frame
  -> Call original AdvanceMovie (game updates state)
  -> CheckPageChange(): read CurrentPage, compare to last, announce if changed
  -> CheckTabChange(): read CurrentTab, compare to last, announce if changed
  -> CheckSummaryStateChange(): existing quest summary detection
```

### Page/Tab Name Resolution
```cpp
// Instead of:
const char* GetPageName(Page p) { switch(p) { case STAT: return "Status"; ... } }

// Use:
std::string GetLocalizedPageName(Page p) {
    static const char* keys[] = { "$STAT", "$INV", "$DATA", "$MAP", "$RADIO" };
    return LocalizationHelper::Translate(keys[static_cast<int>(p)]);
}
```

### What Stays the Same
- Callback hook (Call vtable hook) for inventory/quest/workshop item announcements
- Map scanner key handlers (Page Up/Down/Home/End)
- All announcement formatting logic
- ItemStatFormatter, ItemIdentity usage
