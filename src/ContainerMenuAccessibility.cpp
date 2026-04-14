#include "ContainerMenuAccessibility.h"
#include "CorpseLootTracker.h"
#include "ItemStatFormatter.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>

ContainerMenuAccessibility* ContainerMenuAccessibility::GetSingleton()
{
	static ContainerMenuAccessibility singleton;
	return &singleton;
}

void ContainerMenuAccessibility::Install()
{
	if (m_installed) {
		REX::DEBUG("ContainerMenuAccessibility: Already installed");
		return;
	}

	// Hook ContainerMenu::AdvanceMovie (vtable index 4)
	// VTABLE::ContainerMenu[0] is the primary vtable (IMenu/GameMenuBase)
	static REL::THookVFT<void(RE::ContainerMenu*, float, std::uint64_t)> advanceHook(
		VTABLE::ContainerMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	if (advanceHook.Enable()) {
		s_advanceMovieHook = &advanceHook;
		REX::INFO("ContainerMenuAccessibility: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("ContainerMenuAccessibility: Failed to install AdvanceMovie hook");
	}

	// Hook ContainerMenu::OnButtonEvent (BSInputEventUser vtable index 8)
	// VTABLE::ContainerMenu[1] is the BSInputEventUser vtable
	static REL::THookVFT<void(RE::ContainerMenu*, const RE::ButtonEvent*)> buttonHook(
		VTABLE::ContainerMenu[1],
		8,  // OnButtonEvent is at index 8
		HookedOnButtonEvent);

	if (buttonHook.Enable()) {
		s_buttonEventHook = &buttonHook;
		REX::INFO("ContainerMenuAccessibility: OnButtonEvent hook installed successfully");
	} else {
		REX::ERROR("ContainerMenuAccessibility: Failed to install OnButtonEvent hook");
	}

	m_installed = true;
}

void ContainerMenuAccessibility::OnContainerMenuOpened()
{
	REX::INFO("ContainerMenuAccessibility: Menu opened");

	// Reset state
	m_menuOpen = true;
	m_openingAnnouncementDone = false;  // Don't announce items until container name announced
	m_containerFocused = true;
	m_lastContainerIndex = -1;
	m_lastPlayerIndex = -1;
	m_lastContainerFilterName.clear();
	m_lastPlayerFilterName.clear();
	m_lastSortLabel.clear();
	m_lastPickpocketPercent.clear();
	m_isPickpocketMode = false;
	m_quantityMenuOpen = false;
	m_lastQuantityValue.clear();

	// Schedule deferred announcement (1 frame delay for Scaleform to populate)
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_menuOpen)
				return;

			auto ui = RE::UI::GetSingleton();
			if (!ui)
				return;

			auto menu = ui->GetMenu("ContainerMenu"sv);
			if (!menu)
				return;

			auto* containerMenu = static_cast<RE::ContainerMenu*>(menu.get());

			// Mark corpses as looted when their container menu is opened
			auto refPtr = containerMenu->containerRef.get();
			if (auto* ref = refPtr.get()) {
				if (ref->Is(RE::ENUM_FORM_ID::kACHR)) {
					if (auto* actor = ref->As<RE::Actor>(); actor && actor->IsDead(false)) {
						CorpseLootTracker::GetSingleton()->MarkLooted(ref->GetFormID());
					}
				}
			}

			// Check if pickpocket mode
			m_isPickpocketMode = IsPickpocketMode(containerMenu);

			// Announce container opened
			AnnounceContainerOpened(containerMenu);
		});
	}
}

void ContainerMenuAccessibility::OnContainerMenuClosed()
{
	REX::INFO("ContainerMenuAccessibility: Menu closed");
	m_menuOpen = false;
}

void ContainerMenuAccessibility::HookedAdvanceMovie(RE::ContainerMenu* a_this, float a_timeDelta, std::uint64_t a_time)
{
	// Call original function first to let the game update state
	if (s_advanceMovieHook) {
		(*s_advanceMovieHook)(a_this, a_timeDelta, a_time);
	}

	// Then check for state changes
	auto* hook = GetSingleton();
	if (hook->m_menuOpen) {
		hook->CheckStateChanges(a_this);
	}
}

