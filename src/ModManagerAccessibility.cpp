#include "ModManagerAccessibility.h"
#include "LocalizationHelper.h"
#include "ScaleformHelpers.h"
#include "SpeechManager.h"
#include <mutex>

namespace
{
	std::once_flag g_modRootFailureLogFlag;

	// Strip $ keys / underscores via the game's translator and trim whitespace.
	std::string Clean(const std::string& a_text)
	{
		std::string t = LocalizationHelper::Translate(a_text);
		auto        first = t.find_first_not_of(" \t\n\r");
		if (first == std::string::npos) {
			return "";
		}
		auto last = t.find_last_not_of(" \t\n\r");
		return t.substr(first, last - first + 1);
	}

	// Read a ModCategoryList.selectedIndex (uint; uint.MAX_VALUE == none).
	// Returns -1 for "no selection".
	std::int32_t ReadCategorySelectedIndex(Scaleform::GFx::Value& a_list)
	{
		Scaleform::GFx::Value val;
		if (!a_list.GetMember("selectedIndex", &val)) {
			return -1;
		}
		if (val.IsUInt()) {
			std::uint32_t u = val.GetUInt();
			return (u == UINT32_MAX) ? -1 : static_cast<std::int32_t>(u);
		}
		if (val.IsNumber()) {
			double d = val.GetNumber();
			if (d < 0.0 || d >= 4294967295.0) {
				return -1;
			}
			return static_cast<std::int32_t>(d);
		}
		if (val.IsInt()) {
			std::int32_t i = val.GetInt();
			return i < 0 ? -1 : i;
		}
		return -1;
	}

	// Get the entry data object selected in one ModCategoryList row. Returns
	// false if the row has no selection or its data isn't populated yet.
	// a_outGlobalIndex receives currScrollPage + selectedIndex (for change
	// detection); a_outEntry receives the entry data object.
	bool ReadCategoryRowEntry(
		Scaleform::GFx::Value& a_list,
		std::int32_t&          a_outGlobalIndex,
		Scaleform::GFx::Value& a_outEntry)
	{
		std::int32_t sel = ReadCategorySelectedIndex(a_list);
		if (sel < 0) {
			return false;
		}
		std::int32_t scrollPage = sfh::ReadIntMember(a_list, "currScrollPage", 0);
		std::int32_t global = scrollPage + sel;

		Scaleform::GFx::Value dataObj;
		if (!sfh::GetObjectMember(a_list, "dataObj", dataObj)) {
			return false;
		}
		Scaleform::GFx::Value entries;
		if (!dataObj.GetMember("entries", &entries) || !entries.IsArray()) {
			return false;
		}
		if (global < 0 || static_cast<std::uint32_t>(global) >= entries.GetArraySize()) {
			return false;
		}
		if (!entries.GetElement(static_cast<std::uint32_t>(global), &a_outEntry) || !a_outEntry.IsObject()) {
			return false;
		}
		a_outGlobalIndex = global;
		return true;
	}

	// True if either browse row has at least one entry (data populated).
	bool BrowseHasEntries(Scaleform::GFx::Value& a_root)
	{
		for (const char* listName : { "List1_mc", "List2_mc" }) {
			Scaleform::GFx::Value list;
			if (!sfh::WalkMembers(a_root, { "ListsHolder_mc", listName }, list)) {
				continue;
			}
			Scaleform::GFx::Value dataObj;
			if (!sfh::GetObjectMember(list, "dataObj", dataObj)) {
				continue;
			}
			Scaleform::GFx::Value entries;
			if (dataObj.GetMember("entries", &entries) && entries.IsArray() && entries.GetArraySize() > 0) {
				return true;
			}
		}
		return false;
	}
}

ModManagerAccessibility* ModManagerAccessibility::GetSingleton()
{
	static ModManagerAccessibility singleton;
	return &singleton;
}

