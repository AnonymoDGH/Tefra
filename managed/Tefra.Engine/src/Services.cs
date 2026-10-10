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
        /// <summary>The Players service instance (RespawnTime, children). The static helpers are Tefra.Players.</summary>
        public static PlayersService Players => GetService("Players") as PlayersService;
        public static SoundService SoundService => GetService("SoundService") as SoundService;

        public static Instance GetService(string name)
        {
            ulong id = Native.Service(name ?? "");
            if (id == 0) throw new ArgumentException("Game.GetService: no service named '" + name + "'");
            return Instance.FromId(id);
        }

        // The service, or null when the DataModel has no service of that name (the engine uses this while scanning).
        public static Instance TryService(string name)
        {
            ulong id = Native.IsReady ? Native.Service(name ?? "") : 0UL;
            return id == 0 ? null : Instance.FromId(id);
        }
    }

    /// <summary>
    /// Shortcuts over the Players service, as in Roblox scripts: Players.LocalPlayer, Players.GetPlayerFromCharacter.
    /// </summary>
    public static class Players
    {
        private static PlayersService Service => Game.TryService("Players") as PlayersService;

        /// <summary>The local player (the first Player). Null outside Play mode.</summary>
        public static Player LocalPlayer => Service?.LocalPlayer;

        public static Player[] GetPlayers() => Service?.GetPlayers() ?? new Player[0];

        /// <summary>The player whose character is the given model, or null.</summary>
        public static Player GetPlayerFromCharacter(Instance character) => Service?.GetPlayerFromCharacter(character);
    }

    /// <summary>Keys of the keyboard. The order and names match tefra::Key (engine/core/input.h).</summary>
    public enum KeyCode
    {
        Unknown,
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Zero, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
        Space, Return, Escape, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown,
        Left, Right, Up, Down,
        LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
    }

    /// <summary>Which input produced an InputObject.</summary>
    public enum UserInputType
    {
        Keyboard, MouseButton1, MouseButton2, MouseButton3
    }

    /// <summary>One key or mouse button press / release, passed to UserInputService.InputBegan / InputEnded.</summary>
    public sealed class InputObject
    {
        public KeyCode KeyCode { get; }
        public UserInputType UserInputType { get; }

        internal InputObject(string text)
        {
            KeyCode key;
            if (text != null && Enum.TryParse(text, out key)) KeyCode = key;
            else KeyCode = KeyCode.Unknown;
            UserInputType type;
            if (text != null && text.StartsWith("MouseButton", StringComparison.Ordinal) &&
                Enum.TryParse(text, out type))
                UserInputType = type;
            else
                UserInputType = UserInputType.Keyboard;
        }
    }

    /// <summary>Keyboard and mouse state. IsKeyDown reads the snapshot of the current frame; the events come from the
    /// engine loop.</summary>
    public static class UserInputService
    {
        /// <summary>Fires when a key or mouse button goes down. Use `io.KeyCode == KeyCode.E` to filter.</summary>
        public static event Action<InputObject> InputBegan;

        /// <summary>Fires when a key or mouse button goes up.</summary>
        public static event Action<InputObject> InputEnded;

        public static bool IsKeyDown(KeyCode key) => Native.IsReady && Native.KeyDown((int)key);

        public static bool IsMouseButtonPressed(UserInputType button)
        {
            if (!Native.IsReady || button == UserInputType.Keyboard) return false;
            return Native.MouseDown((int)button - (int)UserInputType.MouseButton1);
        }

        /// <summary>Pointer position in pixels of the game view (top-left origin).</summary>
        public static Vector2 MouseLocation
        {
            get
            {
                double x, y;
                if (Native.IsReady) Native.MouseLocation(out x, out y);
                else { x = 0; y = 0; }
                return new Vector2((float)x, (float)y);
            }
        }

        internal static void Dispatch(bool began, string text)
        {
            Action<InputObject> handlers = began ? InputBegan : InputEnded;
            if (handlers == null) return;
            var io = new InputObject(text);
            foreach (Delegate d in handlers.GetInvocationList())
            {
                try
                {
                    ((Action<InputObject>)d)(io);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, (began ? "InputBegan" : "InputEnded") + " handler failed: " + e);
                }
            }
        }

        internal static void Clear()
        {
            InputBegan = null;
            InputEnded = null;
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