void ContainerMenuAccessibility::HookedOnButtonEvent(RE::ContainerMenu* a_this, const RE::ButtonEvent* a_event)
{
	// F key (switch sides) and H key (announce status) are now handled
	// by MCM keybinds via KeybindActions::SwitchMenuSide / AnnounceStats.

	// Call original OnButtonEvent
	if (s_buttonEventHook) {
		(*s_buttonEventHook)(a_this, a_event);
	}
}

void ContainerMenuAccessibility::CheckStateChanges(RE::ContainerMenu* a_menu)
{
	if (!a_menu)
		return;

	// Check quantity picker first - suppress other checks while it's open
	CheckQuantityMenu(a_menu);
	if (m_quantityMenuOpen)
		return;

	// Check for focus change (container <-> player inventory)
	CheckFocusChange(a_menu);

	// Check for filter/category change (left/right arrows)
	CheckFilterChange(a_menu);

	// Check for sort mode change
	CheckSortChange(a_menu);

	// Check for item selection change
	CheckItemSelection(a_menu);

	// Check pickpocket percentage if in pickpocket mode
	if (m_isPickpocketMode && m_containerFocused) {
		CheckPickpocketPercentage(a_menu);
	}
}

void ContainerMenuAccessibility::SwitchSides()
{
	if (!m_menuOpen) {
		return;
	}

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu<RE::ContainerMenu>();
	if (!menu) {
		return;
	}

	auto& menuObj = menu->menuObj;
	if (!menuObj.IsObject()) {
		return;
	}

	// Call ProcessUserEvent to switch sides (simulates gamepad trigger)
	// LTrigger -> SwitchToPlayerList, RTrigger -> SwitchToContainerList
	// Second param is abPressed=false (button released triggers the action)
	Scaleform::GFx::Value args[2];
	Scaleform::GFx::Value result;

	if (m_containerFocused) {
		// Currently on container, switch to player inventory
		args[0] = "LTrigger";
		args[1] = false;
		menuObj.Invoke("ProcessUserEvent", &result, args, 2);
	} else {
		// Currently on player inventory, switch to container
		args[0] = "RTrigger";
		args[1] = false;
		menuObj.Invoke("ProcessUserEvent", &result, args, 2);
	}
}

void ContainerMenuAccessibility::CheckFocusChange(RE::ContainerMenu* a_menu)
{
	bool currentFocus = IsContainerFocused(a_menu);

	if (currentFocus != m_containerFocused) {
		m_containerFocused = currentFocus;

		// Announce which side we're now on
		AnnounceFocusChange(m_containerFocused);

		// Get and announce the first item on the newly focused side
		std::int32_t index = GetSelectedIndex(a_menu, m_containerFocused);
		if (index >= 0) {
			auto item = GetItemAtIndex(a_menu, index, m_containerFocused);
			if (item.IsValid()) {
				AnnounceItem(item, m_containerFocused);

				// Update the tracked index for this side
				if (m_containerFocused) {
					m_lastContainerIndex = index;
				} else {
					m_lastPlayerIndex = index;
				}
			}
		}
		// Note: Side name already announced above, no need to repeat if list is empty
	}
}

bool ContainerMenuAccessibility::IsContainerFocused(RE::ContainerMenu* a_menu)
{
	if (!a_menu) {
		return true;  // Default to container
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return true;
	}

	Scaleform::GFx::Value containerSelected;
	if (menuObj.GetMember("containerIsSelected", &containerSelected) && containerSelected.IsBoolean()) {
		return containerSelected.GetBoolean();
	}

	return true;  // Default to container if we can't read the value
}

void ContainerMenuAccessibility::CheckFilterChange(RE::ContainerMenu* a_menu)
{
	// Skip until opening announcement is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::string  currentFilterName = GetCurrentFilterName(a_menu, m_containerFocused);
	std::string& lastFilterName = m_containerFocused ? m_lastContainerFilterName : m_lastPlayerFilterName;

	if (!currentFilterName.empty() && currentFilterName != lastFilterName) {
		lastFilterName = currentFilterName;
		SpeakText(currentFilterName);
	}
}

