#include "MCMAccessibility.h"
#include "LocalizationHelper.h"
#include "SpeechManager.h"
#include <mutex>

namespace
{
	// Log each Scaleform path resolution failure only once per session so we
	// don't spam fallout4access.log when MCM isn't present or the path drifts.
	std::once_flag g_mcmRootFailureLogFlag;

	// Read an integer-like Scaleform value (Int / UInt / Number) into an
	// std::int32_t. Returns true on success.
	bool ReadNumberAsInt(const Scaleform::GFx::Value& a_val, std::int32_t& a_out)
	{
		if (a_val.IsInt()) {
			a_out = a_val.GetInt();
			return true;
		}
		if (a_val.IsUInt()) {
			a_out = static_cast<std::int32_t>(a_val.GetUInt());
			return true;
		}
		if (a_val.IsNumber()) {
			a_out = static_cast<std::int32_t>(a_val.GetNumber());
			return true;
		}
		return false;
	}

	// Read a numeric Scaleform value into a double. Returns true on success.
	bool ReadAsDouble(const Scaleform::GFx::Value& a_val, double& a_out)
	{
		if (a_val.IsNumber()) {
			a_out = a_val.GetNumber();
			return true;
		}
		if (a_val.IsInt()) {
			a_out = static_cast<double>(a_val.GetInt());
			return true;
		}
		if (a_val.IsUInt()) {
			a_out = static_cast<double>(a_val.GetUInt());
			return true;
		}
		return false;
	}

	// Try to read a boolean-like Scaleform value. Handles IsBoolean and also
	// accepts IsNumber (treating non-zero as true) since the plan's runtime
	// validations call this out as worth guarding.
	bool ReadAsBool(const Scaleform::GFx::Value& a_val, bool& a_out)
	{
		if (a_val.IsBoolean()) {
			a_out = a_val.GetBoolean();
			return true;
		}
		if (a_val.IsNumber()) {
			a_out = a_val.GetNumber() != 0.0;
			return true;
		}
		if (a_val.IsInt()) {
			a_out = a_val.GetInt() != 0;
			return true;
		}
		if (a_val.IsUInt()) {
			a_out = a_val.GetUInt() != 0;
			return true;
		}
		return false;
	}

	// Read a string-valued member from a Scaleform object. Returns empty
	// string on any failure.
	std::string ReadStringMember(Scaleform::GFx::Value& a_obj, const char* a_name)
	{
		Scaleform::GFx::Value member;
		if (a_obj.GetMember(a_name, &member) && member.IsString()) {
			return member.GetString();
		}
		return "";
	}

	// Read a boolean-valued member from a Scaleform object. Returns a_default
	// on any failure.
	bool ReadBoolMember(Scaleform::GFx::Value& a_obj, const char* a_name, bool a_default = false)
	{
		Scaleform::GFx::Value member;
		if (!a_obj.GetMember(a_name, &member)) {
			return a_default;
		}
		bool out = a_default;
		if (!ReadAsBool(member, out)) {
			return a_default;
		}
		return out;
	}

	// Walk from a rendered SettingsOptionItem clip down to its inner
	// OptionItem child. The `OptionItem` field is declared `private` in
	// SettingsOptionItem.as:43, so Scaleform's GetMember reflection cannot
	// see it directly. Runtime testing confirmed this — a one-shot diagnostic
	// showed `rendered right-pane clip has no 'OptionItem' member` on every
	// movieType that hit this path.
	//
	// MCM's movieType setter (SettingsOptionItem.as:71-131) always performs
	// `removeChild(this.OptionItem)` followed by `addChild(this.OptionItem)`,
	// making the private OptionItem the MOST RECENTLY ADDED child of the
	// rendered clip. Subsequent SetEntryText overrides add further children
	// to `this.OptionItem` (not to `this`), so the last child of the
	// SettingsOptionItem clip remains the OptionItem throughout its life.
	//
	// We therefore fetch it via the public display-list API:
	// `getChildAt(numChildren - 1)`.
	bool GetOptionItemFromClip(Scaleform::GFx::Value& a_renderedClip, Scaleform::GFx::Value& a_out)
	{
		if (!a_renderedClip.IsObject()) {
			return false;
		}
		Scaleform::GFx::Value numChildrenVal;
		if (!a_renderedClip.GetMember("numChildren", &numChildrenVal)) {
			return false;
		}
		std::int32_t n = 0;
		if (!ReadNumberAsInt(numChildrenVal, n) || n <= 0) {
			return false;
		}
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<std::uint32_t>(n - 1);
		if (!a_renderedClip.Invoke("getChildAt", &a_out, args, 1)) {
			return false;
		}
		return a_out.IsObject();
	}

