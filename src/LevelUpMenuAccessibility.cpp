#include "LevelUpMenuAccessibility.h"
#include "LevelUpMenuHook.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <RE/P/PipboyMenu.h>
#include <RE/P/PlayerCharacter.h>
#include <Scaleform/G/GFx_Value.h>

using Scaleform::GFx::Value;

LevelUpMenuAccessibility* LevelUpMenuAccessibility::GetSingleton()
{
	static LevelUpMenuAccessibility singleton;
	return &singleton;
}

void LevelUpMenuAccessibility::OnLevelUpMenuOpened()
{
	m_isOpen = true;
	m_menuAnnounced = false;
	m_lastRow = 0xFFFFFFFF;
	m_lastCol = 0xFFFFFFFF;
	m_prkfView = 0;
	m_prkfKey.clear();
	m_prkfPrimed = false;

	REX::INFO("LevelUpMenuAccessibility: Menu opened");

	// Install and reset the hook
	LevelUpMenuHook::GetSingleton()->Install();
	LevelUpMenuHook::GetSingleton()->ResetState();
	LevelUpMenuHook::GetSingleton()->SetMenuOpen(true);

	// Schedule menu announcement for after Scaleform populates
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		// Wait a few frames for data to be ready
		taskInterface->AddTask([this]() {
			if (auto* taskInterface2 = F4SE::GetTaskInterface()) {
				taskInterface2->AddTask([this]() {
					if (auto* taskInterface3 = F4SE::GetTaskInterface()) {
						taskInterface3->AddTask([this]() {
							if (!m_isOpen)
								return;

							auto* ui = RE::UI::GetSingleton();
							if (!ui)
								return;

							auto menu = ui->GetMenu("LevelUpMenu"sv);
							if (!menu || !menu->uiMovie)
								return;

							std::string announcement = BuildMenuOpenAnnouncement(menu.get());
							if (!announcement.empty()) {
								SpeakText(announcement);
								m_menuAnnounced = true;
							}
						});
					}
				});
			}
		});
	}
}

void LevelUpMenuAccessibility::OnLevelUpMenuClosed()
{
	m_isOpen = false;
	LevelUpMenuHook::GetSingleton()->SetMenuOpen(false);
	Reset();
	REX::INFO("LevelUpMenuAccessibility: Menu closed");
}

void LevelUpMenuAccessibility::Reset()
{
	m_menuAnnounced = false;
	m_lastRow = 0xFFFFFFFF;
	m_lastCol = 0xFFFFFFFF;
	m_prkfView = 0;
	m_prkfKey.clear();
	m_prkfPrimed = false;
}

void LevelUpMenuAccessibility::AnnounceSelection(std::uint32_t a_row, std::uint32_t a_col)
{
	if (!m_isOpen)
		return;

	// Note: Duplicate check is now handled by LevelUpMenuHook which tracks row/col/rankOffset
	m_lastRow = a_row;
	m_lastCol = a_col;

	auto* ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("LevelUpMenu"sv);
	if (!menu || !menu->uiMovie)
		return;

	std::string announcement = BuildSelectionAnnouncement(menu.get(), a_row, a_col);
	if (!announcement.empty()) {
		SpeakText(announcement);
	}
}

void LevelUpMenuAccessibility::AnnounceConfirmation(bool a_confirming)
{
	if (!m_isOpen)
		return;

	if (a_confirming) {
		SpeakText(LocalizationHelper::Translate("$F4A_confirming_perk"));
	}
}

