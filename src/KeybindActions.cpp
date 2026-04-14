#include "KeybindActions.h"
#include "AutoWalk.h"
#include "BarterMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "ContextualInfoAnnouncer.h"
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

		void ScanObjects()
		{
			ObjectScanner::GetSingleton()->ScanAndAnnounce();
		}

		void NextObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnNextMarker(); });
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
			ObjectScanner::GetSingleton()->SelectPrevious();
		}

		void NextCategory()
		{
			ObjectScanner::GetSingleton()->NextCategory();
		}

		void PrevCategory()
		{
			ObjectScanner::GetSingleton()->PreviousCategory();
		}

		void CycleSubcategory()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnCycleFilter(); });
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
			ObjectScanner::GetSingleton()->LookAtCurrentObject();
			ObjectScanner::GetSingleton()->AnnounceCurrentObject();
		}

		void WalkToObject()
		{
			if (auto* mapPage = GetActiveMapPage()) {
				RunOnTask([mapPage]() { mapPage->OnSetReferencePoint(); });
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
				if (ui->GetMenu("PipboyMenu"sv)) {
					PipBoyMenuAccessibility::GetSingleton()->AnnounceInventoryStatus();
					return;
				}
				if (ui->GetMenu("WorkshopMenu"sv)) {
					WorkshopMenuAccessibility::GetSingleton()->AnnounceSettlementStats();
					return;
				}
			}
			ContextualInfoAnnouncer::GetSingleton()->AnnounceCombatStats();
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
		} else {
			return false;
		}
		return true;
	}
}