	// Invoke list.GetClipByIndex(clipIdx) and return true with the clip in
	// a_out on success.
	bool GetClipByIndex(Scaleform::GFx::Value& a_list, std::int32_t a_clipIdx, Scaleform::GFx::Value& a_out)
	{
		if (a_clipIdx < 0 || !a_list.IsObject()) {
			return false;
		}
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<std::uint32_t>(a_clipIdx);
		if (!a_list.Invoke("GetClipByIndex", &a_out, args, 1)) {
			return false;
		}
		return a_out.IsObject();
	}

	// Read the clipIndex field from an entry record. Returns -1 on failure.
	std::int32_t ReadClipIndex(Scaleform::GFx::Value& a_entry)
	{
		Scaleform::GFx::Value clipIndex;
		if (!a_entry.GetMember("clipIndex", &clipIndex)) {
			return -1;
		}
		std::int32_t out = -1;
		if (!ReadNumberAsInt(clipIndex, out)) {
			return -1;
		}
		return out;
	}

	// Walk a list path on mcmRoot: foo -> foo.bar -> foo.bar.baz. a_path is
	// a null-terminated array of member names. Each hop must return IsObject.
	bool WalkMembers(
		const Scaleform::GFx::Value&       a_start,
		std::initializer_list<const char*> a_path,
		Scaleform::GFx::Value&             a_out)
	{
		Scaleform::GFx::Value current = a_start;
		if (!current.IsObject()) {
			return false;
		}
		for (auto* name : a_path) {
			Scaleform::GFx::Value next;
			if (!current.GetMember(name, &next) || !next.IsObject()) {
				return false;
			}
			current = next;
		}
		a_out = current;
		return true;
	}
}

MCMAccessibility* MCMAccessibility::GetSingleton()
{
	static MCMAccessibility singleton;
	return &singleton;
}

bool MCMAccessibility::IsMCMVisible(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	Scaleform::GFx::Value mcmRoot;
	if (!a_menu->uiMovie->GetVariable(&mcmRoot, MCM_ROOT_PATH) || !mcmRoot.IsObject()) {
		// Fallback: two-step resolution through root.mcm_loader.content.
		Scaleform::GFx::Value loaderContent;
		if (!a_menu->uiMovie->GetVariable(&loaderContent, MCM_LOADER_CONTENT_PATH) || !loaderContent.IsObject()) {
			return false;
		}
		if (!loaderContent.GetMember("mcmMenu", &mcmRoot) || !mcmRoot.IsObject()) {
			return false;
		}
	}

	// `mcmMenu.visible` alone is unreliable because the clip is instantiated
	// at MCM_Main construction (MCM_Main.as:20, `public var mcmMenu:MCM_Menu
	// = new mcm.MCM_Menu()`) with the default visible=true, but is NOT yet
	// added to any display list — the `addChild(mcmMenu)` call at line 37 is
	// guarded by `!Extensions.isScaleform` and never runs in-game. mcmMenu
	// is only added to MainMenu when the user clicks MOD CONFIG
	// (MCM_Main.as:147, `MainMenu.addChildAt(mcmMenu, ...)`), and after
	// onQuitPressed (MCM_Menu.as:213) mcmMenu.visible flips to false.
	//
	// The correct active-MCM probe is therefore `mcmMenu.parent != null AND
	// mcmMenu.visible == true`:
	//   - parent == null  => user has never opened MCM this session
	//   - visible == false => user closed MCM via Cancel/Back
	//   - both true        => MCM is the currently active UI
	Scaleform::GFx::Value parent;
	if (!mcmRoot.GetMember("parent", &parent) || !parent.IsObject()) {
		return false;
	}

	Scaleform::GFx::Value visible;
	if (!mcmRoot.GetMember("visible", &visible)) {
		return false;
	}
	bool visibleBool = false;
	if (!ReadAsBool(visible, visibleBool)) {
		return false;
	}
	return visibleBool;
}

bool MCMAccessibility::GetMCMRoot(RE::MainMenu* a_menu, Scaleform::GFx::Value& out)
{
	if (!a_menu || !a_menu->uiMovie) {
		return false;
	}

	// Primary: one deep GetVariable.
	if (a_menu->uiMovie->GetVariable(&out, MCM_ROOT_PATH) && out.IsObject()) {
		return true;
	}

	// Fallback: two-step resolution through root.mcm_loader.content.
	Scaleform::GFx::Value loaderContent;
	if (a_menu->uiMovie->GetVariable(&loaderContent, MCM_LOADER_CONTENT_PATH) && loaderContent.IsObject()) {
		if (loaderContent.GetMember("mcmMenu", &out) && out.IsObject()) {
			return true;
		}
	}

	// Neither path worked; log once and give up for this frame.
	std::call_once(g_mcmRootFailureLogFlag, []() {
		REX::INFO("MCMAccessibility: failed to resolve mcmMenu via primary and fallback paths; MCM integration will silently bail on subsequent frames");
	});
	return false;
}

