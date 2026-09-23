#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace meijin {
struct ClickSettings { int period = 60; int duty = 50; };
struct Hotkey {
    UINT modifiers = 0;
    UINT key = VK_F8;
    bool operator==(const Hotkey&) const = default;
};
std::vector<UINT> AvailableKeys();
bool ValidHotkey(Hotkey value);
std::wstring KeyName(UINT key);
std::wstring HotkeyName(Hotkey value);
std::filesystem::path SettingsDirectory();
ClickSettings LoadClick(const std::filesystem::path& directory) noexcept;
Hotkey LoadHotkey(const std::filesystem::path& directory) noexcept;
void SaveClick(const std::filesystem::path& directory, ClickSettings value);
void SaveHotkey(const std::filesystem::path& directory, Hotkey value);
}
