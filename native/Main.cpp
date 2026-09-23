#include "ClickController.h"
#include "Settings.h"
#include "Resource.h"
#include <commctrl.h>
#include <winrt/base.h>
#include <cwchar>
#include <cstdlib>
#include <memory>

using namespace meijin;
namespace {
constexpr UINT FinishedMessage = WM_APP + 1;
struct App {
    HWND window{};
    HINSTANCE instance{};
    std::filesystem::path directory;
    ClickSettings click;
    Hotkey hotkey;
    ClickController controller;
    int registeredId{};
    UINT_PTR generation{};
    bool initialized{}, closing{}, editing{};
    HFONT statusFont{};
    HICON largeIcon{}, smallIcon{};
    explicit App(std::filesystem::path path = SettingsDirectory(), ClickController::Sender sender = SendLeft)
        : directory(std::move(path)), click(LoadClick(directory)), hotkey(LoadHotkey(directory)), controller(std::move(sender)) {}
    ~App() {
        controller.Stop();
        if (statusFont) DeleteObject(statusFont);
        if (largeIcon) DestroyIcon(largeIcon);
        if (smallIcon) DestroyIcon(smallIcon);
    }
    void Text(int id, const std::wstring& text) { SetDlgItemTextW(window, id, text.c_str()); }
    bool ReadValue(int id, int low, int high, int& value) {
        wchar_t text[64]{};
        GetDlgItemTextW(window, id, text, 64);
        if (!text[0]) return false;
        for (const wchar_t* p = text; *p; ++p) if (*p < L'0' || *p > L'9') return false;
        const unsigned long number = wcstoul(text, nullptr, 10);
        if (number < static_cast<unsigned>(low) || number > static_cast<unsigned>(high)) return false;
        value = static_cast<int>(number);
        return true;
    }
    bool ReadClick(ClickSettings& value) { return ReadValue(IDC_PERIOD, 20, 60000, value.period) && ReadValue(IDC_DUTY, 1, 99, value.duty); }
    void SaveTiming() {
        if (!initialized) return;
        ClickSettings value;
        if (!ReadClick(value)) {
            Text(IDC_TIMING, L"周期20～60000、割合1～99の整数を入力してください。");
            EnableWindow(GetDlgItem(window, IDC_TOGGLE), controller.Running());
            return; // 編集途中の空欄や不正値で、最後の正常な保存値を上書きしない。
        }
        click = value;
        wchar_t preview[120]{};
        swprintf_s(preview, L"Down %.2f ms / Up %.2f ms", value.period * value.duty / 100.0, value.period * (100 - value.duty) / 100.0);
        Text(IDC_TIMING, preview);
        EnableWindow(GetDlgItem(window, IDC_TOGGLE), TRUE);
        try { SaveClick(directory, value); Text(IDC_SAVE_NOTICE, L""); }
        catch (...) { Text(IDC_SAVE_NOTICE, L"設定を保存できませんでした。保存先の権限を確認してください。"); }
    }
    void State() {
        const bool running = controller.Running();
        Text(IDC_STATUS, running ? L"RUNNING" : L"STOPPED");
        Text(IDC_TOGGLE, running ? L"停止" : L"開始");
        for (int id : {IDC_PERIOD, IDC_DUTY, IDC_PERIOD_SPIN, IDC_DUTY_SPIN, IDC_CHANGE}) EnableWindow(GetDlgItem(window, id), !running);
        InvalidateRect(GetDlgItem(window, IDC_STATUS), nullptr, TRUE);
    }
    void HotkeyLabel() { Text(IDC_HOTKEY_LABEL, L"ホットキー：" + HotkeyName(hotkey) + (registeredId ? L"" : L"（未登録）")); }
    bool Register(Hotkey value) {
        if (registeredId && value == hotkey) return true;
        const int next = registeredId == 1 ? 2 : 1;
        // RegisterHotKeyで非アクティブ時も操作。MOD_NOREPEATで長押しの反転を防ぐ。
        if (!RegisterHotKey(window, next, value.modifiers | MOD_NOREPEAT, value.key)) {
            Text(IDC_NOTICE, L"ホットキーを登録できません（使用中・予約済みの可能性）。");
            return false;
        }
        // 新しい登録に成功してから旧登録を解除する。失敗時は元の設定を維持。
        if (registeredId) UnregisterHotKey(window, registeredId);
        registeredId = next;
        hotkey = value;
        return true;
    }
    void Toggle() {
        if (closing || editing) return;
        if (controller.Running()) {
            controller.Stop();
            ++generation; // 停止前の完了通知が次のループの表示を変えないようにする。
            State();
            Text(IDC_NOTICE, controller.Error());
            return;
        }
        ClickSettings value;
        if (!ReadClick(value)) { SaveTiming(); return; }
        SaveTiming();
        Text(IDC_NOTICE, L"");
        const auto runId = ++generation;
        const HWND target = window;
        controller.Start(value.period, value.duty, [target, runId] { PostMessageW(target, FinishedMessage, runId, 0); });
        State();
    }
    void Close() {
        closing = true;
        controller.Stop(); // ウィンドウ破棄前にキャンセルとLEFTUPを完了させる。
        SaveTiming();
        if (registeredId) { UnregisterHotKey(window, registeredId); registeredId = 0; }
        EndDialog(window, 0);
    }
};

Hotkey Selection(HWND dialog) {
    UINT modifiers{};
    for (const auto& pair : {std::pair{IDC_CTRL, MOD_CONTROL}, {IDC_ALT, MOD_ALT}, {IDC_SHIFT, MOD_SHIFT}, {IDC_WIN, MOD_WIN}})
        if (IsDlgButtonChecked(dialog, pair.first) == BST_CHECKED) modifiers |= pair.second;
    const auto index = SendDlgItemMessageW(dialog, IDC_KEY, CB_GETCURSEL, 0, 0);
    const auto key = SendDlgItemMessageW(dialog, IDC_KEY, CB_GETITEMDATA, index, 0);
    return {modifiers, static_cast<UINT>(key)};
}
INT_PTR CALLBACK HotkeyProc(HWND dialog, UINT message, WPARAM w, LPARAM l) noexcept {
    try {
        auto* value = reinterpret_cast<Hotkey*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG) {
            value = reinterpret_cast<Hotkey*>(l);
            SetWindowLongPtrW(dialog, DWLP_USER, l);
            for (const auto& pair : {std::pair{IDC_CTRL, MOD_CONTROL}, {IDC_ALT, MOD_ALT}, {IDC_SHIFT, MOD_SHIFT}, {IDC_WIN, MOD_WIN}})
                CheckDlgButton(dialog, pair.first, value->modifiers & pair.second ? BST_CHECKED : BST_UNCHECKED);
            for (UINT key : AvailableKeys()) {
                const auto name = KeyName(key);
                const auto index = SendDlgItemMessageW(dialog, IDC_KEY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
                SendDlgItemMessageW(dialog, IDC_KEY, CB_SETITEMDATA, index, key);
                if (key == value->key) SendDlgItemMessageW(dialog, IDC_KEY, CB_SETCURSEL, index, 0);
            }
        } else if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
            EndDialog(dialog, IDCANCEL); return TRUE;
        } else if (message == WM_COMMAND && LOWORD(w) == IDOK) {
            const auto selected = Selection(dialog);
            if (ValidHotkey(selected)) { *value = selected; EndDialog(dialog, IDOK); }
            return TRUE;
        } else if (message != WM_COMMAND) return FALSE;
        const auto selected = Selection(dialog);
        const auto label = L"設定：" + HotkeyName(selected);
        SetDlgItemTextW(dialog, IDC_PREVIEW, label.c_str());
        EnableWindow(GetDlgItem(dialog, IDOK), ValidHotkey(selected));
        return message == WM_INITDIALOG;
    } catch (...) { EndDialog(dialog, IDCANCEL); return TRUE; }
}

