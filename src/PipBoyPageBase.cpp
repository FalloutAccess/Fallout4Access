#include "PipBoyPageBase.h"
#include "SpeechManager.h"
#include <SRAL.h>

const Scaleform::GFx::Value* PipBoyPageBase::GetDataObj(RE::IMenu* a_menu)
{
	if (!a_menu) {
		return nullptr;
	}

	auto*       pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
	const auto& dataObj = pipboyMenu->dataObj;

	if (dataObj.IsUndefined() || !dataObj.IsObject()) {
		return nullptr;
	}

	return &pipboyMenu->dataObj;
}

std::int32_t PipBoyPageBase::GetCurrentTab(RE::IMenu* a_menu)
{
	const auto* dataObj = GetDataObj(a_menu);
	if (!dataObj) {
		return -1;
	}

	Scaleform::GFx::Value tabValue;
	if (dataObj->GetMember("CurrentTab", &tabValue)) {
		int tab = -1;
		if (tabValue.IsInt()) {
			tab = tabValue.GetInt();
		} else if (tabValue.IsNumber()) {
			tab = static_cast<int>(tabValue.GetNumber());
		} else if (tabValue.IsUInt()) {
			tab = static_cast<int>(tabValue.GetUInt());
		}
		return tab;
	}

	return -1;
}

bool PipBoyPageBase::GetInvSelectedItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray)
{
	const auto* dataObj = GetDataObj(a_menu);
	if (!dataObj || !a_outArray) {
		return false;
	}

	if (dataObj->GetMember("InvSelectedItems", a_outArray) && a_outArray->IsArray()) {
		return true;
	}

	return false;
}

bool PipBoyPageBase::GetInvItems(RE::IMenu* a_menu, Scaleform::GFx::Value* a_outArray)
{
	const auto* dataObj = GetDataObj(a_menu);
	if (!dataObj || !a_outArray) {
		return false;
	}

	if (dataObj->GetMember("InvItems", a_outArray) && a_outArray->IsArray()) {
		return true;
	}

	return false;
}

std::int32_t PipBoyPageBase::GetInventoryListSelection(RE::IMenu* a_menu)
{
	// We cannot directly access the live List_mc.selectedIndex because it's in
	// a dynamically loaded SWF. Instead, we use InvSelectedItems from dataObj
	// which stores the saved selection per tab.
	//
	// Note: This gets the selection from the C++ side's data, which is updated
	// when onInvItemSelection callback is received from ActionScript.

	if (!a_menu) {
		return -1;
	}

	auto*       pipboyMenu = static_cast<RE::PipboyMenu*>(a_menu);
	const auto& dataObj = pipboyMenu->dataObj;

	if (dataObj.IsUndefined() || !dataObj.IsObject()) {
		return -1;
	}

	// Get current tab
	Scaleform::GFx::Value tabValue;
	int                   currentTab = 0;
	if (dataObj.GetMember("CurrentTab", &tabValue)) {
		if (tabValue.IsInt())
			currentTab = tabValue.GetInt();
		else if (tabValue.IsNumber())
			currentTab = static_cast<int>(tabValue.GetNumber());
		else if (tabValue.IsUInt())
			currentTab = static_cast<int>(tabValue.GetUInt());
	}

	// Get InvSelectedItems array - this is updated by the game when selection changes
	Scaleform::GFx::Value invSelectedItems;
	if (dataObj.GetMember("InvSelectedItems", &invSelectedItems) && invSelectedItems.IsArray()) {
		std::uint32_t size = invSelectedItems.GetArraySize();
		if (currentTab >= 0 && static_cast<std::uint32_t>(currentTab) < size) {
			Scaleform::GFx::Value selectedVal;
			if (invSelectedItems.GetElement(static_cast<std::uint32_t>(currentTab), &selectedVal)) {
				int index = -1;
				if (selectedVal.IsInt())
					index = selectedVal.GetInt();
				else if (selectedVal.IsNumber())
					index = static_cast<int>(selectedVal.GetNumber());
				else if (selectedVal.IsUInt())
					index = static_cast<int>(selectedVal.GetUInt());
				return index;
			}
		}
	}

	return -1;
}

