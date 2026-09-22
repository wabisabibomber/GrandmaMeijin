namespace GrandmaMeijin;

internal sealed class HotkeyDialog : Form
{
    private readonly CheckBox ctrl = new() { Text = "Ctrl", AutoSize = true };
    private readonly CheckBox alt = new() { Text = "Alt", AutoSize = true };
    private readonly CheckBox shift = new() { Text = "Shift", AutoSize = true };
    private readonly CheckBox win = new() { Text = "Win", AutoSize = true };
    private readonly ComboBox key = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    private readonly Label preview = new() { AutoSize = true };
    internal HotkeySettings Selection => new((ctrl.Checked ? 2u : 0) | (alt.Checked ? 1u : 0) |
        (shift.Checked ? 4u : 0) | (win.Checked ? 8u : 0), (Keys)(key.SelectedItem ?? Keys.F8));

    internal HotkeyDialog(HotkeySettings current)
    {
        Text = "ホットキーの変更";
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(420, 265);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
            WrapContents = false, Padding = new Padding(16) };
        Controls.Add(panel);
        var modifiers = new FlowLayoutPanel { AutoSize = true };
        modifiers.Controls.AddRange([ctrl, alt, shift, win]);
        panel.Controls.Add(modifiers);
        key.Items.AddRange(HotkeySettings.AvailableKeys.Cast<object>().ToArray());
        key.SelectedItem = current.Key;
        ctrl.Checked = (current.Modifiers & 2) != 0;
        alt.Checked = (current.Modifiers & 1) != 0;
        shift.Checked = (current.Modifiers & 4) != 0;
        win.Checked = (current.Modifiers & 8) != 0;
        panel.Controls.Add(key);
        panel.Controls.Add(preview);
        panel.Controls.Add(new Label { AutoSize = true, MaximumSize = new Size(380, 0),
            Text = "Fキー以外は修飾キーが必要です。\nF12はWindows予約キーです。その他にもOSや他アプリが使用している組み合わせは登録できません。" });
        var buttons = new FlowLayoutPanel { AutoSize = true, Margin = new Padding(0, 12, 0, 0) };
        var apply = new Button { Text = "適用", AutoSize = true };
        var cancel = new Button { Text = "キャンセル", AutoSize = true, DialogResult = DialogResult.Cancel };
        buttons.Controls.AddRange([apply, cancel]);
        panel.Controls.Add(buttons);
        CancelButton = cancel;
        AcceptButton = apply;
        void RefreshPreview() { preview.Text = "設定：" + Selection; apply.Enabled = Selection.IsValid; }
        foreach (var box in new[] { ctrl, alt, shift, win }) box.CheckedChanged += (_, _) => RefreshPreview();
        key.SelectedIndexChanged += (_, _) => RefreshPreview();
        apply.Click += (_, _) => { if (Selection.IsValid) DialogResult = DialogResult.OK; };
        RefreshPreview();
    }
}
