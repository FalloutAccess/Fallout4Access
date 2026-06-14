#include "KeybindActions.h"
#include "AutoWalk.h"
#include "BarterMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "ContextualInfoAnnouncer.h"
#include "ExamineMenuHook.h"
#include "FolonTimerAccessibility.h"
#include "LevelUpMenuAccessibility.h"
#include "ObjectScanner.h"
#include "PipBoyMapPage.h"
#include "PipBoyMenuAccessibility.h"
#include "SpeechManager.h"
#include "WorkshopMenuAccessibility.h"

namespace KeybindActions
{
	namespace
	{
		// Returns the active map page when the Pip-Boy menu is on the Map tab (page 3).
		// Returns nullptr when Pip-Boy is closed or on a different page.
		PipBoyMapPage* GetActiveMapPage()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return nullptr;
			}
			auto menu = ui->GetMenu("PipboyMenu"sv);
			if (!menu) {
				return nullptr;
			}

			auto* pipboyMenu = static_cast<RE::PipboyMenu*>(menu.get());
			if (!pipboyMenu || pipboyMenu->dataObj.IsUndefined() || !pipboyMenu->dataObj.IsObject()) {
				return nullptr;
			}

			Scaleform::GFx::Value pageValue;
			if (!pipboyMenu->dataObj.GetMember("CurrentPage", &pageValue)) {
				return nullptr;
			}

			int pageNum = -1;
			if (pageValue.IsInt()) {
				pageNum = pageValue.GetInt();
			} else if (pageValue.IsNumber()) {
				pageNum = static_cast<int>(pageValue.GetNumber());
			} else if (pageValue.IsUInt()) {
				pageNum = static_cast<int>(pageValue.GetUInt());
			}

			if (pageNum != 3) {
				return nullptr;
			}

