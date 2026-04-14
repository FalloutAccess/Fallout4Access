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
	// a_interrupt: Whether to interrupt current speech. Prefer leaving this false
	//   and using StopSpeech() at the dispatcher level instead — see StopSpeech()
	//   docs and CLAUDE.md "Speech interruption policy".
	void Speak(const std::string& a_text, bool a_interrupt = false);

	// Stop any currently-playing speech.
	//
	// This exists because users can rebind our actions to non-standard keys
	// via MCM, and screen readers (NVDA/JAWS) only auto-interrupt speech on
	// a fixed set of keys (Ctrl, arrows). When a user remaps "next object"
	// to e.g. F2, pressing it would stack new announcements on top of stale
	// ones unless we explicitly stop the current speech first.
	//
	// Called centrally from input dispatchers (KeybindActions::Dispatch and
	// MenuAccessibility::OnButtonEvent for navigation keys) before invoking
	// the action handler. Individual Speak() callsites do not need to use
	// interrupt=true — the dispatcher-level stop covers them implicitly.
	void StopSpeech();

	// Optional: Mute support for future features (thread-safe)
	void SetMuted(bool a_muted) { m_muted.store(a_muted, std::memory_order_relaxed); }
	bool IsMuted() const { return m_muted.load(std::memory_order_relaxed); }

private:
	SpeechManager() = default;
	~SpeechManager() noexcept;
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
