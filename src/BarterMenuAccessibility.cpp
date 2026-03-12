#include "BarterMenuAccessibility.h"
#include "ItemStatFormatter.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <F4SE/F4SE.h>

BarterMenuAccessibility* BarterMenuAccessibility::GetSingleton()
{
	static BarterMenuAccessibility singleton;
	return &singleton;
}

void BarterMenuAccessibility::Install()
{
	if (m_installed) {
		return;
	}

	// Hook BarterMenu::AdvanceMovie (vtable index 4)
	// VTABLE::BarterMenu[0] is the primary vtable (IMenu/GameMenuBase)
	static REL::THookVFT<void(RE::BarterMenu*, float, std::uint64_t)> advanceHook(
		VTABLE::BarterMenu[0],
		4,  // AdvanceMovie is at index 4
		HookedAdvanceMovie);

	if (advanceHook.Enable()) {
		s_advanceMovieHook = &advanceHook;
		REX::INFO("BarterMenuAccessibility: AdvanceMovie hook installed successfully");
	} else {
		REX::ERROR("BarterMenuAccessibility: Failed to install AdvanceMovie hook");
	}

	// Hook BarterMenu::OnButtonEvent (BSInputEventUser vtable index 8)
	// VTABLE::BarterMenu[1] is the BSInputEventUser vtable
	static REL::THookVFT<void(RE::BarterMenu*, const RE::ButtonEvent*)> buttonHook(
		VTABLE::BarterMenu[1],
		8,  // OnButtonEvent is at index 8
		HookedOnButtonEvent);

	if (buttonHook.Enable()) {
		s_buttonEventHook = &buttonHook;
		REX::INFO("BarterMenuAccessibility: OnButtonEvent hook installed successfully");
	} else {
		REX::ERROR("BarterMenuAccessibility: Failed to install OnButtonEvent hook");
	}

	m_installed = true;
}

void BarterMenuAccessibility::OnBarterMenuOpened()
{
	REX::INFO("BarterMenuAccessibility: Menu opened");

	// Reset state
	m_menuOpen = true;
	m_openingAnnouncementDone = false;  // Don't announce items until vendor name announced
	m_vendorFocused = true;
	m_lastVendorIndex = -1;
	m_lastPlayerIndex = -1;
	m_lastVendorFilterName.clear();
	m_lastPlayerFilterName.clear();
	m_lastSortLabel.clear();
	m_lastCapsTransfer.clear();
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

			auto menu = ui->GetMenu("BarterMenu"sv);
			if (!menu)
				return;

			auto* barterMenu = static_cast<RE::BarterMenu*>(menu.get());

			// Announce barter opened
			AnnounceBarterOpened(barterMenu);
		});
	}
}

void BarterMenuAccessibility::OnBarterMenuClosed()
{
	REX::INFO("BarterMenuAccessibility: Menu closed");
	m_menuOpen = false;
}

void BarterMenuAccessibility::HookedAdvanceMovie(RE::BarterMenu* a_this, float a_timeDelta, std::uint64_t a_time)
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

void BarterMenuAccessibility::HookedOnButtonEvent(RE::BarterMenu* a_this, const RE::ButtonEvent* a_event)
{
	auto* hook = GetSingleton();

	// Check for Q key to switch sides
	if (a_event && a_event->QJustPressed() && hook->m_menuOpen) {
		auto device = a_event->device.get();
		auto idCode = a_event->QIDCode();

		if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x46) {  // F key
			REX::INFO("BarterMenu: F key pressed, switching sides");
			hook->SwitchSides();
		}

		// H key - announce caps status
		if (device == RE::INPUT_DEVICE::kKeyboard && idCode == 0x48) {  // H key (VK_H)
			hook->AnnounceBarterStatus();
		}
	}

	// Call original OnButtonEvent
	if (s_buttonEventHook) {
		(*s_buttonEventHook)(a_this, a_event);
	}
}

void BarterMenuAccessibility::CheckStateChanges(RE::BarterMenu* a_menu)
{
	if (!a_menu)
		return;

	// Check quantity picker first - suppress other checks while it's open
	CheckQuantityMenu(a_menu);
	if (m_quantityMenuOpen)
		return;

	// Check for focus change (vendor <-> player inventory)
	CheckFocusChange(a_menu);

	// Check for filter/category change (left/right arrows)
	CheckFilterChange(a_menu);

	// Check for sort mode change
	CheckSortChange(a_menu);

	// Check for item selection change
	CheckItemSelection(a_menu);

	// Check for caps transfer change
	CheckCapsChange(a_menu);
}

