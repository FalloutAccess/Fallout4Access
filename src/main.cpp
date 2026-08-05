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
#include "VATSMenuHook.h"
#include "WorkshopCaravanMenuAccessibility.h"
#include "WorkshopMenuAccessibility.h"
#include "pch.h"
#include <SRAL.h>

namespace
{
	// Initialize the screen reader (SRAL), excluding SAPI so NVDA/JAWS are
	// preferred, then announce that the plugin loaded. Runs at most once.
	//
	// IMPORTANT: SRAL_Initialize spawns a worker thread and creates COM/WASAPI
	// objects. It must NOT run while the Windows loader lock is held, or it
	// deadlocks. On OG the xSE PluginPreloader loads us during early process
	// init (under the loader lock), so this is deferred to a later F4SE message.
	void InitializeScreenReader()
	{
		static bool s_done = false;
		if (s_done) {
			return;
		}
		s_done = true;

		REX::INFO("Initializing SRAL...");
		if (SRAL_Initialize(SRAL_ENGINE_SAPI)) {
			REX::INFO("SRAL initialized successfully");
		} else {
			REX::ERROR("SRAL initialization failed");
		}

		// Speak test message via centralized manager
		if (SRAL_GetCurrentEngine() != 0) {
			SpeechManager::GetSingleton()->Speak("Fallout 4 accessibility plugin loaded successfully");
		} else {
			REX::WARN("No speech engine available");
		}
	}

#ifdef SUPPORT_OG
	// OG-only setup deferred out of F4SEPlugin_Load. The xSE PluginPreloader loads
	// us under the loader lock and before the UI singleton exists, so anything
	// needing either (SRAL's thread/COM init; the menu-event sink) is run here
	// instead. Every step is idempotent, so it is safe to invoke more than once.
	void RunOGDeferredSetup()
	{
		InitializeScreenReader();
		MenuAccessibility::GetSingleton()->Register();
	}

	// Schedules RunOGDeferredSetup on the F4SE task interface, retrying a bounded
	// number of frames until the UI singleton exists. Tasks first run when the
	// game's main loop starts — which on OG is many seconds BEFORE the main menu
	// and far earlier than any F4SE message (kInputLoaded/kGameDataReady don't
	// fire until every master has loaded, ~tens of seconds in). This is what lets
	// speech be ready and the menu sink catch the main-menu open event.
	void ScheduleOGDeferredSetup(int a_retries)
	{
		auto* task = F4SE::GetTaskInterface();
		if (!task) {
			return;  // No task interface; the kGameDataReady fallback still covers us.
		}
		task->AddTask([a_retries]() {
			if (RE::UI::GetSingleton()) {
				RunOGDeferredSetup();
			} else if (a_retries > 0) {
				ScheduleOGDeferredSetup(a_retries - 1);
			}
		});
	}
#endif
}

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
#ifdef SUPPORT_OG
	// Fallback for the OG deferred setup. The primary path is the task interface
	// (scheduled from Load, runs ~when the game loop starts). These messages fire
	// much later on OG but guarantee the setup runs even if tasks never did.
	if (REX::FModule::IsRuntimeOG() &&
		(a_msg->type == F4SE::MessagingInterface::kInputLoaded ||
			a_msg->type == F4SE::MessagingInterface::kGameDataReady)) {
		RunOGDeferredSetup();
	}
#endif

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

#ifdef SUPPORT_OG
// --- Pre-next-gen (OG, 1.10.163) F4SE compatibility ---
// The original F4SE only understands the legacy F4SEPlugin_Query / F4SEPlugin_Load
// entry points and hands plugins a smaller (48-byte) interface that ends after
// GetPluginInfo, with no GetSaveFolderName pointer. CommonLibF4's F4SE::Init calls
// GetSaveFolderName during initialization, so on OG we copy the 48 valid bytes into
// a full-size interface and install a stub before initializing (see F4SE/API.cpp).
namespace
{
	constexpr std::size_t  kOGInterfaceSize = 48;  // through GetPluginInfo; no GetSaveFolderName
	constexpr REL::Version kOGRuntimeVersion{ 1, 10, 163, 0 };

	const char* F4SEAPI OG_GetSaveFolderName()
	{
		return "Fallout4";
	}
}

F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_query, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = "fallout4access";
	a_info->version = REL::Version{ 0, 1, 0, 0 }.pack();

	return a_query->RuntimeVersion() == kOGRuntimeVersion;
}
#endif

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
#ifdef SUPPORT_OG
	if (REX::FModule::IsRuntimeOG()) {
		// OG's interface is 48 bytes and lacks GetSaveFolderName; copy the valid
		// bytes into a full-size struct and stub the missing pointer so F4SE::Init
		// (which dereferences GetSaveFolderName) does not read past the struct.
		static F4SE::Impl::F4SEInterface ogInterface{};
		std::memcpy(&ogInterface, a_f4se, kOGInterfaceSize);
		ogInterface.GetSaveFolderName = &OG_GetSaveFolderName;
		F4SE::Init(reinterpret_cast<const F4SE::LoadInterface*>(&ogInterface));
	} else {
		F4SE::Init(a_f4se);
	}
#else
	F4SE::Init(a_f4se);
#endif

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

	// Initialize speech manager (creates dedicated speech log). Safe during the
	// early plugin-load window: it only resolves the Documents path and opens a
	// file, with no thread/COM work.
	SpeechManager::GetSingleton()->Initialize();

	// Initialize the screen reader. On OG this must be deferred: the xSE
	// PluginPreloader loads us under the loader lock (where SRAL's thread + COM
	// init would deadlock) and before the UI singleton exists. We schedule it on
	// the F4SE task interface, which runs once the game loop starts — early
	// enough to beat the main menu. On AE we load at the normal (post-lock) time,
	// so initialize immediately.
#ifdef SUPPORT_OG
	if (REX::FModule::IsRuntimeOG()) {
		REX::INFO("OG runtime: scheduling deferred screen-reader/menu setup via task interface");
		ScheduleOGDeferredSetup(120);  // ~120 frames of retry budget for the UI singleton
	} else
#endif
	{
		InitializeScreenReader();
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

	// Install VATSMenu hook for VATS accessibility (per-frame AdvanceMovie poll
	// handles both initial target and A/D target cycling)
	VATSMenuHook::GetSingleton()->Install();

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