bool ModManagerAccessibility::IsModRoot(Scaleform::GFx::Value& a_val)
{
	if (!a_val.IsObject()) {
		return false;
	}
	Scaleform::GFx::Value probe;
	return sfh::GetObjectMember(a_val, "ListsHolder_mc", probe) ||
	       sfh::GetObjectMember(a_val, "LibraryPage_mc", probe) ||
	       sfh::GetObjectMember(a_val, "DetailsPage_mc", probe);
}

bool ModManagerAccessibility::GetModRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& a_out)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value holder;
	if (!a_menu->uiMovie->GetVariable(&holder, MOD_HOLDER_PATH) || !holder.IsObject()) {
		std::call_once(g_modRootFailureLogFlag, []() {
			REX::INFO("ModManagerAccessibility: root.ModManager_mc not resolvable; Mod Manager announcements will silently bail");
		});
		return false;
	}

	// ModManager_Loader is a private var, so reach the loaded Main_ModManager
	// instance through ModManager_mc's display list. The loader's content is
	// either exposed via .content or as its first display child.
	std::int32_t n = sfh::GetNumChildren(holder);
	for (std::int32_t i = 0; i < n && i < 20; ++i) {
		Scaleform::GFx::Value child;
		if (!sfh::GetChildAt(holder, i, child)) {
			continue;
		}
		Scaleform::GFx::Value content;
		if (sfh::GetObjectMember(child, "content", content) && IsModRoot(content)) {
			a_out = content;
			return true;
		}
		Scaleform::GFx::Value sub;
		if (sfh::GetChildAt(child, 0, sub) && IsModRoot(sub)) {
			a_out = sub;
			return true;
		}
		if (IsModRoot(child)) {
			a_out = child;
			return true;
		}
	}

	// Loader may not have finished yet — don't log (transient on first frames).
	return false;
}

ModManagerAccessibility::View ModManagerAccessibility::DetectView(Scaleform::GFx::Value& a_root)
{
	if (sfh::ChildVisible(a_root, "LoginHolder_mc")) {
		return View::Login;
	}
	if (sfh::ChildVisible(a_root, "DetailsPage_mc")) {
		return View::Details;
	}
	if (sfh::ChildVisible(a_root, "AccountSettingsPage_mc")) {
		return View::AccountSettings;
	}
	if (sfh::ChildVisible(a_root, "SearchPage_mc")) {
		return View::Search;
	}
	if (sfh::ChildVisible(a_root, "LibraryPage_mc")) {
		return View::Library;
	}
	if (sfh::ChildVisible(a_root, "ListsHolder_mc")) {
		return View::Browse;
	}
	return View::None;
}

const char* ModManagerAccessibility::ViewName(View a_view)
{
	switch (a_view) {
		case View::Browse:
			return "Browse mods";
		case View::Details:
			return "Mod details";
		case View::Library:
			return "My library";
		case View::Search:
			return "Search mods";
		case View::AccountSettings:
			return "Account settings";
		case View::Login:
			return "Bethesda dot net sign in";
		default:
			return "";
	}
}

std::string ModManagerAccessibility::BuildModEntrySpeech(Scaleform::GFx::Value& a_entry)
{
	std::string title = Clean(sfh::ReadStringMember(a_entry, "text"));
	if (title.empty()) {
		return "";
	}

	std::string author = Clean(sfh::ReadStringMember(a_entry, "author"));
	std::string result = title;
	if (!author.empty()) {
		result += ", by " + author;
	}

	// One status flag, in priority order.
	std::int32_t progress = sfh::ReadIntMember(a_entry, "downloadProgress", 0);
	if (sfh::ReadBoolMember(a_entry, "needsUpdate")) {
		result += ", update available";
	} else if (progress > 0 && progress < 100) {
		result += ", downloading " + std::to_string(progress) + " percent";
	} else if (sfh::ReadBoolMember(a_entry, "installed")) {
		result += ", installed";
	} else if (sfh::ReadBoolMember(a_entry, "downloaded")) {
		result += ", downloaded";
	}
	return result;
}

