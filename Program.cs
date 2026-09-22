namespace GrandmaMeijin;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var instance = new Mutex(true, @"Local\GrandmaMeijin.SingleInstance", out bool first);
        if (!first) { MessageBox.Show("GrandmaMeijin は既に起動しています。"); return; }
        ApplicationConfiguration.Initialize();
        // UI側の未処理例外もfinallyへ伝え、クリックループを停止してから終了する。
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        try
        {
            using var form = new MainForm();
            Application.Run(form);
        }
        finally { NativeMethods.SendLeft(false); } // 通常終了・例外終了でも最後にLEFTUPを試みる。
    }
}
