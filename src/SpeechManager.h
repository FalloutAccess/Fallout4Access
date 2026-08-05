#pragma once

#include "pch.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>

// Forward-declared so this header doesn't drag <prism.h> (and windows.h) into
// every translation unit that only wants to speak.
struct PrismContext;
struct PrismBackend;

// Centralized speech output manager for fallout4access plugin
// Provides a single point of control for all screen reader output with dedicated logging
//
// Usage: SpeechManager::GetSingleton()->Speak()
//
// Features:
// - Dedicated speech log: fallout4access_speech.log with timestamped entries
// - Thread-safe logging with mutex protection
// - Optional muting support for future features
// - Owns the Prism screen reader context and the backend selection policy
class SpeechManager
{
public:
	static SpeechManager* GetSingleton();

	// Initialize the speech log file
	// Called once from main.cpp before InitializeEngine()
	void Initialize();

	// Bring up Prism and attach to the best available speech backend.
	//
	// MUST NOT be called while the Windows loader lock is held: Prism calls
	// CoInitializeEx and starts its logging thread. On OG the xSE
	// PluginPreloader loads us during early process init, so main.cpp defers
	// this to the F4SE task interface. Idempotent; returns whether a backend
	// is attached.
	bool InitializeEngine();

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

	// Backend selection. All of these require m_engineMutex to be held —
	// Prism backend instances are explicitly not thread-safe.

	// Attach the highest-priority backend that initializes successfully and
	// outranks a_minPriority. Pass -1 to consider every backend. Replaces any
	// currently attached backend only on success.
	bool SelectBackendLocked(int a_minPriority);

	// Re-run selection if we have no backend, or if we settled for a plain
	// TTS engine and a screen reader may have started since. Rate-limited.
	void RefreshBackendLocked();

	void ReleaseBackendLocked();

	// Write to speech log file with timestamp
	void LogSpeech(const std::string& a_text);

	// Get current timestamp formatted as [HH:MM:SS]
	std::string GetTimestamp();

	std::ofstream     m_speechLog;
	std::mutex        m_logMutex;
	std::atomic<bool> m_muted{ false };
	bool              m_initialized = false;

	std::mutex    m_engineMutex;
	PrismContext* m_ctx = nullptr;
	PrismBackend* m_backend = nullptr;
	int           m_backendPriority = -1;
	std::string   m_backendName;

	std::chrono::steady_clock::time_point m_lastSelectAttempt{};
};
