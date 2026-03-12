#include "PipBoyStatsPage.h"
#include "LocalizationHelper.h"
#include <RE/P/PipboyMenu.h>
#include <cmath>
#include <cstdio>

// Returns localized damage type name using plugin translation keys
static std::string GetLocalizedDamageType(std::int32_t a_type)
{
	const char* key = nullptr;
	switch (a_type) {
		case 1:
			key = "$F4A_dmgtype_ballistic";
			break;
		case 2:
			key = "$F4A_dmgtype_poison";
			break;
		case 3:
			key = "$F4A_dmgtype_fire";
			break;
		case 4:
			key = "$F4A_dmgtype_energy";
			break;
		case 5:
			key = "$F4A_dmgtype_cryo";
			break;
		case 6:
			key = "$F4A_dmgtype_radiation";
			break;
		default:
			return "";
	}
	return LocalizationHelper::Translate(key);
}

void PipBoyStatsPage::OnPageActivated()
{
	REX::INFO("PipBoyStatsPage: Activated");
	m_announced = false;
	m_lastSelection = -1;
	m_lastTab = -1;
	m_statusAnnounced = false;
	m_lastSpecialIndex = -1;
	m_lastPerksIndex = -1;
	m_lastPerksDescription.clear();

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("PipboyMenu"sv);
	if (!menu)
		return;

	auto tab = GetCurrentTab(menu.get());
	if (tab >= 0) {
		m_lastTab = tab;
		m_currentTab = static_cast<Tab>(tab);
		SpeakText(GetLocalizedTabName(tab));

		// For Status tab, announce overview after 2-frame delay for data to populate
		if (m_currentTab == Tab::Status) {
			if (auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([this]() {
					if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
						taskInterface2->AddTask([this]() {
							auto ui2 = RE::UI::GetSingleton();
							if (!ui2)
								return;
							auto menu2 = ui2->GetMenu("PipboyMenu"sv);
							if (!menu2)
								return;
							AnnounceStatusOverview(menu2.get());
						});
					}
				});
			}
		}
	}
}

void PipBoyStatsPage::OnPageDeactivated()
{
	REX::INFO("PipBoyStatsPage: Deactivated");
}

void PipBoyStatsPage::CheckSelection()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("PipboyMenu"sv);
	if (!menu)
		return;

	switch (m_currentTab) {
		case Tab::SPECIAL:
			CheckSpecialSelection(menu.get());
			break;
		case Tab::Perks:
			CheckPerksSelection(menu.get());
			break;
		case Tab::Status:
			// Status tab has no list navigation - stats are read on activation
			break;
	}
}

void PipBoyStatsPage::CheckTabChange()
{
	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("PipboyMenu"sv);
	if (!menu)
		return;

	auto currentTab = GetCurrentTab(menu.get());
	if (currentTab >= 0 && currentTab != m_lastTab) {
		m_lastTab = currentTab;
		m_currentTab = static_cast<Tab>(currentTab);
		m_lastSpecialIndex = -1;
		m_lastPerksIndex = -1;
		m_lastPerksDescription.clear();
		m_statusAnnounced = false;
		SpeakText(GetLocalizedTabName(currentTab));

		// For Status tab, announce overview after 2-frame delay
		if (m_currentTab == Tab::Status) {
			if (auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([this]() {
					if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
						taskInterface2->AddTask([this]() {
							auto ui2 = RE::UI::GetSingleton();
							if (!ui2)
								return;
							auto menu2 = ui2->GetMenu("PipboyMenu"sv);
							if (!menu2)
								return;
							AnnounceStatusOverview(menu2.get());
						});
					}
				});
			}
		}
	}
}

void PipBoyStatsPage::Reset()
{
	m_announced = false;
	m_lastSelection = -1;
	m_lastTab = -1;
	m_currentTab = Tab::Status;
	m_statusAnnounced = false;
	m_lastSpecialIndex = -1;
	m_lastPerksIndex = -1;
	m_lastPerksDescription.clear();
}

