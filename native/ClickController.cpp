#include "ClickController.h"
#include <cmath>
#include <algorithm>
#include <utility>

namespace meijin {
ClickController::ClickController(Sender sender)
    : sender_(std::move(sender)), cancel_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!cancel_) throw std::runtime_error("CreateEvent failed");
}
ClickController::~ClickController() { Stop(); }

bool ClickController::Start(double periodMs, double duty, std::function<void()> completed) {
    if (!std::isfinite(periodMs) || !std::isfinite(duty) || periodMs < 20 || periodMs > 60000 || duty < 1 || duty > 99)
        throw std::invalid_argument("Invalid timing");
    std::lock_guard lock(gate_);
    if (running_) return false;
    if (worker_.joinable()) worker_.join();
    { std::lock_guard errorLock(errorGate_); error_.clear(); }
    if (!ResetEvent(cancel_.get())) throw std::runtime_error("ResetEvent failed");
    running_ = true;
    try {
        worker_ = std::thread([this, periodMs, duty, done = std::move(completed)] {
            Run(periodMs, duty);
            running_ = false;
            // UIへの通知は非同期。ここからStop/Startを呼び戻さない。
            try { if (done) done(); } catch (...) {}
        });
    } catch (...) { running_ = false; Release(); throw; }
    return true;
}
void ClickController::Stop() noexcept {
    std::lock_guard lock(gate_);
    // キャンセルイベントで待機を即解除し、スレッド終了後に次のStartを許可する。
    SetEvent(cancel_.get());
    if (worker_.joinable()) worker_.join();
    running_ = false;
    Release(); // 開始前・停止中でもLEFTUPを必ず試みる。
}
void ClickController::SetError(const wchar_t* message) noexcept {
    try { std::lock_guard lock(errorGate_); error_ = message; } catch (...) {}
}
std::wstring ClickController::Error() const { std::lock_guard lock(errorGate_); return error_; }
void ClickController::Release() noexcept {
    try {
        if (!sender_(false)) SetError(L"LEFTUPを送信できませんでした。対象アプリの権限を確認してください。");
    } catch (...) { SetError(L"LEFTUPの送信中に例外が発生しました。"); }
}
bool ClickController::WaitUntil(double deadline, double frequency, HANDLE timer) {
    double remaining = deadline - Counter();
    if (remaining <= 0) return WaitForSingleObject(cancel_.get(), 0) == WAIT_TIMEOUT;
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(std::max(1.0, std::ceil(remaining * 10000000.0 / frequency)));
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) throw std::runtime_error("SetWaitableTimer failed");
    HANDLE waits[]{cancel_.get(), timer};
    const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (result == WAIT_OBJECT_0) return false;
    if (result != WAIT_OBJECT_0 + 1) throw std::runtime_error("Timer wait failed");
    return true; // busy waitせず、Windowsの高分解能タイマーで待機する。
}
void ClickController::Run(double periodMs, double duty) noexcept {
    try {
        HANDLE raw = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!raw) raw = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        Handle timer(raw);
        if (!timer) throw std::runtime_error("CreateWaitableTimer failed");
        double frequency = Frequency();
        // 周期はDown+Upの合計。50%では101msも50.5msずつとして計算する。
        const double down = periodMs * duty / 100.0 * frequency / 1000.0;
        const double up = periodMs * (100.0 - duty) / 100.0 * frequency / 1000.0;
        double deadline = Counter();
        while (WaitForSingleObject(cancel_.get(), 0) == WAIT_TIMEOUT) {
            if (!sender_(true)) {
                SetError(L"マウス入力を送信できませんでした。対象アプリの権限などを確認してください。");
                break;
            }
            deadline += down;
            if (!WaitUntil(deadline, frequency, timer.get())) break;
            if (!sender_(false)) { SetError(L"LEFTUPの送信に失敗しました。"); break; }
            const double now = Counter();
            // 大きな遅延時に入力をまとめて送らず、Upの時間を確保する。
            deadline = now - deadline > up ? now + up : deadline + up;
            if (!WaitUntil(deadline, frequency, timer.get())) break;
            if (Counter() - deadline > down) deadline = Counter();
        }
    } catch (...) { SetError(L"連打処理でエラーが発生したため停止しました。"); }
    Release(); // 例外・キャンセル・通常停止のすべてで終了時のLEFTUPを送る。
}
}
