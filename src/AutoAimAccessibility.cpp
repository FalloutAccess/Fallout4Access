#include "AutoAimAccessibility.h"
#include "SpeechManager.h"
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IStackCallbackFunctor.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScript_Variable.h>
#include <RE/G/GameScript.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/T/TESDataHandler.h>
#include <RE/U/UIUtils.h>
#include <RE/V/VATS.h>
#include <cmath>

// IStackCallbackFunctor dtor is declared non-inline in CommonLibF4 but has no
// .cpp definition — it normally lives in the game binary.  Provide a trivial
// definition so our derived LOSSoundCallback can link.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

static AutoAimAccessibility* s_singleton = nullptr;

AutoAimAccessibility* AutoAimAccessibility::GetSingleton()
{
	if (!s_singleton) {
		s_singleton = new AutoAimAccessibility();
	}
	return s_singleton;
}

void AutoAimAccessibility::Initialize()
{
	if (m_initialized)
		return;

	m_initialized = true;
	REX::INFO("AutoAimAccessibility: Initialized");
}

void AutoAimAccessibility::Update()
{
	if (!m_enabled || !m_initialized)
		return;

	// Skip if in menu mode
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->menuMode) {
		return;
	}

	// Skip if in VATS - let VATS handle targeting
	// Check both menu open and playback mode (executing attacks)
	auto* vats = RE::VATS::GetSingleton();
	bool  inVATS = (ui && ui->GetMenuOpen("VATSMenu")) ||
	              (vats && vats->mode == RE::VATS::VATS_MODE_ENUM::kPlayback);
	if (inVATS) {
		if (m_isTracking) {
			StopTracking();
		}
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return;

	// Get current gun state from player's ActorState
	RE::GUN_STATE currentGunState = player->gunState;

	// Check for transition from non-sighted to sighted state
	bool wasSighted = (m_lastGunState == RE::GUN_STATE::kSighted ||
					   m_lastGunState == RE::GUN_STATE::kFireSighted);
	bool isSighted = (currentGunState == RE::GUN_STATE::kSighted ||
					  currentGunState == RE::GUN_STATE::kFireSighted);

	// Just entered ironsights
	if (!wasSighted && isSighted) {
		OnEnterIronsights();
	}
	// Still in ironsights - continue tracking
	else if (isSighted && m_isTracking) {
		UpdateTracking();
	}
	// Just exited ironsights
	else if (wasSighted && !isSighted) {
		StopTracking();
	}

	m_lastGunState = currentGunState;
}

void AutoAimAccessibility::OnEnterIronsights()
{
	REX::INFO("AutoAimAccessibility: Entered ironsights, searching for hostiles...");
	RE::Actor* nearestHostile = FindNearestHostileActor();

	if (nearestHostile) {
		REX::INFO("AutoAimAccessibility: Found hostile target");
		LockTarget(nearestHostile);
		RotateToTarget(nearestHostile);
		AnnounceTarget(nearestHostile);
		m_isTracking = true;
		m_trackingFrameCount = 0;

		// Start LOS sound if we have line of sight
		m_targetHasLOS = HasLineOfSight(nearestHostile);
		if (m_targetHasLOS) {
			REX::INFO("AutoAimAccessibility: Has LOS, playing sound");
			StartLOSSound();
		}
	} else {
		REX::INFO("AutoAimAccessibility: No hostile targets found");
	}
}

RE::Actor* AutoAimAccessibility::FindNearestHostileActor()
{
	auto* processLists = RE::ProcessLists::GetSingleton();
	if (!processLists)
		return nullptr;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return nullptr;

	// Get hostile actors from ProcessLists
	RE::BSScrapArray<RE::ActorHandle> hostileActors;
	bool                              hasHostiles = processLists->AreHostileActorsNear(&hostileActors);

	if (!hasHostiles || hostileActors.empty()) {
		return nullptr;
	}

	RE::Actor* bestActor = nullptr;
	float      bestScore = MAX_TARGET_DISTANCE * NO_LOS_PENALTY;  // Worst possible score

	for (const auto& handle : hostileActors) {
		if (!handle)
			continue;

		auto actorPtr = handle.get();
		if (!actorPtr)
			continue;

		RE::Actor* actor = actorPtr.get();
		if (!IsValidTarget(actor))
			continue;

		float score = CalculateTargetScore(actor);
		if (score < bestScore) {
			bestScore = score;
			bestActor = actor;
		}
	}

	return bestActor;
}

