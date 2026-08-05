#include "VATSMenuAccessibility.h"
#include "SpeechManager.h"
#include "VATSMenuHook.h"
#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/C/Console.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/V/VATS.h>
#include <RE/V/VATSCommand.h>
#include <SRAL.h>

VATSMenuAccessibility* VATSMenuAccessibility::GetSingleton()
{
	static VATSMenuAccessibility singleton;
	return &singleton;
}

void VATSMenuAccessibility::OnVATSMenuOpened()
{
	m_isOpen = true;
	m_menuAnnounced = false;
	m_lastSelectedPart = 0xFFFFFFFF;
	m_lastQueuedShotCount = 0;
	m_lastTargetName.clear();
	m_lastCriticalPercent = -1;

	REX::INFO("VATSMenu opened - initializing accessibility");

	// Tell the hook the menu is open and reset its state.
	// VATSMenuHook handles target and body-part announcements (including A/D
	// target cycling) via its per-frame AdvanceMovie hook.
	VATSMenuHook::GetSingleton()->SetMenuOpen(true);
	VATSMenuHook::GetSingleton()->ResetState();

	// Announce "VATS" immediately
	SpeakText("VATS");
	m_menuAnnounced = true;

	// Critical charge announcement (still deferred to let game populate)
	if (auto* taskInterface = F4SE::GetTaskInterface()) {
		taskInterface->AddTask([this]() {
			if (!m_isOpen)
				return;

			float critCharge = GetCriticalChargePercent();
			if (critCharge > 0) {
				int critPercent = static_cast<int>(critCharge);
				m_lastCriticalPercent = critPercent;
				if (critPercent >= 100) {
					SpeakText("Critical ready");
				} else {
					SpeakText("Critical charge: " + std::to_string(critPercent) + " percent");
				}
			}
		});
	}
}

void VATSMenuAccessibility::OnVATSMenuClosed()
{
	m_isOpen = false;

	// Tell the hook the menu is closed
	VATSMenuHook::GetSingleton()->SetMenuOpen(false);

	// Silent exit per requirements - no announcement
	REX::INFO("VATSMenu closed");
	Reset();
}

void VATSMenuAccessibility::Reset()
{
	m_menuAnnounced = false;
	m_lastSelectedPart = 0xFFFFFFFF;
	m_lastQueuedShotCount = 0;
	m_lastTargetName.clear();
	m_lastCriticalPercent = -1;
}

void VATSMenuAccessibility::AnnounceBodyPartChange(std::uint32_t a_partIndex)
{
	if (!m_isOpen || a_partIndex == 0xFFFFFFFF)
		return;

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("VATSMenu"sv);
	if (!menu || !menu->uiMovie)
		return;

	std::string announcement = BuildPartAnnouncement(menu.get(), a_partIndex);
	if (!announcement.empty()) {
		SpeakText(announcement);
	}
}

void VATSMenuAccessibility::CheckQueuedShots()
{
	if (!m_isOpen)
		return;

	auto ui = RE::UI::GetSingleton();
	if (!ui)
		return;

	auto menu = ui->GetMenu("VATSMenu"sv);
	if (!menu || !menu->uiMovie)
		return;

	int currentQueued = GetQueuedShotCount();

	// Only announce when shots are added (count increases)
	if (currentQueued > m_lastQueuedShotCount) {
		m_lastQueuedShotCount = currentQueued;

		// Get current part info for the queued shot
		auto        partIndex = GetSelectedPartIndex(menu.get());
		std::string partName = GetPartName(menu.get(), partIndex);

		std::string announcement = "Shot queued";
		if (!partName.empty()) {
			announcement += ": " + partName;
		}
		announcement += ". " + std::to_string(currentQueued) + " shot";
		if (currentQueued != 1) {
			announcement += "s";
		}
		announcement += " queued";

		// Add AP remaining info
		float apRemaining = GetAPRemaining();
		if (apRemaining > 0) {
			announcement += ", " + std::to_string(static_cast<int>(apRemaining)) + " AP remaining";
		}

		SpeakText(announcement);
	} else if (currentQueued < m_lastQueuedShotCount) {
		// Update tracking but don't announce decreases
		m_lastQueuedShotCount = currentQueued;
	}
}

void VATSMenuAccessibility::CheckCriticalCharge()
{
	if (!m_isOpen)
		return;

	float critCharge = GetCriticalChargePercent();
	int   critPercent = static_cast<int>(critCharge);

	// Announce when critical becomes ready (crosses 100% threshold)
	if (critPercent >= 100 && m_lastCriticalPercent < 100 && m_lastCriticalPercent >= 0) {
		SpeakText("Critical ready");
	}

	m_lastCriticalPercent = critPercent;
}