std::string ContainerMenuAccessibility::GetCurrentFilterName(RE::ContainerMenu* a_menu, bool a_containerSide)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	// Navigate to header text
	// Container side: ContainerInventory_mc.ContainerListHeader.headerText
	// Player side: PlayerInventory_mc.PlayerListHeader.headerText
	Scaleform::GFx::Value inventoryMc;
	const char*           invName = a_containerSide ? "ContainerInventory_mc" : "PlayerInventory_mc";
	if (!menuObj.GetMember(invName, &inventoryMc) || !inventoryMc.IsObject())
		return "";

	const char*           headerName = a_containerSide ? "ContainerListHeader" : "PlayerListHeader";
	Scaleform::GFx::Value header;
	if (!inventoryMc.GetMember(headerName, &header) || !header.IsObject())
		return "";

	Scaleform::GFx::Value headerText;
	if (!header.GetMember("headerText", &headerText) || !headerText.IsString())
		return "";

	return LocalizationHelper::CleanString(headerText.GetString());
}

void ContainerMenuAccessibility::CheckSortChange(RE::ContainerMenu* a_menu)
{
	// Skip until opening announcement is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::string currentSortLabel = GetSortButtonLabel(a_menu);

	if (!currentSortLabel.empty() && currentSortLabel != m_lastSortLabel) {
		m_lastSortLabel = currentSortLabel;
		SpeakText(currentSortLabel);
	}
}

std::string ContainerMenuAccessibility::GetSortButtonLabel(RE::ContainerMenu* a_menu)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	// Navigate: SortButton -> ButtonText
	Scaleform::GFx::Value sortButton;
	if (!menuObj.GetMember("SortButton", &sortButton) || !sortButton.IsObject())
		return "";

	Scaleform::GFx::Value buttonText;
	if (!sortButton.GetMember("ButtonText", &buttonText) || !buttonText.IsString())
		return "";

	return LocalizationHelper::CleanString(buttonText.GetString());
}

void ContainerMenuAccessibility::CheckItemSelection(RE::ContainerMenu* a_menu)
{
	// Skip until opening announcement (container name) is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::int32_t currentIndex = GetSelectedIndex(a_menu, m_containerFocused);

	// Get the last index for the current side
	std::int32_t& lastIndex = m_containerFocused ? m_lastContainerIndex : m_lastPlayerIndex;

	if (currentIndex >= 0 && currentIndex != lastIndex) {
		lastIndex = currentIndex;

		auto item = GetItemAtIndex(a_menu, currentIndex, m_containerFocused);
		if (item.IsValid()) {
			AnnounceItem(item, m_containerFocused);
		}
	}
}

std::int32_t ContainerMenuAccessibility::GetSelectedIndex(RE::ContainerMenu* a_menu, bool a_containerSide)
{
	if (!a_menu) {
		return -1;
	}

	// Access through menuObj (set up by SetMenuCodeObject) rather than uiMovie->GetVariable
	// menuObj is the root "Menu" object in ActionScript
	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return -1;
	}

	// Get the list object: ContainerList_mc or PlayerInventory_mc.PlayerList_mc
	Scaleform::GFx::Value listObj;
	if (a_containerSide) {
		if (!menuObj.GetMember("ContainerList_mc", &listObj) || !listObj.IsObject()) {
			return -1;
		}
	} else {
		Scaleform::GFx::Value playerInv;
		if (!menuObj.GetMember("PlayerInventory_mc", &playerInv) || !playerInv.IsObject()) {
			return -1;
		}
		if (!playerInv.GetMember("PlayerList_mc", &listObj) || !listObj.IsObject()) {
			return -1;
		}
	}

	// Get selectedIndex from the list
	Scaleform::GFx::Value indexValue;
	if (!listObj.GetMember("selectedIndex", &indexValue)) {
		return -1;
	}

	if (indexValue.IsInt()) {
		return indexValue.GetInt();
	} else if (indexValue.IsNumber()) {
		return static_cast<std::int32_t>(indexValue.GetNumber());
	} else if (indexValue.IsUInt()) {
		return static_cast<std::int32_t>(indexValue.GetUInt());
	}

	return -1;
}