bool LevelUpMenuAccessibility::AnnounceLevelAndXP()
{
	if (!m_isOpen)
		return false;

	auto* ui = RE::UI::GetSingleton();
	if (!ui)
		return false;

	// LevelUpMenu's AS3 doesn't expose player level at any stable Scaleform
	// path — it sets XPMeterHolder_mc.textField.text to "$$LEVEL N" via
	// BGSCodeObj.GetXPInfo() and nothing else. Read from the C++ API so we
	// get a correct value whether the user opened the Perk Chart from the
	// Stats page, the Map page, or any other Pip-Boy tab.
	auto* player = RE::PlayerCharacter::GetSingleton();
	int   level = player ? static_cast<int>(player->GetLevel()) : 0;
	if (level <= 0)
		return false;

	// LevelUpMenu has no reachable XP% — GhostBarMeter stores _Value/_MaxValue
	// as AS3 private. The Pip-Boy menu, however, is usually still live in
	// memory while LevelUpMenu is on top (user enters perk chart from the
	// Pipboy Stats page). Piggyback its dataObj.XPProgressPct — same source
	// the on-screen XP meter renders from.
	std::int32_t xpPercent = -1;
	if (auto pipboy = ui->GetMenu("PipboyMenu"sv)) {
		auto*       pipboyMenu = static_cast<RE::PipboyMenu*>(pipboy.get());
		const auto& dataObj = pipboyMenu->dataObj;
		if (dataObj.IsObject()) {
			Value pctValue;
			if (dataObj.GetMember("XPProgressPct", &pctValue) && pctValue.IsNumber()) {
				// Same formula as LoadingMenuHook::GetXPProgressPercent,
				// just with the ratio pre-normalized by the game.
				xpPercent = static_cast<std::int32_t>(pctValue.GetNumber() * 100.0);
			}
		}
	}

	std::string announcement = LocalizationHelper::Translate("$F4A_level") + " " + std::to_string(level);
	if (xpPercent >= 0) {
		announcement += ", " + std::to_string(xpPercent) + "% to next level";
	}

	SpeakText(announcement);
	return true;
}

std::string LevelUpMenuAccessibility::BuildMenuOpenAnnouncement(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	// Fallout London (PRKF) variant: list-based screen with a different layout.
	// Announce the points-available header + count, then the first focused item.
	{
		Value perkList;
		if (a_menu->uiMovie->GetVariable(&perkList, PRKF_PERKLIST) && (perkList.IsObject() || perkList.IsDisplayObject())) {
			std::string announcement = LocalizationHelper::Translate("$F4A_levelup");

			std::string header = CleanForSpeech(ReadStringVar(a_menu, PRKF_HEADER));
			std::string count = CleanForSpeech(ReadStringVar(a_menu, PRKF_PERKCOUNT));
			if (!count.empty() && !header.empty()) {
				announcement += ", " + count + " " + header;
			} else if (!header.empty()) {
				announcement += ", " + header;
			}

			std::string  listPath;
			std::int32_t index = -1;
			PRKFView     view = DeterminePRKFView(a_menu, listPath, index);
			std::string  item = BuildPRKFItem(a_menu, view, listPath, index);
			if (!item.empty()) {
				announcement += ". " + item;
			}
			return announcement;
		}
	}

	std::string announcement = LocalizationHelper::Translate("$F4A_levelup");

	// Get player level
	Value levelValue;
	if (a_menu->uiMovie->GetVariable(&levelValue, PLAYER_LEVEL_PATH)) {
		int level = 0;
		if (levelValue.IsUInt()) {
			level = static_cast<int>(levelValue.GetUInt());
		} else if (levelValue.IsInt()) {
			level = levelValue.GetInt();
		} else if (levelValue.IsNumber()) {
			level = static_cast<int>(levelValue.GetNumber());
		}

		if (level > 0) {
			announcement += ", " + LocalizationHelper::Translate("$F4A_level") + " " + std::to_string(level);
		}
	}

	// Get perk points available
	Value perkPointsValue;
	if (a_menu->uiMovie->GetVariable(&perkPointsValue, PERK_POINTS_PATH)) {
		int perkPoints = 0;
		if (perkPointsValue.IsUInt()) {
			perkPoints = static_cast<int>(perkPointsValue.GetUInt());
		} else if (perkPointsValue.IsInt()) {
			perkPoints = perkPointsValue.GetInt();
		} else if (perkPointsValue.IsNumber()) {
			perkPoints = static_cast<int>(perkPointsValue.GetNumber());
		}

		if (perkPoints > 0) {
			std::string perkPointStr = (perkPoints == 1) ? LocalizationHelper::Translate("$F4A_perk_point") : LocalizationHelper::Translate("$F4A_perk_points");
			announcement += ", " + std::to_string(perkPoints) + " " + perkPointStr;
		}
	}

	return announcement;
}

