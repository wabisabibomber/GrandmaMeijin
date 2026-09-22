using System.ComponentModel;
using System.Diagnostics;

namespace GrandmaMeijin;

internal sealed class ClickController : IDisposable
{
    private sealed class TimerHandle : WaitHandle
    {
        internal TimerHandle(Microsoft.Win32.SafeHandles.SafeWaitHandle handle) => SafeWaitHandle = handle;
    }
    private readonly object gate = new();
    private readonly Action<bool> send;
    private CancellationTokenSource? cancellation;
    private Task loop = Task.CompletedTask;
    private bool disposed;
    internal string? LastError { get; private set; }
    internal ClickController(Action<bool>? sender = null) => send = sender ?? (down =>
    {
        if (!NativeMethods.SendLeft(down))
            throw new InvalidOperationException("マウス入力を送信できませんでした。対象アプリの権限などを確認してください。");
    });

    internal Task Start(double periodMs, double dutyPercent)
    {
        if (!double.IsFinite(periodMs) || periodMs < 20 || periodMs > 60000 ||
            !double.IsFinite(dutyPercent) || dutyPercent < 1 || dutyPercent > 99)
            throw new ArgumentOutOfRangeException(nameof(periodMs));
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            if (!loop.IsCompleted) return loop;
            cancellation?.Dispose();
            cancellation = new();
            var token = cancellation.Token;
            LastError = null;
            loop = Task.Factory.StartNew(() => Run(periodMs, dutyPercent, token), token,
                TaskCreationOptions.LongRunning, TaskScheduler.Default);
            return loop;
        }
    }

    internal void Stop()
    {
        lock (gate)
        {
            if (disposed) return;
            // キャンセルで待機を即解除。終了とLEFTUPが完了するまで次のStartを許さない。
            cancellation?.Cancel();
            try { loop.GetAwaiter().GetResult(); } catch (OperationCanceledException) { }
            Release(); // 停止時はループ開始前のキャンセルも含め、必ずLEFTUPを送る。
        }
    }

    private void Release()
    {
        try { send(false); }
        catch (Exception ex) { LastError = ex.Message + " LEFTUPの送信に失敗しました。"; }
    }

    private void Run(double periodMs, double dutyPercent, CancellationToken token)
    {
        try
        {
            var handle = NativeMethods.CreateWaitableTimerEx(IntPtr.Zero, null, 2, 0x1F0003);
            if (handle.IsInvalid)
            {
                handle.Dispose();
                handle = NativeMethods.CreateWaitableTimerEx(IntPtr.Zero, null, 0, 0x1F0003);
            }
            if (handle.IsInvalid) { handle.Dispose(); throw new Win32Exception(); }
            using var timer = new TimerHandle(handle);
            WaitHandle[] waits = [token.WaitHandle, timer];
            double scale = Stopwatch.Frequency / 1000.0;
            // 周期はDownとUpの合計。50%なら101msも50.5msずつ保持する。
            double downTicks = periodMs * dutyPercent / 100.0 * scale;
            double upTicks = periodMs * (100 - dutyPercent) / 100.0 * scale;
            double deadline = Stopwatch.GetTimestamp();
            while (!token.IsCancellationRequested)
            {
                send(true);
                deadline += downTicks;
                if (!WaitUntil(deadline, timer, waits)) break;
                send(false);
                double now = Stopwatch.GetTimestamp();
                // 大きな遅延時には追いつくための連続入力をせず、Up時間を確保する。
                deadline = now - deadline > upTicks ? now + upTicks : deadline + upTicks;
                if (!WaitUntil(deadline, timer, waits)) break;
                if (Stopwatch.GetTimestamp() - deadline > downTicks) deadline = Stopwatch.GetTimestamp();
            }
        }
        catch (Exception ex) { LastError = ex.Message; }
        finally { Release(); } // キャンセル・例外のどちらでも押しっぱなしを防ぐ。
    }

    private static bool WaitUntil(double deadline, WaitHandle timer, WaitHandle[] waits)
    {
        double remaining = deadline - Stopwatch.GetTimestamp();
        if (remaining <= 0) return !waits[0].WaitOne(0);
        long due = -Math.Max(1, (long)Math.Ceiling(remaining * 10_000_000 / Stopwatch.Frequency));
        if (!NativeMethods.SetWaitableTimer(timer.SafeWaitHandle, ref due, 0, IntPtr.Zero, IntPtr.Zero, false))
            throw new Win32Exception();
        return WaitHandle.WaitAny(waits) == 1; // busy waitせず高分解能タイマーとキャンセルを同時に待つ。
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            Stop();
            cancellation?.Dispose();
            disposed = true;
        }
    }
}
