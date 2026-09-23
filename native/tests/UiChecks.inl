// テスト専用ビルド。実際のマウス入力・ユーザーの設定ファイルは使わない。
#include <fstream>
#include <vector>
static void Capture(HWND window, const std::filesystem::path& path) {
    ShowWindow(window, SW_SHOWNOACTIVATE);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    Sleep(80);
    RECT rect{}; GetWindowRect(window, &rect);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    auto old = SelectObject(memory, bitmap);
    PrintWindow(window, memory, PW_RENDERFULLCONTENT);
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<char> pixels(static_cast<size_t>(width) * height * 4);
    GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<char*>(&header), sizeof(header));
    file.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, screen);
    ShowWindow(window, SW_HIDE);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    std::ofstream report("ui-checks.txt");
    auto check = [&](bool valid, const char* name) { if (!valid) throw std::runtime_error(name); report << "PASS: " << name << std::endl; };
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        const auto directory = std::filesystem::current_path() / L"ui-settings";
        SaveClick(directory, {101, 37});
        SaveHotkey(directory, {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11});
        std::atomic<int> down{}, up{};
        App app(directory, [&](bool pressed) { if (pressed) ++down; else ++up; return true; });
        app.instance = instance;
        HWND window = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainProc, reinterpret_cast<LPARAM>(&app));
        check(window != nullptr, "native window created");
        check(GetDlgItemInt(window, IDC_PERIOD, nullptr, FALSE) == 101 && GetDlgItemInt(window, IDC_DUTY, nullptr, FALSE) == 37,
            "UI restores C# compatible settings");
        SetDlgItemTextW(window, IDC_PERIOD, L"");
        check(!IsWindowEnabled(GetDlgItem(window, IDC_TOGGLE)) && LoadClick(directory).period == 101, "invalid input blocks start and preserves saved value");
        SetDlgItemTextW(window, IDC_PERIOD, L"60");
        SetDlgItemTextW(window, IDC_DUTY, L"50");
        check(LoadClick(directory).period == 60 && LoadClick(directory).duty == 50, "UI changes save immediately");
        check(app.registeredId != 0, "global combination registered");
        const auto previous = app.hotkey;
        HWND blocker = CreateWindowExW(0, L"STATIC", L"test", 0, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        check(RegisterHotKey(blocker, 99, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F10) != FALSE, "conflict fixture registered");
        check(!app.Register({MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F10}) && app.hotkey == previous && app.registeredId != 0,
            "failed registration preserves previous hotkey");
        UnregisterHotKey(blocker, 99); DestroyWindow(blocker);
        app.Text(IDC_NOTICE, L"");
        // 非アクティブなウィンドウへWM_HOTKEYを配送し、ボタンと同じ状態を操作することを確認。
        SendMessageW(window, WM_HOTKEY, app.registeredId, 0);
        Sleep(120);
        check(app.controller.Running() && down > 0 && !IsWindowEnabled(GetDlgItem(window, IDC_PERIOD)), "hotkey starts worker and locks settings");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_TOGGLE, BN_CLICKED), 0);
        check(!app.controller.Running() && up > 0 && IsWindowEnabled(GetDlgItem(window, IDC_PERIOD)), "button stops same worker and releases");
        for (int i = 0; i < 30; ++i) {
            SendMessageW(window, WM_HOTKEY, app.registeredId, 0);
            SendMessageW(window, WM_HOTKEY, app.registeredId, 0);
        }
        check(!app.controller.Running(), "rapid UI hotkey toggles stop safely");
        Capture(window, L"native-main.bmp");
        Hotkey value = previous;
        HWND dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_HOTKEY), window, HotkeyProc, reinterpret_cast<LPARAM>(&value));
        check(dialog != nullptr, "hotkey dialog created");
        Capture(dialog, L"native-hotkey.bmp");
        DestroyWindow(dialog);
        SendMessageW(window, WM_HOTKEY, app.registeredId, 0);
        Sleep(10);
        SendMessageW(window, WM_CLOSE, 0, 0);
        check(!app.controller.Running() && app.registeredId == 0, "close stops and unregisters");
        DestroyWindow(window);
        App restored(directory, [](bool) { return true; });
        check(restored.click.period == 60 && restored.click.duty == 50 && restored.hotkey == previous, "restart preserves all settings");
        return 0;
    } catch (const std::exception& error) { report << "FAIL: " << error.what() << std::endl; return 1; }
    catch (...) { report << "FAIL: unexpected exception" << std::endl; return 1; }
}