std::string LevelUpMenuAccessibility::BuildSelectionAnnouncement(RE::IMenu* a_menu, std::uint32_t /*a_row*/, std::uint32_t a_col)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	std::string announcement;

	// Get SelectedPerkEntry object which contains perk data
	Value selectedPerk;
	if (!a_menu->uiMovie->GetVariable(&selectedPerk, SELECTED_PERK_PATH) || !selectedPerk.IsObject()) {
		// If no selected perk, just announce position
		announcement = GetColumnName(a_col);
		announcement += " " + LocalizationHelper::Translate("$F4A_column");
		return announcement;
	}

	// Get perk name
	Value textValue;
	if (selectedPerk.GetMember("text", &textValue) && textValue.IsString()) {
		const char* perkName = textValue.GetString();
		if (perkName && perkName[0] != '\0') {
			announcement = perkName;
		}
	}

	// Add column name (SPECIAL stat)
	announcement += ", " + std::string(GetColumnName(a_col)) + " " + LocalizationHelper::Translate("$F4A_column");

	// Get rank info by reading star states
	RankInfo rankInfo = GetRankInfoFromStars(a_menu);

	if (rankInfo.maxRank > 1) {
		// Multi-rank perk - show rank info
		// Note: We can't reliably track viewed rank when using Alt/Ctrl
		// So we show owned count instead of trying to say if current rank is owned
		announcement += ", " + std::to_string(rankInfo.ownedRanks) + " " +
		                LocalizationHelper::Translate("$F4A_of") + " " +
		                std::to_string(rankInfo.maxRank) + " " +
		                LocalizationHelper::Translate("$F4A_ranks_owned");
	} else if (rankInfo.maxRank == 1) {
		// Single-rank perk - check if owned
		Value ownedValue;
		if (selectedPerk.GetMember("owned", &ownedValue) && ownedValue.IsBoolean()) {
			if (ownedValue.GetBoolean()) {
				announcement += ", " + LocalizationHelper::Translate("$F4A_owned");
			}
		}
	} else {
		// Fallback if no stars found - use perk entry data
		Value ownedValue;
		if (selectedPerk.GetMember("owned", &ownedValue) && ownedValue.IsBoolean()) {
			if (ownedValue.GetBoolean()) {
				announcement += ", " + LocalizationHelper::Translate("$F4A_owned");
			}
		}
	}

	// Check if available (can acquire next rank now)
	Value availableValue;
	if (selectedPerk.GetMember("available", &availableValue) && availableValue.IsBoolean()) {
		if (!availableValue.GetBoolean()) {
			announcement += ", " + LocalizationHelper::Translate("$F4A_unavailable");
		}
	}

	// Get requirements text (shows "Requires: X" for the current rank)
	Value reqTextValue;
	if (a_menu->uiMovie->GetVariable(&reqTextValue, REQUIREMENTS_TEXT_PATH) && reqTextValue.IsString()) {
		const char* req = reqTextValue.GetString();
		if (req && req[0] != '\0' && req[0] != ' ') {
			// Requirements text exists and is not empty/whitespace
			announcement += ". " + std::string(req);
		}
	}

	// Get description from the text field (updated when viewing different ranks via Alt/Ctrl)
	// Fall back to selectedPerk.description if text field is empty
	std::string descStr;

	Value descTextValue;
	if (a_menu->uiMovie->GetVariable(&descTextValue, DESCRIPTION_TEXT_PATH) && descTextValue.IsString()) {
		const char* desc = descTextValue.GetString();
		if (desc && desc[0] != '\0') {
			descStr = desc;
		}
	}

	// Fallback to perk entry description if text field is empty
	if (descStr.empty()) {
		Value descValue;
		if (selectedPerk.GetMember("description", &descValue) && descValue.IsString()) {
			const char* desc = descValue.GetString();
			if (desc && desc[0] != '\0') {
				descStr = desc;
			}
		}
	}

	if (!descStr.empty()) {
		// Replace newlines with periods for better speech
		for (size_t i = 0; i < descStr.size(); ++i) {
			if (descStr[i] == '\n') {
				descStr[i] = '.';
				// Add space after period if not at end
				if (i + 1 < descStr.size() && descStr[i + 1] != ' ') {
					descStr.insert(i + 1, " ");
				}
			}
		}
		announcement += ". " + descStr;
	}

	return announcement;
}

