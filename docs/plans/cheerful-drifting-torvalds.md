# Plan: Complete Pip-Boy Accessibility

## Scope

Make all Pip-Boy pages fully accessible for blind users. This includes:

1. **STAT page** (3 tabs) - Status, SPECIAL, Perks
2. **RADIO page** - Radio station list with active state
3. **DATA page fixes** - Workshops tab and Stats/Log tab (only Quests currently works)

## Current State

| Page | Status |
|------|--------|
| INV (Inventory) | Fully accessible - 7 tabs, item identity + stats |
| DATA (Quests tab) | Accessible - quest selection, objectives, summary toggle |
| DATA (Workshops tab) | NOT accessible - dead code exists but never called |
| DATA (Stats/Log tab) | NOT accessible - no implementation |
| MAP | Fully accessible - scanner keys, marker navigation |
| STAT | Tab names announced only, NO content |
| RADIO | Page name announced only, NO content |

## Architecture

Follow the existing `PipBoyPageBase` pattern: create new page classes, register them in `PipBoyMenuAccessibility`, and use either dataObj reading or menuObj polling for selection tracking.

### Data Access Strategy

Each page uses different data access depending on what's available:

| Page/Tab | Data Source | Selection Tracking |
|----------|-------------|-------------------|
| STAT/Status | `dataObj` direct members (CurrHP, limb conditions, ActiveEffects) | No list - read all stats on activation |
| STAT/SPECIAL | `dataObj.SPECIALList` array | Poll via HookedCall callback or menuObj List_mc |
| STAT/Perks | `dataObj.PerksList` array | Poll via HookedCall callback or menuObj List_mc |
| RADIO | `dataObj.RadioList` array | Poll via HookedCall callback or menuObj List_mc |
| DATA/Workshops | `dataObj.WorkshopsList` array | Poll menuObj for List_mc.selectedIndex |
| DATA/Stats | `dataObj.GeneralStatsList` array | Poll menuObj for CategoryList_mc.selectedIndex |

### Key Challenge: Selection Tracking in Dynamic SWFs

The SPECIAL, Perks, Radio, Workshops, and Stats tabs all use `BSScrollingList` components inside dynamically loaded SWFs. Unlike the INV and Quests tabs (which have AS-to-C++ callbacks), these pages have **no selection callbacks**.

**Approach**: Try to intercept any existing AS-to-C++ callbacks via the existing `PipBoyCallbackHook`. For pages without callbacks, access the sub-menu's `menuObj` reference to read `List_mc.selectedIndex`. If `menuObj` doesn't provide access to the loaded SWF's components, fall back to reading the data arrays from `dataObj` and announcing all items on activation (less ideal but functional).

## Files to Create

### 1. `src/PipBoyStatsPage.h` and `src/PipBoyStatsPage.cpp`
New page handler for the STAT page (page index 0).

**Status tab (tab 0):**
- On activation: announce player name, HP (current/max), AP (current/max)
- Announce limb conditions: Head, Left Arm, Right Arm, Left Leg, Right Leg with percentage
- Announce active effects (iterate ActiveEffects array, announce source + effect names + values)
- Announce damage output and resistances from TotalDamages/TotalResists arrays
- Announce stimpak and RadAway counts

**SPECIAL tab (tab 1):**
- On activation: announce first stat
- Track selection changes via menuObj List_mc polling or callback
- For each stat: announce name, value, modifier (if non-zero), and description
- 7 entries total (always all visible, no scrolling)

**Perks tab (tab 2):**
- On activation: announce first perk
- Track selection changes via menuObj List_mc polling or callback
- For each perk: announce name, rank (if >1), and description for current rank
- Scrollable list (10 visible items)

### 2. `src/PipBoyRadioPage.h` and `src/PipBoyRadioPage.cpp`
New page handler for the RADIO page (page index 4).

- On activation: announce current station count and which (if any) is active
- Track selection changes via menuObj List_mc polling or callback
- For each station: announce name and whether it's currently playing (active)
- Announce when station is toggled on/off (detect `active` property changes)

