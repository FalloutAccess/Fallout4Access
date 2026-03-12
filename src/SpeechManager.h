#pragma once

#include "pch.h"
#include <atomic>
#include <fstream>
#include <mutex>

// Centralized speech output manager for fallout4access plugin
// Provides a single point of control for all screen reader output with dedicated logging
//
// Usage: Replace direct SRAL_Speak() calls with SpeechManager::GetSingleton()->Speak()
//
// Features:
// - Dedicated speech log: fallout4access_speech.log with timestamped entries
// - Thread-safe logging with mutex protection
// - Optional muting support for future features
class SpeechManager
{
public:
	static SpeechManager* GetSingleton();

	// Initialize the speech log file
	// Called once from main.cpp after SRAL_Initialize()
	void Initialize();

	// Main speech output function
	// All speech should go through this method
	// a_text: Text to speak via screen reader
	// a_interrupt: Whether to interrupt current speech (should always be false per CLAUDE.md)
	void Speak(const std::string& a_text, bool a_interrupt = false);

	// Optional: Mute support for future features (thread-safe)
	void SetMuted(bool a_muted) { m_muted.store(a_muted, std::memory_order_relaxed); }
	bool IsMuted() const { return m_muted.load(std::memory_order_relaxed); }

private:
	SpeechManager() = default;
	~SpeechManager();
	SpeechManager(const SpeechManager&) = delete;
	SpeechManager(SpeechManager&&) = delete;
	SpeechManager& operator=(const SpeechManager&) = delete;
	SpeechManager& operator=(SpeechManager&&) = delete;

	// Write to speech log file with timestamp
	void LogSpeech(const std::string& a_text);

	// Get current timestamp formatted as [HH:MM:SS]
	std::string GetTimestamp();

	std::ofstream     m_speechLog;
	std::mutex        m_logMutex;
	std::atomic<bool> m_muted{ false };
	bool              m_initialized = false;
};
