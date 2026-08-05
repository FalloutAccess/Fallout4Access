#include "SpeechManager.h"
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <chrono>
#include <iomanip>
#include <prism.h>
#include <sstream>

namespace
{
	// Prism ranks every screen reader backend at 100 or above and the generic
	// Windows TTS engines below that (OneCore 98, SAPI 97). We are happy to
	// speak through OneCore/SAPI when nothing else is running, but we keep
	// looking for a real screen reader for as long as we are on one of them.
	constexpr int kScreenReaderPriority = 100;

	// How often RefreshBackendLocked may re-probe the registry. Probing walks
	// every backend and asks it to initialize (an RPC round trip for NVDA, a
	// CoCreateInstance for JAWS, a delay-load attempt for the DLL-based
	// readers), so it is far too heavy to do on every line of speech. Only
	// opportunistic upgrades wait this long — losing the attached backend
	// mid-sentence reselects immediately.
	constexpr auto kSelectRetryInterval = std::chrono::seconds(10);

	// Errors that mean the attached backend is gone (screen reader quit or
	// crashed) rather than "this particular utterance was bad".
	bool IsBackendLost(PrismError a_error)
	{
		switch (a_error) {
			case PRISM_ERROR_BACKEND_NOT_AVAILABLE:
			case PRISM_ERROR_BACKEND_ENTERED_UNDEFINED_STATE:
			case PRISM_ERROR_INTERNAL:
			case PRISM_ERROR_SPEAK_FAILURE:
			case PRISM_ERROR_NOT_INITIALIZED:
			case PRISM_ERROR_UNKNOWN:
				return true;
			default:
				return false;
		}
	}

	// Forward Prism's own diagnostics into fallout4access.log. Invoked from
	// Prism's logging thread; REX logs through spdlog _mt sinks, so this is
	// safe.
	//
	// Registered at ERROR, not WARN: probing the DLL-based readers (ZDSR,
	// PCTalker, BoyPCReader) warns once per backend every time those DLLs are
	// absent, which is almost every machine, and RefreshBackendLocked reprobes
	// on a timer. At WARN that would steadily fill the log with noise about
	// screen readers the user has never heard of.
	void PRISM_CALL OnPrismLog(void*, PrismLogLevel, const char* a_source, const char* a_message)
	{
		REX::ERROR("Prism [{}]: {}", a_source ? a_source : "prism", a_message ? a_message : "");
	}
}

SpeechManager* SpeechManager::GetSingleton()
{
	static SpeechManager instance;
	return &instance;
}

SpeechManager::~SpeechManager() noexcept
{
	// Deliberately no prism_shutdown()/prism_backend_free() here. F4SE plugins
	// are only ever unloaded at process teardown, where this destructor runs
	// under the loader lock — joining Prism's logging thread there would
	// deadlock. The OS reclaims everything a moment later anyway.
	try {
		std::lock_guard<std::mutex> lock(m_logMutex);
		if (m_speechLog.is_open()) {
			m_speechLog.flush();
			m_speechLog.close();
		}
	} catch (...) {
	}
}

void SpeechManager::Initialize()
{
	if (m_initialized) {
		return;
	}

	// Get Documents folder path (same approach as F4SE::API::InitLog)
	wchar_t*   knownBuffer{ nullptr };
	const auto knownResult = REX::W32::SHGetKnownFolderPath(
		REX::W32::FOLDERID_Documents,
		REX::W32::KF_FLAG_DEFAULT,
		nullptr,
		std::addressof(knownBuffer));

	std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
		knownBuffer,
		REX::W32::CoTaskMemFree);

	if (!knownPath || knownResult != 0) {
		REX::ERROR("SpeechManager: Failed to get Documents folder path");
		m_initialized = true;  // Mark as initialized to prevent retries
		return;
	}

	// Build path: Documents/My Games/{SaveFolder}/F4SE/fallout4access_speech.log
	std::filesystem::path speechLogPath = knownPath.get();
	speechLogPath /= std::format("My Games/{}/F4SE/fallout4access_speech.log", F4SE::GetSaveFolderName());

	// Open in truncate mode (fresh log each session)
	m_speechLog.open(speechLogPath, std::ios::out | std::ios::trunc);

	if (m_speechLog.is_open()) {
		REX::INFO("Speech log initialized: {}", speechLogPath.string());
	} else {
		REX::ERROR("SpeechManager: Failed to open speech log: {}", speechLogPath.string());
	}

	m_initialized = true;
}

bool SpeechManager::InitializeEngine()
{
	std::lock_guard<std::mutex> lock(m_engineMutex);

	if (m_ctx) {
		return m_backend != nullptr;
	}

	static_cast<void>(prism_set_log_level(PRISM_LOG_LEVEL_ERROR));
	static_cast<void>(prism_set_log_handler(PrismLogHandler{ .fn = OnPrismLog, .userdata = nullptr }));

	// No availability_callback, so Prism starts no background polling thread;
	// we re-probe lazily from Speak() instead.
	PrismConfig config = prism_config_init();
	m_ctx = prism_init(&config);
	if (!m_ctx) {
		REX::ERROR("Prism initialization failed");
		return false;
	}

	if (!SelectBackendLocked(-1)) {
		REX::WARN("Prism initialized but no speech backend is available");
		return false;
	}

	REX::INFO("Prism initialized, speaking through {}", m_backendName);
	return true;
}

