#include "AutoAimAccessibility.h"
#include "AutoWalk.h"
#include "BarterMenuAccessibility.h"
#include "BookMenuAccessibility.h"
#include "ContainerMenuAccessibility.h"
#include "CorpseLootTracker.h"
#include "CrosshairAccessibility.h"
#include "DialogueMenuHook.h"
#include "ExamineConfirmMenuHook.h"
#include "ExamineMenuHook.h"
#include "FreedomTrailAccessibility.h"
#include "GameplayStateAnnouncer.h"
#include "HUDMessageHook.h"
#include "HitMarkerAudioFeedback.h"
#include "ItemStatFormatter.h"
#include "LevelUpMenuHook.h"
#include "LoadingMenuHook.h"
#include "MCMKeybindDispatcher.h"
#include "MenuAccessibility.h"
#include "MessageBoxMenuHook.h"
#include "ObjectScanner.h"
#include "SleepWaitMenuAccessibility.h"
#include "SpeechManager.h"
#include "VATSCallbackHook.h"
#include "VATSMenuHook.h"
#include "WorkshopCaravanMenuAccessibility.h"
#include "WorkshopMenuAccessibility.h"
#include "pch.h"
#include <SRAL.h>

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
	if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
		// Game data is loaded - initialize components that need PlayerCharacter
		AutoWalk::GetSingleton()->Initialize();
		AutoAimAccessibility::GetSingleton()->Initialize();
		CrosshairAccessibility::GetSingleton()->Register();

		// Register corpse loot tracking event sink
		CorpseLootTracker::GetSingleton()->Register();

		// Register hit marker audio feedback for combat accessibility
		HitMarkerAudioFeedback::GetSingleton()->Register();

		// Register gameplay state announcer (view changes, sneak, detection)
		GameplayStateAnnouncer::GetSingleton()->Register();

		// Register Freedom Trail Ring puzzle accessibility (reports current letter
		// at the pointer when the player interacts with the ring)
		FreedomTrailAccessibility::GetSingleton()->Register();

		// Load damage type translations
		ItemStatFormatter::LoadTranslations();
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	// Register for F4SE messages (data loaded, etc.)
	auto* messaging = F4SE::GetMessagingInterface();
	if (messaging) {
		messaging->RegisterListener(OnF4SEMessage);
	}

	// Register cosave serialization for persistent data (corpse loot tracking)
	auto* serialization = F4SE::GetSerializationInterface();
	if (serialization) {
		serialization->SetUniqueID(CorpseLootTracker::kUniqueID);
		serialization->SetSaveCallback(CorpseLootTracker::OnSave);
		serialization->SetLoadCallback(CorpseLootTracker::OnLoad);
		serialization->SetRevertCallback(CorpseLootTracker::OnRevert);
	}

	// Initialize SRAL, excluding SAPI to prefer NVDA/JAWS
	REX::INFO("Initializing SRAL...");
	if (SRAL_Initialize(SRAL_ENGINE_SAPI)) {
		REX::INFO("SRAL initialized successfully");
	} else {
		REX::ERROR("SRAL initialization failed");
	}

	// Initialize speech manager (creates dedicated speech log)
	SpeechManager::GetSingleton()->Initialize();

	// Speak test message via centralized manager
	if (SRAL_GetCurrentEngine() != 0) {
		SpeechManager::GetSingleton()->Speak("Fallout 4 accessibility plugin loaded successfully");
	} else {
		REX::WARN("No speech engine available");
	}

	// Load MCM keybindings from settings.ini so our dispatcher has current
	// assignments. We handle dispatch ourselves (not MCM) so we can work in
	// pausing menus (Pip-Boy, Container, Barter) and skip MCM's conflict
	// checks against game controls.
	MCMKeybindDispatcher::GetSingleton()->LoadFromFile();

	// Register menu accessibility event handler
	MenuAccessibility::GetSingleton()->Register();

	// Register object scanner for gameplay
	ObjectScanner::GetSingleton()->Register();

	// Install HUD message hook for item pickups, location discoveries, etc.
	HUDMessageHook::GetSingleton()->Install();

	// Install BookMenu hook for book/note/holotape text accessibility
	BookMenuAccessibility::GetSingleton()->Install();

	// Install ExamineMenu hook for workbench accessibility
	ExamineMenuHook::GetSingleton()->Install();

	// Install ExamineConfirmMenu hook for workbench confirmation accessibility
	ExamineConfirmMenuHook::GetSingleton()->Install();

	// Install VATSMenu hooks for VATS accessibility
	VATSMenuHook::GetSingleton()->Install();
	VATSCallbackHook::GetSingleton()->Install();

	// Install ContainerMenu hook for looting accessibility
	ContainerMenuAccessibility::GetSingleton()->Install();

	// Install BarterMenu hook for trading accessibility
	BarterMenuAccessibility::GetSingleton()->Install();

	// Install LevelUpMenu hook for perk selection accessibility
	LevelUpMenuHook::GetSingleton()->Install();

	// Install MessageBoxMenu hook for dialog accessibility
	MessageBoxMenuHook::GetSingleton()->Install();

	// Install DialogueMenu hook for NPC conversation accessibility
	DialogueMenuHook::GetSingleton()->Install();

	// Install LoadingMenu hook for loading screen accessibility
	LoadingMenuHook::GetSingleton()->Install();

	// Install WorkshopMenu hook for building accessibility
	WorkshopMenuAccessibility::GetSingleton()->Install();

	// Install SleepWaitMenu/SitWaitMenu hooks for wait accessibility
	SleepWaitMenuAccessibility::GetSingleton()->Install();

	// Install Workshop_CaravanMenu hook for supply line accessibility
	WorkshopCaravanMenuAccessibility::GetSingleton()->Install();

	// Note: CrosshairAccessibility is registered in OnF4SEMessage when game data is ready
	// because it needs PlayerCharacter to be available

	return true;
}