std::string VATSMenuAccessibility::GetTargetName()
{
	// Try multiple sources for the current VATS target
	// Source 1: PlayerCharacter::commandTarget (updates when cycling targets with A/D)
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player && player->commandTarget) {
		const char* name = player->commandTarget->GetDisplayFullName();
		if (name && name[0] != '\0') {
			return std::string(name);
		}
	}

	// Source 2: Console pick ref (crosshair target - might update in VATS)
	auto pickRefHandle = RE::Console::GetPickRef();
	if (pickRefHandle) {
		auto* pickRef = pickRefHandle.get().get();
		if (pickRef) {
			const char* name = pickRef->GetDisplayFullName();
			if (name && name[0] != '\0') {
				return std::string(name);
			}
		}
	}

	// Source 3: PlayerCamera::cameraTarget (initial target on VATS entry)
	auto* camera = RE::PlayerCamera::GetSingleton();
	if (camera) {
		auto targetActor = camera->cameraTarget.get();
		if (targetActor) {
			const char* name = targetActor->GetDisplayFullName();
			if (name && name[0] != '\0') {
				return std::string(name);
			}
		}
	}

	// Source 4: VATS::VATSTarget (populated during playback mode)
	auto* vats = RE::VATS::GetSingleton();
	if (vats && vats->VATSTarget) {
		const char* name = vats->VATSTarget->GetDisplayFullName();
		if (name && name[0] != '\0') {
			return std::string(name);
		}
	}

	return "";
}

std::string VATSMenuAccessibility::GetTargetLevel(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return "";

	// Try to get level from Resistances panel (visible with Awareness perk)
	Scaleform::GFx::Value levelValue;
	if (a_menu->uiMovie->GetVariable(&levelValue, TARGET_LEVEL_PATH)) {
		if (levelValue.IsString()) {
			std::string level = levelValue.GetString();
			if (!level.empty() && level != "0") {
				return level;
			}
		}
	}

	// Fallback 1: PlayerCharacter::commandTarget (updates when cycling targets)
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player && player->commandTarget) {
		auto* actor = player->commandTarget->As<RE::Actor>();
		if (actor) {
			auto level = actor->GetLevel();
			return std::to_string(level);
		}
	}

	// Fallback 2: PlayerCamera::cameraTarget
	auto* camera = RE::PlayerCamera::GetSingleton();
	if (camera) {
		auto targetActor = camera->cameraTarget.get();
		if (targetActor) {
			auto level = targetActor->GetLevel();
			return std::to_string(level);
		}
	}

	// Fallback 3: VATS::VATSTarget (playback mode)
	auto* vats = RE::VATS::GetSingleton();
	if (vats && vats->VATSTarget) {
		auto* actor = vats->VATSTarget->As<RE::Actor>();
		if (actor) {
			auto level = actor->GetLevel();
			return std::to_string(level);
		}
	}

	return "";
}

std::uint32_t VATSMenuAccessibility::GetSelectedPartIndex(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return 0xFFFFFFFF;

	Scaleform::GFx::Value value;
	if (a_menu->uiMovie->GetVariable(&value, SELECTED_PART_PATH)) {
		if (value.IsUInt()) {
			return value.GetUInt();
		} else if (value.IsInt()) {
			auto intVal = value.GetInt();
			if (intVal >= 0) {
				return static_cast<std::uint32_t>(intVal);
			}
		} else if (value.IsNumber()) {
			auto numVal = value.GetNumber();
			if (numVal >= 0) {
				return static_cast<std::uint32_t>(numVal);
			}
		}
	}

	return 0xFFFFFFFF;
}

bool VATSMenuAccessibility::GetPartAnimationInstance(RE::IMenu* a_menu, std::uint32_t a_partIndex, Scaleform::GFx::Value* a_outAnim)
{
	if (!a_menu || !a_menu->uiMovie || a_partIndex == 0xFFFFFFFF || !a_outAnim)
		return false;

	// Get PartInfos array
	Scaleform::GFx::Value partInfos;
	if (!a_menu->uiMovie->GetVariable(&partInfos, PART_INFOS_PATH) || !partInfos.IsArray()) {
		return false;
	}

	if (a_partIndex >= partInfos.GetArraySize()) {
		return false;
	}

	// Get the part info at index
	Scaleform::GFx::Value partInfo;
	if (!partInfos.GetElement(a_partIndex, &partInfo) || !partInfo.IsObject()) {
		return false;
	}

	// Navigate: PartInfos[N].GraphicInstance.TintedContainer.AnimationInstance
	Scaleform::GFx::Value graphic, tinted;

	if (!partInfo.GetMember("GraphicInstance", &graphic) || !graphic.IsObject()) {
		return false;
	}

	if (!graphic.GetMember("TintedContainer", &tinted) || !tinted.IsObject()) {
		return false;
	}

	if (!tinted.GetMember("AnimationInstance", a_outAnim) || !a_outAnim->IsObject()) {
		return false;
	}

	return true;
}

std::string VATSMenuAccessibility::GetPartName(RE::IMenu* a_menu, std::uint32_t a_partIndex)
{
	Scaleform::GFx::Value anim;
	if (!GetPartAnimationInstance(a_menu, a_partIndex, &anim)) {
		return "";
	}

	// Navigate: AnimationInstance.Name.text
	Scaleform::GFx::Value nameObj, text;

	if (!anim.GetMember("Name", &nameObj) || !nameObj.IsObject()) {
		return "";
	}

	if (!nameObj.GetMember("text", &text) || !text.IsString()) {
		return "";
	}

	return text.GetString();
}