ItemIdentity ContainerMenuAccessibility::GetItemAtIndex(RE::ContainerMenu* a_menu, std::int32_t a_index, bool a_containerSide)
{
	ItemIdentity item;

	if (!a_menu || a_index < 0) {
		return item;
	}

	// Access through menuObj
	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return item;
	}

	// Get the list object
	Scaleform::GFx::Value listObj;
	if (a_containerSide) {
		if (!menuObj.GetMember("ContainerList_mc", &listObj) || !listObj.IsObject()) {
			return item;
		}
	} else {
		Scaleform::GFx::Value playerInv;
		if (!menuObj.GetMember("PlayerInventory_mc", &playerInv) || !playerInv.IsObject()) {
			return item;
		}
		if (!playerInv.GetMember("PlayerList_mc", &listObj) || !listObj.IsObject()) {
			return item;
		}
	}

	// Get entryList from the list
	Scaleform::GFx::Value entryList;
	if (!listObj.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return item;
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return item;
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return item;
	}

	// Extract item properties
	Scaleform::GFx::Value prop;

	// Name (text)
	if (entry.GetMember("text", &prop) && prop.IsString()) {
		item.name = LocalizationHelper::CleanString(prop.GetString());
	}

	// Count
	if (entry.GetMember("count", &prop)) {
		if (prop.IsInt()) {
			item.count = prop.GetInt();
		} else if (prop.IsNumber()) {
			item.count = static_cast<std::int32_t>(prop.GetNumber());
		} else if (prop.IsUInt()) {
			item.count = static_cast<std::int32_t>(prop.GetUInt());
		}
	}

	// Equip state
	if (entry.GetMember("equipState", &prop)) {
		if (prop.IsInt()) {
			item.equipState = prop.GetInt();
		} else if (prop.IsNumber()) {
			item.equipState = static_cast<std::int32_t>(prop.GetNumber());
		} else if (prop.IsUInt()) {
			item.equipState = static_cast<std::int32_t>(prop.GetUInt());
		}
	}

	// Legendary
	if (entry.GetMember("isLegendary", &prop) && prop.IsBoolean()) {
		item.isLegendary = prop.GetBoolean();
	}

	// Favorite
	if (entry.GetMember("favorite", &prop)) {
		if (prop.IsBoolean()) {
			item.isFavorite = prop.GetBoolean();
		} else if (prop.IsNumber() || prop.IsInt() || prop.IsUInt()) {
			int favVal = 0;
			if (prop.IsInt())
				favVal = prop.GetInt();
			else if (prop.IsNumber())
				favVal = static_cast<int>(prop.GetNumber());
			else if (prop.IsUInt())
				favVal = static_cast<int>(prop.GetUInt());
			item.isFavorite = (favVal > 0);
		}
	}

	// Tagged for search
	if (entry.GetMember("taggedForSearch", &prop) && prop.IsBoolean()) {
		item.isTagged = prop.GetBoolean();
	}

	return item;
}

void ContainerMenuAccessibility::CheckPickpocketPercentage(RE::ContainerMenu* a_menu)
{
	std::string percent = GetPickpocketPercentage(a_menu);

	if (!percent.empty() && percent != m_lastPickpocketPercent) {
		m_lastPickpocketPercent = percent;
		SpeakText(percent);
	}
}

