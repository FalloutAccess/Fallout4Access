# Known Scaleform Paths

This document contains discovered Scaleform/Flash UI element paths for Fallout 4 menus. These paths are used with `GetVariable()` calls to read UI state.

**How to discover new paths:** See the Scaleform UI Discovery section in CLAUDE.md.

## MainMenu.swf

```
Main menu list:           root.Menu_mc.MainPanel_mc.List_mc
  - selectedIndex         (int) current selection
  - entryList             (array) menu items with .text property

Splash screen text:       root.Menu_mc.SplashScreenHolder_mc.SplashScreenText_tf.text
                          ("$Press any button to start" - localized)

Confirmation dialog:      root.Menu_mc.ConfirmPanel_mc
  - visible               (bool) whether dialog is shown
  - Text path:            root.Menu_mc.ConfirmPanel_mc.Text_mc.TextPulse_mc.textField.text

Save/Load panel:          root.Menu_mc.SaveLoadHolder_mc
  - visible               (bool) whether panel is shown
  - Panel_mc.isSaving     (bool) true=save mode, false=load mode
  - Panel_mc.List_mc      (BSScrollingList) save entries list
    - selectedIndex       (int) current selection
    - entryList           (array) save entries with properties:
      - text              (string) location name
      - name              (string) character/save name
      - playTime          (string) format "days.hours.minutes.seconds"
      - level             (int) player level
      - corrupt           (bool) save is corrupted
      - obsolete          (bool) save is from older version

Settings panel:           root.Menu_mc.SettingsPanel_mc
  - SettingsList_mc       category list (Gameplay, Controls, Display, Audio)
Options panel:            root.Menu_mc.OptionsPanel_mc.Fader_mc.List_mc
Controls panel:           root.Menu_mc.ControlsPanel_mc.Fader_mc.List_mc
```

## TerminalMenu.swf

```
Terminal base:            root.TerminalBase_mc (Terminal class instance)

Text fields (pre-animation strings for immediate access):
  - strHeaderText         (string) terminal name/header
  - strWelcomeText        (string) welcome message
  - strDisplayText        (string) body text or hacking grid

Menu list:                root.TerminalBase_mc.MenuItemList_mc (BSScrollingList)
  - selectedIndex         (int) current selection
  - entryList             (array) menu items with .text property

Hacking mode:
  - HackingAttempts_tf.text (string) "X ATTEMPT(S) LEFT"

Terminal modes (RE::TerminalMenu::Mode):
  - kInit (0), kHack (1), kLogin (2), kList (3)
  - kText (4), kImage (5), kHolotape (6), kWaitingForPapyrus (7)
```

## PipboyMenu.swf

```
Data object:              root.DataObj (central data container populated from C++)

Page/Tab state:
  - CurrentPage           (int) 0=STAT, 1=INV, 2=DATA, 3=MAP, 4=RADIO
  - CurrentTab            (int) sub-tab index within current page

Inventory arrays:
  - InvItems              (array) all inventory items
  - InvSelectedItems      (array) selected index for each tab [7 elements]

Inventory item properties (InvItems[i]):
  - text                  (string) item display name
  - count                 (int) stack count
  - equipState            (int) 0=unequipped, >0=equipped
  - isLegendary           (bool) has legendary effects
  - taggedForSearch       (bool) tagged for component search
  - favorite              (int) 0=not favorite, >0=quickkey slot

Item card details:        root.DataObj.ItemCard_mc.InfoObj (array)
  - text                  (string) stat label (e.g., "Value", "Weight", "DAM")
  - value                 (number/string) stat value
  - damageType            (int) damage type ID
  - showAsDescription     (bool) true if this is description text

Quest data:               root.DataObj.QuestsList (array)
  - text                  (string) quest name
  - description           (string) quest description
  - active                (bool) currently tracked
  - completed             (bool) quest completed
  - isDivider             (bool) is a category divider
  - formID                (uint) quest form ID (0 = misc container)

Workshop data:            root.DataObj.WorkshopsList (array)
  - text                  (string) settlement name
  - happiness             (number) settlement happiness percentage
  - population            (int) number of settlers

General stats:            root.DataObj.GeneralStatsList (array)
  - text                  (string) stat name
  - value                 (number/string) stat value

Pip-Boy pages:
  - STAT (0): Status, SPECIAL, Perks tabs
  - INV (1): Weapons, Apparel, Aid, Misc, Junk, Mods, Ammo tabs
  - DATA (2): Quests, Workshops, Stats tabs
  - MAP (3): World Map, Local Map tabs
  - RADIO (4): Radio stations list
```