bool AutoAimAccessibility::IsValidTarget(RE::Actor* a_actor)
{
	if (!a_actor)
		return false;
	if (a_actor->IsDeleted() || a_actor->IsDisabled())
		return false;
	if (a_actor->IsDead(false))
		return false;

	// Make sure it's not the player
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (a_actor == player)
		return false;

	return true;
}

float AutoAimAccessibility::CalculateDistanceToActor(RE::Actor* a_actor)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !a_actor)
		return MAX_TARGET_DISTANCE + 1.0f;

	return player->GetPosition().GetDistance(a_actor->GetPosition());
}

void AutoAimAccessibility::RotateToTarget(RE::Actor* a_target)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !a_target)
		return;

	// Get player eye position (aiming origin)
	RE::NiPoint3 eyePos, eyeDir;
	player->GetEyeVector(eyePos, eyeDir, true);

	// Get target center (not feet position)
	RE::NiPoint3 targetCenter;
	a_target->GetObjectCenter(targetCenter);

	// Calculate direction from eye to target center
	RE::NiPoint3 direction = targetCenter - eyePos;

	// Calculate yaw (horizontal rotation) - atan2(x, y) gives angle from north
	float yaw = std::atan2(direction.x, direction.y);

	// Calculate pitch (vertical rotation)
	float horizontalDist = std::sqrt(direction.x * direction.x + direction.y * direction.y);
	float pitch = -std::atan2(direction.z, horizontalDist);

	// Apply rotation: SetAngleOnReference takes (pitch, roll, yaw)
	player->SetAngleOnReference(RE::NiPoint3(pitch, 0.0f, yaw));

	REX::INFO("AutoAimAccessibility: Rotated to target '{}' (yaw={:.2f}, pitch={:.2f})",
		a_target->GetDisplayFullName(), yaw, pitch);
}

void AutoAimAccessibility::AnnounceTarget(RE::Actor* a_target)
{
	if (!a_target)
		return;

	const char* name = a_target->GetDisplayFullName();
	if (!name || name[0] == '\0') {
		name = "Enemy";
	}

	float distance = CalculateDistanceToActor(a_target);
	int   distInt = static_cast<int>(distance);

	std::string announcement = std::string(name) + ", " + std::to_string(distInt) + " units";
	SpeechManager::GetSingleton()->Speak(announcement);
}

void AutoAimAccessibility::LockTarget(RE::Actor* a_target)
{
	if (!a_target)
		return;
	m_lockedTargetHandle = RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(a_target);
}

void AutoAimAccessibility::StopTracking()
{
	StopLOSSound();
	m_lockedTargetHandle = RE::ActorHandle();
	m_isTracking = false;
	m_trackingFrameCount = 0;
	m_targetHasLOS = false;
}

bool AutoAimAccessibility::IsLockedTargetValid()
{
	auto ptr = m_lockedTargetHandle.get();
	if (!ptr)
		return false;
	RE::Actor* actor = ptr.get();
	return IsValidTarget(actor);
}

void AutoAimAccessibility::UpdateTracking()
{
	if (!m_isTracking)
		return;

	// Rate limit: only update every TRACK_UPDATE_INTERVAL frames
	m_trackingFrameCount++;
	if (m_trackingFrameCount % TRACK_UPDATE_INTERVAL != 0) {
		return;
	}

	// Check if target still valid
	if (!IsLockedTargetValid()) {
		SpeechManager::GetSingleton()->Speak("Target lost");
		StopLOSSound();
		StopTracking();
		return;
	}

	// Re-aim at target (no announcement - silent tracking)
	auto       ptr = m_lockedTargetHandle.get();
	RE::Actor* target = ptr.get();

	// Check LOS and update sound
	bool hasLOS = HasLineOfSight(target);
	if (hasLOS && !m_losSoundPlaying) {
		StartLOSSound();
	} else if (!hasLOS && m_losSoundPlaying) {
		StopLOSSound();
	}
	m_targetHasLOS = hasLOS;

	RotateToTarget(target);
}