std::string ModManagerAccessibility::BuildDetailsSummary(Scaleform::GFx::Value& a_root)
{
	std::string title = Clean(sfh::ReadTextAtPath(a_root, { "DetailsPage_mc", "Title_tf" }));
	std::string author = Clean(sfh::ReadTextAtPath(a_root, { "DetailsPage_mc", "AuthorName_tf" }));
	std::string result = title;
	if (!author.empty()) {
		result += result.empty() ? author : (". " + author);
	}
	return result;
}

std::string ModManagerAccessibility::GetDownloadProgressText(Scaleform::GFx::Value& a_root)
{
	Scaleform::GFx::Value optionsList;
	if (!sfh::WalkMembers(a_root, { "DetailsPage_mc", "OptionsList_mc" }, optionsList)) {
		return "";
	}
	Scaleform::GFx::Value entryList;
	if (!optionsList.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return "";
	}
	std::uint32_t count = entryList.GetArraySize();
	for (std::uint32_t i = 0; i < count; ++i) {
		Scaleform::GFx::Value entry;
		if (!entryList.GetElement(i, &entry) || !entry.IsObject()) {
			continue;
		}
		std::string text = sfh::ReadStringMember(entry, "text");
		if (text.find('%') != std::string::npos) {
			return Clean(text);
		}
	}
	return "";
}

bool ModManagerAccessibility::ReadBrowseSelection(
	Scaleform::GFx::Value& a_root,
	std::string&           a_outSpeech,
	int&                   a_outListId,
	int&                   a_outIndex)
{
	int listId = 0;
	for (const char* listName : { "List1_mc", "List2_mc" }) {
		++listId;
		Scaleform::GFx::Value list;
		if (!sfh::WalkMembers(a_root, { "ListsHolder_mc", listName }, list)) {
			continue;
		}

		std::int32_t          global = -1;
		Scaleform::GFx::Value entry;
		if (!ReadCategoryRowEntry(list, global, entry)) {
			continue;
		}

		std::string speech = BuildModEntrySpeech(entry);
		if (speech.empty()) {
			continue;
		}

		// Prefix the category name so the user knows which row they're in.
		std::string category = Clean(sfh::ReadTextAtPath(list, { "CategoryName_tf" }));
		a_outSpeech = category.empty() ? speech : (category + ": " + speech);
		a_outListId = listId;
		a_outIndex = global;
		return true;
	}
	return false;
}

std::int32_t ModManagerAccessibility::GetListSelectedIndex(
	Scaleform::GFx::Value&             a_root,
	std::initializer_list<const char*> a_listPath)
{
	Scaleform::GFx::Value list;
	if (!sfh::WalkMembers(a_root, a_listPath, list)) {
		return -1;
	}
	return sfh::ReadIntMember(list, "selectedIndex", -1);
}

std::string ModManagerAccessibility::GetListEntryText(
	Scaleform::GFx::Value&             a_root,
	std::initializer_list<const char*> a_listPath,
	std::int32_t                       a_index)
{
	if (a_index < 0) {
		return "";
	}
	Scaleform::GFx::Value list;
	if (!sfh::WalkMembers(a_root, a_listPath, list)) {
		return "";
	}
	Scaleform::GFx::Value entryList;
	if (!list.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return "";
	}
	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return "";
	}
	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return "";
	}

	std::string text = Clean(sfh::ReadStringMember(entry, "text"));
	// Library entries carry an enabled flag (`checked`) worth announcing.
	Scaleform::GFx::Value checked;
	if (entry.GetMember("checked", &checked)) {
		bool isChecked = false;
		if (sfh::ReadAsBool(checked, isChecked)) {
			text += isChecked ? ", enabled" : ", disabled";
		}
	}
	return text;
}

void ModManagerAccessibility::OnOpened()
{
	REX::INFO("ModManagerAccessibility: Mod Manager opened");
	m_isOpen = true;
	m_lastView = View::None;
	m_lastSelection = -1;
	m_lastBrowseListId = -1;
	m_lastDownloadText.clear();
	m_loadingAnnounced = false;
	m_lastSpokenText = "Mods";
	SpeakText(m_lastSpokenText);
}

