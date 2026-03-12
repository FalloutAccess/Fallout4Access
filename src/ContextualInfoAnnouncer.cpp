#include "ContextualInfoAnnouncer.h"
#include "SpeechManager.h"
#include <RE/A/ActorValue.h>
#include <RE/B/BSSpinLock.h>
#include <RE/E/EquippedItem.h>
#include <RE/E/EquippedWeaponData.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/T/TESAmmo.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESObjectWEAP.h>

ContextualInfoAnnouncer* ContextualInfoAnnouncer::GetSingleton()
{
	static ContextualInfoAnnouncer singleton;
	return &singleton;
}

void ContextualInfoAnnouncer::AnnounceCombatStats()
{
	auto        stats = GatherCombatStats();
	std::string announcement = FormatCombatStatsAnnouncement(stats);

	if (!announcement.empty()) {
		SpeakText(announcement);
	} else {
		SpeakText("Stats unavailable");
	}
}

void ContextualInfoAnnouncer::AnnounceMenuContext(const std::string& a_menuName)
{
	// Placeholder for future menu-specific announcements
	// For now, just announce the menu name
	if (!a_menuName.empty()) {
		SpeakText("In " + a_menuName);
	}
}

ContextualInfoAnnouncer::CombatStats ContextualInfoAnnouncer::GatherCombatStats()
{
	CombatStats stats;

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		REX::WARN("ContextualInfoAnnouncer: PlayerCharacter not available");
		return stats;
	}

	// Get ActorValue singleton for health and AP
	auto* avSingleton = RE::ActorValue::GetSingleton();
	if (!avSingleton) {
		REX::WARN("ContextualInfoAnnouncer: ActorValue singleton not available");
		return stats;
	}

	// Get health values
	if (avSingleton->health) {
		stats.healthCurrent = player->GetActorValue(*avSingleton->health);
		stats.healthMax = player->GetPermanentActorValue(*avSingleton->health);
	}

	// Get AP values
	if (avSingleton->actionPoints) {
		stats.apCurrent = player->GetActorValue(*avSingleton->actionPoints);
		stats.apMax = player->GetPermanentActorValue(*avSingleton->actionPoints);
	}

	// Get radiation level
	if (avSingleton->rads) {
		stats.rads = player->GetActorValue(*avSingleton->rads);
	}

	// Get equipped weapon info from MiddleHighProcessData
	if (player->currentProcess && player->currentProcess->middleHigh) {
		auto* middleHigh = player->currentProcess->middleHigh;

		// Use RAII lock guard for exception safety
		RE::BSAutoLock lock(middleHigh->equippedItemsLock);

		for (const auto& equipped : middleHigh->equippedItems) {
			if (!equipped.item.object)
				continue;

			// Check if this is a weapon
			if (equipped.item.object->GetFormType() != RE::ENUM_FORM_ID::kWEAP)
				continue;

			// Get weapon name using TESFullName static method
			auto weaponNameStr = RE::TESFullName::GetFullName(*equipped.item.object);
			if (!weaponNameStr.empty()) {
				stats.weaponName = std::string(weaponNameStr);
				stats.hasWeapon = true;
			}

			// Get ammo info - check if weapon has ammo type (indicates ranged weapon)
			// Cast to TESObjectWEAP to check if it has ammo defined
			auto* weapon = equipped.item.object->As<RE::TESObjectWEAP>();
			if (weapon && weapon->IsRangedWeapon()) {
				// This is a ranged weapon, try to get ammo data
				if (equipped.data) {
					auto* weaponData = equipped.data.get();
					if (weaponData) {
						// Safe cast - we've verified this is a ranged weapon
						auto* ewData = static_cast<RE::EquippedWeaponData*>(weaponData);
						if (ewData->ammo) {
							stats.isRangedWeapon = true;
							stats.clipAmmo = ewData->ammoCount;

							// Get reserve ammo count from inventory
							stats.reserveAmmo = player->GetInventoryObjectCount(ewData->ammo);
						}
					}
				}
			}

			// Found a weapon, stop looking
			break;
		}
	}

	return stats;
}

std::string ContextualInfoAnnouncer::FormatCombatStatsAnnouncement(const CombatStats& a_stats)
{
	std::string announcement;

	// Health: "Health 85 of 100"
	int healthCurrent = static_cast<int>(a_stats.healthCurrent);
	int healthMax = static_cast<int>(a_stats.healthMax);
	announcement += "Health " + std::to_string(healthCurrent) + " of " + std::to_string(healthMax);

	// AP: ", 120 AP"
	int apCurrent = static_cast<int>(a_stats.apCurrent);
	announcement += ", " + std::to_string(apCurrent) + " AP";

	// Rads: ", 50 rads" (only if irradiated)
	int rads = static_cast<int>(a_stats.rads);
	if (rads > 0) {
		announcement += ", " + std::to_string(rads) + " rads";
	}

	// Weapon and ammo
	if (a_stats.hasWeapon) {
		announcement += ", " + a_stats.weaponName;

		// Only add ammo info for ranged weapons
		if (a_stats.isRangedWeapon) {
			announcement += ", " + std::to_string(a_stats.clipAmmo) + " rounds";
			announcement += ", " + std::to_string(a_stats.reserveAmmo) + " reserve";
		}
	}

	return announcement;
}

void ContextualInfoAnnouncer::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
