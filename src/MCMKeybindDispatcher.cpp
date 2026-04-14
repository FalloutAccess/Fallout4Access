#include "MCMKeybindDispatcher.h"
#include "KeybindActions.h"
#include <Windows.h>
#include <charconv>
#include <xinput.h>
#undef ERROR

namespace
{
	constexpr const char* kDefaultsIniPath = "Data\\MCM\\Config\\fallout4access\\settings.ini";
	constexpr const char* kUserIniPath = "Data\\MCM\\Settings\\fallout4access.ini";
	constexpr const char* kSection = "Keybinds";

	// Gamepad keycode offsets — mirrors f4se/f4se/InputMap.h. MCM stores
	// gamepad bindings in this packed range (266..281).
	constexpr std::uint32_t kMacroGamepadOffset = 266;

	// Map an XInput button mask (as delivered by ButtonEvent::QIDCode() for
	// gamepad) into MCM's packed keycode. Mirrors InputMap::GamepadMaskToKeycode
	// in f4se/f4se/InputMap.cpp. Returns 0 for unrecognized masks so the
	// caller skips dispatch.
	std::uint32_t GamepadMaskToKeycode(std::uint32_t a_mask)
	{
		switch (a_mask) {
			case XINPUT_GAMEPAD_DPAD_UP:
				return kMacroGamepadOffset + 0;
			case XINPUT_GAMEPAD_DPAD_DOWN:
				return kMacroGamepadOffset + 1;
			case XINPUT_GAMEPAD_DPAD_LEFT:
				return kMacroGamepadOffset + 2;
			case XINPUT_GAMEPAD_DPAD_RIGHT:
				return kMacroGamepadOffset + 3;
			case XINPUT_GAMEPAD_START:
				return kMacroGamepadOffset + 4;
			case XINPUT_GAMEPAD_BACK:
				return kMacroGamepadOffset + 5;
			case XINPUT_GAMEPAD_LEFT_THUMB:
				return kMacroGamepadOffset + 6;
			case XINPUT_GAMEPAD_RIGHT_THUMB:
				return kMacroGamepadOffset + 7;
			case XINPUT_GAMEPAD_LEFT_SHOULDER:
				return kMacroGamepadOffset + 8;
			case XINPUT_GAMEPAD_RIGHT_SHOULDER:
				return kMacroGamepadOffset + 9;
			case XINPUT_GAMEPAD_A:
				return kMacroGamepadOffset + 10;
			case XINPUT_GAMEPAD_B:
				return kMacroGamepadOffset + 11;
			case XINPUT_GAMEPAD_X:
				return kMacroGamepadOffset + 12;
			case XINPUT_GAMEPAD_Y:
				return kMacroGamepadOffset + 13;
			case 0x9:
				return kMacroGamepadOffset + 14;  // LT (F4SE-assigned, not a real XInput mask)
			case 0xA:
				return kMacroGamepadOffset + 15;  // RT
			default:
				return 0;  // unknown — skip
		}
	}
}

MCMKeybindDispatcher* MCMKeybindDispatcher::GetSingleton()
{
	static MCMKeybindDispatcher singleton;
	return &singleton;
}

void MCMKeybindDispatcher::LoadFromFile()
{
	m_bindings.clear();

	// Defaults first, then user overrides (which will overwrite matching keys).
	MergeFromIni(kDefaultsIniPath);
	MergeFromIni(kUserIniPath);

	REX::INFO("MCMKeybindDispatcher: Loaded {} bindings", m_bindings.size());
}

