#pragma once
#include <windows.h>
#include <stdexcept>

namespace meijin {
class Handle {
    HANDLE value_{};
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
};
inline bool SendLeft(bool down) noexcept {
    // DownとUpを別々のSendInputで送信する。mouse_eventは使用しない。
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    return SendInput(1, &input, sizeof(input)) == 1;
}
inline double Counter() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart);
}
inline double Frequency() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return static_cast<double>(value.QuadPart);
}
}