void ModManagerAccessibility::Reset()
{
	m_isOpen = false;
	m_lastView = View::None;
	m_lastSelection = -1;
	m_lastBrowseListId = -1;
	m_lastDownloadText.clear();
	m_lastSpokenText.clear();
	m_loadingAnnounced = false;
}

void ModManagerAccessibility::CheckState(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return;
	}

	Scaleform::GFx::Value modRoot;
	if (!GetModRoot(a_menu, modRoot)) {
		return;
	}

	View view = DetectView(modRoot);

	// On view change, announce the view (plus a detail summary when entering
	// Details), and reset per-view tracking.
	if (view != m_lastView) {
		m_lastView = view;
		m_lastSelection = -1;
		m_lastBrowseListId = -1;
		m_lastDownloadText.clear();
		m_loadingAnnounced = false;

		std::string announcement = ViewName(view);
		if (view == View::Details) {
			std::string summary = BuildDetailsSummary(modRoot);
			if (!summary.empty()) {
				announcement += ". " + summary;
			}
		}
		if (!announcement.empty()) {
			m_lastSpokenText = announcement;
			SpeakText(announcement);
		}
		return;
	}

	switch (view) {
		case View::Browse: {
			std::string speech;
			int         listId = 0;
			int         index = 0;
			if (ReadBrowseSelection(modRoot, speech, listId, index)) {
				if (listId != m_lastBrowseListId || index != m_lastSelection) {
					m_lastBrowseListId = listId;
					m_lastSelection = index;
					if (!speech.empty() && speech != m_lastSpokenText) {
						m_lastSpokenText = speech;
						SpeakText(speech);
					}
				}
			} else if (!m_loadingAnnounced && !BrowseHasEntries(modRoot)) {
				m_loadingAnnounced = true;
				SpeakText("Loading mods");
			}
			break;
		}

		case View::Library: {
			std::int32_t idx = GetListSelectedIndex(modRoot, { "LibraryPage_mc", "List_mc" });
			if (idx != m_lastSelection) {
				m_lastSelection = idx;
				std::string speech = GetListEntryText(modRoot, { "LibraryPage_mc", "List_mc" }, idx);
				if (!speech.empty() && speech != m_lastSpokenText) {
					m_lastSpokenText = speech;
					SpeakText(speech);
				}
			}
			break;
		}

		case View::AccountSettings: {
			std::int32_t idx = GetListSelectedIndex(modRoot, { "AccountSettingsPage_mc", "List_mc" });
			if (idx != m_lastSelection) {
				m_lastSelection = idx;
				std::string speech = GetListEntryText(modRoot, { "AccountSettingsPage_mc", "List_mc" }, idx);
				if (!speech.empty() && speech != m_lastSpokenText) {
					m_lastSpokenText = speech;
					SpeakText(speech);
				}
			}
			break;
		}

		case View::Details: {
			std::int32_t idx = GetListSelectedIndex(modRoot, { "DetailsPage_mc", "OptionsList_mc" });
			if (idx != m_lastSelection) {
				m_lastSelection = idx;
				std::string speech = GetListEntryText(modRoot, { "DetailsPage_mc", "OptionsList_mc" }, idx);
				if (!speech.empty() && speech != m_lastSpokenText) {
					m_lastSpokenText = speech;
					SpeakText(speech);
				}
			} else {
				// Selection is stable — poll for inline download progress, shown
				// in an OptionsList entry's text ("Downloading... N%"). Done in
				// the else branch so a selection change and a progress update in
				// the same frame can't talk over each other.
				std::string download = GetDownloadProgressText(modRoot);
				if (download != m_lastDownloadText) {
					m_lastDownloadText = download;
					if (!download.empty()) {
						SpeakText(download);
					}
				}
			}
			break;
		}

		case View::Search:
		case View::Login:
		case View::None:
		default:
			break;
	}
}

void ModManagerAccessibility::SpeakText(const std::string& a_text)
{
	if (a_text.empty()) {
		return;
	}
	SpeechManager::GetSingleton()->Speak(a_text);
}