void MCMAccessibility::OnOpened()
{
	REX::INFO("MCMAccessibility: MCM opened");
	m_isOpen = true;
	m_focusedPane = Pane::Left;
	m_lastLeftIndex = -1;
	m_lastRightIndex = -1;
	m_lastIMode = -1;
	m_lastHint.clear();
	m_lastSpokenText.clear();
	m_lastRightValueText.clear();
	// Do NOT reset m_imodeReadStrategy here — it's a session-level probe result.

	SpeakText("Mod Configuration Menu");
}

void MCMAccessibility::Reset()
{
	m_isOpen = false;
	m_focusedPane = Pane::Left;
	m_lastLeftIndex = -1;
	m_lastRightIndex = -1;
	m_lastIMode = -1;
	m_lastHint.clear();
	m_lastSpokenText.clear();
	m_lastRightValueText.clear();
	m_lastSectionName.clear();
}

void MCMAccessibility::CheckState(RE::MainMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie) {
		return;
	}

	Scaleform::GFx::Value mcmRoot;
	if (!GetMCMRoot(a_menu, mcmRoot)) {
		return;
	}

	// Step 1: handle iMode transitions. Suppress row polling while in a
	// sub-mode so we don't talk over the popup.
	std::int32_t currentIMode = GetIMode(mcmRoot);
	if (currentIMode != m_lastIMode) {
		// On the very first frame after OnOpened, m_lastIMode is -1. Don't
		// announce a bogus "main mode" transition; just latch the value.
		bool        firstFrame = (m_lastIMode == -1);
		const char* transition = nullptr;
		if (!firstFrame) {
			switch (currentIMode) {
				case MCM_REMAP_MODE:
					transition = "Key remap mode";
					break;
				case MCM_CONFLICT_MODE:
					transition = "Key conflict, choose option";
					break;
				case MCM_DD_MODE:
					transition = "Dropdown open";
					break;
				case MCM_POSITIONER_MODE:
					transition = "Positioner active";
					break;
				case MCM_POSITIONER_CONFIRM_MODE:
					transition = "Positioner confirm";
					break;
				case MCM_TEXTINPUT_MODE:
					transition = "Text input active";
					break;
				default:
					break;
			}
		}
		REX::INFO("MCMAccessibility: iMode change {} -> {}", m_lastIMode, currentIMode);
		m_lastIMode = currentIMode;
		if (transition) {
			SpeakText(transition);
			return;
		}
	}

	if (currentIMode != MCM_MAIN_MODE) {
		return;
	}

	// Step 2: read both pane selections so we can detect pane focus flips.
	std::int32_t leftIdx = GetLeftSelectedIndex(mcmRoot);
	std::int32_t rightIdx = GetRightSelectedIndex(mcmRoot);

	// Pane-flip rules (plan "Focused pane tracking"):
	//   - Right transitions from < 0 to >= 0 => focus flips to Right.
	//   - Right transitions to < 0 while we thought it was focused => focus flips back to Left.
	if (rightIdx >= 0 && m_focusedPane == Pane::Left && m_lastRightIndex < 0) {
		m_focusedPane = Pane::Right;
	} else if (rightIdx < 0 && m_focusedPane == Pane::Right) {
		m_focusedPane = Pane::Left;
		m_lastSectionName.clear();  // Re-announce section when returning to right pane
	}

	// Step 3: detect selection change in the focused pane and speak it.
	// For the right pane we also detect value changes on the currently
	// focused row: if the user presses left/right on a slider, or Enter on
	// a switcher, the row's selectedIndex doesn't change but its value
	// text does. In that case we speak just the new value (not the full
	// "label: value" again) to avoid being chatty.
	std::string announcement;
	if (m_focusedPane == Pane::Right) {
		if (rightIdx >= 0) {
			const bool  rowChanged = (rightIdx != m_lastRightIndex);
			std::string label;
			std::string valueText;
			BuildRightEntryParts(mcmRoot, rightIdx, label, valueText);

			if (rowChanged) {
				// Announce section heading when entering a new group
				std::string section = FindSectionForIndex(mcmRoot, rightIdx);
				if (!section.empty() && section != m_lastSectionName) {
					m_lastSectionName = section;
					announcement = section + ". ";
				} else if (section.empty()) {
					m_lastSectionName.clear();
				}

				// Full row announcement: "label: value" (or just label if the
				// row has no value to report).
				announcement += label;
				if (!valueText.empty()) {
					announcement += ": " + valueText;
				}
				m_lastRightValueText = valueText;
			} else if (valueText != m_lastRightValueText) {
				// Same row, value changed — speak only the new value.
				announcement = valueText;
				m_lastRightValueText = valueText;
			}
		}
		m_lastRightIndex = rightIdx;
	} else {
		if (leftIdx >= 0 && leftIdx != m_lastLeftIndex) {
			m_lastLeftIndex = leftIdx;
			announcement = BuildLeftEntrySpeech(mcmRoot, leftIdx);
		} else {
			m_lastLeftIndex = leftIdx;
		}
		// When focus is on the left pane, keep the right tracker current
		// too so the flip detector works on the next transition.
		m_lastRightIndex = rightIdx;
	}

	if (!announcement.empty() && announcement != m_lastSpokenText) {
		m_lastSpokenText = announcement;
		SpeakText(announcement);
	}

	// Step 4: announce hint text on change, independently of selection
	// changes (MCM updates it asynchronously).
	// Trim whitespace-only hints: MCM sets hint_tf to " " (single space)
	// when a row has no help text (MCM_Menu.as:625), and speaking that
	// causes the screen reader to announce "blank" / "пусто".
	std::string hint = GetHintText(mcmRoot);
	if (hint.find_first_not_of(" \t\n\r") == std::string::npos) {
		hint.clear();
	}
	if (hint != m_lastHint) {
		m_lastHint = hint;
		if (!hint.empty()) {
			SpeakText(hint);
		}
	}
}