INT_PTR CALLBACK MainProc(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            app = reinterpret_cast<App*>(l);
            SetWindowLongPtrW(window, DWLP_USER, l);
            app->window = window;
            const UINT dpi = GetDpiForWindow(window);
            app->largeIcon = static_cast<HICON>(LoadImageW(app->instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi), 0));
            app->smallIcon = static_cast<HICON>(LoadImageW(app->instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), 0));
            if (app->largeIcon) SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app->largeIcon));
            if (app->smallIcon) SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app->smallIcon));
            LOGFONTW font{};
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)), sizeof(font), &font);
            font.lfHeight = -MulDiv(16, static_cast<int>(dpi), 72);
            font.lfWeight = FW_BOLD;
            app->statusFont = CreateFontIndirectW(&font);
            SendDlgItemMessageW(window, IDC_STATUS, WM_SETFONT, reinterpret_cast<WPARAM>(app->statusFont), TRUE);
            for (const auto& pair : {std::pair{IDC_PERIOD_SPIN, IDC_PERIOD}, {IDC_DUTY_SPIN, IDC_DUTY}})
                SendDlgItemMessageW(window, pair.first, UDM_SETBUDDY, reinterpret_cast<WPARAM>(GetDlgItem(window, pair.second)), 0);
            SendDlgItemMessageW(window, IDC_PERIOD_SPIN, UDM_SETRANGE32, 20, 60000);
            SendDlgItemMessageW(window, IDC_DUTY_SPIN, UDM_SETRANGE32, 1, 99);
            SendDlgItemMessageW(window, IDC_PERIOD, EM_SETLIMITTEXT, 5, 0);
            SendDlgItemMessageW(window, IDC_DUTY, EM_SETLIMITTEXT, 2, 0);
            SetDlgItemInt(window, IDC_PERIOD, app->click.period, FALSE);
            SetDlgItemInt(window, IDC_DUTY, app->click.duty, FALSE);
            app->initialized = true;
            app->SaveTiming();
            app->Register(app->hotkey);
            app->HotkeyLabel();
            return TRUE;
        }
        if (!app) return FALSE;
        switch (message) {
        case WM_COMMAND:
            if ((LOWORD(w) == IDC_PERIOD || LOWORD(w) == IDC_DUTY) && HIWORD(w) == EN_CHANGE) app->SaveTiming();
            else if (LOWORD(w) == IDC_TOGGLE && HIWORD(w) == BN_CLICKED) app->Toggle();
            else if (LOWORD(w) == IDC_CHANGE && HIWORD(w) == BN_CLICKED && !app->controller.Running()) {
                app->editing = true;
                Hotkey value = app->hotkey;
                const auto result = DialogBoxParamW(app->instance, MAKEINTRESOURCEW(IDD_HOTKEY), window, HotkeyProc, reinterpret_cast<LPARAM>(&value));
                app->editing = false;
                if (result == IDOK && app->Register(value)) {
                    app->HotkeyLabel();
                    try { SaveHotkey(app->directory, value); app->Text(IDC_NOTICE, L""); }
                    catch (...) { app->Text(IDC_NOTICE, L"ホットキーは変更しましたが、保存できませんでした。"); }
                }
            }
            // Enter/Escがダイアログ既定動作で終了・開始を引き起こさないようにする。
            return TRUE;
        case WM_HOTKEY:
            if (app->registeredId && w == static_cast<WPARAM>(app->registeredId)) app->Toggle();
            return TRUE;
        case FinishedMessage:
            if (w == app->generation && !app->closing) { app->State(); app->Text(IDC_NOTICE, app->controller.Error()); }
            return TRUE;
        case WM_CLOSE: app->Close(); return TRUE;
        case WM_QUERYENDSESSION: app->controller.Stop(); app->SaveTiming(); SetWindowLongPtrW(window, DWLP_MSGRESULT, TRUE); return TRUE;
        case WM_ENDSESSION: if (w) app->Close(); return TRUE;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(l) == GetDlgItem(window, IDC_STATUS)) {
                SetTextColor(reinterpret_cast<HDC>(w), app->controller.Running() ? RGB(0, 120, 45) : GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_3DFACE));
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
            }
            break;
        }
    } catch (...) {
        // 例外をWindowsのコールバック境界へ出さず、最優先で停止してLEFTUPを送る。
        if (app) { app->controller.Stop(); app->Text(IDC_NOTICE, L"エラーが発生したため停止しました。"); app->State(); }
        return TRUE;
    }
    return FALSE;
}
}

#ifdef MEIJIN_UI_CHECK
#include "tests/UiChecks.inl"
#else
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // C#版と同じMutex名で、両方を同時に起動した場合も二重実行を防ぐ。
    Handle single(CreateMutexW(nullptr, FALSE, L"Local\\GrandmaMeijin.SingleInstance"));
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, single ? L"GrandmaMeijin は既に起動しています。" : L"起動状態を確認できませんでした。", L"GrandmaMeijin", MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
        if (!InitCommonControlsEx(&controls)) throw std::runtime_error("InitCommonControls failed");
        {
            App app;
            app.instance = instance;
            if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, MainProc, reinterpret_cast<LPARAM>(&app)) == -1)
                throw std::runtime_error("Dialog creation failed");
        } // Appの破棄時にスレッド終了・LEFTUPを確実に実行する。
        winrt::uninit_apartment();
    } catch (...) {
        SendLeft(false);
        MessageBoxW(nullptr, L"アプリを実行できませんでした。", L"GrandmaMeijin", MB_OK | MB_ICONERROR);
        return 1;
    }
    SendLeft(false);
    return 0;
}
#endif