std::string PipBoyStatsPage::GetLocalizedTabName(std::int32_t a_tabIndex)
{
	if (a_tabIndex >= 0 && a_tabIndex < kTabCount) {
		return LocalizationHelper::Translate(kTabKeys[a_tabIndex]);
	}
	return "Unknown";
}

// ============================================================================
// Status Tab
// ============================================================================

void PipBoyStatsPage::AnnounceStatusOverview(RE::IMenu* a_menu)
{
	if (m_statusAnnounced)
		return;
	m_statusAnnounced = true;

	const auto* dataObj = GetDataObj(a_menu);
	if (!dataObj)
		return;

	std::string announcement;

	// HP
	double currHP = GetNumberMember(*dataObj, "CurrHP");
	double maxHP = GetNumberMember(*dataObj, "MaxHP");
	if (maxHP > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), "HP %.0f of %.0f", currHP, maxHP);
		announcement += buf;
	}

	// AP
	double currAP = GetNumberMember(*dataObj, "CurrAP");
	double maxAP = GetNumberMember(*dataObj, "MaxAP");
	if (maxAP > 0) {
		char buf[64];
		snprintf(buf, sizeof(buf), ", AP %.0f of %.0f", currAP, maxAP);
		announcement += buf;
	}

	if (!announcement.empty()) {
		SpeakText(announcement);
	}

	// Limb conditions - always announce
	struct LimbInfo
	{
		const char* memberName;
		const char* displayName;
	};
	static const LimbInfo kLimbs[] = {
		{ "HeadCondition", "Head" },
		{ "TorsoCondition", "Torso" },
		{ "LArmCondition", "Left Arm" },
		{ "RArmCondition", "Right Arm" },
		{ "LLegCondition", "Left Leg" },
		{ "RLegCondition", "Right Leg" },
	};

	std::string limbAnnouncement;
	bool        anyDamaged = false;
	int         limbsRead = 0;
	for (const auto& limb : kLimbs) {
		double condition = GetNumberMember(*dataObj, limb.memberName, -1.0);
		if (condition < 0)
			continue;  // Member doesn't exist
		limbsRead++;

		if (condition < 100.0) {
			anyDamaged = true;
		}

		char buf[64];
		snprintf(buf, sizeof(buf), "%s %.0f%%", limb.displayName, condition);
		if (!limbAnnouncement.empty()) {
			limbAnnouncement += ", ";
		}
		limbAnnouncement += buf;
	}

	if (limbsRead > 0) {
		if (anyDamaged) {
			SpeakText("Limbs: " + limbAnnouncement);
		} else {
			SpeakText("All limbs healthy");
		}
	}

	// Stimpak and RadAway counts
	std::int32_t stimpaks = GetIntMember(*dataObj, "StimpakCount");
	std::int32_t radaway = GetIntMember(*dataObj, "RadawayCount");
	if (stimpaks > 0 || radaway > 0) {
		std::string supplies;
		if (stimpaks > 0) {
			supplies += std::to_string(stimpaks) + " Stimpak";
			if (stimpaks != 1)
				supplies += "s";
		}
		if (radaway > 0) {
			if (!supplies.empty())
				supplies += ", ";
			supplies += std::to_string(radaway) + " RadAway";
		}
		SpeakText(supplies);
	}

	// Damage and resistance totals - using localized damage type names
	Scaleform::GFx::Value totalDamages;
	if (dataObj->GetMember("TotalDamages", &totalDamages) && totalDamages.IsArray()) {
		std::string   dmgStr;
		std::uint32_t count = totalDamages.GetArraySize();
		for (std::uint32_t i = 0; i < count && i < 6; i++) {
			Scaleform::GFx::Value entry;
			if (totalDamages.GetElement(i, &entry) && entry.IsObject()) {
				double value = GetNumberMember(entry, "value");
				if (value > 0) {
					std::int32_t type = GetIntMember(entry, "type", static_cast<std::int32_t>(i + 1));
					std::string  typeName = GetLocalizedDamageType(type);
					if (typeName.empty())
						typeName = "Unknown";
					char buf[64];
					snprintf(buf, sizeof(buf), "%s %.0f", typeName.c_str(), value);
					if (!dmgStr.empty())
						dmgStr += ", ";
					dmgStr += buf;
				}
			}
		}
		if (!dmgStr.empty()) {
			SpeakText("Damage: " + dmgStr);
		}
	}

	Scaleform::GFx::Value totalResists;
	if (dataObj->GetMember("TotalResists", &totalResists) && totalResists.IsArray()) {
		std::string   resStr;
		std::uint32_t count = totalResists.GetArraySize();
		for (std::uint32_t i = 0; i < count && i < 6; i++) {
			Scaleform::GFx::Value entry;
			if (totalResists.GetElement(i, &entry) && entry.IsObject()) {
				double value = GetNumberMember(entry, "value");
				if (value > 0) {
					std::int32_t type = GetIntMember(entry, "type", static_cast<std::int32_t>(i + 1));
					std::string  typeName = GetLocalizedDamageType(type);
					if (typeName.empty())
						typeName = "Unknown";
					char buf[64];
					snprintf(buf, sizeof(buf), "%s %.0f", typeName.c_str(), value);
					if (!resStr.empty())
						resStr += ", ";
					resStr += buf;
				}
			}
		}
		if (!resStr.empty()) {
			SpeakText("Resistance: " + resStr);
		}
	}

	// Active effects
	Scaleform::GFx::Value activeEffects;
	if (dataObj->GetMember("ActiveEffects", &activeEffects) && activeEffects.IsArray()) {
		std::uint32_t count = activeEffects.GetArraySize();
		if (count > 0) {
			std::string effectsStr;
			for (std::uint32_t i = 0; i < count; i++) {
				Scaleform::GFx::Value group;
				if (!activeEffects.GetElement(i, &group) || !group.IsObject())
					continue;

				std::string sourceName = GetStringMember(group, "text");

				Scaleform::GFx::Value effects;
				if (!group.GetMember("effects", &effects) || !effects.IsArray())
					continue;

				std::uint32_t effectCount = effects.GetArraySize();
				for (std::uint32_t j = 0; j < effectCount; j++) {
					Scaleform::GFx::Value effect;
					if (!effects.GetElement(j, &effect) || !effect.IsObject())
						continue;

					std::string effectName = GetStringMember(effect, "text");
					if (effectName.empty())
						continue;

					if (!effectsStr.empty())
						effectsStr += ". ";

					Scaleform::GFx::Value usesCustomDesc;
					bool                  isCustom = false;
					if (effect.GetMember("usesCustomDesc", &usesCustomDesc) && usesCustomDesc.IsBoolean()) {
						isCustom = usesCustomDesc.GetBoolean();
					}

					if (isCustom) {
						effectsStr += effectName;
					} else {
						double                value = GetNumberMember(effect, "value");
						Scaleform::GFx::Value showPct;
						bool                  asPercent = false;
						if (effect.GetMember("showAsPercent", &showPct) && showPct.IsBoolean()) {
							asPercent = showPct.GetBoolean();
						}

						char buf[128];
						if (asPercent) {
							snprintf(buf, sizeof(buf), "%s %+.0f%%", effectName.c_str(), value);
						} else if (value != 0.0) {
							snprintf(buf, sizeof(buf), "%s %+.0f", effectName.c_str(), value);
						} else {
							snprintf(buf, sizeof(buf), "%s", effectName.c_str());
						}
						effectsStr += buf;
					}

					if (!sourceName.empty()) {
						effectsStr += " from " + sourceName;
					}
				}
			}
			if (!effectsStr.empty()) {
				SpeakText("Effects: " + effectsStr);
			}
		}
	}
}