std::int32_t MCMAccessibility::GetIMode(const Scaleform::GFx::Value& a_mcmRoot)
{
	// Try the getter path first (one probe per session, then latch).
	if (m_imodeReadStrategy == IModeStrategy::Unknown || m_imodeReadStrategy == IModeStrategy::Getter) {
		Scaleform::GFx::Value iModeValue;
		// GetMember is const on the value in intent but the API takes non-const.
		auto& root = const_cast<Scaleform::GFx::Value&>(a_mcmRoot);
		if (root.GetMember("iMode", &iModeValue) && iModeValue.IsNumber()) {
			if (m_imodeReadStrategy == IModeStrategy::Unknown) {
				m_imodeReadStrategy = IModeStrategy::Getter;
				REX::INFO("MCMAccessibility: iMode getter probe succeeded, latching getter strategy");
			}
			std::int32_t out = MCM_MAIN_MODE;
			ReadNumberAsInt(iModeValue, out);
			return out;
		}

		// Probe failed. If this is the first attempt, move to clip-visibility fallback.
		if (m_imodeReadStrategy == IModeStrategy::Unknown) {
			m_imodeReadStrategy = IModeStrategy::ClipVisibility;
			REX::INFO("MCMAccessibility: iMode getter probe failed, falling back to clip-visibility strategy");
		}
	}

	// Clip-visibility fallback. Probes MCM's own state-tracking fields in
	// priority order. The last one (POS_WIN) is the least reliable, so it's
	// last.
	//
	//   2 CONFLICT  -> configPanel_mc.hotkey_conflict_mc.visible == true
	//   1/6 REMAP / TEXTINPUT -> mcmMenu.bmForInput is a non-null object.
	//                 bmForInput is the Option_ButtonMapping OR Option_textinput
	//                 instance currently capturing input. MCM_Menu.as:39
	//                 declares `public var bmForInput:InteractiveObject = null`
	//                 and only sets it in StartInput (MCM_Menu.as:442) when
	//                 START_INPUT fires from those widgets. It clears back to
	//                 null in EndInput (MCM_Menu.as:455). So `bmForInput !=
	//                 null` is an unambiguous "some widget is capturing a
	//                 key/text input right now" signal. Distinguish REMAP
	//                 vs TEXTINPUT by probing for `allowModifierKeys` which
	//                 only Option_ButtonMapping has (Option_ButtonMapping.as:46).
	//   4 POSITIONER -> mcmMenu.POS_WIN is on display list AND visible == true
	//   otherwise    -> MAIN
	//
	// The previous iteration used `configList_mc.disableInput == true` as
	// the REMAP signal and regressed to silence on fresh MCM open: MCM's
	// welcome page setup (MCM_Menu.as:537 inside `listcreated` for
	// configList_mc) sets `disableInput = true` AND `disableSelection =
	// true` on the welcome content, which made our fallback return REMAP
	// and CheckState silently early-return until the user activated a page.
	auto& root = const_cast<Scaleform::GFx::Value&>(a_mcmRoot);

	Scaleform::GFx::Value configPanel;
	if (root.GetMember("configPanel_mc", &configPanel) && configPanel.IsObject()) {
		Scaleform::GFx::Value hotkeyConflict;
		if (configPanel.GetMember("hotkey_conflict_mc", &hotkeyConflict) && hotkeyConflict.IsObject()) {
			if (ReadBoolMember(hotkeyConflict, "visible")) {
				return MCM_CONFLICT_MODE;
			}
		}
	}

	Scaleform::GFx::Value bmForInput;
	if (root.GetMember("bmForInput", &bmForInput) && bmForInput.IsObject()) {
		// Capturing input. Tell REMAP apart from TEXTINPUT by probing a
		// field only Option_ButtonMapping exposes.
		Scaleform::GFx::Value allowMod;
		if (bmForInput.GetMember("allowModifierKeys", &allowMod) &&
			(allowMod.IsNumber() || allowMod.IsInt() || allowMod.IsUInt())) {
			return MCM_REMAP_MODE;
		}
		return MCM_TEXTINPUT_MODE;
	}

	Scaleform::GFx::Value posWin;
	if (root.GetMember("POS_WIN", &posWin) && posWin.IsObject()) {
		// Require POS_WIN to both be on the display list and have visible=true.
		// An orphan MovieClip (parent == null) has visible=true by default
		// but is not rendering; only when MCM actually adds it to the stage
		// in positioner mode will both conditions hold.
		Scaleform::GFx::Value posParent;
		if (posWin.GetMember("parent", &posParent) && posParent.IsObject()) {
			if (ReadBoolMember(posWin, "visible")) {
				return MCM_POSITIONER_MODE;
			}
		}
	}

	return MCM_MAIN_MODE;
}