## HUDMenu.swf

```
Subtitles:                root.BottomCenterGroup_mc.SubtitleText_mc
  - SpeakerName_tf.text   (string) speaker's name
  - SubtitleText_tf.text  (string) subtitle/dialogue text

HUD Notifications:        root.HUDNotificationsGroup_mc
  - Messages_mc           message display container
    - ShownMessageArray   (array) currently visible messages
    - MessageArray        (array) queued messages
  - QuestUpdates_mc       quest notification area
    - QuestName_tf.text   (string) quest name
    - UpdateType_tf.text  (string) "Started", "Completed", etc.
  - ObjectiveUpdates_mc   objective notification area
    - ShownObjectivesV    (array) visible objectives
  - XPMeter_mc            XP gain display
    - NumberText.text     (string) XP amount
    - LeveUpTextClip.textField.text (string) level up text (note: "LeveUp" typo in game)
  - TutorialText_mc       tutorial popup
    - TutorialText_tf.text (string) tutorial message

Quickloot widget:         root.CenterGroup_mc.QuickContainerWidget_mc
  - _selectedIndex        (int) current selection
  - ListItems_mc          container for item clips
    - ItemText0..N        (MovieClip) individual item entries
      - visible           (bool) whether clip is shown
      - selected          (bool) whether item is selected
      - data              (object) item data with properties:
        - text            (string) item name
        - count           (int) stack count
        - equipState      (int) 0=unequipped, >0=equipped
        - isLegendary     (bool) has legendary effects
        - favorite        (int) quickkey slot
        - taggedForSearch (bool) tagged for component search
  - ListHeaderAndBracket_mc.ContainerName_mc.textField_tf.text (string) container name

Enemy health meter:       root.TopCenterGroup_mc.EnemyHealthMeter_mc
  - DisplayText_tf.text   (string) enemy name
  - LegendaryIcon_mc.visible (bool) is legendary enemy
  - SkullIcon_mc.visible  (bool) is skull (high level) enemy
  - MeterBar_mc.Percent   (number) health percentage
```

## ContainerMenu.swf

```
Side selection:           root.containerIsSelected (bool) true=container, false=player

Container side:           root.ContainerInventory_mc
  - ContainerListHeader   header with container name
    - headerText          (string) container name
    - textField.text      (string) alternative text access
  - ContainerList_mc      (BSScrollingList) container items
    - selectedIndex       (int) current selection
    - entryList           (array) items

Player side:              root.PlayerInventory_mc
  - PlayerListHeader      header with player inventory label
    - headerText          (string) header text
  - PlayerList_mc         (BSScrollingList) player items
    - selectedIndex       (int) current selection
    - entryList           (array) items

Item card:                root.ItemCard_mc
  - InfoObj               (array) stat entries (same format as Pip-Boy)

Sort button:              root.SortButton
  - ButtonText            (string) current sort mode

Pickpocket mode:          root.PickpocketInfo_mc
  - visible               (bool) whether pickpocket UI is shown
  - Percent_tf.text       (string) success chance percentage

Entry properties (entryList[i]):
  - text                  (string) item name
  - count                 (int) stack count
  - equipState            (int) 0=unequipped, >0=equipped
  - isLegendary           (bool) has legendary effects
  - favorite              (int) quickkey slot
  - taggedForSearch       (bool) tagged for component search
```

## DialogueMenu.swf

```
NPC name:                 root.name_tf.text (string)

Dialogue list:            root.List_mc (BSScrollingList)
  - alpha                 (number) visibility (0 when hidden)
  - selectedIndex         (int) current selection
  - entryList             (array) dialogue options

Entry properties (entryList[i]):
  - text                  (string) dialogue option text
  - challengeLevel        (int) speech check difficulty (0=none)
  - challengeResult       (int) success/fail result
  - said                  (bool) already selected this option
  - isBarterOption        (bool) opens barter menu
  - isInventoryOption     (bool) opens inventory
  - endsScene             (bool) ends conversation
```

## ExamineMenu.swf