// ============================================================================
// SPECIAL Tab
// ============================================================================

void PipBoyStatsPage::CheckSpecialSelection(RE::IMenu* a_menu)
{
	std::int32_t selectedIndex = GetListSelectedIndex(a_menu, "SPECIALTab_mc");
	if (selectedIndex < 0 || selectedIndex == m_lastSpecialIndex)
		return;
	m_lastSpecialIndex = selectedIndex;

	// Read the entry from SPECIALList in dataObj
	const auto* dataObj = GetDataObj(a_menu);
	if (!dataObj)
		return;

	Scaleform::GFx::Value specialList;
	if (!dataObj->GetMember("SPECIALList", &specialList) || !specialList.IsArray())
		return;

	std::uint32_t count = specialList.GetArraySize();
	if (static_cast<std::uint32_t>(selectedIndex) >= count)
		return;

	Scaleform::GFx::Value entry;
	if (specialList.GetElement(static_cast<std::uint32_t>(selectedIndex), &entry) && entry.IsObject()) {
		AnnounceSpecialStat(entry);
	}
}

void PipBoyStatsPage::AnnounceSpecialStat(const Scaleform::GFx::Value& a_entry)
{
	std::string name = GetStringMember(a_entry, "text");
	if (name.empty())
		return;

	double value = GetNumberMember(a_entry, "value");
	double modifier = GetNumberMember(a_entry, "modifier");

	char buf[128];
	if (modifier != 0.0) {
		snprintf(buf, sizeof(buf), "%s %.0f (%+.0f)", name.c_str(), value, modifier);
	} else {
		snprintf(buf, sizeof(buf), "%s %.0f", name.c_str(), value);
	}

	std::string announcement(buf);

	// Also announce description
	std::string desc = GetStringMember(a_entry, "description");
	if (!desc.empty()) {
		announcement += ". " + desc;
	}

	SpeakText(announcement);
}