std::int32_t MCMAccessibility::GetLeftSelectedIndex(Scaleform::GFx::Value& a_mcmRoot)
{
	// root.HelpPanel_mc.HelpList_mc.selectedIndex
	Scaleform::GFx::Value helpList;
	if (!WalkMembers(a_mcmRoot, { "HelpPanel_mc", "HelpList_mc" }, helpList)) {
		return -1;
	}

	Scaleform::GFx::Value selectedIndex;
	if (!helpList.GetMember("selectedIndex", &selectedIndex)) {
		return -1;
	}
	std::int32_t out = -1;
	if (!ReadNumberAsInt(selectedIndex, out)) {
		return -1;
	}
	return out;
}

std::int32_t MCMAccessibility::GetRightSelectedIndex(Scaleform::GFx::Value& a_mcmRoot)
{
	// root.configPanel_mc.configList_mc.selectedIndex
	Scaleform::GFx::Value configList;
	if (!WalkMembers(a_mcmRoot, { "configPanel_mc", "configList_mc" }, configList)) {
		return -1;
	}

	Scaleform::GFx::Value selectedIndex;
	if (!configList.GetMember("selectedIndex", &selectedIndex)) {
		return -1;
	}
	std::int32_t out = -1;
	if (!ReadNumberAsInt(selectedIndex, out)) {
		return -1;
	}
	return out;
}

std::string MCMAccessibility::BuildLeftEntrySpeech(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index)
{
	if (a_index < 0) {
		return "";
	}

	// Walk to the left pane list's entryList.
	Scaleform::GFx::Value helpList;
	if (!WalkMembers(a_mcmRoot, { "HelpPanel_mc", "HelpList_mc" }, helpList)) {
		return "";
	}

	Scaleform::GFx::Value entryList;
	if (!helpList.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return "";
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return "";
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return "";
	}

	// Entry `text` fields frequently contain $KEY localization references
	// (e.g. $MCM_SETTINGS). The game's UI renders these via GlobalFunc.SetText
	// which goes through BSScaleformTranslator; we do the same lookup here so
	// the spoken text matches what the user sees on screen.
	std::string text = LocalizationHelper::Translate(ReadStringMember(entry, "text"));

	Scaleform::GFx::Value filterFlagVal;
	std::int32_t          filterFlag = 0;
	if (entry.GetMember("filterFlag", &filterFlagVal)) {
		ReadNumberAsInt(filterFlagVal, filterFlag);
	}

	switch (filterFlag) {
		case FILTER_MOD_HEADER: {
			// Mod header row: speak name + ", N pages" if numPages > 0.
			std::string           result = text;
			Scaleform::GFx::Value numPagesVal;
			std::int32_t          numPages = 0;
			if (entry.GetMember("numPages", &numPagesVal)) {
				ReadNumberAsInt(numPagesVal, numPages);
			}
			if (numPages > 0) {
				result += ", " + std::to_string(numPages) + " pages";
			}
			return result;
		}
		case FILTER_PAGE: {
			// Page row: "Page: <text>", append ", current" if pageSelected.
			std::string result = "Page: " + text;
			if (ReadBoolMember(entry, "pageSelected")) {
				result += ", current";
			}
			return result;
		}
		default:
			return text;
	}
}