std::string ContainerMenuAccessibility::GetPickpocketPercentage(RE::ContainerMenu* a_menu)
{
	if (!a_menu) {
		return "";
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return "";
	}

	// Navigate: PickpocketInfo_mc -> Percent_tf -> text
	Scaleform::GFx::Value pickpocketInfo;
	if (!menuObj.GetMember("PickpocketInfo_mc", &pickpocketInfo) || !pickpocketInfo.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value percentTf;
	if (!pickpocketInfo.GetMember("Percent_tf", &percentTf) || !percentTf.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value text;
	if (percentTf.GetMember("text", &text) && text.IsString()) {
		return text.GetString();
	}

	return "";
}

void ContainerMenuAccessibility::CheckQuantityMenu(RE::ContainerMenu* a_menu)
{
	bool isOpen = IsQuantityMenuOpen(a_menu);

	if (isOpen && !m_quantityMenuOpen) {
		// Picker just opened
		m_quantityMenuOpen = true;
		std::string qty = GetQuantityValue(a_menu);
		m_lastQuantityValue = qty;
		if (!qty.empty()) {
			SpeakText(qty);
		}
	} else if (!isOpen && m_quantityMenuOpen) {
		// Picker just closed
		m_quantityMenuOpen = false;
		m_lastQuantityValue.clear();
	} else if (isOpen) {
		// Picker still open - check for value change
		std::string qty = GetQuantityValue(a_menu);
		if (qty != m_lastQuantityValue) {
			m_lastQuantityValue = qty;
			SpeakText(qty);
		}
	}
}

bool ContainerMenuAccessibility::IsQuantityMenuOpen(RE::ContainerMenu* a_menu)
{
	if (!a_menu)
		return false;

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return false;

	Scaleform::GFx::Value quantityMenu;
	if (!menuObj.GetMember("QuantityMenu_mc", &quantityMenu) || !quantityMenu.IsObject())
		return false;

	// Check alpha > 0 (ActionScript sets alpha=1 on open, alpha=0 on close)
	Scaleform::GFx::Value alpha;
	if (quantityMenu.GetMember("alpha", &alpha) && alpha.IsNumber()) {
		return alpha.GetNumber() > 0.0;
	}

	return false;
}

std::string ContainerMenuAccessibility::GetQuantityValue(RE::ContainerMenu* a_menu)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	Scaleform::GFx::Value quantityMenu;
	if (!menuObj.GetMember("QuantityMenu_mc", &quantityMenu) || !quantityMenu.IsObject())
		return "";

	Scaleform::GFx::Value valueTf;
	if (!quantityMenu.GetMember("Value_tf", &valueTf) || !valueTf.IsObject())
		return "";

	Scaleform::GFx::Value text;
	if (valueTf.GetMember("text", &text) && text.IsString()) {
		return text.GetString();
	}

	return "";
}

bool ContainerMenuAccessibility::IsPickpocketMode(RE::ContainerMenu* a_menu)
{
	if (!a_menu) {
		return false;
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return false;
	}

	// Check if PickpocketInfo_mc is visible
	Scaleform::GFx::Value pickpocketInfo;
	if (menuObj.GetMember("PickpocketInfo_mc", &pickpocketInfo) && pickpocketInfo.IsObject()) {
		Scaleform::GFx::Value visible;
		if (pickpocketInfo.GetMember("visible", &visible) && visible.IsBoolean()) {
			return visible.GetBoolean();
		}
	}

	return false;
}

void ContainerMenuAccessibility::AnnounceContainerStatus()
{
	if (!m_menuOpen)
		return;

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu<RE::ContainerMenu>();
	if (!menu)
		return;

	const auto& menuObj = menu->menuObj;
	if (!menuObj.IsObject())
		return;

	// Read PlayerInventory_mc -> PlayerWeight_tf -> text (format: "120/250")
	Scaleform::GFx::Value playerInv;
	if (!menuObj.GetMember("PlayerInventory_mc", &playerInv) || !playerInv.IsObject())
		return;

	Scaleform::GFx::Value weightTf;
	if (!playerInv.GetMember("PlayerWeight_tf", &weightTf) || !weightTf.IsObject())
		return;

	Scaleform::GFx::Value text;
	if (weightTf.GetMember("text", &text) && text.IsString()) {
		std::string weight = text.GetString();
		if (!weight.empty()) {
			SpeakText(weight);
		}
	}
}

void ContainerMenuAccessibility::AnnounceContainerOpened(RE::ContainerMenu* a_menu)
{
	std::string containerName = GetContainerName(a_menu);

	if (containerName.empty()) {
		containerName = "Container";
	}

	REX::INFO("ContainerMenuAccessibility: Container name = '{}'", containerName);

	// Announce container name
	SpeakText(containerName);

	// If pickpocket mode, announce the steal percentage
	if (m_isPickpocketMode) {
		std::string percent = GetPickpocketPercentage(a_menu);
		if (!percent.empty()) {
			m_lastPickpocketPercent = percent;
			SpeakText(percent);
		}
	}

	// Announce first item with side prefix
	std::int32_t index = GetSelectedIndex(a_menu, m_containerFocused);
	if (index >= 0) {
		auto item = GetItemAtIndex(a_menu, index, m_containerFocused);
		if (item.IsValid()) {
			AnnounceItem(item, m_containerFocused);
			if (m_containerFocused) {
				m_lastContainerIndex = index;
			} else {
				m_lastPlayerIndex = index;
			}
		}
	}

	// Initialize filter/sort state to prevent duplicate announcements
	m_lastContainerFilterName = GetCurrentFilterName(a_menu, true);
	m_lastPlayerFilterName = GetCurrentFilterName(a_menu, false);
	m_lastSortLabel = GetSortButtonLabel(a_menu);

	// Now allow state change checks to announce changes
	m_openingAnnouncementDone = true;
}

std::string ContainerMenuAccessibility::GetItemStats(RE::ContainerMenu* a_menu)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	// Get ItemCard_mc
	Scaleform::GFx::Value itemCard;
	if (!menuObj.GetMember("ItemCard_mc", &itemCard) || !itemCard.IsObject()) {
		return "";
	}

	// Get InfoObj array
	Scaleform::GFx::Value infoObj;
	if (!itemCard.GetMember("InfoObj", &infoObj) || !infoObj.IsArray()) {
		return "";
	}

	// Use ItemStatFormatter like Pip-Boy does
	auto stats = ItemStatFormatter::ParseInfoObj(infoObj);
	return ItemStatFormatter::BuildStatAnnouncement(stats);
}