// ============================================================================
// Perks Tab
// ============================================================================

void PipBoyStatsPage::CheckPerksSelection(RE::IMenu* a_menu)
{
	std::int32_t selectedIndex = GetListSelectedIndex(a_menu, "PerksTab_mc");

	// Handle perk selection changes
	if (selectedIndex >= 0 && selectedIndex != m_lastPerksIndex) {
		m_lastPerksIndex = selectedIndex;
		m_lastPerksDescription.clear();

		Scaleform::GFx::Value entry;
		if (GetListSelectedEntry(a_menu, "PerksTab_mc", &entry)) {
			AnnouncePerk(entry);
		}
		return;
	}

	// Check for rank changes (Ctrl/Alt switch ranks without changing selection).
	// The only readable indicator is Description_tf.text on the PerksTab_mc.
	if (selectedIndex >= 0 && selectedIndex == m_lastPerksIndex) {
		std::string currentDesc = GetPerksDescriptionText(a_menu);
		if (currentDesc.empty())
			return;

		if (m_lastPerksDescription.empty()) {
			// First read after selection change - store baseline, don't announce
			m_lastPerksDescription = currentDesc;
			return;
		}

		if (currentDesc != m_lastPerksDescription) {
			m_lastPerksDescription = currentDesc;
			AnnouncePerksRankChange(a_menu, currentDesc);
		}
	}
}