void MCMKeybindDispatcher::MergeFromIni(const char* a_iniPath)
{
	// GetPrivateProfileSection returns a double-null-terminated buffer of
	// "key=value\0key=value\0\0". Start with 1 KiB and grow until the
	// returned size is under (buflen - 2) — that's how the API signals
	// truncation (see MSDN).
	std::vector<char> buffer(1024);
	while (true) {
		DWORD written = GetPrivateProfileSectionA(kSection, buffer.data(), static_cast<DWORD>(buffer.size()), a_iniPath);
		if (written < static_cast<DWORD>(buffer.size()) - 2) {
			break;
		}
		if (buffer.size() >= 65536) {
			REX::WARN("MCMKeybindDispatcher: {} section too large, giving up", a_iniPath);
			return;
		}
		buffer.resize(buffer.size() * 2);
	}

	// We don't treat a missing file as an error — defaults may be shipped
	// separately and the user file doesn't exist until the user remaps
	// something through the MCM UI.
	for (char* p = buffer.data(); *p != '\0'; p += std::strlen(p) + 1) {
		std::string pair(p);
		auto        eq = pair.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		std::string id = pair.substr(0, eq);
		std::string value = pair.substr(eq + 1);

		std::uint32_t keycode = 0;
		std::uint8_t  modifiers = 0;
		if (!ParseKeyValue(value, keycode, modifiers) || keycode == 0) {
			continue;
		}

		m_bindings[PackKey(keycode, modifiers)] = std::move(id);
	}
}

bool MCMKeybindDispatcher::ParseKeyValue(const std::string& a_value, std::uint32_t& a_outKeycode, std::uint8_t& a_outModifiers)
{
	auto comma = a_value.find(',');
	if (comma == std::string::npos) {
		return false;
	}

	std::uint32_t keycode = 0;
	std::uint32_t modifiers = 0;

	auto [p1, e1] = std::from_chars(a_value.data(), a_value.data() + comma, keycode);
	if (e1 != std::errc{}) {
		return false;
	}
	auto [p2, e2] = std::from_chars(a_value.data() + comma + 1, a_value.data() + a_value.size(), modifiers);
	if (e2 != std::errc{}) {
		return false;
	}

	a_outKeycode = keycode;
	a_outModifiers = static_cast<std::uint8_t>(modifiers);
	return true;
}

bool MCMKeybindDispatcher::TryDispatch(const RE::ButtonEvent* a_event)
{
	if (!a_event || !a_event->QJustPressed()) {
		return false;
	}

	// Convert the event's idCode into MCM's packed keycode space. This mirrors
	// f4mcm/src/ScaleformMCM.cpp:817-828: keyboard keys stay as-is (0..255),
	// mouse buttons get shifted into 256..263, gamepad into 266..281.
	std::uint32_t packedKeycode = a_event->QIDCode();
	switch (a_event->device.get()) {
		case RE::INPUT_DEVICE::kKeyboard:
			// idCode is already the VK code.
			break;
		case RE::INPUT_DEVICE::kMouse:
			// MCM ignores wheel-up/down and left/right click bindings (those
			// remain available for in-game use). We still attempt the lookup
			// for Middle and MouseButton3..7 which MCM allows binding.
			// kMacro_MouseButtonOffset = 256.
			packedKeycode = 256 + packedKeycode;
			break;
		case RE::INPUT_DEVICE::kGamepad: {
			// Gamepad ButtonEvent idCode is the raw XInput button mask (or
			// 0x9/0xA for triggers, per F4SE convention). Convert to MCM's
			// packed keycode (266..281) so the lookup key matches what the
			// MCM UI wrote to the INI.
			auto mapped = GamepadMaskToKeycode(packedKeycode);
			if (mapped == 0) {
				return false;  // Unknown gamepad input
			}
			packedKeycode = mapped;
			break;
		}
		default:
			return false;
	}

	// Modifier detection — match MCM's approach (f4mcm/src/MCMInput.cpp:90-92)
	// so a shift-modified binding matches exactly how MCM would have captured it.
	std::uint8_t mods = 0;
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
		mods |= 1;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
		mods |= 2;
	if (GetAsyncKeyState(VK_MENU) & 0x8000)
		mods |= 4;

	auto key = PackKey(packedKeycode, mods);
	auto it = m_bindings.find(key);
	if (it == m_bindings.end()) {
		return false;
	}

	return KeybindActions::Dispatch(it->second);
}