const char* LevelUpMenuAccessibility::GetColumnName(std::uint32_t a_col)
{
	// Use game's ActorValue singleton to get localized SPECIAL names
	RE::ActorValue* av = RE::ActorValue::GetSingleton();
	if (av) {
		RE::ActorValueInfo* info = nullptr;
		switch (a_col) {
			case 0:
				info = av->strength;
				break;
			case 1:
				info = av->perception;
				break;
			case 2:
				info = av->endurance;
				break;
			case 3:
				info = av->charisma;
				break;
			case 4:
				info = av->intelligence;
				break;
			case 5:
				info = av->agility;
				break;
			case 6:
				info = av->luck;
				break;
			default:
				break;
		}
		if (info) {
			const char* name = info->GetFullName();
			if (name && name[0] != '\0') {
				return name;
			}
		}
	}
	// Fallback to hardcoded names if lookup fails
	if (a_col < 7) {
		return SPECIAL_NAMES[a_col];
	}
	return "Unknown";
}

LevelUpMenuAccessibility::RankInfo LevelUpMenuAccessibility::GetRankInfoFromStars(RE::IMenu* a_menu)
{
	RankInfo info;
	if (!a_menu || !a_menu->uiMovie)
		return info;

	// Get StarHolder_mc
	Value starHolder;
	if (!a_menu->uiMovie->GetVariable(&starHolder, STAR_HOLDER_PATH) || !starHolder.IsObject()) {
		return info;
	}

	// Get number of stars (children)
	Value numChildrenValue;
	if (!starHolder.GetMember("numChildren", &numChildrenValue)) {
		return info;
	}

	int numChildren = 0;
	if (numChildrenValue.IsUInt()) {
		numChildren = static_cast<int>(numChildrenValue.GetUInt());
	} else if (numChildrenValue.IsInt()) {
		numChildren = numChildrenValue.GetInt();
	} else if (numChildrenValue.IsNumber()) {
		numChildren = static_cast<int>(numChildrenValue.GetNumber());
	}

	if (numChildren <= 0) {
		return info;
	}

	info.maxRank = numChildren;

	// Iterate through each star to find states
	// Star labels: "Full" = owned, "Available" = next to purchase, "Empty" = future
	// Note: "Available" marks the NEXT rank to purchase, NOT the currently viewed rank
	// When user presses Alt/Ctrl, star labels don't change - only description changes
	for (int i = 0; i < numChildren; ++i) {
		// Call getChildAt(i) to get the star MovieClip
		Value star;
		Value args[1];
		args[0] = static_cast<double>(i);

		if (!starHolder.Invoke("getChildAt", &star, args, 1) || !star.IsObject()) {
			continue;
		}

		// Read currentFrameLabel to get the star state
		Value frameLabelValue;
		if (!star.GetMember("currentFrameLabel", &frameLabelValue) || !frameLabelValue.IsString()) {
			continue;
		}

		const char* frameLabel = frameLabelValue.GetString();
		if (!frameLabel)
			continue;

		std::string label = frameLabel;

		if (label == "Full") {
			info.ownedRanks++;
		} else if (label == "Available") {
			// "Available" marks the next rank to purchase (ownedRanks + 1)
			// This is NOT necessarily the viewed rank when using Alt/Ctrl
			info.viewedRank = i + 1;
		}
	}

	// We can reliably determine:
	// - maxRank: total number of stars
	// - ownedRanks: count of "Full" stars
	// We CANNOT reliably determine the viewed rank when using Alt/Ctrl
	// because star labels don't change - only description changes

	// If "Available" star found, viewedRank is set to that (next purchasable rank)
	// If no "Available" (all owned or none owned), estimate from owned count
	if (info.viewedRank == 0) {
		if (info.ownedRanks >= info.maxRank) {
			// All ranks owned - default to max rank
			info.viewedRank = info.maxRank;
		} else if (info.ownedRanks > 0) {
			// Some owned - next rank would be ownedRanks + 1
			info.viewedRank = info.ownedRanks + 1;
		} else {
			// None owned - first rank
			info.viewedRank = 1;
		}
	}

	// isViewedOwned: we can only know this for the "base" view
	// When Alt/Ctrl is used, we can't track which rank is being viewed
	// So we DON'T set isViewedOwned here - let the caller handle it
	// based on comparing viewedRank to ownedRanks if needed
	info.isViewedOwned = false;  // Default to false, don't assume ownership

	return info;
}

void LevelUpMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}

// --- Fallout London (PRKF) list-based perk/skill/trait menu ---

std::int32_t LevelUpMenuAccessibility::ReadIntVar(RE::IMenu* a_menu, const char* a_path)
{
	if (!a_menu || !a_menu->uiMovie)
		return -1;
	Value v;
	if (a_menu->uiMovie->GetVariable(&v, a_path)) {
		if (v.IsInt())
			return v.GetInt();
		if (v.IsUInt())
			return static_cast<std::int32_t>(v.GetUInt());
		if (v.IsNumber())
			return static_cast<std::int32_t>(v.GetNumber());
	}
	return -1;
}

std::string LevelUpMenuAccessibility::ReadStringVar(RE::IMenu* a_menu, const char* a_path)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";
	Value v;
	if (a_menu->uiMovie->GetVariable(&v, a_path) && v.IsString()) {
		const char* s = v.GetString();
		if (s)
			return s;
	}
	return "";
}

bool LevelUpMenuAccessibility::ReadBoolVar(RE::IMenu* a_menu, const char* a_path)
{
	if (!a_menu || !a_menu->uiMovie)
		return false;
	Value v;
	if (a_menu->uiMovie->GetVariable(&v, a_path) && v.IsBoolean())
		return v.GetBoolean();
	return false;
}

std::string LevelUpMenuAccessibility::CleanForSpeech(const std::string& a_text)
{
	std::string out;
	out.reserve(a_text.size());
	for (char c : a_text) {
		if (c == '\n' || c == '\r') {
			// Turn line breaks into sentence breaks for speech.
			if (!out.empty() && out.back() != ' ' && out.back() != '.') {
				out += ". ";
			}
		} else {
			out += c;
		}
	}
	// Trim leading/trailing whitespace.
	std::size_t start = out.find_first_not_of(' ');
	if (start == std::string::npos)
		return "";
	std::size_t end = out.find_last_not_of(" \t");
	return out.substr(start, end - start + 1);
}