// Recursive DFS through the Scaleform display tree to find a child by name.
// Used because PipboySubMenu::menuObj stores a reference to the loaded SWF's
// root MovieClip ("Menu_mc"), and its children (like List_mc) are nested inside
// container instances (e.g. "instance4") that aren't accessible via GetMember
// or AS3 getChildByName from the C++ bridge.
static bool FindChildInDisplayTree(Scaleform::GFx::Value& a_parent, const char* a_name, Scaleform::GFx::Value* a_outChild, int a_depth = 0)
{
	if (a_depth > 4 || !a_parent.IsObject())
		return false;

	Scaleform::GFx::Value numVal;
	if (!a_parent.GetMember("numChildren", &numVal))
		return false;

	int numChildren = 0;
	if (numVal.IsNumber())
		numChildren = static_cast<int>(numVal.GetNumber());
	else if (numVal.IsInt())
		numChildren = numVal.GetInt();

	for (int i = 0; i < numChildren && i < 50; i++) {
		Scaleform::GFx::Value child;
		Scaleform::GFx::Value indexArg(static_cast<double>(i));
		if (!a_parent.Invoke("getChildAt", &child, &indexArg, 1) || !child.IsObject())
			continue;

		Scaleform::GFx::Value childName;
		if (child.GetMember("name", &childName) && childName.IsString()) {
			if (std::strcmp(childName.GetString(), a_name) == 0) {
				*a_outChild = child;
				return true;
			}
		}

		// Recurse into children
		if (FindChildInDisplayTree(child, a_name, a_outChild, a_depth + 1)) {
			return true;
		}
	}

	return false;
}

bool PipBoyPageBase::GetNamedChild(const Scaleform::GFx::Value& a_displayObj, const char* a_childName, Scaleform::GFx::Value* a_outChild)
{
	if (!a_outChild || !a_displayObj.IsObject())
		return false;

	auto& obj = const_cast<Scaleform::GFx::Value&>(a_displayObj);

	// Approach 1: Direct GetMember (works on callback-provided page refs)
	if (obj.GetMember(a_childName, a_outChild) && a_outChild->IsObject()) {
		return true;
	}

	// Approach 2: Recursive display tree search via getChildAt
	// Menu_mc has children like BottomBar_mc, Header_mc, ButtonHintBar_mc, instance4
	// List_mc lives inside instance4 (the content container), so we need recursion
	if (FindChildInDisplayTree(obj, a_childName, a_outChild)) {
		return true;
	}

	return false;
}

bool PipBoyPageBase::GetListViaMovie(RE::IMenu* a_menu, const char* a_tabName, const char* a_listName, Scaleform::GFx::Value* a_outList)
{
	// Note: movie->GetVariable() cannot traverse the Scaleform display list for
	// dynamically loaded sub-SWFs. It only resolves AS3 property paths on the document
	// class. For Pip-Boy sub-pages, GetNamedChild with recursive getChildAt is the
	// working approach. This method is kept as a minimal fallback.
	if (!a_menu || !a_menu->uiMovie || !a_outList)
		return false;

	auto* movie = a_menu->uiMovie.get();

	// Try root.Menu_mc path (Menu_mc is accessible but its display children aren't)
	if (a_tabName && a_tabName[0] != '\0') {
		std::string path = std::string("root.Menu_mc.") + a_tabName + "." + a_listName;
		if (movie->GetVariable(a_outList, path.c_str()) && a_outList->IsObject()) {
			return true;
		}
	}

	{
		std::string path = std::string("root.Menu_mc.") + a_listName;
		if (movie->GetVariable(a_outList, path.c_str()) && a_outList->IsObject()) {
			return true;
		}
	}

	return false;
}

void PipBoyPageBase::SpeakText(const std::string& a_text, bool a_interrupt)
{
	SpeechManager::GetSingleton()->Speak(a_text, a_interrupt);
}

std::string PipBoyPageBase::CleanLocalizationString(const std::string& a_text)
{
	if (a_text.empty()) {
		return a_text;
	}

	std::string result = a_text;

	// Remove $ prefix used for localization keys
	if (result[0] == '$') {
		result = result.substr(1);
	}

	// Replace underscores and hyphens with spaces for better speech
	for (char& c : result) {
		if (c == '_' || c == '-') {
			c = ' ';
		}
	}

	return result;
}
