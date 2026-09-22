using System.Text.Json;

namespace GrandmaMeijin;

internal sealed record ClickSettings
{
    public decimal PeriodMs { get; init; } = 60;
    public decimal DutyPercent { get; init; } = 50;
    internal static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "GrandmaMeijin", "click.json");

    internal static ClickSettings Load(string? path = null)
    {
        try
        {
            var value = JsonSerializer.Deserialize<ClickSettings>(File.ReadAllText(path ?? FilePath)) ?? new();
            // 古い設定で項目がない場合や範囲外の値でも、項目ごとに安全な初期値へ戻す。
            return value with
            {
                PeriodMs = value.PeriodMs is >= 20 and <= 60000 ? decimal.Round(value.PeriodMs) : 60,
                DutyPercent = value.DutyPercent is >= 1 and <= 99 ? decimal.Round(value.DutyPercent) : 50
            };
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new();
        }
    }

    internal void Save(string? path = null)
    {
        path ??= FilePath;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        // 書き込み途中の終了で既存設定を壊さないよう、完成した一時ファイルと置き換える。
        File.WriteAllText(path + ".tmp", JsonSerializer.Serialize(this));
        File.Move(path + ".tmp", path, true);
    }
}