### 3. `src/PipBoyDataPage.cpp` (modify existing)
Expand the DATA page to handle Workshops and Stats tabs, not just Quests.

**Workshops tab (tab 1):**
- Track selection via menuObj polling (no C++ callback exists)
- Announce settlement name on selection change
- Announce workshop stats: People, Food, Water, Power, Safety, Beds, Happiness
- Read from `workshopData` array (7 entries with value and rating)

**Stats/Log tab (tab 2):**
- Dual-list: CategoryList (left) + ValueList (right, view-only)
- Track category selection changes
- On category change: announce category name and read first N stat entries
- Stats entries have `text` (label) and `value` (number)

## Files to Modify

### `src/PipBoyMenuAccessibility.h`
- Add `#include` for new page headers
- Add `std::unique_ptr<PipBoyStatsPage> m_statsPage`
- Add `std::unique_ptr<PipBoyRadioPage> m_radioPage`

### `src/PipBoyMenuAccessibility.cpp`
- Create stats and radio pages in constructor
- Return them from `GetActivePage()` for Page::STAT and Page::RADIO
- Reset them in `Reset()`
- Remove fallback tab-change handling for STAT (now handled by PipBoyStatsPage)

### `src/PipBoyDataPage.h`
- Add members for workshop and stats tab selection tracking
- Add methods for workshop/stats polling

### `src/PipBoyDataPage.cpp`
- Implement `CheckSelection()` to poll workshop/stats list selections
- Add workshop announcement with full settlement stats
- Add stats category navigation with stat value reading

### `src/PipBoyCallbackHook.cpp`
- Check if any new callbacks exist for STAT/RADIO pages (onPerkSelection, etc.)
- If found, add handlers similar to HandleInvItemSelection

### `src/PipBoyPageBase.h`
- Add helper method to access sub-menu menuObj references for selection polling
- Add helper to get a PipboySubMenu by type from PipboyMenu

### `xmake.lua`
- Add new source files to the build

### `docs/scaleform-paths.md`
- Document newly discovered STAT page paths (HP, limbs, SPECIAL, Perks)
- Document RADIO page paths
- Document Workshop and Stats tab paths

## Implementation Order

1. **PipBoyStatsPage** (STAT page) - highest value, most content
   - Status tab first (direct dataObj reads, no selection tracking needed)
   - SPECIAL tab (7 fixed items with selection)
   - Perks tab (scrollable list with selection)

2. **PipBoyDataPage updates** (Workshops + Stats/Log tabs)
   - Workshops tab (settlement list with stats detail)
   - Stats/Log tab (category + value dual-list)

3. **PipBoyRadioPage** (RADIO page)
   - Station list with active state tracking

4. **Documentation updates**

## Testing

After implementation:
1. Build with `xmake build`
2. Deploy with the `/deploy` skill
3. Test each page in-game:
   - Open Pip-Boy, navigate to STAT page, verify HP/limb/effects are read
   - Switch to SPECIAL tab, navigate through all 7 stats
   - Switch to Perks tab, scroll through perks
   - Navigate to DATA page, Workshops tab, select settlements
   - Switch to Stats/Log tab, navigate categories
   - Navigate to RADIO page, scroll through stations
4. Check `fallout4access_speech.log` for proper announcements
5. Check `fallout4access.log` for errors

## Localization Keys

All announcements will use game localization where available:
- STAT tabs: `$PipboyConditionCategory`, `$PipboySPECIALCategory`, `$PipboyPerksCategory`
- Workshop stats: `$WS_People`, `$WS_Food`, `$WS_Water`, `$WS_Power`, `$WS_Safety`, `$WS_Beds`, `$WS_Happy`
- DATA tabs: `$QUESTS`, `$SANCTUARY`, `$LOG`
- Radio: `$RadioOff`, `$TuneToStation`

Hardcoded English strings for concepts the game doesn't localize (limb names, stat labels like "HP", "AP") will be kept minimal.
