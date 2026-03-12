#include "SpeechManager.h"
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <SRAL.h>
#include <chrono>
#include <iomanip>
#include <sstream>

SpeechManager* SpeechManager::GetSingleton()
{
	static SpeechManager instance;
	return &instance;
}

SpeechManager::~SpeechManager()
{
	std::lock_guard<std::mutex> lock(m_logMutex);
	if (m_speechLog.is_open()) {
		m_speechLog.flush();
		m_speechLog.close();
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

void SpeechManager::Speak(const std::string& a_text, bool a_interrupt)
{
	if (a_text.empty()) {
		return;
	}

	// Check if speech engine is available
	if (SRAL_GetCurrentEngine() == 0) {
		return;
	}

	// Check mute flag (atomic read for thread safety)
	if (m_muted.load(std::memory_order_relaxed)) {
		return;
	}

	// Log to speech file
	LogSpeech(a_text);

	// Perform actual speech output
	SRAL_Speak(a_text.c_str(), a_interrupt);
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