bool AutoAimAccessibility::HasLineOfSight(RE::Actor* a_target)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !a_target)
		return false;

	bool pickPerformed = false;
	return player->HasLOSToTarget(a_target, &pickPerformed);
}

float AutoAimAccessibility::CalculateTargetScore(RE::Actor* a_actor)
{
	float distance = CalculateDistanceToActor(a_actor);
	bool  hasLOS = HasLineOfSight(a_actor);

	// Targets without LOS get penalty (higher score = lower priority)
	return hasLOS ? distance : (distance * NO_LOS_PENALTY);
}

class LOSSoundCallback : public RE::BSScript::IStackCallbackFunctor
{
public:
	LOSSoundCallback(AutoAimAccessibility* a_owner) : m_owner(a_owner) {}

	void CallQueued() override {}
	void CallCanceled() override {}
	void StartMultiDispatch() override {}
	void EndMultiDispatch() override {}
	void operator()(RE::BSScript::Variable a_result) override
	{
		if (a_result.is<std::int32_t>()) {
			auto id = RE::BSScript::get<std::int32_t>(a_result);
			if (id > 0) {
				m_owner->SetLOSSoundInstanceID(static_cast<std::uint32_t>(id));
			}
		}
	}
	bool CanSave() override { return false; }

private:
	AutoAimAccessibility* m_owner;
};

void AutoAimAccessibility::StartLOSSound()
{
	if (m_losSoundPlaying)
		return;

	// Cache SNDR form lookup
	if (!m_losSoundForm) {
		m_losSoundForm = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSSoundDescriptorForm>(
			0x006C57, "fallout4access.esp");
		if (!m_losSoundForm) {
			REX::ERROR("AutoAimAccessibility: SNDR form 0x006C57 not found in fallout4access.esp");
			return;
		}
	}

	auto* gameVM = RE::GameVM::GetSingleton();
	if (!gameVM)
		return;
	auto vm = gameVM->GetVM();
	if (!vm)
		return;

	// Call F4A:SoundHelper.PlaySound(formID) via Papyrus VM
	// Script gets player reference internally via Game.GetPlayer()
	auto callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>(
		new LOSSoundCallback(this));

	bool dispatched = vm->DispatchStaticCall(
		RE::BSFixedString("F4A:SoundHelper"),
		RE::BSFixedString("PlaySound"),
		callback,
		static_cast<std::int32_t>(m_losSoundForm->GetFormID()));

	if (dispatched) {
		m_losSoundPlaying = true;
		m_pendingStop = false;
	} else {
		REX::ERROR("AutoAimAccessibility: DispatchStaticCall PlaySound failed");
	}
}

void AutoAimAccessibility::SetLOSSoundInstanceID(std::uint32_t a_id)
{
	m_losSoundInstanceID = a_id;

	// If a stop was requested before the callback arrived, stop immediately
	if (m_pendingStop && a_id != 0) {
		m_pendingStop = false;
		StopSoundInstance(a_id);
		m_losSoundInstanceID = 0;
	}
}

void AutoAimAccessibility::StopLOSSound()
{
	if (!m_losSoundPlaying)
		return;

	if (m_losSoundInstanceID != 0) {
		StopSoundInstance(m_losSoundInstanceID);
	} else {
		// Callback hasn't arrived yet - flag for stop when it does
		m_pendingStop = true;
	}

	m_losSoundPlaying = false;
	m_losSoundInstanceID = 0;
}

void AutoAimAccessibility::StopSoundInstance(std::uint32_t a_instanceID)
{
	auto* gameVM = RE::GameVM::GetSingleton();
	if (!gameVM)
		return;
	auto vm = gameVM->GetVM();
	if (!vm)
		return;

	vm->DispatchStaticCall(
		RE::BSFixedString("F4A:SoundHelper"),
		RE::BSFixedString("StopSound"),
		nullptr,
		static_cast<std::int32_t>(a_instanceID));
}
