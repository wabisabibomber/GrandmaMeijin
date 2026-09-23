using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace GrandmaMeijin;

internal static class NativeMethods
{
    internal const int HotkeyMessage = 0x0312;
    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int X, Y;
        public uint Data, Flags, Time;
        public UIntPtr ExtraInfo;
    }
    // x64のINPUTは40バイト。共用体はオフセット8、MOUSEINPUTは32バイト。
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    private struct Input
    {
        [FieldOffset(0)] public uint Type;
        [FieldOffset(8)] public MouseInput Mouse;
    }
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, Input[] inputs, int size);
    internal static bool SendLeft(bool down) => SendInput(1,
        [new Input { Type = 0, Mouse = new MouseInput { Flags = down ? 0x0002u : 0x0004u } }],
        Marshal.SizeOf<Input>()) == 1; // SendInputでLEFTDOWN/LEFTUPを別々に送信する。

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool UnregisterHotKey(IntPtr window, int id);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    internal static extern SafeWaitHandle CreateWaitableTimerEx(IntPtr attributes, string? name, uint flags, uint access);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool SetWaitableTimer(SafeWaitHandle timer, ref long dueTime, int period,
        IntPtr completion, IntPtr argument, [MarshalAs(UnmanagedType.Bool)] bool resume);
}
