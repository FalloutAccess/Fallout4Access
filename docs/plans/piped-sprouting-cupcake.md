# Plan: Detect and Announce Message Box Content Changes

## Problem

When a message box stays open but its content updates (body text or buttons change after a button press), the new state is never announced. Both `MessageBoxMenuHook` and `PipBoyMapPage::CheckMessageBox` read content once on open, then only track button **selection** changes.

## Root Cause

- **MessageBoxMenuHook**: `m_initialAnnouncementDone = true` prevents body text from ever being re-read. Button labels are never cached or compared.
- **PipBoyMapPage::CheckMessageBox**: The `m_messageBoxOpen = true` branch only polls `selectedIndex`. Body text and button labels are never re-checked.

## Fix

Track last-known body text and button labels. Each frame, compare current content to cached values. If different, announce the full new state.

---

### File: `src/MessageBoxMenuHook.h`

Add two state variables and replace two method declarations:

```cpp
// Add to State section (after m_framesSinceOpen):
std::string m_lastBodyText;
std::vector<std::string> m_lastButtonLabels;
```

Replace method declarations:
- Remove `void AnnounceMessageBox();` from public (line 24)
- Remove `void CheckSelectionChange(RE::MessageBoxMenu* a_menu);` from private (line 38)
- Add `void CheckContentChanges(RE::MessageBoxMenu* a_menu);` to private

Add `#include <vector>` and `#include <string>` if not already covered by pch.h.

### File: `src/MessageBoxMenuHook.cpp`

**`ResetState()`** — clear new cached state:
```cpp
m_lastBodyText.clear();
m_lastButtonLabels.clear();
```

**`HookedAdvanceMovie()`** — simplify to single `CheckContentChanges()` call:
- Remove the split between initial-announcement and selection-change phases
- Just call `CheckContentChanges(a_this)` each frame while `m_menuOpen`
- Keep `m_framesSinceOpen` tracking (still needed so we don't poll forever on empty menus)

**New `CheckContentChanges()`** — replaces both `AnnounceMessageBox()` and `CheckSelectionChange()`:
1. Read current body text, button count, all button labels, selected index
2. Compare body text + button labels vector against cached `m_lastBodyText` / `m_lastButtonLabels`
3. If content changed → announce full state (body + button count + selected button), update cache
4. Else if only selection changed → announce just the selected button text (existing behavior)

**Delete** `AnnounceMessageBox()` and `CheckSelectionChange()`.

---

### File: `src/PipBoyMapPage.h`

Add two state variables to the embedded message box section (after line 94):
```cpp
std::string m_messageBoxLastBodyText;
std::vector<std::string> m_messageBoxLastButtonLabels;
```

### File: `src/PipBoyMapPage.cpp`

**`Reset()`** — clear new cached state:
```cpp
m_messageBoxLastBodyText.clear();
m_messageBoxLastButtonLabels.clear();
```

**Close detection** (two paths in `CheckMessageBox`) — also clear cached content:
```cpp
m_messageBoxLastBodyText.clear();
m_messageBoxLastButtonLabels.clear();
```

**`CheckMessageBox()` main logic** — unify the `!m_messageBoxOpen` and `else` branches:
1. After finding `menuMc`, always read body text, full button array, and selected index
2. Compare against `m_messageBoxLastBodyText` / `m_messageBoxLastButtonLabels`
3. If `!m_messageBoxOpen` OR content changed → announce full state (body + count + selected button), update cache
4. Else if only selection changed → announce just the selected button (existing behavior)

---

## Files NOT Changed

- `MainMenuHook` confirm dialog — already tracks text changes via `m_lastConfirmText`
- Other `CleanLocalizationString` uses (ExamineMenuHook, PipBoyCallbackHook, etc.) — unrelated

## Verification

1. Open any MessageBoxMenu (e.g. quit confirmation) — body + buttons announced on open
2. If a button press updates the message box content without closing it — new body + buttons should be re-announced
3. Navigating between buttons still announces each button
4. Pip-Boy map fast travel confirmation — same behavior as above
5. Closing and reopening a message box should work correctly (state reset)