void BarterMenuAccessibility::SwitchSides()
{
	if (!m_menuOpen) {
		return;
	}

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu<RE::BarterMenu>();
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

	if (m_vendorFocused) {
		// Currently on vendor, switch to player inventory
		args[0] = "LTrigger";
		args[1] = false;
		menuObj.Invoke("ProcessUserEvent", &result, args, 2);
	} else {
		// Currently on player inventory, switch to vendor
		args[0] = "RTrigger";
		args[1] = false;
		menuObj.Invoke("ProcessUserEvent", &result, args, 2);
	}
}

void BarterMenuAccessibility::CheckFocusChange(RE::BarterMenu* a_menu)
{
	bool currentFocus = IsVendorFocused(a_menu);

	if (currentFocus != m_vendorFocused) {
		m_vendorFocused = currentFocus;

		// Announce which side we're now on
		AnnounceFocusChange(m_vendorFocused);

		// Get and announce the first item on the newly focused side
		std::int32_t index = GetSelectedIndex(a_menu, m_vendorFocused);
		if (index >= 0) {
			auto item = GetItemAtIndex(a_menu, index, m_vendorFocused);
			if (item.IsValid()) {
				std::int32_t barterCount = GetBarterCount(a_menu, index, m_vendorFocused);
				AnnounceItem(item, barterCount, m_vendorFocused);

				// Update the tracked index for this side
				if (m_vendorFocused) {
					m_lastVendorIndex = index;
				} else {
					m_lastPlayerIndex = index;
				}
			}
		}
		// Note: Side name already announced above, no need to repeat if list is empty
	}
}

bool BarterMenuAccessibility::IsVendorFocused(RE::BarterMenu* a_menu)
{
	if (!a_menu) {
		return true;  // Default to vendor
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return true;
	}

	Scaleform::GFx::Value containerSelected;
	if (menuObj.GetMember("containerIsSelected", &containerSelected) && containerSelected.IsBoolean()) {
		return containerSelected.GetBoolean();
	}

	return true;  // Default to vendor if we can't read the value
}

void BarterMenuAccessibility::CheckFilterChange(RE::BarterMenu* a_menu)
{
	// Skip until opening announcement is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::string  currentFilterName = GetCurrentFilterName(a_menu, m_vendorFocused);
	std::string& lastFilterName = m_vendorFocused ? m_lastVendorFilterName : m_lastPlayerFilterName;

	if (!currentFilterName.empty() && currentFilterName != lastFilterName) {
		lastFilterName = currentFilterName;
		SpeakText(currentFilterName);
	}
}

std::string BarterMenuAccessibility::GetCurrentFilterName(RE::BarterMenu* a_menu, bool a_vendorSide)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	// Navigate to header text
	// Vendor side: ContainerInventory_mc.ContainerListHeader.headerText
	// Player side: PlayerInventory_mc.PlayerListHeader.headerText
	Scaleform::GFx::Value inventoryMc;
	const char*           invName = a_vendorSide ? "ContainerInventory_mc" : "PlayerInventory_mc";
	if (!menuObj.GetMember(invName, &inventoryMc) || !inventoryMc.IsObject())
		return "";

	const char*           headerName = a_vendorSide ? "ContainerListHeader" : "PlayerListHeader";
	Scaleform::GFx::Value header;
	if (!inventoryMc.GetMember(headerName, &header) || !header.IsObject())
		return "";

	Scaleform::GFx::Value headerText;
	if (!header.GetMember("headerText", &headerText) || !headerText.IsString())
		return "";

	return LocalizationHelper::CleanString(headerText.GetString());
}

void BarterMenuAccessibility::CheckSortChange(RE::BarterMenu* a_menu)
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

std::string BarterMenuAccessibility::GetSortButtonLabel(RE::BarterMenu* a_menu)
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

void BarterMenuAccessibility::CheckItemSelection(RE::BarterMenu* a_menu)
{
	// Skip until opening announcement (vendor name) is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::int32_t currentIndex = GetSelectedIndex(a_menu, m_vendorFocused);

	// Get the last index for the current side
	std::int32_t& lastIndex = m_vendorFocused ? m_lastVendorIndex : m_lastPlayerIndex;

	if (currentIndex >= 0 && currentIndex != lastIndex) {
		lastIndex = currentIndex;

		auto item = GetItemAtIndex(a_menu, currentIndex, m_vendorFocused);
		if (item.IsValid()) {
			std::int32_t barterCount = GetBarterCount(a_menu, currentIndex, m_vendorFocused);
			AnnounceItem(item, barterCount, m_vendorFocused);
		}
	}
}

