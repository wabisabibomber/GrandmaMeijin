#include "ClickController.h"
#include "Settings.h"
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace meijin;
using namespace std::chrono_literals;
void Check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
    std::cout << "PASS: " << name << '\n';
}
int main() {
    try {
        winrt::init_apartment();
        auto path = std::filesystem::temp_directory_path() / (L"GrandmaMeijinChecks-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(path);
        Check(LoadClick(path).period == 60 && LoadClick(path).duty == 50, "defaults 60ms / 50%");
        { std::ofstream f(path / L"click.json"); f << R"({"PeriodMs":101,"DutyPercent":37})"; }
        Check(LoadClick(path).period == 101 && LoadClick(path).duty == 37, "C# click JSON compatibility");
        SaveClick(path, {60, 30});
        Check(LoadClick(path).period == 60 && LoadClick(path).duty == 30, "save and reload timing");
        { std::ofstream f(path / L"click.json"); f << R"({"PeriodMs":0,"DutyPercent":99})"; }
        Check(LoadClick(path).period == 60 && LoadClick(path).duty == 99, "invalid period preserves valid duty");
        { std::ofstream f(path / L"click.json"); f << "{broken"; }
        Check(LoadClick(path).period == 60, "malformed JSON falls back safely");
        { std::ofstream f(path / L"click.json"); f << R"({"PeriodMs":100.5,"DutyPercent":33.5})"; }
        Check(LoadClick(path).period == 100 && LoadClick(path).duty == 34, "C# midpoint rounding compatibility");
        { std::ofstream f(path / L"hotkey.json"); f << R"({"Modifiers":6,"Key":65})"; }
        Check(LoadHotkey(path) == Hotkey{MOD_CONTROL | MOD_SHIFT, 'A'}, "C# hotkey JSON compatibility");
        SaveHotkey(path, {MOD_ALT, VK_F9});
        Check(LoadHotkey(path) == Hotkey{MOD_ALT, VK_F9}, "save and reload hotkey");
        Check(!ValidHotkey({0, 'A'}) && !ValidHotkey({16, VK_F8}), "hotkey validation");
        // 自分のテストが作成した既知のファイルのみ削除する。
        std::filesystem::remove(path / L"click.json");
        std::filesystem::remove(path / L"hotkey.json");
        std::filesystem::remove(path);

        std::mutex samplesGate;
        std::vector<std::pair<bool, double>> samples;
        auto cpuTicks = [] {
            FILETIME created{}, exited{}, kernel{}, user{};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
            return (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime
                 + (static_cast<ULONGLONG>(user.dwHighDateTime) << 32) + user.dwLowDateTime;
        };
        ClickController controller([&](bool down) { std::lock_guard lock(samplesGate); samples.emplace_back(down, Counter()); return true; });
        const auto cpuBefore = cpuTicks();
        controller.Start(101, 50);
        Check(!controller.Start(20, 50), "duplicate Start rejected");
        std::this_thread::sleep_for(1200ms);
        controller.Stop();
        std::cout << "Process CPU time during 1.2s loop: " << (cpuTicks() - cpuBefore) / 10000.0 << "ms\n";
        Check(samples.size() > 16 && !samples.back().first, "loop releases on stop");
        std::vector<double> down, up;
        for (size_t i = 0; i + 1 < samples.size(); ++i) {
            if (samples[i].first == samples[i + 1].first) continue;
            (samples[i].first ? down : up).push_back((samples[i + 1].second - samples[i].second) * 1000 / Frequency());
        }
        auto median = [](std::vector<double> list) { std::sort(list.begin(), list.end()); return list[list.size() / 2]; };
        std::cout << "101ms / 50%: down=" << median(down) << "ms up=" << median(up) << "ms\n";
        Check(std::abs(median(down) - 50.5) < 12 && std::abs(median(up) - 50.5) < 12, "fractional half-period timing");
        samples.clear(); down.clear(); up.clear();
        controller.Start(100, 30);
        std::this_thread::sleep_for(800ms);
        controller.Stop();
        for (size_t i = 0; i + 1 < samples.size(); ++i) {
            if (samples[i].first == samples[i + 1].first) continue;
            (samples[i].first ? down : up).push_back((samples[i + 1].second - samples[i].second) * 1000 / Frequency());
        }
        Check(down.size() >= 5 && up.size() >= 5, "adjustable duty produces samples");
        std::cout << "100ms / 30%: down=" << median(down) << "ms up=" << median(up) << "ms\n";
        Check(std::abs(median(down) - 30) < 12 && std::abs(median(up) - 70) < 12, "adjustable duty timing");
        for (int i = 0; i < 100; ++i) { controller.Start(20, 50); controller.Stop(); }
        Check(!samples.back().first, "100 rapid Start/Stop cycles");
        controller.Start(60000, 99);
        std::this_thread::sleep_for(20ms);
        const auto started = Counter();
        controller.Stop();
        Check((Counter() - started) / Frequency() < 1 && !samples.back().first, "cancel interrupts long Down wait");
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) threads.emplace_back([&] { for (int j = 0; j < 30; ++j) { controller.Start(20, 30); controller.Stop(); } });
        for (auto& thread : threads) thread.join();
        controller.Stop();
        Check(!samples.back().first && !controller.Running(), "concurrent Start/Stop");
        bool rejected = false;
        try { controller.Start(std::numeric_limits<double>::quiet_NaN(), 50); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "NaN timing rejected");
        std::atomic<int> releases{};
        {
            ClickController failing([&](bool pressed) -> bool { if (pressed) throw std::runtime_error("injected"); ++releases; return true; });
            failing.Start(60, 50);
            for (int i = 0; i < 100 && failing.Running(); ++i) std::this_thread::sleep_for(5ms);
            failing.Stop();
            Check(releases > 0 && !failing.Error().empty(), "exception releases and reports error");
        }
        std::atomic<int> attempts{};
        {
            ClickController denied([&](bool pressed) { if (!pressed) ++attempts; return false; });
            denied.Start(60, 50);
            std::this_thread::sleep_for(20ms);
            denied.Stop();
            Check(attempts >= 2 && !denied.Error().empty(), "SendInput failure retries release and stops");
        }
        Check(sizeof(INPUT) == 40, "x64 SendInput layout");
        winrt::uninit_apartment();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    catch (...) { std::cerr << "FAIL: unexpected exception\n"; return 1; }
}
