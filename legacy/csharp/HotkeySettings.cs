using System.Text.Json;

namespace GrandmaMeijin;

internal sealed record HotkeySettings(uint Modifiers, Keys Key)
{
    internal static readonly HotkeySettings Default = new(0, Keys.F8);
    internal static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GrandmaMeijin", "hotkey.json");
    internal bool IsValid => (Modifiers & ~15u) == 0 && AvailableKeys.Contains(Key) &&
        (Modifiers != 0 || Key >= Keys.F1 && Key <= Keys.F12);
    internal static Keys[] AvailableKeys => Enumerable.Range((int)Keys.F1, 12)
        .Concat(Enumerable.Range((int)Keys.A, 26)).Concat(Enumerable.Range((int)Keys.D0, 10))
        .Select(k => (Keys)k).Concat([Keys.Space, Keys.Enter, Keys.Tab, Keys.Escape, Keys.Insert,
            Keys.Delete, Keys.Home, Keys.End, Keys.PageUp, Keys.PageDown, Keys.Left, Keys.Right,
            Keys.Up, Keys.Down, Keys.Back, Keys.Oemplus, Keys.OemMinus,
            Keys.NumPad0, Keys.NumPad1, Keys.NumPad2, Keys.NumPad3, Keys.NumPad4,
            Keys.NumPad5, Keys.NumPad6, Keys.NumPad7, Keys.NumPad8, Keys.NumPad9,
            Keys.Multiply, Keys.Add, Keys.Subtract, Keys.Decimal, Keys.Divide,
            Keys.Oemcomma, Keys.OemPeriod, Keys.OemQuestion, Keys.OemSemicolon,
            Keys.OemQuotes, Keys.OemOpenBrackets, Keys.OemCloseBrackets, Keys.OemPipe,
            Keys.OemBackslash, Keys.Oemtilde, Keys.Pause, Keys.Scroll, Keys.NumLock,
            Keys.CapsLock, Keys.PrintScreen, Keys.Apps]).ToArray();
    internal static HotkeySettings Load()
    {
        try { var value = JsonSerializer.Deserialize<HotkeySettings>(File.ReadAllText(FilePath)); return value?.IsValid == true ? value : Default; }
        catch { return Default; }
    }
    internal void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        File.WriteAllText(FilePath + ".tmp", JsonSerializer.Serialize(this));
        File.Move(FilePath + ".tmp", FilePath, true);
    }
    public override string ToString() => string.Concat(
        (Modifiers & 2) != 0 ? "Ctrl + " : "", (Modifiers & 1) != 0 ? "Alt + " : "",
        (Modifiers & 4) != 0 ? "Shift + " : "", (Modifiers & 8) != 0 ? "Win + " : "") + Key;
}