void ContainerMenuAccessibility::AnnounceItem(const ItemIdentity& a_item, [[maybe_unused]] bool a_containerSide)
{
	// Format: "item name, count, flags, stats" (no side prefix)
	std::string announcement = a_item.BuildBasicAnnouncement();

	// Get item stats from ItemCard
	auto ui = RE::UI::GetSingleton();
	if (ui) {
		auto menu = ui->GetMenu<RE::ContainerMenu>();
		if (menu) {
			std::string stats = GetItemStats(menu.get());
			if (!stats.empty()) {
				announcement += stats;  // BuildStatAnnouncement already includes leading ", "
			}
		}
	}

	SpeakText(announcement);
}

void ContainerMenuAccessibility::AnnounceFocusChange(bool a_containerSide)
{
	// Use localized strings from the game
	std::string sideName = a_containerSide ? LocalizationHelper::Translate("$TransferContainerLabel") : LocalizationHelper::Translate("$TransferPlayerLabel");
	SpeakText(sideName);
}

std::string ContainerMenuAccessibility::GetContainerName(RE::ContainerMenu* a_menu)
{
	if (!a_menu) {
		return "";
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return "";
	}

	// Navigate: ContainerInventory_mc -> ContainerListHeader -> headerText
	Scaleform::GFx::Value containerInv;
	if (!menuObj.GetMember("ContainerInventory_mc", &containerInv) || !containerInv.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value header;
	if (!containerInv.GetMember("ContainerListHeader", &header) || !header.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value headerText;
	if (header.GetMember("headerText", &headerText) && headerText.IsString()) {
		return LocalizationHelper::CleanString(headerText.GetString());
	}

	// Fallback: try textField.text
	Scaleform::GFx::Value textField;
	if (header.GetMember("textField", &textField) && textField.IsObject()) {
		Scaleform::GFx::Value text;
		if (textField.GetMember("text", &text) && text.IsString()) {
			return LocalizationHelper::CleanString(text.GetString());
		}
	}

	return "";
}

void ContainerMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