std::int32_t BarterMenuAccessibility::GetSelectedIndex(RE::BarterMenu* a_menu, bool a_vendorSide)
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
	if (a_vendorSide) {
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

ItemIdentity BarterMenuAccessibility::GetItemAtIndex(RE::BarterMenu* a_menu, std::int32_t a_index, bool a_vendorSide)
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
	if (a_vendorSide) {
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

std::int32_t BarterMenuAccessibility::GetBarterCount(RE::BarterMenu* a_menu, std::int32_t a_index, bool a_vendorSide)
{
	if (!a_menu || a_index < 0) {
		return 0;
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return 0;
	}

	// Get the list object
	Scaleform::GFx::Value listObj;
	if (a_vendorSide) {
		if (!menuObj.GetMember("ContainerList_mc", &listObj) || !listObj.IsObject()) {
			return 0;
		}
	} else {
		Scaleform::GFx::Value playerInv;
		if (!menuObj.GetMember("PlayerInventory_mc", &playerInv) || !playerInv.IsObject()) {
			return 0;
		}
		if (!playerInv.GetMember("PlayerList_mc", &listObj) || !listObj.IsObject()) {
			return 0;
		}
	}

	// Get entryList from the list
	Scaleform::GFx::Value entryList;
	if (!listObj.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return 0;
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return 0;
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return 0;
	}

	// Get barterCount property
	Scaleform::GFx::Value barterCount;
	if (entry.GetMember("barterCount", &barterCount)) {
		if (barterCount.IsInt()) {
			return barterCount.GetInt();
		} else if (barterCount.IsNumber()) {
			return static_cast<std::int32_t>(barterCount.GetNumber());
		} else if (barterCount.IsUInt()) {
			return static_cast<std::int32_t>(barterCount.GetUInt());
		}
	}

	return 0;
}

void BarterMenuAccessibility::CheckQuantityMenu(RE::BarterMenu* a_menu)
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

bool BarterMenuAccessibility::IsQuantityMenuOpen(RE::BarterMenu* a_menu)
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

std::string BarterMenuAccessibility::GetQuantityValue(RE::BarterMenu* a_menu)
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

void BarterMenuAccessibility::CheckCapsChange(RE::BarterMenu* a_menu)
{
	// Skip until opening announcement is done
	if (!m_openingAnnouncementDone) {
		return;
	}

	std::string capsAmount = GetCapsTransferAmount(a_menu);

	if (capsAmount != m_lastCapsTransfer) {
		m_lastCapsTransfer = capsAmount;
		if (!capsAmount.empty()) {
			// Determine direction from arrow visibility
			std::string direction = GetCapsTransferDirection(a_menu);
			SpeakText(direction + capsAmount);
		}
	}
}

std::string BarterMenuAccessibility::GetCapsTransferDirection(RE::BarterMenu* a_menu)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	Scaleform::GFx::Value capsInfo;
	if (!menuObj.GetMember("CapsTransferInfo_mc", &capsInfo) || !capsInfo.IsObject())
		return "";

	// CapsArrowRight_mc visible = player pays vendor (cost)
	// CapsArrowLeft_mc visible = vendor pays player (profit)
	Scaleform::GFx::Value arrow;
	Scaleform::GFx::Value arrowVisible;

	if (capsInfo.GetMember("CapsArrowRight_mc", &arrow) && arrow.IsObject()) {
		if (arrow.GetMember("visible", &arrowVisible) && arrowVisible.IsBoolean() && arrowVisible.GetBoolean()) {
			return "Cost: ";
		}
	}

	if (capsInfo.GetMember("CapsArrowLeft_mc", &arrow) && arrow.IsObject()) {
		if (arrow.GetMember("visible", &arrowVisible) && arrowVisible.IsBoolean() && arrowVisible.GetBoolean()) {
			return "Profit: ";
		}
	}

	return "";
}

std::string BarterMenuAccessibility::GetCapsTransferAmount(RE::BarterMenu* a_menu)
{
	if (!a_menu) {
		return "";
	}

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject()) {
		return "";
	}

	// Navigate: CapsTransferInfo_mc -> TransferCaps_tf -> text
	Scaleform::GFx::Value capsInfo;
	if (!menuObj.GetMember("CapsTransferInfo_mc", &capsInfo) || !capsInfo.IsObject()) {
		return "";
	}

	// Check if visible - caps info only shown when trade pending
	Scaleform::GFx::Value visible;
	if (capsInfo.GetMember("visible", &visible) && visible.IsBoolean()) {
		if (!visible.GetBoolean()) {
			return "";  // Not visible means no pending trade
		}
	}

	Scaleform::GFx::Value transferTf;
	if (!capsInfo.GetMember("TransferCaps_tf", &transferTf) || !transferTf.IsObject()) {
		return "";
	}

	Scaleform::GFx::Value text;
	if (transferTf.GetMember("text", &text) && text.IsString()) {
		return text.GetString();
	}

	return "";
}