void PipBoyStatsPage::AnnouncePerk(const Scaleform::GFx::Value& a_entry)
{
	std::string name = GetStringMember(a_entry, "text");
	if (name.empty())
		return;

	std::int32_t rank = GetIntMember(a_entry, "rank", 1);
	std::int32_t maxRank = GetIntMember(a_entry, "maxRank", 1);

	std::string announcement = name;
	if (maxRank > 1) {
		announcement += ", rank " + std::to_string(rank) + " of " + std::to_string(maxRank);
	}

	// Read description for current rank
	Scaleform::GFx::Value descriptions;
	if (a_entry.GetMember("descriptions", &descriptions) && descriptions.IsArray()) {
		std::uint32_t descCount = descriptions.GetArraySize();
		// Description index = rank - 1 (rank 1 = index 0)
		std::uint32_t descIndex = (rank > 0) ? static_cast<std::uint32_t>(rank - 1) : 0;
		if (descIndex < descCount) {
			Scaleform::GFx::Value descVal;
			if (descriptions.GetElement(descIndex, &descVal) && descVal.IsString()) {
				std::string desc = descVal.GetString();
				if (!desc.empty()) {
					announcement += ". " + desc;
				}
			}
		}
	}

	SpeakText(announcement);
}

std::string PipBoyStatsPage::GetPerksDescriptionText(RE::IMenu* a_menu)
{
	const auto* subMenuObj = GetActiveSubMenuObj(a_menu);
	if (!subMenuObj || !subMenuObj->IsObject())
		return "";

	// Find PerksTab_mc → Description_tf → text
	Scaleform::GFx::Value tabMc;
	if (!GetNamedChild(*subMenuObj, "PerksTab_mc", &tabMc))
		return "";

	Scaleform::GFx::Value descTf;
	if (!GetNamedChild(tabMc, "Description_tf", &descTf))
		return "";

	Scaleform::GFx::Value textVal;
	if (descTf.GetMember("text", &textVal) && textVal.IsString()) {
		return textVal.GetString();
	}
	return "";
}

void PipBoyStatsPage::AnnouncePerksRankChange(RE::IMenu* a_menu, const std::string& a_currentDesc)
{
	Scaleform::GFx::Value entry;
	if (!GetListSelectedEntry(a_menu, "PerksTab_mc", &entry))
		return;

	std::int32_t maxRank = GetIntMember(entry, "maxRank", 1);

	// Determine which rank is being viewed by matching description against the array
	std::int32_t          viewingRank = -1;
	Scaleform::GFx::Value descriptions;
	if (entry.GetMember("descriptions", &descriptions) && descriptions.IsArray()) {
		std::uint32_t descCount = descriptions.GetArraySize();
		for (std::uint32_t i = 0; i < descCount; i++) {
			Scaleform::GFx::Value descVal;
			if (descriptions.GetElement(i, &descVal) && descVal.IsString()) {
				if (a_currentDesc == descVal.GetString()) {
					viewingRank = static_cast<std::int32_t>(i + 1);
					break;
				}
			}
		}
	}

	std::string announcement;
	if (viewingRank > 0 && maxRank > 1) {
		announcement = "Rank " + std::to_string(viewingRank) + " of " + std::to_string(maxRank) + ": " + a_currentDesc;
	} else {
		announcement = a_currentDesc;
	}

	SpeakText(announcement);
}

// ============================================================================
// Sub-menu access helpers
// ============================================================================

const Scaleform::GFx::Value* PipBoyStatsPage::GetActiveSubMenuObj(RE::IMenu* a_menu)
{
	if (!a_menu)
		return nullptr;

	auto* pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);

	// Each STAT sub-tab has its own PipboySubMenu with a menuObj reference
	const Scaleform::GFx::Value* menuObj = nullptr;
	switch (m_currentTab) {
		case Tab::Status:
			menuObj = pipboyMenu->statsMenuObj.menuObj;
			break;
		case Tab::SPECIAL:
			menuObj = pipboyMenu->specialMenuObj.menuObj;
			break;
		case Tab::Perks:
			menuObj = pipboyMenu->perksMenuObj.menuObj;
			break;
	}

	return menuObj;
}