void MCMAccessibility::BuildRightEntryParts(
	Scaleform::GFx::Value& a_mcmRoot,
	std::int32_t           a_index,
	std::string&           out_label,
	std::string&           out_valueText)
{
	out_label.clear();
	out_valueText.clear();

	if (a_index < 0) {
		return;
	}

	// Walk to the right pane list object (needed later for GetClipByIndex).
	Scaleform::GFx::Value configList;
	if (!WalkMembers(a_mcmRoot, { "configPanel_mc", "configList_mc" }, configList)) {
		return;
	}

	Scaleform::GFx::Value entryList;
	if (!configList.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return;
	}

	if (static_cast<std::uint32_t>(a_index) >= entryList.GetArraySize()) {
		return;
	}

	Scaleform::GFx::Value entry;
	if (!entryList.GetElement(static_cast<std::uint32_t>(a_index), &entry) || !entry.IsObject()) {
		return;
	}

	// Entry `text` fields frequently contain $KEY localization references
	// (e.g. $MCM_SETTINGS). The game's UI renders these via GlobalFunc.SetText
	// which goes through BSScaleformTranslator; we do the same lookup here so
	// the spoken text matches what the user sees on screen.
	std::string text = LocalizationHelper::Translate(ReadStringMember(entry, "text"));

	Scaleform::GFx::Value movieTypeVal;
	std::int32_t          movieType = -1;
	if (entry.GetMember("movieType", &movieTypeVal)) {
		ReadNumberAsInt(movieTypeVal, movieType);
	}

	// EMPTY_LINE and IMAGE: both parts stay empty so BuildRightEntrySpeech
	// returns "" (no speech). The MCM navigation skipper normally prevents
	// these from being selected but initial focus or mouse clicks can land
	// here, so we defend against it.
	if (movieType == MOVIETYPE_EMPTY_LINE || movieType == MOVIETYPE_IMAGE) {
		return;
	}

	// SECTION/TEXT: label-only, no value. Value-change detection doesn't apply.
	if (movieType == MOVIETYPE_SECTION) {
		out_label = "Section: " + text;
		return;
	}
	if (movieType == MOVIETYPE_TEXT) {
		out_label = text;
		return;
	}

	out_label = text;

	// BUTTON / POSITIONER: static "value" suffix, not a changeable value.
	if (movieType == MOVIETYPE_BUTTON) {
		out_valueText = "button";
		return;
	}
	if (movieType == MOVIETYPE_POSITIONER) {
		out_valueText = "positioner";
		return;
	}

	// Remaining movieTypes need the rendered clip for live value state.
	std::int32_t clipIdx = ReadClipIndex(entry);
	if (clipIdx < 0) {
		// Entry doesn't expose clipIndex where SettingsMenuAccessibility
		// expects it (MCM uses BSScrollingList1, which may use a different
		// field name). Log once so the first test run tells us.
		static std::once_flag s_flag;
		std::call_once(s_flag, [movieType]() {
			REX::INFO("MCMAccessibility: ReadClipIndex returned -1 for right-pane entry (movieType={}); live value reads will fall back to name-only. Check whether MCM entries expose clipIndex under a different name.", movieType);
		});
	}
	out_valueText = GetRenderedRightValueText(configList, clipIdx, movieType, entry);
}

std::string MCMAccessibility::BuildRightEntrySpeech(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index)
{
	std::string label;
	std::string valueText;
	BuildRightEntryParts(a_mcmRoot, a_index, label, valueText);
	if (label.empty()) {
		return "";
	}
	if (valueText.empty()) {
		return label;
	}
	return label + ": " + valueText;
}

