#pragma once
#include "Native.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace meijin {
class ClickController {
public:
    using Sender = std::function<bool(bool)>;
    explicit ClickController(Sender sender = SendLeft);
    ~ClickController();
    bool Start(double periodMs, double duty, std::function<void()> completed = {});
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(); }
    std::wstring Error() const;
private:
    Sender sender_;
    Handle cancel_;
    std::thread worker_;
    std::mutex gate_;
    mutable std::mutex errorGate_;
    std::wstring error_;
    std::atomic<bool> running_{false};
    void SetError(const wchar_t* message) noexcept;
    void Release() noexcept;
    void Run(double periodMs, double duty) noexcept;
    bool WaitUntil(double deadline, double frequency, HANDLE timer);
};
}
