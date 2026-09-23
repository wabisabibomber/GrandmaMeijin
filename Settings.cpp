#include "Settings.h"
#include "Native.h"
#include <shlobj.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace meijin {
using winrt::Windows::Data::Json::JsonObject;
std::vector<UINT> AvailableKeys() {
    std::vector<UINT> keys;
    for (UINT k = VK_F1; k <= VK_F12; ++k) keys.push_back(k);
    for (UINT k = 'A'; k <= 'Z'; ++k) keys.push_back(k);
    for (UINT k = '0'; k <= '9'; ++k) keys.push_back(k);
    for (UINT k = VK_NUMPAD0; k <= VK_NUMPAD9; ++k) keys.push_back(k);
    for (UINT k : {VK_SPACE, VK_RETURN, VK_TAB, VK_ESCAPE, VK_INSERT, VK_DELETE, VK_HOME, VK_END,
        VK_PRIOR, VK_NEXT, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_BACK, VK_OEM_PLUS, VK_OEM_MINUS,
        VK_MULTIPLY, VK_ADD, VK_SUBTRACT, VK_DECIMAL, VK_DIVIDE, VK_OEM_COMMA, VK_OEM_PERIOD,
        VK_OEM_2, VK_OEM_1, VK_OEM_7, VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_102, VK_OEM_3,
        VK_PAUSE, VK_SCROLL, VK_NUMLOCK, VK_CAPITAL, VK_SNAPSHOT, VK_APPS}) keys.push_back(k);
    return keys;
}
bool ValidHotkey(Hotkey value) {
    const auto keys = AvailableKeys();
    return !(value.modifiers & ~15u) && std::find(keys.begin(), keys.end(), value.key) != keys.end()
        && (value.modifiers || (value.key >= VK_F1 && value.key <= VK_F12));
}
std::wstring KeyName(UINT key) {
    if (key >= VK_F1 && key <= VK_F12) return L"F" + std::to_wstring(key - VK_F1 + 1);
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return L"Num " + std::to_wstring(key - VK_NUMPAD0);
    auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN || key == VK_INSERT ||
        key == VK_DELETE || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT) scan |= 0x100;
    wchar_t name[80]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 80)) return name;
    return L"Key " + std::to_wstring(key);
}
std::wstring HotkeyName(Hotkey value) {
    std::wstring text;
    if (value.modifiers & MOD_CONTROL) text += L"Ctrl + ";
    if (value.modifiers & MOD_ALT) text += L"Alt + ";
    if (value.modifiers & MOD_SHIFT) text += L"Shift + ";
    if (value.modifiers & MOD_WIN) text += L"Win + ";
    return text + KeyName(value.key);
}
std::filesystem::path SettingsDirectory() {
    PWSTR raw{};
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw));
    std::filesystem::path path;
    try { path = raw; } catch (...) { CoTaskMemFree(raw); throw; }
    CoTaskMemFree(raw);
    return path / L"GrandmaMeijin";
}
static JsonObject Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file || std::filesystem::file_size(path) > 65536) throw std::runtime_error("Cannot read settings");
    std::string text((std::istreambuf_iterator<char>(file)), {});
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    // Windows標準のJSONパーサーを利用。.NETは不要で、C#版と同じJSONを扱う。
    return JsonObject::Parse(winrt::to_hstring(text));
}
static double Number(const JsonObject& object, const wchar_t* key, double fallback) {
    try { return object.GetNamedNumber(key, fallback); } catch (...) { return fallback; }
}
static int Rounded(double number) { // C#のdecimal.Roundと同じ偶数丸め。
    const double lower = std::floor(number);
    if (number - lower == 0.5) return static_cast<int>(lower) + (static_cast<int>(lower) % 2);
    return static_cast<int>(std::round(number));
}
ClickSettings LoadClick(const std::filesystem::path& directory) noexcept {
    ClickSettings result;
    try {
        const auto object = Read(directory / L"click.json");
        const double period = Number(object, L"PeriodMs", 60), duty = Number(object, L"DutyPercent", 50);
        if (std::isfinite(period) && period >= 20 && period <= 60000) result.period = Rounded(period);
        if (std::isfinite(duty) && duty >= 1 && duty <= 99) result.duty = Rounded(duty);
    } catch (...) {}
    return result;
}
Hotkey LoadHotkey(const std::filesystem::path& directory) noexcept {
    try {
        const auto object = Read(directory / L"hotkey.json");
        const double modifiers = Number(object, L"Modifiers", -1), key = Number(object, L"Key", -1);
        if (modifiers < 0 || modifiers > 15 || key < 1 || key > 255 ||
            std::floor(modifiers) != modifiers || std::floor(key) != key) return {};
        Hotkey result{static_cast<UINT>(modifiers), static_cast<UINT>(key)};
        if (ValidHotkey(result)) return result;
    } catch (...) {}
    return {};
}
static void Write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.wstring() + L".native.tmp";
    // 完成したファイルのみ置き換える。書き込み失敗時に元の設定を壊さない。
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) throw std::runtime_error("Cannot create settings");
        DWORD written{};
        if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
            written != text.size() || !FlushFileBuffers(file.get())) throw std::runtime_error("Cannot write settings");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
}
void SaveClick(const std::filesystem::path& directory, ClickSettings value) {
    if (value.period < 20 || value.period > 60000 || value.duty < 1 || value.duty > 99) throw std::invalid_argument("Invalid click settings");
    Write(directory / L"click.json", "{\"PeriodMs\":" + std::to_string(value.period) + ",\"DutyPercent\":" + std::to_string(value.duty) + "}");
}
void SaveHotkey(const std::filesystem::path& directory, Hotkey value) {
    if (!ValidHotkey(value)) throw std::invalid_argument("Invalid hotkey");
    Write(directory / L"hotkey.json", "{\"Modifiers\":" + std::to_string(value.modifiers) + ",\"Key\":" + std::to_string(value.key) + "}");
}
}