std::string BarterMenuAccessibility::GetCaps(RE::BarterMenu* a_menu, bool a_vendorSide)
{
	if (!a_menu)
		return "";

	const auto& menuObj = a_menu->menuObj;
	if (!menuObj.IsObject())
		return "";

	// Vendor caps: ContainerInventory_mc -> VendorCaps_tf -> text
	// Player caps: PlayerInventory_mc -> PlayerCaps_tf -> text
	const char* panelName = a_vendorSide ? "ContainerInventory_mc" : "PlayerInventory_mc";
	const char* tfName = a_vendorSide ? "VendorCaps_tf" : "PlayerCaps_tf";

	Scaleform::GFx::Value panel;
	if (!menuObj.GetMember(panelName, &panel) || !panel.IsObject())
		return "";

	Scaleform::GFx::Value capsTf;
	if (!panel.GetMember(tfName, &capsTf) || !capsTf.IsObject())
		return "";

	Scaleform::GFx::Value text;
	if (capsTf.GetMember("text", &text) && text.IsString()) {
		return text.GetString();
	}

	return "";
}

void BarterMenuAccessibility::AnnounceBarterStatus()
{
	if (!m_menuOpen)
		return;

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu<RE::BarterMenu>();
	if (!menu)
		return;

	std::string announcement;

	// Caps for the focused side
	std::string caps = GetCaps(menu.get(), m_vendorFocused);
	if (!caps.empty()) {
		announcement = caps + " caps";
	}

	// Trade total if there's a pending trade
	std::string transferAmount = GetCapsTransferAmount(menu.get());
	if (!transferAmount.empty()) {
		std::string direction = GetCapsTransferDirection(menu.get());
		if (!announcement.empty())
			announcement += ", ";
		announcement += direction + transferAmount;
	}

	if (!announcement.empty()) {
		SpeakText(announcement);
	}
}

void BarterMenuAccessibility::AnnounceBarterOpened(RE::BarterMenu* a_menu)
{
	std::string vendorName = GetVendorName(a_menu);

	if (vendorName.empty()) {
		vendorName = "Vendor";
	}

	REX::INFO("BarterMenuAccessibility: Vendor name = '{}'", vendorName);

	// Announce vendor name
	SpeakText(vendorName);

	// Announce first item
	std::int32_t index = GetSelectedIndex(a_menu, m_vendorFocused);
	if (index >= 0) {
		auto item = GetItemAtIndex(a_menu, index, m_vendorFocused);
		if (item.IsValid()) {
			std::int32_t barterCount = GetBarterCount(a_menu, index, m_vendorFocused);
			AnnounceItem(item, barterCount, m_vendorFocused);
			if (m_vendorFocused) {
				m_lastVendorIndex = index;
			} else {
				m_lastPlayerIndex = index;
			}
		}
	}

	// Initialize filter/sort/caps state to prevent duplicate announcements
	m_lastVendorFilterName = GetCurrentFilterName(a_menu, true);
	m_lastPlayerFilterName = GetCurrentFilterName(a_menu, false);
	m_lastSortLabel = GetSortButtonLabel(a_menu);
	m_lastCapsTransfer = GetCapsTransferAmount(a_menu);

	// Now allow state change checks to announce changes
	m_openingAnnouncementDone = true;
}

std::string BarterMenuAccessibility::GetItemStats(RE::BarterMenu* a_menu)
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

	// Use ItemStatFormatter like Pip-Boy and ContainerMenu do
	auto stats = ItemStatFormatter::ParseInfoObj(infoObj);
	return ItemStatFormatter::BuildStatAnnouncement(stats);
}

void BarterMenuAccessibility::AnnounceItem(const ItemIdentity& a_item, std::int32_t a_barterCount, [[maybe_unused]] bool a_vendorSide)
{
	// Format: "item name, count, trading X, flags, stats"
	std::string announcement = a_item.BuildBasicAnnouncement();

	// Add barter count if items are marked for trade
	if (a_barterCount != 0) {
		// Positive barterCount = buying from vendor, negative = selling to vendor
		// Just announce the absolute value as "trading X"
		announcement += ", trading " + std::to_string(std::abs(a_barterCount));
	}

	// Get item stats from ItemCard
	auto ui = RE::UI::GetSingleton();
	if (ui) {
		auto menu = ui->GetMenu<RE::BarterMenu>();
		if (menu) {
			std::string stats = GetItemStats(menu.get());
			if (!stats.empty()) {
				announcement += stats;  // BuildStatAnnouncement already includes leading ", "
			}
		}
	}

	SpeakText(announcement);
}

void BarterMenuAccessibility::AnnounceFocusChange(bool a_vendorSide)
{
	// Use localized strings from the game (same as ContainerMenu)
	std::string sideName = a_vendorSide ? LocalizationHelper::Translate("$TransferContainerLabel") : LocalizationHelper::Translate("$TransferPlayerLabel");
	SpeakText(sideName);
}

std::string BarterMenuAccessibility::GetVendorName(RE::BarterMenu* a_menu)
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

void BarterMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
