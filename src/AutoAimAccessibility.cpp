#include "AutoAimAccessibility.h"
#include "SpeechManager.h"
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IStackCallbackFunctor.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScript_Variable.h>
#include <RE/B/bhkPickData.h>
#include <RE/G/GameScript.h>
#include <RE/N/NiAVObject.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/U/UIUtils.h>
#include <RE/V/VATS.h>
#include <array>
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
		// RotateToTarget returns true when a visible body part was found and
		// aimed at — that doubles as our fine-grained LOS signal.
		m_targetHasLOS = RotateToTarget(nearestHostile);
		AnnounceTarget(nearestHostile);
		m_isTracking = true;
		m_trackingFrameCount = 0;

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

bool AutoAimAccessibility::RotateToTarget(RE::Actor* a_target)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player || !a_target)
		return false;

	// Get player eye position (aiming origin)
	RE::NiPoint3 eyePos, eyeDir;
	player->GetEyeVector(eyePos, eyeDir, true);

	// Try to find the best visible body part; fall back to object center if no
	// bones resolve or none are visible. For non-humanoids (turrets, robots) the
	// humanoid bone vocabulary rarely matches, so the center-visibility check is
	// the real signal.
	RE::NiPoint3 aimPoint;
	const char*  chosenBone = nullptr;
	int          resolvedBones = 0;
	bool         foundVisible = FindBestAimPoint(a_target, aimPoint, chosenBone, resolvedBones);
	if (!foundVisible) {
		a_target->GetObjectCenter(aimPoint);
		if (IsPointVisibleFromEye(eyePos, aimPoint, a_target)) {
			foundVisible = true;
			chosenBone = "center";
		}
	}

	// Calculate direction from eye to aim point
	RE::NiPoint3 direction = aimPoint - eyePos;

	// Calculate yaw (horizontal rotation) - atan2(x, y) gives angle from north
	float yaw = std::atan2(direction.x, direction.y);

	// Calculate pitch (vertical rotation)
	float horizontalDist = std::sqrt(direction.x * direction.x + direction.y * direction.y);
	float pitch = -std::atan2(direction.z, horizontalDist);

	// Apply rotation: SetAngleOnReference takes (pitch, roll, yaw)
	player->SetAngleOnReference(RE::NiPoint3(pitch, 0.0f, yaw));

	const char* name = a_target->GetDisplayFullName();
	if (!name || !*name)
		name = "<unnamed>";
	const char* aimLabel = foundVisible ? chosenBone : (resolvedBones == 0 ? "center(no-bones)" : "center(all-blocked)");
	REX::INFO("AutoAim: '{}' aim=[{}] bonesResolved={} pos=({:.0f},{:.0f},{:.0f}) yaw={:.2f} pitch={:.2f}",
		name, aimLabel, resolvedBones, aimPoint.x, aimPoint.y, aimPoint.z, yaw, pitch);

	return foundVisible;
}

// Priority-ordered list of skeleton bone names to try as aim points. First
// match wins. Vocabulary comes from extracted FO4 skeleton NIFs (see
// tools/nif_bones.py) — covers humans, creatures (deathclaw, supermutant,
// ghoul), synths, turrets (standing + mounted), and robots (Mr Handy etc.).
static constexpr std::array<const char*, 15> kAimBoneCandidates = {
	"Chest",    // human upper torso — primary humanoid aim
	"BODY",     // turret main body (above mount, usually visible)
	"Spine02",  // creature upper spine (deathclaw / supermutant / ghoul)
	"COM",      // universal center of mass — catches robots and anything else
	"SPINE2",   // humanoid upper spine alt caps
	"HEAD",     // head shots / peek
	"Spine01",
	"SPINE1",
	"Brain",          // turret / robot core (visible when base is obscured)
	"Pelvis",         // lower torso / turret base
	"Gun",            // turret weapon (visible above low cover)
	"RArm_UpperArm",  // shoulder peeks
	"LArm_UpperArm",
	"RLeg_Thigh",  // legs below low cover
	"LLeg_Thigh",
};

bool AutoAimAccessibility::FindBestAimPoint(
	RE::Actor*    a_target,
	RE::NiPoint3& a_outPoint,
	const char*&  a_outBoneName,
	int&          a_outBonesResolved)
{
	a_outBoneName = nullptr;
	a_outBonesResolved = 0;

	if (!a_target)
		return false;

	auto* target3D = a_target->Get3D();
	if (!target3D)
		return false;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return false;

	RE::NiPoint3 eyePos, eyeDir;
	player->GetEyeVector(eyePos, eyeDir, true);

	for (const char* boneName : kAimBoneCandidates) {
		auto* node = target3D->GetObjectByName(RE::BSFixedString(boneName));
		if (!node)
			continue;
		a_outBonesResolved++;

		const RE::NiPoint3& bonePos = node->world.translate;
		if (IsPointVisibleFromEye(eyePos, bonePos, a_target)) {
			a_outPoint = bonePos;
			a_outBoneName = boneName;
			return true;
		}
	}

	return false;
}

bool AutoAimAccessibility::IsPointVisibleFromEye(
	const RE::NiPoint3& a_eyePos,
	const RE::NiPoint3& a_point,
	RE::Actor*          a_target)
{
	if (!a_target)
		return false;

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = (player ? player->GetParentCell() : nullptr);
	if (!cell)
		cell = a_target->GetParentCell();
	if (!cell)
		return false;

	RE::bhkPickData pickData;
	pickData.SetStartEnd(a_eyePos, a_point);
	(void)cell->Pick(pickData);

	if (!pickData.HasHit())
		return true;  // Nothing in the way at all.

	// bhkPickData returns the hit position in Havok world units, not game units.
	// Convert with the standard Bethesda scale (1 havok unit ≈ 69.99 game units)
	// before comparing against the eye/bone positions, which are in game units.
	constexpr float kHavokToGame = 69.99125f;
	const float*    q = reinterpret_cast<const float*>(&pickData.result.position.quad);
	RE::NiPoint3    hitPos(q[0] * kHavokToGame, q[1] * kHavokToGame, q[2] * kHavokToGame);

	// A ray to a bone always pierces the target's own collision capsule before
	// reaching the bone, so "hit near the bone" means no cover between us and
	// the target. "Hit well short of the bone" means real cover is in the way.
	// Threshold is generous enough to cover thin limb capsules (~20-30u radius)
	// plus a small margin.
	constexpr float kBodyHitRadius = 60.0f;
	const float     distFromBone = (hitPos - a_point).Length();
	return distFromBone < kBodyHitRadius;
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

	// Re-aim at target (no announcement - silent tracking).
	// RotateToTarget re-evaluates which body part is currently uncovered and
	// returns true when it rotated to a visible bone — we reuse that as the
	// fine-grained LOS signal so the sound tracks cover state, not just
	// whole-actor visibility.
	auto       ptr = m_lockedTargetHandle.get();
	RE::Actor* target = ptr.get();
	bool       hasLOS = RotateToTarget(target);

	if (hasLOS && !m_losSoundPlaying) {
		StartLOSSound();
	} else if (!hasLOS && m_losSoundPlaying) {
		StopLOSSound();
	}
	m_targetHasLOS = hasLOS;
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