std::string VATSMenuAccessibility::GetPartHitChance(RE::IMenu* a_menu, std::uint32_t a_partIndex)
{
	Scaleform::GFx::Value anim;
	if (!GetPartAnimationInstance(a_menu, a_partIndex, &anim)) {
		return "";
	}

	// Navigate: AnimationInstance.HitChance.text
	Scaleform::GFx::Value hitChanceObj, text;

	if (!anim.GetMember("HitChance", &hitChanceObj) || !hitChanceObj.IsObject()) {
		return "";
	}

	if (!hitChanceObj.GetMember("text", &text) || !text.IsString()) {
		return "";
	}

	return text.GetString();
}

std::string VATSMenuAccessibility::GetPartHealth(RE::IMenu* a_menu, std::uint32_t a_partIndex)
{
	Scaleform::GFx::Value anim;
	if (!GetPartAnimationInstance(a_menu, a_partIndex, &anim)) {
		return "";
	}

	// Navigate: AnimationInstance.PartHealth.BarInstance.scaleX
	Scaleform::GFx::Value healthObj, barObj, scaleX;

	if (!anim.GetMember("PartHealth", &healthObj) || !healthObj.IsObject()) {
		return "";
	}

	if (!healthObj.GetMember("BarInstance", &barObj) || !barObj.IsObject()) {
		return "";
	}

	if (!barObj.GetMember("scaleX", &scaleX)) {
		return "";
	}

	double health = 0.0;
	if (scaleX.IsNumber()) {
		health = scaleX.GetNumber() * 100.0;  // Convert 0.0-1.0 to percentage
	}

	int healthPercent = static_cast<int>(health);
	return std::to_string(healthPercent);
}

std::uint32_t VATSMenuAccessibility::GetPartCount(RE::IMenu* a_menu)
{
	if (!a_menu || !a_menu->uiMovie)
		return 0;

	Scaleform::GFx::Value partInfos;
	if (!a_menu->uiMovie->GetVariable(&partInfos, PART_INFOS_PATH) || !partInfos.IsArray()) {
		return 0;
	}

	return partInfos.GetArraySize();
}

int VATSMenuAccessibility::GetQueuedShotCount()
{
	auto* vats = RE::VATS::GetSingleton();
	if (!vats)
		return 0;

	// VATS::commands array contains queued attacks
	return static_cast<int>(vats->commands.size());
}

float VATSMenuAccessibility::GetAPRemaining()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return 0.0f;

	// Get current action points
	auto* avSingleton = RE::ActorValue::GetSingleton();
	if (!avSingleton || !avSingleton->actionPoints)
		return 0.0f;

	float currentAP = player->GetActorValue(*avSingleton->actionPoints);

	// Subtract the cost of all queued attacks
	auto* vats = RE::VATS::GetSingleton();
	if (vats) {
		float totalQueuedCost = 0.0f;
		for (const auto& cmd : vats->commands) {
			if (cmd) {
				totalQueuedCost += cmd->actionPointCost;
			}
		}
		return currentAP - totalQueuedCost;
	}

	return currentAP;
}

float VATSMenuAccessibility::GetCriticalChargePercent()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return 0.0f;

	return player->vatsCriticalCharge;
}

std::string VATSMenuAccessibility::BuildFullAnnouncement(RE::IMenu* a_menu, std::uint32_t a_partIndex)
{
	std::string targetName = GetTargetName();
	std::string targetLevel = GetTargetLevel(a_menu);

	std::string announcement = "VATS";

	if (!targetName.empty()) {
		announcement += ". " + targetName;
		if (!targetLevel.empty() && targetLevel != "0") {
			announcement += ", Level " + targetLevel;
		}
	}

	if (a_partIndex != 0xFFFFFFFF) {
		std::string partAnnouncement = BuildPartAnnouncement(a_menu, a_partIndex);
		if (!partAnnouncement.empty()) {
			announcement += ". " + partAnnouncement;
		}
	}

	return announcement;
}

std::string VATSMenuAccessibility::BuildPartAnnouncement(RE::IMenu* a_menu, std::uint32_t a_partIndex)
{
	if (a_partIndex == 0xFFFFFFFF)
		return "";

	std::string partName = GetPartName(a_menu, a_partIndex);
	std::string hitChance = GetPartHitChance(a_menu, a_partIndex);
	std::string health = GetPartHealth(a_menu, a_partIndex);

	if (partName.empty()) {
		return "";
	}

	std::string announcement = partName;

	if (!hitChance.empty()) {
		announcement += ", " + hitChance + " percent hit chance";
	}

	if (!health.empty()) {
		announcement += ", " + health + " percent health";
	}

	return announcement;
}

void VATSMenuAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