std::string MCMAccessibility::GetRenderedRightValueText(
	Scaleform::GFx::Value& a_rightList,
	std::int32_t           a_clipIdx,
	std::int32_t           a_movieType,
	Scaleform::GFx::Value& a_entry)
{
	// SettingsOptionItem exposes two PUBLIC getters that delegate to its
	// private `OptionItem` child from inside the class:
	//   - `get value()`        (SettingsOptionItem.as:173)
	//       SCROLLBAR        -> Option_Scrollbar.value       (raw number)
	//       STEPPER          -> Option_OptionStepper.index   (index into options[])
	//       SWITCHER         -> Option_Switcher.checked ? 1 : 0
	//       DROPDOWN/DD_FILES-> Option_DropDown.index        (index into options[])
	//       TEXTINPUT_INT/F  -> Option_textinput.value       (raw number)
	//       HOTKEY/KEYINPUT  -> 0 (no delegation)
	//   - `get valueString()`  (SettingsOptionItem.as:203)
	//       TEXTINPUT_STRING -> Option_textinput.valueString
	//
	// For everything the public `value` getter covers, we read it directly
	// off the rendered SettingsOptionItem clip — Scaleform's GetMember
	// invokes the AS3 getter, which legally touches the private field from
	// within the owning class. This is what the first runtime test revealed
	// was needed: `OptionItem` is declared `private var OptionItem:MovieClip`
	// on SettingsOptionItem.as:43 and cannot be reached through GetMember
	// reflection from C++, but the public getters can.
	//
	// HOTKEY and KEYINPUT are the only cases where the public getter returns
	// nothing useful — MCM's getter falls through to `return 0` and
	// `valueString` for those types has a known MCM bug (missing return in
	// default branch). For these we walk to the inner clip via getChildAt
	// and read Option_ButtonMapping.PCKey_tf.text, which IS public
	// (Option_ButtonMapping.as:45 `public var PCKey_tf:TextField`).

	Scaleform::GFx::Value renderedClip;
	if (!GetClipByIndex(a_rightList, a_clipIdx, renderedClip)) {
		static std::once_flag s_flag;
		std::call_once(s_flag, [a_clipIdx, a_movieType]() {
			REX::INFO("MCMAccessibility: GetClipByIndex({}) returned non-object in right-pane value walk (movieType={}). Right-pane live values will be unavailable.", a_clipIdx, a_movieType);
		});
		return "";
	}

	switch (a_movieType) {
		case MOVIETYPE_SCROLLBAR: {
			Scaleform::GFx::Value valueVal;
			double                num = 0.0;
			if (renderedClip.GetMember("value", &valueVal) && ReadAsDouble(valueVal, num)) {
				// Raw value (per plan decision — MCM scrollbars have arbitrary ranges).
				if (num == static_cast<double>(static_cast<std::int64_t>(num))) {
					return std::to_string(static_cast<std::int64_t>(num));
				}
				return std::format("{:g}", num);
			}
			static std::once_flag s_flag;
			std::call_once(s_flag, []() {
				REX::INFO("MCMAccessibility: SCROLLBAR renderedClip.value getter returned no numeric value.");
			});
			return "";
		}
		case MOVIETYPE_STEPPER:
		case MOVIETYPE_DROPDOWN:
		case MOVIETYPE_DD_FILES: {
			// SettingsOptionItem.get value() returns the selected index for
			// these types. Resolve it to a string via entry.options[idx].
			Scaleform::GFx::Value valueVal;
			std::int32_t          idx = -1;
			if (!renderedClip.GetMember("value", &valueVal) || !ReadNumberAsInt(valueVal, idx)) {
				static std::once_flag s_flag;
				std::call_once(s_flag, [a_movieType]() {
					REX::INFO("MCMAccessibility: STEPPER/DROPDOWN renderedClip.value getter returned no numeric value (movieType={}).", a_movieType);
				});
				return "";
			}

			Scaleform::GFx::Value options;
			if (!a_entry.GetMember("options", &options) || !options.IsArray()) {
				return std::to_string(idx);
			}
			if (idx < 0 || static_cast<std::uint32_t>(idx) >= options.GetArraySize()) {
				return std::to_string(idx);
			}
			Scaleform::GFx::Value optionText;
			if (options.GetElement(static_cast<std::uint32_t>(idx), &optionText) && optionText.IsString()) {
				// Option labels may also be $KEY localization references
				// (e.g. a DROPDOWN with $OPTION_ON / $OPTION_OFF entries).
				return LocalizationHelper::Translate(optionText.GetString());
			}
			return std::to_string(idx);
		}
		case MOVIETYPE_SWITCHER: {
			// SettingsOptionItem.get value() returns 1 if checked, 0 otherwise
			// (SettingsOptionItem.as:185). Treat non-zero as "on".
			Scaleform::GFx::Value valueVal;
			double                num = 0.0;
			if (!renderedClip.GetMember("value", &valueVal) || !ReadAsDouble(valueVal, num)) {
				static std::once_flag s_flag;
				std::call_once(s_flag, []() {
					REX::INFO("MCMAccessibility: SWITCHER renderedClip.value getter returned no numeric value.");
				});
				return "";
			}
			return num != 0.0 ? "on" : "off";
		}
		case MOVIETYPE_KEYINPUT:
		case MOVIETYPE_HOTKEY: {
			// SettingsOptionItem.value returns 0 for these — walk to the
			// inner Option_ButtonMapping child and read PCKey_tf.text.
			Scaleform::GFx::Value optionItem;
			if (!GetOptionItemFromClip(renderedClip, optionItem)) {
				static std::once_flag s_flag;
				std::call_once(s_flag, [a_movieType]() {
					REX::INFO("MCMAccessibility: could not resolve inner OptionItem via getChildAt for HOTKEY/KEYINPUT row (movieType={}).", a_movieType);
				});
				return "unbound";
			}
			Scaleform::GFx::Value pcKeyTf;
			if (!optionItem.GetMember("PCKey_tf", &pcKeyTf) || !pcKeyTf.IsObject()) {
				static std::once_flag s_flag;
				std::call_once(s_flag, []() {
					REX::INFO("MCMAccessibility: HOTKEY/KEYINPUT inner clip has no PCKey_tf member. The resolved child may not be Option_ButtonMapping.");
				});
				return "unbound";
			}
			std::string raw = ReadStringMember(pcKeyTf, "text");
			return FormatKeyBinding(raw);
		}
		case MOVIETYPE_TEXTINPUT_INT:
		case MOVIETYPE_TEXTINPUT_FLOAT: {
			Scaleform::GFx::Value valueVal;
			double                num = 0.0;
			if (renderedClip.GetMember("value", &valueVal) && ReadAsDouble(valueVal, num)) {
				if (a_movieType == MOVIETYPE_TEXTINPUT_INT ||
					num == static_cast<double>(static_cast<std::int64_t>(num))) {
					return std::to_string(static_cast<std::int64_t>(num));
				}
				return std::format("{:g}", num);
			}
			return "";
		}
		case MOVIETYPE_TEXTINPUT_STRING: {
			// SettingsOptionItem.get valueString() delegates to Option_textinput.
			Scaleform::GFx::Value valueStringVal;
			if (renderedClip.GetMember("valueString", &valueStringVal) && valueStringVal.IsString()) {
				return valueStringVal.GetString();
			}
			return "";
		}
		default:
			return "";
	}
}