std::int32_t PipBoyStatsPage::GetListSelectedIndex(RE::IMenu* a_menu, const char* a_tabName, const char* a_listName)
{
	Scaleform::GFx::Value listMc;

	const auto* subMenuObj = GetActiveSubMenuObj(a_menu);
	if (subMenuObj && subMenuObj->IsObject()) {
		// Try 1: Scoped search - find tab container first, then list within it.
		// Prevents finding the wrong tab's List_mc when multiple tabs share the same menuObj root.
		if (a_tabName && a_tabName[0] != '\0') {
			Scaleform::GFx::Value tabMc;
			if (GetNamedChild(*subMenuObj, a_tabName, &tabMc)) {
				if (GetNamedChild(tabMc, a_listName, &listMc)) {
					goto readIndex;
				}
			}
		}

		// Try 2: Direct search (works when menuObj points to the tab itself)
		if (GetNamedChild(*subMenuObj, a_listName, &listMc)) {
			goto readIndex;
		}
	}

	// Try 3: movie path fallback
	if (!GetListViaMovie(a_menu, a_tabName, a_listName, &listMc)) {
		return -1;
	}

readIndex:
	Scaleform::GFx::Value selVal;
	if (!listMc.GetMember("selectedIndex", &selVal)) {
		return -1;
	}

	if (selVal.IsInt())
		return selVal.GetInt();
	if (selVal.IsNumber())
		return static_cast<std::int32_t>(selVal.GetNumber());
	if (selVal.IsUInt())
		return static_cast<std::int32_t>(selVal.GetUInt());

	return -1;
}

bool PipBoyStatsPage::GetListSelectedEntry(RE::IMenu* a_menu, const char* a_tabName, Scaleform::GFx::Value* a_outEntry, const char* a_listName)
{
	if (!a_outEntry)
		return false;

	Scaleform::GFx::Value listMc;

	const auto* subMenuObj = GetActiveSubMenuObj(a_menu);
	if (subMenuObj && subMenuObj->IsObject()) {
		// Try 1: Scoped search - find tab container first, then list within it
		if (a_tabName && a_tabName[0] != '\0') {
			Scaleform::GFx::Value tabMc;
			if (GetNamedChild(*subMenuObj, a_tabName, &tabMc)) {
				if (GetNamedChild(tabMc, a_listName, &listMc)) {
					goto readEntry;
				}
			}
		}

		// Try 2: Direct search
		if (GetNamedChild(*subMenuObj, a_listName, &listMc)) {
			goto readEntry;
		}
	}

	// Try 3: movie path fallback
	if (!GetListViaMovie(a_menu, a_tabName, a_listName, &listMc)) {
		return false;
	}

readEntry:
	if (listMc.GetMember("selectedEntry", a_outEntry) && a_outEntry->IsObject()) {
		return true;
	}

	return false;
}

// ============================================================================
// Value extraction helpers
// ============================================================================

double PipBoyStatsPage::GetNumberMember(const Scaleform::GFx::Value& a_obj, const char* a_name, double a_default)
{
	Scaleform::GFx::Value val;
	if (a_obj.GetMember(a_name, &val)) {
		if (val.IsNumber())
			return val.GetNumber();
		if (val.IsInt())
			return static_cast<double>(val.GetInt());
		if (val.IsUInt())
			return static_cast<double>(val.GetUInt());
	}
	return a_default;
}

std::int32_t PipBoyStatsPage::GetIntMember(const Scaleform::GFx::Value& a_obj, const char* a_name, std::int32_t a_default)
{
	Scaleform::GFx::Value val;
	if (a_obj.GetMember(a_name, &val)) {
		if (val.IsInt())
			return val.GetInt();
		if (val.IsNumber())
			return static_cast<std::int32_t>(val.GetNumber());
		if (val.IsUInt())
			return static_cast<std::int32_t>(val.GetUInt());
	}
	return a_default;
}

std::string PipBoyStatsPage::GetStringMember(const Scaleform::GFx::Value& a_obj, const char* a_name)
{
	Scaleform::GFx::Value val;
	if (a_obj.GetMember(a_name, &val) && val.IsString()) {
		return val.GetString();
	}
	return "";
}
