using System.Runtime.InteropServices;

namespace GrandmaMeijin;

internal sealed class MainForm : Form
{
    private readonly ClickController controller = new();
    private readonly NumericUpDown period = new() { Minimum = 20, Maximum = 60000, Value = 100, Width = 110 };
    private readonly NumericUpDown duty = new() { Minimum = 1, Maximum = 99, Value = 50, Width = 110 };
    private readonly Label timing = new() { AutoSize = true };
    private readonly Label status = new() { Text = "STOPPED", AutoSize = true, Font = new Font("Segoe UI", 16, FontStyle.Bold) };
    private readonly Label hotkeyLabel = new() { AutoSize = true };
    private readonly Label notice = new() { AutoSize = true, MaximumSize = new Size(400, 0), ForeColor = Color.Firebrick };
    private readonly Button toggle = new() { Text = "開始", Width = 130, Height = 36 };
    private readonly Button change = new() { Text = "変更…", AutoSize = true };
    private HotkeySettings hotkey = HotkeySettings.Load();
    private int registeredId;
    private Task? active;
    private bool closing;
    private bool editingHotkey;

    internal MainForm()
    {
        Text = "GrandmaMeijin";
        // 単一exeでもフォーム・タスクバーに同じアイコンを表示できるよう埋め込む。
        using (var iconStream = typeof(MainForm).Assembly.GetManifestResourceStream("GrandmaMeijin.AppIcon"))
        {
            if (iconStream is not null)
            {
                using var sourceIcon = new Icon(iconStream);
                Icon = (Icon)sourceIcon.Clone();
            }
        }
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(440, 340);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        var layout = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
            WrapContents = false, Padding = new Padding(16), AutoScroll = true };
        Controls.Add(layout);
        layout.Controls.Add(status);
        layout.Controls.Add(Row(new Label { Text = "クリック周期（ms）", Width = 195, AutoSize = false }, period));
        layout.Controls.Add(Row(new Label { Text = "押下時間の割合（%）", Width = 195, AutoSize = false }, duty));
        layout.Controls.Add(timing);
        layout.Controls.Add(toggle);
        layout.Controls.Add(Row(hotkeyLabel, change));
        layout.Controls.Add(new Label { Text = "現在のマウスポインター位置で左クリックします。", AutoSize = true });
        layout.Controls.Add(notice);
        period.ValueChanged += (_, _) => UpdateTiming();
        duty.ValueChanged += (_, _) => UpdateTiming();
        toggle.Click += (_, _) => Toggle();
        change.Click += (_, _) => ChangeHotkey();
        UpdateTiming();
        Shown += (_, _) =>
        {
            if (!Register(hotkey, out string error)) notice.Text = error + " 開始ボタンは使用できます。";
            UpdateHotkeyLabel();
        };
    }

    private static FlowLayoutPanel Row(params Control[] controls)
    {
        var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 6, 0, 6) };
        row.Controls.AddRange(controls);
        return row;
    }

    private void UpdateTiming() => timing.Text = $"Down {period.Value * duty.Value / 100:0.##} ms / Up {period.Value * (100 - duty.Value) / 100:0.##} ms";
    private void UpdateHotkeyLabel() => hotkeyLabel.Text = $"ホットキー：{hotkey}" + (registeredId == 0 ? "（未登録）" : "");
    private void SetRunning(bool running)
    {
        status.Text = running ? "RUNNING" : "STOPPED";
        status.ForeColor = running ? Color.ForestGreen : SystemColors.ControlText;
        toggle.Text = running ? "停止" : "開始";
        period.Enabled = duty.Enabled = change.Enabled = !running;
    }

    private void Toggle()
    {
        if (closing || editingHotkey) return;
        if (active is not null)
        {
            controller.Stop();
            active = null;
            SetRunning(false);
            notice.Text = controller.LastError ?? "";
            return;
        }
        // NumericUpDownが編集中の値を確定・範囲補正してから読み取る。
        ValidateChildren();
        notice.Text = "";
        active = controller.Start((double)period.Value, (double)duty.Value);
        SetRunning(true);
        Observe(active);
    }

    private async void Observe(Task task)
    {
        try { await task; } catch (OperationCanceledException) { }
        if (closing || IsDisposed || active != task) return;
        active = null;
        SetRunning(false);
        notice.Text = controller.LastError ?? "";
    }

    private bool Register(HotkeySettings value, out string error)
    {
        error = "";
        if (registeredId != 0 && value == hotkey) return true;
        int nextId = registeredId == 1 ? 2 : 1;
        // MOD_NOREPEATで押し続けによる反転を防ぐ。新しい登録成功後に旧登録を解除する。
        if (!NativeMethods.RegisterHotKey(Handle, nextId, value.Modifiers | 0x4000, (uint)value.Key))
        {
            error = $"{value} は登録できません（エラー {Marshal.GetLastWin32Error()}）。使用中または予約済みの可能性があります。";
            return false;
        }
        if (registeredId != 0) NativeMethods.UnregisterHotKey(Handle, registeredId);
        registeredId = nextId;
        hotkey = value;
        return true;
    }

    private void ChangeHotkey()
    {
        editingHotkey = true;
        try
        {
            using var dialog = new HotkeyDialog(hotkey);
            if (dialog.ShowDialog(this) != DialogResult.OK) return;
            if (!Register(dialog.Selection, out string error)) { notice.Text = error; return; }
            UpdateHotkeyLabel();
            try { hotkey.Save(); notice.Text = ""; }
            catch (Exception ex) { notice.Text = "変更は有効ですが、次回起動用の保存に失敗しました：" + ex.Message; }
        }
        finally { editingHotkey = false; }
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == NativeMethods.HotkeyMessage && registeredId != 0 && m.WParam.ToInt32() == registeredId)
        {
            Toggle();
            return;
        }
        base.WndProc(ref m);
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        closing = true;
        controller.Stop(); // ウィンドウ終了時は待機を解除し、LEFTUPまで完了させる。
        base.OnFormClosing(e);
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            closing = true;
            if (registeredId != 0) { NativeMethods.UnregisterHotKey(Handle, registeredId); registeredId = 0; }
            controller.Dispose();
        }
        base.Dispose(disposing);
    }
}