std::string MCMAccessibility::FindSectionForIndex(Scaleform::GFx::Value& a_mcmRoot, std::int32_t a_index)
{
	if (a_index < 0) {
		return "";
	}

	Scaleform::GFx::Value configList;
	if (!WalkMembers(a_mcmRoot, { "configPanel_mc", "configList_mc" }, configList)) {
		return "";
	}

	Scaleform::GFx::Value entryList;
	if (!configList.GetMember("entryList", &entryList) || !entryList.IsArray()) {
		return "";
	}

	// Walk backwards from a_index to find the nearest SECTION entry
	for (std::int32_t i = a_index; i >= 0; --i) {
		Scaleform::GFx::Value entry;
		if (!entryList.GetElement(static_cast<std::uint32_t>(i), &entry) || !entry.IsObject()) {
			continue;
		}

		Scaleform::GFx::Value movieTypeVal;
		std::int32_t          movieType = -1;
		if (entry.GetMember("movieType", &movieTypeVal)) {
			ReadNumberAsInt(movieTypeVal, movieType);
		}

		if (movieType == MOVIETYPE_SECTION) {
			return LocalizationHelper::Translate(ReadStringMember(entry, "text"));
		}
	}

	return "";
}

std::string MCMAccessibility::GetHintText(Scaleform::GFx::Value& a_mcmRoot)
{
	Scaleform::GFx::Value configPanel;
	if (!a_mcmRoot.GetMember("configPanel_mc", &configPanel) || !configPanel.IsObject()) {
		return "";
	}
	Scaleform::GFx::Value hintTf;
	if (!configPanel.GetMember("hint_tf", &hintTf) || !hintTf.IsObject()) {
		return "";
	}
	return ReadStringMember(hintTf, "text");
}

void MCMAccessibility::SpeakText(const std::string& a_text)
{
	if (a_text.empty()) {
		return;
	}
	SpeechManager::GetSingleton()->Speak(a_text);
}

std::string MCMAccessibility::FormatKeyBinding(const std::string& a_raw)
{
	// Trim whitespace.
	auto firstNon = a_raw.find_first_not_of(" \t\n\r");
	if (firstNon == std::string::npos) {
		return "unbound";
	}
	auto        lastNon = a_raw.find_last_not_of(" \t\n\r");
	std::string trimmed = a_raw.substr(firstNon, lastNon - firstNon + 1);

	// Strip trailing ')' that MCM appends in Option_ButtonMapping.RefreshText.
	if (!trimmed.empty() && trimmed.back() == ')') {
		trimmed.pop_back();
		// Re-trim in case the ')' was preceded by whitespace.
		auto lastAfter = trimmed.find_last_not_of(" \t\n\r");
		if (lastAfter == std::string::npos) {
			return "unbound";
		}
		trimmed.erase(lastAfter + 1);
	}

	if (trimmed.empty() || trimmed == "None") {
		return "unbound";
	}
	return trimmed;
}
