#include "CrosshairAccessibility.h"
#include "SpeechManager.h"
#include <SRAL.h>
#include <chrono>

static std::uint64_t GetCurrentTimeMs()
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

CrosshairAccessibility* CrosshairAccessibility::GetSingleton()
{
	static CrosshairAccessibility singleton;
	return &singleton;
}

void CrosshairAccessibility::Register()
{
	if (m_registered) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		REX::WARN("CrosshairAccessibility: PlayerCharacter not available");
		return;
	}

	// PlayerCharacter inherits from BSTEventSource<PickRefUpdateEvent>
	auto* eventSource = static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent>*>(player);
	eventSource->RegisterSink(this);
	m_registered = true;
	REX::INFO("CrosshairAccessibility registered");
}

void CrosshairAccessibility::Unregister()
{
	if (!m_registered) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		auto* eventSource = static_cast<RE::BSTEventSource<RE::PickRefUpdateEvent>*>(player);
		eventSource->UnregisterSink(this);
		m_registered = false;
		REX::INFO("CrosshairAccessibility unregistered");
	}
}

RE::BSEventNotifyControl CrosshairAccessibility::ProcessEvent(
	const RE::PickRefUpdateEvent& a_event,
	RE::BSTEventSource<RE::PickRefUpdateEvent>*)
{
	// Don't announce while in menus
	auto* ui = RE::UI::GetSingleton();
	if (ui && ui->menuMode) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Get the target reference from the event
	// ObjectRefHandle.get() returns NiPointer<TESObjectREFR>
	// NiPointer.get() returns TESObjectREFR*
	auto targetRef = a_event.target.get();
	auto now = GetCurrentTimeMs();

	if (!targetRef || !targetRef.get()) {
		// Crosshair is no longer pointing at anything
		// If it's been long enough since we had a target, reset tracking
		// so looking back at the same object will announce it again
		auto lastTime = m_lastTargetTime.load();
		if (lastTime > 0 && (now - lastTime) > kResetDelayMs) {
			m_lastAnnouncedFormID.store(0);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	auto* ref = targetRef.get();

	// Update the time we last had a valid target
	m_lastTargetTime.store(now);

	// Check if this is a new target (different from last announced)
	RE::TESFormID currentFormID = ref->GetFormID();
	RE::TESFormID lastFormID = m_lastAnnouncedFormID.load();

	// If same object as last announced, skip entirely
	if (currentFormID == lastFormID) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Try to atomically claim this announcement (prevent multiple threads)
	if (!m_lastAnnouncedFormID.compare_exchange_strong(lastFormID, currentFormID)) {
		// Another thread beat us, skip
		return RE::BSEventNotifyControl::kContinue;
	}

	AnnounceTarget(ref);

	return RE::BSEventNotifyControl::kContinue;
}

void CrosshairAccessibility::AnnounceTarget(RE::TESObjectREFR* a_ref)
{
	if (!a_ref) {
		return;
	}

	// Try to get the full activate text first (e.g., "Use Overseer's Terminal")
	std::string announcement = GetActivateText(a_ref);

	// Fall back to just the name if no activate text
	if (announcement.empty()) {
		const char* name = a_ref->GetDisplayFullName();
		if (!name || name[0] == '\0') {
			return;
		}
		announcement = name;
	}

	REX::INFO("CrosshairAccessibility: '{}'", announcement);
	SpeakText(announcement);
}

std::string CrosshairAccessibility::GetActivateText(RE::TESObjectREFR* a_ref)
{
	if (!a_ref) {
		return "";
	}

	// Try to get the activate text from the base object
	auto* baseObj = a_ref->GetObjectReference();
	if (!baseObj) {
		return "";
	}

	RE::BSString activateText;
	if (baseObj->GetActivateText(a_ref, activateText)) {
		const char* text = activateText.c_str();
		if (text && text[0] != '\0') {
			std::string result = text;

			// The game returns "ObjectName\nAction" format
			// We want "Action ObjectName" format
			auto newlinePos = result.find('\n');
			if (newlinePos != std::string::npos) {
				std::string objectName = result.substr(0, newlinePos);
				std::string action = result.substr(newlinePos + 1);
				// Trim any trailing whitespace from action
				while (!action.empty() && (action.back() == ' ' || action.back() == '\r' || action.back() == '\n')) {
					action.pop_back();
				}
				if (!action.empty()) {
					return action + " " + objectName;
				}
				return objectName;
			}
			return result;
		}
	}

	return "";
}

void CrosshairAccessibility::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