```
Item name:                root.ItemName_tf.text (string)

Inventory list:           root.InventoryBase_mc.InventoryList_mc
  - selectedIndex         (int) current selection
  - entryList             (array) items to examine

Mod slots:                root.ModSlotBase_mc
  - SlotsLabel_tf.text    (string) "MOD SLOTS" label
  - ModSlotList_mc        (BSScrollingList) available mod slots
    - selectedIndex       (int) current selection
    - entryList           (array) mod slots

Mod description:          root.ModDescriptionBase_mc.ModDescription_tf.text (string)

Item card:                root.ItemCardList_mc.InfoObj (array)
  - Same format as Pip-Boy/Container item cards

Legendary description:    root.LegendaryItemDescription_tf.text (string)

Entry properties for mods (entryList[i]):
  - text                  (string) mod name
  - currentMod            (bool) currently installed
  - hasRequired           (bool) has required materials

Entry properties for items (entryList[i]):
  - text                  (string) item name
  - count                 (int) stack count
  - equipState            (int) equipped state
  - isLegendary           (bool) has legendary effects
  - bestInClass           (bool) best item of this type
```

## LevelUpMenu.swf

```
Perk grid:                root.Menu_mc.GridView_mc (PerkGrid instance)

Selected perk:            root.Menu_mc.GridView_mc.selectedPerkEntry (object)
  - text                  (string) perk name
  - description           (string) perk description
  - displayRank           (int) current viewing rank
  - displayMaxRank        (int) maximum rank available
  - owned                 (bool) player has this perk
  - available             (bool) can be purchased

Selected perk clip:       root.Menu_mc.GridView_mc.selectedPerkClip
  - row                   (int) grid row (0-9 for SPECIAL stats)
  - col                   (int) grid column (rank level)

Player info:
  - root.Menu_mc.playerLevel  (int) current player level
  - root.Menu_mc.perkCount    (int) available perk points

Rank viewing:             root.Menu_mc._ViewingRankOffset (int)
Description text:         root.Menu_mc._SelectionDesc (string)
```

## SPECIALMenu.swf

```
SPECIAL list:             root.Menu_mc.List_mc (BSScrollingList)
  - selectedIndex         (int) current selection (0-6 for S.P.E.C.I.A.L.)
  - entryList             (array) SPECIAL stats

Points counter:           root.Menu_mc.PointCounter_tf.text (string)
Description:              root.Menu_mc.Description_tf.text (string)

Entry properties (entryList[i]):
  - text                  (string) stat name (Strength, Perception, etc.)
  - value                 (int) current stat value (1-10)
```

## VATSMenu.swf

```
Menu instance:            root.MenuInstance (main container)

Selected body part:       root.MenuInstance.SelectedPart (int) index

Part info array:          root.MenuInstance.PartInfos (array)
  Each entry contains:
  - GraphicInstance.TintedContainer.AnimationInstance
    - Name.text           (string) body part name
    - HitChance.text      (string) hit chance percentage
    - PartHealth.BarInstance.scaleX (number) health bar scale

Target resistances:       root.MenuInstance.ResistancesInstance
  - Level.text            (string) target level
```

## MessageBoxMenu.swf

```
Message text:             root.Menu_mc.Panel_mc.messageText_tf.text (string)
Button bar:               root.Menu_mc.Panel_mc.buttonBar_mc
  - selectedIndex         (int) current button selection
  - numButtons            (int) number of visible buttons
```

## Input Key Codes

Fallout 4 uses **Windows Virtual Key codes** (not DirectInput scan codes) for keyboard input in `RE::ButtonEvent::QIDCode()`:

```
Navigation:
  - Up Arrow:    0x26 (VK_UP)
  - Down Arrow:  0x28 (VK_DOWN)
  - Left Arrow:  0x25 (VK_LEFT)
  - Right Arrow: 0x27 (VK_RIGHT)
  - W:           0x57
  - A:           0x41
  - S:           0x53
  - D:           0x44

Actions:
  - Enter:       0x0D (VK_RETURN)
  - E:           0x45 (Activate)
  - Tab:         0x09 (VK_TAB, used as Back in terminals)
  - Escape:      0x1B (VK_ESCAPE)
  - Space:       0x20 (VK_SPACE)

Pip-Boy specific:
  - Q:           0x51 (Previous tab)
  - E:           0x45 (Next tab)
  - 1-5:         0x31-0x35 (Direct page select)

Device types (RE::INPUT_DEVICE):
  - kKeyboard:   0
  - kMouse:      1
  - kGamepad:    2
```