LevelUpMenuAccessibility::PRKFView LevelUpMenuAccessibility::DeterminePRKFView(
	RE::IMenu* a_menu, std::string& a_listPath, std::int32_t& a_index)
{
	a_listPath.clear();
	a_index = -1;

	// Overlay dialogs take focus priority over the main lists.
	if (ReadBoolVar(a_menu, (std::string(PRKF_INFO) + ".opened").c_str())) {
		a_listPath = std::string(PRKF_INFO) + ".confirmlist_mc";
		a_index = ReadIntVar(a_menu, (a_listPath + ".selectedIndex").c_str());
		return PRKFView::Info;
	}
	if (ReadBoolVar(a_menu, (std::string(PRKF_CONFIRM) + ".opened").c_str())) {
		a_listPath = std::string(PRKF_CONFIRM) + ".confirmlist_mc";
		a_index = ReadIntVar(a_menu, (a_listPath + ".selectedIndex").c_str());
		return PRKFView::Confirm;
	}
	if (ReadBoolVar(a_menu, (std::string(PRKF_INTENSE) + ".opened").c_str())) {
		a_listPath = std::string(PRKF_INTENSE) + ".stats_list";
		a_index = ReadIntVar(a_menu, (a_listPath + ".selectedIndex").c_str());
		return PRKFView::Intense;
	}

	// Main lists: the focused list has selectedIndex >= 0; the other is set to
	// -1 by the AS3 when switching tabs.
	std::int32_t perkIdx = ReadIntVar(a_menu, (std::string(PRKF_PERKLIST) + ".selectedIndex").c_str());
	if (perkIdx >= 0) {
		a_listPath = PRKF_PERKLIST;
		a_index = perkIdx;
		return ReadBoolVar(a_menu, PRKF_TRAITSMODE) ? PRKFView::Traits : PRKFView::Perks;
	}
	std::int32_t skillIdx = ReadIntVar(a_menu, (std::string(PRKF_SKILLLIST) + ".selectedIndex").c_str());
	if (skillIdx >= 0) {
		a_listPath = PRKF_SKILLLIST;
		a_index = skillIdx;
		return ReadBoolVar(a_menu, PRKF_TAGSKILLSMODE) ? PRKFView::TagSkills : PRKFView::Skills;
	}

	return PRKFView::None;
}

std::string LevelUpMenuAccessibility::BuildPRKFContext(RE::IMenu* a_menu, PRKFView a_view)
{
	switch (a_view) {
		case PRKFView::Perks:
			return "Perks";
		case PRKFView::Traits:
			return "Traits";
		case PRKFView::Skills:
			return "Skills";
		case PRKFView::TagSkills:
			return "Tag skills";
		case PRKFView::Intense:
			return "Intense training";
		case PRKFView::Confirm: {
			std::string prompt = CleanForSpeech(ReadStringVar(a_menu, (std::string(PRKF_CONFIRM) + ".confirmtext_tf.text").c_str()));
			std::string item = CleanForSpeech(ReadStringVar(a_menu, (std::string(PRKF_CONFIRM) + ".itemtext_tf.text").c_str()));
			if (!item.empty()) {
				return prompt.empty() ? item : prompt + " " + item;
			}
			return prompt;
		}
		case PRKFView::Info:
		case PRKFView::None:
		default:
			return "";
	}
}