			return PipBoyMenuAccessibility::GetSingleton()->GetMapPage();
		}

		// Run a map-page action on the next frame (the map page's Scaleform
		// state may not be ready on the same frame a key is pressed).
		void RunOnTask(std::function<void()> a_fn)
		{
			if (auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask(std::move(a_fn));
			}
		}

		// True when a menu owns input (Pip-Boy, Container, etc.). The gameplay
		// object scanner and auto-walk are open-world features and must never
		// run while a menu is up: these actions are also dispatched from
		// MenuAccessibility::OnButtonEvent, so a scanner key pressed inside the
		// Pip-Boy on a non-map page would otherwise fall through to the gameplay
		// scanner. For Shift+Home (WalkToObject) that starts the player
		// auto-walking while the Pip-Boy stays open — and AutoWalk's cancel
		// driver only runs in gameplay, so the user gets trapped in a menu they
		// can't act in. The Pip-Boy Map page is handled separately above each
		// fallback via GetActiveMapPage(), so reaching a gameplay fallback while
		// in menu mode is always wrong. (Mirrors ObjectScanner's own menuMode
		// gate in ShouldHandleEvent.)
		bool InMenuMode()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->menuMode != 0;
		}

		void ScanObjects()
		{
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->ScanAndAnnounce();
		}

		void NextObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnNextMarker(); });
				return;
			}
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->SelectNext();
		}

		void PrevObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnPreviousMarker(); });
				return;
			}
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->SelectPrevious();
		}

		void NextCategory()
		{
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->NextCategory();
		}

		void PrevCategory()
		{
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->PreviousCategory();
		}

		void CycleSubcategory()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnCycleFilter(); });
				return;
			}
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->CycleSubcategory();
		}

		void AnnounceObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnAnnounceDetails(); });
				return;
			}
			if (InMenuMode()) {
				return;
			}
			ObjectScanner::GetSingleton()->LookAtCurrentObject();
			ObjectScanner::GetSingleton()->AnnounceCurrentObject();
		}

		void WalkToObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnSetReferencePoint(); });
				return;
			}
			if (InMenuMode()) {
				return;
			}
			if (AutoWalk::GetSingleton()->IsWalking()) {
				AutoWalk::GetSingleton()->Stop();
				SpeechManager::GetSingleton()->Speak("Stopped");
			} else {
				ObjectScanner::GetSingleton()->WalkToCurrentObject();
			}
		}

		void AnnounceStats()
		{
			// Context-dependent: check which menu is open
			auto* ui = RE::UI::GetSingleton();
			if (ui) {
				if (ui->GetMenu("BarterMenu"sv)) {
					BarterMenuAccessibility::GetSingleton()->AnnounceBarterStatus();
					return;
				}
				if (ui->GetMenu("ContainerMenu"sv)) {
					ContainerMenuAccessibility::GetSingleton()->AnnounceContainerStatus();
					return;
				}
				// LevelUpMenu sits on top of PipboyMenu, so check it first.
				if (ui->GetMenu("LevelUpMenu"sv)) {
					LevelUpMenuAccessibility::GetSingleton()->AnnounceLevelAndXP();
					return;
				}
				if (ui->GetMenu("PipboyMenu"sv)) {
					// Mirrors the in-game bottom bar: carry weight + caps
					// on INV, level + XP % on STAT, silent on the rest.
					PipBoyMenuAccessibility::GetSingleton()->AnnouncePageStats();
					return;
				}
				if (ui->GetMenu("WorkshopMenu"sv)) {
					WorkshopMenuAccessibility::GetSingleton()->AnnounceSettlementStats();
					return;
				}
				// Workbench mod menus: re-announce the selected improvement's full
				// characteristics on demand (stat changes + description), which the
				// automatic selection announcement can't always deliver in time.
				if (ui->GetMenu("ExamineMenu"sv) || ui->GetMenu("PowerArmorModMenu"sv) ||
					ui->GetMenu("CookingMenu"sv) || ui->GetMenu("RobotModMenu"sv)) {
					ExamineMenuHook::GetSingleton()->AnnounceCurrentDetails();
					return;
				}
			}
			// No blocking menu: in Fallout London, surface the active quest
			// countdown (FOLON_UITimer) here so the existing stats key reports
			// remaining time on demand. Falls through to combat stats otherwise.
			if (FolonTimerAccessibility::GetSingleton()->AnnounceCurrentTime()) {
				return;
			}
			ContextualInfoAnnouncer::GetSingleton()->AnnounceCombatStats();
		}

		void ActivateMarker()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}

			// PipboyMenu.CurrentPage stays at MAP (3) while a sub-menu like
			// the Perk Chart (LevelUpMenu) is up on top. Without this gate,
			// GetActiveMapPage() returns the map page and we'd fast travel on
			// the user's Enter/Accept inside the overlay.
			if (ui->GetMenu("LevelUpMenu"sv)) {
				REX::INFO("ActivateMarker: blocked (LevelUpMenu open)");
				return;
			}

			auto* mapPage = GetActiveMapPage();
			if (!mapPage) {
				REX::INFO("ActivateMarker: no active map page");
				return;
			}
			REX::INFO("ActivateMarker: dispatching to map page");
			RunOnTask([mapPage]() { mapPage->OnActivateMarker(); });
		}

		void SwitchMenuSide()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			if (ui->GetMenu("ContainerMenu"sv)) {
				ContainerMenuAccessibility::GetSingleton()->SwitchSides();
			} else if (ui->GetMenu("BarterMenu"sv)) {
				BarterMenuAccessibility::GetSingleton()->SwitchSides();
			}
		}
	}

	bool Dispatch(std::string_view a_actionId)
	{
		// All MCM-bound actions are user-initiated key presses where the user
		// expects an immediate, fresh announcement. Stop any in-flight speech
		// before invoking the action so its Speak() calls aren't queued behind
		// a stale announcement. Screen readers' built-in key-interrupt heuristic
		// only fires for a fixed set of keys (Ctrl, arrows), so we can't rely
		// on it now that users can rebind to arbitrary keys via MCM.
		// Done once here so individual actions don't need interrupt=true at
		// each Speak() callsite (some actions speak multiple times).
		SpeechManager::GetSingleton()->StopSpeech();

		// Action IDs match the INI key names (with 's' prefix required for
		// Bethesda's Setting class to treat them as strings).
		if (a_actionId == "sScanObjects") {
			ScanObjects();
		} else if (a_actionId == "sNextObject") {
			NextObject();
		} else if (a_actionId == "sPrevObject") {
			PrevObject();
		} else if (a_actionId == "sNextCategory") {
			NextCategory();
		} else if (a_actionId == "sPrevCategory") {
			PrevCategory();
		} else if (a_actionId == "sCycleSubcategory") {
			CycleSubcategory();
		} else if (a_actionId == "sAnnounceObject") {
			AnnounceObject();
		} else if (a_actionId == "sWalkToObject") {
			WalkToObject();
		} else if (a_actionId == "sAnnounceStats") {
			AnnounceStats();
		} else if (a_actionId == "sSwitchMenuSide") {
			SwitchMenuSide();
		} else if (a_actionId == "sActivateMarker") {
			ActivateMarker();
		} else {
			return false;
		}
		return true;
	}
}
