// Engine services visible to scripts: Game, RunService, Task (yielding on the engine loop) and Debug (Output).
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;

namespace Tefra
{
    /// <summary>The DataModel's services ("game" in Roblox terms).</summary>
    public static class Game
    {
        public static Workspace Workspace => GetService("Workspace") as Workspace;
        public static Lighting Lighting => GetService("Lighting") as Lighting;
        public static Instance ReplicatedStorage => GetService("ReplicatedStorage");
        public static Instance ServerScriptService => GetService("ServerScriptService");
        public static Instance StarterPlayer => GetService("StarterPlayer");
        public static Instance StarterGui => GetService("StarterGui");

        public static Instance GetService(string name)
        {
            ulong id = Native.Service(name ?? "");
            if (id == 0) throw new ArgumentException("Game.GetService: no service named '" + name + "'");
            return Instance.FromId(id);
        }
    }

    /// <summary>Per-frame events. Handlers run on the engine loop, after the scheduled tasks resume.</summary>
    public static class RunService
    {
        /// <summary>Fires once per engine step with the frame time in seconds.</summary>
        public static event Action<float> Heartbeat;

        internal static void Fire(float dt)
        {
            Action<float> handlers = Heartbeat;
            if (handlers == null) return;
            foreach (Delegate d in handlers.GetInvocationList())
            {
                try
                {
                    ((Action<float>)d)(dt);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "RunService.Heartbeat handler failed: " + e);
                }
            }
        }

        internal static void Clear()
        {
            Heartbeat = null;
        }
    }

    /// <summary>
    /// Yielding without threads: `await Task.Wait(1f);` suspends the calling async method and resumes it from the
    /// engine loop after the given time (Roblox task.wait). Use it in `public override async void Start()`.
    /// </summary>
    public static class Task
    {
        public static Yield Wait(float seconds) => new Yield(seconds);

        public static Yield Delay(float seconds) => new Yield(seconds);
    }

    /// <summary>Awaitable returned by Task.Wait / Task.Delay. Resumed by the engine loop, never by another thread.</summary>
    public sealed class Yield : INotifyCompletion
    {
        private static readonly List<Yield> Waiting = new List<Yield>();

        private float _remaining;
        private Action _continuation;

        internal Yield(float seconds)
        {
            _remaining = seconds;
        }

        public Yield GetAwaiter() => this;

        public bool IsCompleted => _remaining <= 0f;

        public void OnCompleted(Action continuation)
        {
            _continuation = continuation;
            Waiting.Add(this);
        }

        public void UnsafeOnCompleted(Action continuation) => OnCompleted(continuation);

        public void GetResult()
        {
        }

        // Called once per engine step, before Heartbeat.
        internal static void Advance(float dt)
        {
            if (Waiting.Count == 0) return;
            var ready = new List<Yield>();
            for (int i = Waiting.Count - 1; i >= 0; i--)
            {
                Yield y = Waiting[i];
                y._remaining -= dt;
                if (y._remaining <= 0f)
                {
                    ready.Add(y);
                    Waiting.RemoveAt(i);
                }
            }
            // Resume in the order the waits were started.
            for (int i = ready.Count - 1; i >= 0; i--)
            {
                Action c = ready[i]._continuation;
                ready[i]._continuation = null;
                try
                {
                    c?.Invoke();
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "Task resumed with an exception: " + e);
                }
            }
        }

        internal static void Clear()
        {
            Waiting.Clear();
        }
    }

    /// <summary>Output (the Studio Output panel or the tefra_run console).</summary>
    public static class Debug
    {
        public static void Log(object message) => Native.Log(Native.LogInfo, Format(message));

        public static void LogWarning(object message) => Native.Log(Native.LogWarn, Format(message));

        public static void LogError(object message) => Native.Log(Native.LogError, Format(message));

        // Roblox-style print: arguments separated by tabs.
        public static void print(params object[] args)
        {
            var sb = new StringBuilder();
            for (int i = 0; i < args.Length; i++)
            {
                if (i > 0) sb.Append('\t');
                sb.Append(Format(args[i]));
            }
            Native.Log(Native.LogInfo, sb.ToString());
        }

        internal static void Report(string text)
        {
            if (Native.IsReady) Native.Log(Native.LogError, text);
            else Console.Error.WriteLine(text);
        }

        private static string Format(object o)
        {
            if (o == null) return "nil";
            if (o is IFormattable f) return f.ToString(null, System.Globalization.CultureInfo.InvariantCulture);
            return o.ToString();
        }
    }
}