std::string LevelUpMenuAccessibility::BuildPRKFItem(
	RE::IMenu* a_menu, PRKFView a_view, const std::string& a_listPath, std::int32_t a_index)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	// Info window: the whole content is the text field.
	if (a_view == PRKFView::Info) {
		return CleanForSpeech(ReadStringVar(a_menu, (std::string(PRKF_INFO) + ".text_tf.text").c_str()));
	}

	// Fetch the selected entry object from the list's entryList.
	Value entry;
	bool  haveEntry = false;
	if (!a_listPath.empty() && a_index >= 0) {
		Value entryList;
		if (a_menu->uiMovie->GetVariable(&entryList, (a_listPath + ".entryList").c_str()) && entryList.IsArray()) {
			if (static_cast<std::uint32_t>(a_index) < entryList.GetArraySize()) {
				if (entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) && entry.IsObject()) {
					haveEntry = true;
				}
			}
		}
	}

	auto entryString = [&](const char* a_member) -> std::string {
		if (!haveEntry)
			return "";
		Value v;
		if (entry.GetMember(a_member, &v) && v.IsString()) {
			const char* s = v.GetString();
			if (s)
				return s;
		}
		return "";
	};

	// Yes/No confirmation entry.
	if (a_view == PRKFView::Confirm) {
		return CleanForSpeech(entryString("text"));
	}

	std::string name = entryString("text");

	// Skills / tag-skills: name + numeric value (+ tagged flag).
	if (a_view == PRKFView::Skills || a_view == PRKFView::TagSkills) {
		std::string ann = name;
		if (haveEntry) {
			Value v;
			if (entry.GetMember("value", &v)) {
				std::int32_t value = 0;
				bool         have = true;
				if (v.IsInt())
					value = v.GetInt();
				else if (v.IsUInt())
					value = static_cast<std::int32_t>(v.GetUInt());
				else if (v.IsNumber())
					value = static_cast<std::int32_t>(v.GetNumber());
				else
					have = false;
				if (have)
					ann += ", " + std::to_string(value);
			}
		}
		if (a_view == PRKFView::TagSkills && haveEntry) {
			Value tagged;
			if (entry.GetMember("tagged", &tagged) && tagged.IsBoolean() && tagged.GetBoolean()) {
				ann += ", tagged";
			}
		}
		std::string desc = CleanForSpeech(ReadStringVar(a_menu, PRKF_DESC));
		if (!desc.empty())
			ann += ". " + desc;
		return ann;
	}

	// Perks / traits / intense training: name + eligibility + requirements +
	// description. Requirements and description come from the shared text fields
	// the AS3 formats for the focused selection (rank prefix, level reqs, etc.).
	std::string ann = name;
	if (haveEntry) {
		Value elig;
		if (entry.GetMember("iselig", &elig) && elig.IsBoolean() && !elig.GetBoolean()) {
			ann += ", ineligible";
		}
	}
	std::string reqs = CleanForSpeech(ReadStringVar(a_menu, PRKF_REQS));
	if (!reqs.empty())
		ann += ". " + reqs;
	std::string desc = CleanForSpeech(ReadStringVar(a_menu, PRKF_DESC));
	if (!desc.empty())
		ann += ". " + desc;
	return ann;
}

void LevelUpMenuAccessibility::CheckPRKFSelection(RE::IMenu* a_menu)
{
	if (!m_isOpen || !a_menu || !a_menu->uiMovie)
		return;

	std::string  listPath;
	std::int32_t index = -1;
	PRKFView     view = DeterminePRKFView(a_menu, listPath, index);
	if (view == PRKFView::None)
		return;

	std::string item = BuildPRKFItem(a_menu, view, listPath, index);

	// Snapshot of the current state; re-announce only when it changes.
	std::string key = std::to_string(static_cast<int>(view)) + ":" + listPath + ":" +
	                  std::to_string(index) + ":" + item;

	// Let the deferred menu-open announcement speak first (it already includes
	// the first focused item), then prime our state to it without re-announcing.
	if (!m_menuAnnounced)
		return;
	if (!m_prkfPrimed) {
		m_prkfView = static_cast<int>(view);
		m_prkfKey = key;
		m_prkfPrimed = true;
		return;
	}

	if (static_cast<int>(view) == m_prkfView && key == m_prkfKey)
		return;

	bool viewChanged = static_cast<int>(view) != m_prkfView;
	m_prkfView = static_cast<int>(view);
	m_prkfKey = key;

	if (item.empty() && !viewChanged)
		return;

	std::string announcement;
	if (viewChanged) {
		std::string context = BuildPRKFContext(a_menu, view);
		if (!context.empty())
			announcement = context + ", ";
	}
	announcement += item;

	if (!announcement.empty())
		SpeakText(announcement);
}
