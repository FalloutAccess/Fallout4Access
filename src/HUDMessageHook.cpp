#include "HUDMessageHook.h"
#include "InstructionLength.h"
#include "SpeechManager.h"

HUDMessageHook* HUDMessageHook::GetSingleton()
{
	static HUDMessageHook singleton;
	return &singleton;
}

void HUDMessageHook::Install()
{
	// Installation guard - prevent double installation
	if (m_installed) {
		REX::DEBUG("HUDMessageHook: Already installed");
		return;
	}

	constexpr std::size_t kMinBytesNeeded = sizeof(REL::ASM::JMP14);  // 14 bytes

	// --- Calculate trampoline sizes for both hooks ---
	REL::Relocation<std::uintptr_t> msgTarget{ kShowHUDMessageID };
	REL::Relocation<std::uintptr_t> notifTarget{ kShowHUDNotificationID };
	auto                            msgAddr = msgTarget.address();
	auto                            notifAddr = notifTarget.address();

	std::size_t msgBytesToCopy = InstructionLength::CalculateMinimumCopySize(msgAddr, kMinBytesNeeded);
	std::size_t notifBytesToCopy = InstructionLength::CalculateMinimumCopySize(notifAddr, kMinBytesNeeded);

	if (msgBytesToCopy == 0) {
		REX::ERROR("HUDMessageHook: Failed to calculate instruction boundary for ShowHUDMessage");
		return;
	}
	if (notifBytesToCopy == 0) {
		REX::ERROR("HUDMessageHook: Failed to calculate instruction boundary for ShowHUDNotification");
		return;
	}

	std::size_t msgTrampolineSize = msgBytesToCopy + sizeof(REL::ASM::JMP14);
	std::size_t notifTrampolineSize = notifBytesToCopy + sizeof(REL::ASM::JMP14);

	// Allocate trampoline memory for both hooks
	auto& trampoline = REL::GetTrampoline();
	trampoline.create(msgTrampolineSize + notifTrampolineSize + 128);

	// --- Hook ShowHUDMessage ---
	auto* msgMem = static_cast<std::byte*>(trampoline.allocate(msgTrampolineSize));
	if (!msgMem) {
		REX::ERROR("HUDMessageHook: Failed to allocate trampoline for ShowHUDMessage");
		return;
	}

	std::memcpy(msgMem, reinterpret_cast<void*>(msgAddr), msgBytesToCopy);
	new (reinterpret_cast<REL::ASM::JMP14*>(msgMem + msgBytesToCopy))
		REL::ASM::JMP14(msgAddr + msgBytesToCopy);
	s_originalFunc = reinterpret_cast<OriginalFunc_t>(msgMem);

	REL::WriteSafeData(msgAddr,
		REL::ASM::JMP14(reinterpret_cast<std::uintptr_t>(&HookedShowHUDMessage)));

	REX::INFO("HUDMessageHook: ShowHUDMessage hooked at {:X}", msgAddr);

	// --- Hook ShowHUDNotification (radio signal found/lost) ---
	auto* notifMem = static_cast<std::byte*>(trampoline.allocate(notifTrampolineSize));
	if (!notifMem) {
		REX::ERROR("HUDMessageHook: Failed to allocate trampoline for ShowHUDNotification");
		// ShowHUDMessage hook is still active, mark as partially installed
		m_installed = true;
		return;
	}

	std::memcpy(notifMem, reinterpret_cast<void*>(notifAddr), notifBytesToCopy);
	new (reinterpret_cast<REL::ASM::JMP14*>(notifMem + notifBytesToCopy))
		REL::ASM::JMP14(notifAddr + notifBytesToCopy);
	s_originalNotificationFunc = reinterpret_cast<OriginalNotificationFunc_t>(notifMem);

	REL::WriteSafeData(notifAddr,
		REL::ASM::JMP14(reinterpret_cast<std::uintptr_t>(&HookedShowHUDNotification)));

	REX::INFO("HUDMessageHook: ShowHUDNotification hooked at {:X}", notifAddr);

	m_installed = true;
}

void HUDMessageHook::AnnounceMessage(const char* a_message, const char* a_label)
{
	if (!a_message || a_message[0] == '\0')
		return;

	std::string messageText(a_message);
	REX::INFO("{}: \"{}\"", a_label, messageText);

	if (ShouldAnnounce(messageText)) {
		RecordAnnouncement(messageText);
		SpeakText(messageText);
	}
}

void HUDMessageHook::HookedShowHUDMessage(const char* a_message, const char* a_sound,
	bool a_throttle, bool a_warning)
{
	GetSingleton()->AnnounceMessage(a_message, "HUD Message");

	if (s_originalFunc) {
		s_originalFunc(a_message, a_sound, a_throttle, a_warning);
	}
}

void HUDMessageHook::HookedShowHUDNotification(const char* a_message)
{
	GetSingleton()->AnnounceMessage(a_message, "HUD Notification");

	if (s_originalNotificationFunc) {
		s_originalNotificationFunc(a_message);
	}
}

std::uint64_t HUDMessageHook::GetCurrentTimeMs() const
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

bool HUDMessageHook::ShouldAnnounce(const std::string& a_text)
{
	if (a_text.empty()) {
		return false;
	}

	std::lock_guard<std::mutex> lock(m_mutex);

	auto it = m_announcementHistory.find(a_text);
	if (it == m_announcementHistory.end()) {
		return true;
	}

	auto now = GetCurrentTimeMs();
	return (now - it->second) >= kDeduplicationWindowMs;
}

void HUDMessageHook::RecordAnnouncement(const std::string& a_text)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	m_announcementHistory[a_text] = GetCurrentTimeMs();

	// Clean old entries periodically to prevent memory growth
	static std::uint64_t lastCleanTime = 0;
	auto                 now = GetCurrentTimeMs();

	if (now - lastCleanTime >= 10000) {  // Clean every 10 seconds
		lastCleanTime = now;
		for (auto it = m_announcementHistory.begin(); it != m_announcementHistory.end();) {
			if ((now - it->second) > kDeduplicationWindowMs * 2) {
				it = m_announcementHistory.erase(it);
			} else {
				++it;
			}
		}
	}
}

void HUDMessageHook::SpeakText(const std::string& a_text)
{
	SpeechManager::GetSingleton()->Speak(a_text);
}