bool SpeechManager::SelectBackendLocked(int a_minPriority)
{
	if (!m_ctx) {
		return false;
	}

	m_lastSelectAttempt = std::chrono::steady_clock::now();

	// The registry is sorted by descending priority, so the first backend that
	// initializes is the best one available.
	const std::size_t count = prism_registry_count(m_ctx);
	for (std::size_t i = 0; i < count; ++i) {
		const PrismBackendId id = prism_registry_id_at(m_ctx, i);
		if (id == PRISM_BACKEND_INVALID) {
			continue;
		}

		const int priority = prism_registry_priority(m_ctx, id);
		if (priority <= a_minPriority) {
			break;  // Nothing further down the list can beat what we already have.
		}

		// prism_registry_create rather than acquire: acquire hands back the
		// registry's cached instance, which would keep returning the fallback
		// engine we are trying to trade up from.
		PrismBackend* candidate = prism_registry_create(m_ctx, id);
		if (!candidate) {
			continue;
		}

		const PrismError err = prism_backend_initialize(candidate);
		if (err != PRISM_OK && err != PRISM_ERROR_ALREADY_INITIALIZED) {
			prism_backend_free(candidate);
			continue;
		}

		const char* name = prism_backend_name(candidate);
		ReleaseBackendLocked();
		m_backend = candidate;
		m_backendPriority = priority;
		m_backendName = name ? name : "unknown";
		return true;
	}

	return false;
}

void SpeechManager::RefreshBackendLocked()
{
	// Nothing to do while a real screen reader is driving us.
	if (m_backend && m_backendPriority >= kScreenReaderPriority) {
		return;
	}

	if (std::chrono::steady_clock::now() - m_lastSelectAttempt < kSelectRetryInterval) {
		return;
	}

	const std::string previous = m_backendName;
	if (SelectBackendLocked(m_backendPriority) && !previous.empty()) {
		REX::INFO("Speech backend switched from {} to {}", previous, m_backendName);
	}
}

void SpeechManager::ReleaseBackendLocked()
{
	if (m_backend) {
		prism_backend_free(m_backend);
		m_backend = nullptr;
	}
	m_backendPriority = -1;
	m_backendName.clear();
}

void SpeechManager::Speak(const std::string& a_text, bool a_interrupt)
{
	if (a_text.empty()) {
		return;
	}

	// Check mute flag (atomic read for thread safety)
	if (m_muted.load(std::memory_order_relaxed)) {
		return;
	}

	std::lock_guard<std::mutex> lock(m_engineMutex);
	if (!m_ctx) {
		return;
	}

	// Picks up a screen reader that started after the game did, and upgrades
	// away from the OneCore/SAPI fallback when one appears.
	RefreshBackendLocked();
	if (!m_backend) {
		return;
	}

	// Log to speech file
	LogSpeech(a_text);

	PrismError err = prism_backend_speak(m_backend, a_text.c_str(), a_interrupt);
	if (err == PRISM_OK || !IsBackendLost(err)) {
		return;
	}

	// The screen reader went away mid-session. Drop it, find whatever is left
	// (possibly the same reader after a restart) and say the line there.
	REX::WARN("Speech backend {} failed ({}), reselecting", m_backendName, prism_error_string(err));
	ReleaseBackendLocked();
	if (!SelectBackendLocked(-1)) {
		return;
	}

	REX::INFO("Speech backend is now {}", m_backendName);
	err = prism_backend_speak(m_backend, a_text.c_str(), a_interrupt);
	if (err != PRISM_OK) {
		REX::ERROR("Speech failed on {}: {}", m_backendName, prism_error_string(err));
	}
}

void SpeechManager::StopSpeech()
{
	// No engine, nothing to stop. Don't log "no engine" — this is called on
	// every relevant key press and would flood the log when no screen reader
	// is running. Deliberately does not RefreshBackendLocked(): stopping is
	// not worth a registry probe, and the next Speak() will do it anyway.
	std::lock_guard<std::mutex> lock(m_engineMutex);
	if (!m_backend) {
		return;
	}

	static_cast<void>(prism_backend_stop(m_backend));
}

void SpeechManager::LogSpeech(const std::string& a_text)
{
	std::lock_guard<std::mutex> lock(m_logMutex);
	if (!m_speechLog.is_open()) {
		return;
	}

	m_speechLog << GetTimestamp() << " " << a_text << "\n";
	m_speechLog.flush();  // Immediate flush for crash diagnostics
}

std::string SpeechManager::GetTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto now_time_t = std::chrono::system_clock::to_time_t(now);

	std::tm local_tm;
	localtime_s(&local_tm, &now_time_t);

	std::ostringstream oss;
	oss << "[" << std::setfill('0')
		<< std::setw(2) << local_tm.tm_hour << ":"
		<< std::setw(2) << local_tm.tm_min << ":"
		<< std::setw(2) << local_tm.tm_sec << "]";

	return oss.str();
}
