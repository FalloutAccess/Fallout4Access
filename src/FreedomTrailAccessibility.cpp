#include "FreedomTrailAccessibility.h"
#include "SpeechManager.h"
#include <array>
#include <chrono>
#include <cmath>
#include <thread>

namespace
{
	// FormIDs from Fallout4.esm (Railroad quest RR101).
	// Spinner / submit IDs are the placed REFRs (what GetFormID returns at
	// runtime), NOT the base ACTI IDs from the dump. The puzzle ring REFR
	// whose Z-angle we read is also a REFR.
	constexpr RE::TESFormID kClockwiseSpinnerID = 0x00019B43;         // REFR placing ACTI 0x00019B19
	constexpr RE::TESFormID kCounterClockwiseSpinnerID = 0x00019B2C;  // REFR placing ACTI 0x00019B1F
	constexpr RE::TESFormID kCenterRingID = 0x00108092;               // REFR placing ACTI 0x00108091
	constexpr RE::TESFormID kPuzzleRingActivatorID = 0x00108090;      // REFR placing ACTI 0x000E168E

	struct LetterMarker
	{
		float rotationDegrees;
		char  letter;
	};

	// 21 letter positions. Values copied verbatim from the MarkerLetters array
	// property on RR101TrailSpinMarker01 (base ACTI 0x000E168E). Reading them
	// in this order spells "THE FREEDOM TRAIL BOSTON", which matches the text
	// carved around the ring's outer edge.
	constexpr std::array<LetterMarker, 21> kMarkerLetters{ {
		{ 65.96f, 'T' },
		{ 51.06f, 'H' },
		{ 35.42f, 'E' },
		{ 12.56f, 'F' },
		{ 0.00f, 'R' },
		{ -14.89f, 'E' },
		{ -29.05f, 'E' },
		{ -42.80f, 'D' },
		{ -58.73f, 'O' },
		{ -72.28f, 'M' },
		{ -101.07f, 'T' },
		{ -115.05f, 'R' },
		{ -130.52f, 'A' },
		{ -143.18f, 'I' },
		{ -154.92f, 'L' },
		{ 174.88f, 'B' },
		{ 159.69f, 'O' },
		{ 145.03f, 'S' },
		{ 130.53f, 'T' },
		{ 115.12f, 'O' },
		{ 98.22f, 'N' },
	} };

	// Max distance (in degrees) between runtime angle and a table entry for us
	// to accept that letter. Word-boundary gaps on the dial are ~30°, so 10° is
	// a comfortable threshold for "clearly on a letter".
	constexpr float kLetterSnapToleranceDegrees = 10.0f;

	float RadiansToDegrees(float a_radians)
	{
		return a_radians * (180.0f / 3.14159265f);
	}

	// Returns the signed angular difference a-b normalized to [-180, 180].
	float AngleDiffDegrees(float a_a, float a_b)
	{
		float d = a_a - a_b;
		while (d > 180.0f) {
			d -= 360.0f;
		}
		while (d < -180.0f) {
			d += 360.0f;
		}
		return d;
	}
}

FreedomTrailAccessibility* FreedomTrailAccessibility::GetSingleton()
{
	static FreedomTrailAccessibility singleton;
	return &singleton;
}

void FreedomTrailAccessibility::Register()
{
	if (m_registered) {
		return;
	}
	auto* source = RE::TESActivateEvent::GetEventSource();
	if (!source) {
		REX::ERROR("FreedomTrailAccessibility: TESActivateEvent source unavailable");
		return;
	}
	source->RegisterSink(this);
	m_registered = true;
	REX::INFO("FreedomTrailAccessibility registered");
}

FreedomTrailAccessibility::RingRole FreedomTrailAccessibility::ClassifyRef(RE::TESObjectREFR* a_ref) const
{
	if (!a_ref) {
		return RingRole::None;
	}
	switch (a_ref->GetFormID()) {
		case kClockwiseSpinnerID:
			return RingRole::RotateClockwise;
		case kCounterClockwiseSpinnerID:
			return RingRole::RotateCounterClockwise;
		case kCenterRingID:
			return RingRole::Submit;
		default:
			return RingRole::None;
	}
}

char FreedomTrailAccessibility::GetCurrentLetter() const
{
	auto* form = RE::TESForm::GetFormByID(kPuzzleRingActivatorID);
	if (!form) {
		return '\0';
	}
	auto* ringRef = form->As<RE::TESObjectREFR>();
	if (!ringRef) {
		return '\0';
	}

	const float currentDeg = RadiansToDegrees(ringRef->data.angle.z);

	float bestAbsDiff = 360.0f;
	char  bestLetter = '\0';
	for (const auto& entry : kMarkerLetters) {
		const float absDiff = std::abs(AngleDiffDegrees(currentDeg, entry.rotationDegrees));
		if (absDiff < bestAbsDiff) {
			bestAbsDiff = absDiff;
			bestLetter = entry.letter;
		}
	}

	if (bestAbsDiff > kLetterSnapToleranceDegrees) {
		return '\0';
	}
	return bestLetter;
}

std::optional<std::string> FreedomTrailAccessibility::GetLetterSuffix(RE::TESObjectREFR* a_ref)
{
	if (ClassifyRef(a_ref) == RingRole::None) {
		return std::nullopt;
	}
	const char letter = GetCurrentLetter();
	if (letter == '\0') {
		return std::string(", current letter unknown");
	}
	return std::string(", current letter ") + letter;
}

RE::BSEventNotifyControl FreedomTrailAccessibility::ProcessEvent(
	const RE::TESActivateEvent& a_event,
	RE::BSTEventSource<RE::TESActivateEvent>*)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* actionRef = a_event.actionRef.get();
	auto* activated = a_event.objectActivated.get();
	if (!player || !actionRef || actionRef != player || !activated) {
		return RE::BSEventNotifyControl::kContinue;
	}

	const RingRole role = ClassifyRef(activated);
	if (role == RingRole::None) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// The ring's Papyrus script animates the rotation over ~1 second. Read the
	// angle after that settles so we report the NEW letter at the pointer.
	std::thread([role]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
		auto* task = F4SE::GetTaskInterface();
		if (!task) {
			return;
		}
		task->AddTask([role]() {
			auto*       self = FreedomTrailAccessibility::GetSingleton();
			const char  letter = self->GetCurrentLetter();
			std::string msg;
			if (role == RingRole::Submit) {
				if (letter != '\0') {
					msg = std::string("Submitted letter ") + letter;
				} else {
					msg = "Submitted";
				}
			} else {
				if (letter != '\0') {
					msg = std::string("Letter ") + letter;
				} else {
					msg = "Between letters";
				}
			}
			SpeechManager::GetSingleton()->Speak(msg);
		});
	}).detach();

	return RE::BSEventNotifyControl::kContinue;
}
