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

    /// <summary>
    /// The changes of one attribute, from Instance.GetAttributeChangedSignal(name). Connect(handler) runs the handler
    /// after the attribute is set or removed (on the next engine step, like Changed).
    /// </summary>
    public sealed class AttributeChangedSignal
    {
        private readonly Instance _owner;
        private readonly string _name;
        private readonly List<KeyValuePair<Action, Action<string>>> _links = new List<KeyValuePair<Action, Action<string>>>();

        internal AttributeChangedSignal(Instance owner, string name)
        {
            _owner = owner;
            _name = name;
        }

        public void Connect(Action handler)
        {
            if (handler == null) return;
            Action<string> filter = changed =>
            {
                if (changed == _name) handler();
            };
            _links.Add(new KeyValuePair<Action, Action<string>>(handler, filter));
            _owner.AddSignal("AttributeChanged", filter);
        }

        public void Disconnect(Action handler)
        {
            for (int i = _links.Count - 1; i >= 0; i--)
            {
                if (!_links[i].Key.Equals(handler)) continue;
                _owner.RemoveSignal("AttributeChanged", _links[i].Value);
                _links.RemoveAt(i);
                return;
            }
        }
    }

    /// <summary>
    /// Connect/Disconnect for the InstanceAdded or InstanceRemoved events of one tag (CollectionService).
    /// </summary>
    public sealed class TagSignal
    {
        private readonly List<Handler> _handlers;

        internal TagSignal(List<Handler> handlers)
        {
            _handlers = handlers;
        }

        public void Connect(Action<Instance> handler) => Handlers.Add(_handlers, handler);

        public void Disconnect(Action<Instance> handler) => Handlers.Remove(_handlers, handler);
    }

    /// <summary>
    /// Tags are labels on instances, shared by scripts: GetTagged lists every instance under the DataModel that carries
    /// a tag. GetInstanceAddedSignal and GetInstanceRemovedSignal report the instances that gain or lose one.
    /// Instances removed by Destroy do not raise InstanceRemoved.
    /// </summary>
    public static class CollectionService
    {
        private static readonly Dictionary<string, List<Handler>> AddedHandlers = new Dictionary<string, List<Handler>>();
        private static readonly Dictionary<string, List<Handler>> RemovedHandlers = new Dictionary<string, List<Handler>>();

        public static void AddTag(Instance instance, string tag)
        {
            if (instance == null) throw new ArgumentNullException(nameof(instance));
            instance.AddTag(tag);
        }

        public static void RemoveTag(Instance instance, string tag)
        {
            if (instance != null) instance.RemoveTag(tag);
        }

        public static bool HasTag(Instance instance, string tag) => instance != null && instance.HasTag(tag);

        public static string[] GetTags(Instance instance) => instance == null ? new string[0] : instance.GetTags();

        public static Instance[] GetTagged(string tag)
        {
            tag ??= "";
            int n = Native.TaggedCount(tag);
            var list = new Instance[Math.Max(0, n)];
            for (int i = 0; i < list.Length; i++) list[i] = Instance.FromId(Native.TaggedAt(tag, i));
            return list;
        }

        public static TagSignal GetInstanceAddedSignal(string tag) => new TagSignal(Bucket(AddedHandlers, tag));

        public static TagSignal GetInstanceRemovedSignal(string tag) => new TagSignal(Bucket(RemovedHandlers, tag));

        private static List<Handler> Bucket(Dictionary<string, List<Handler>> map, string tag)
        {
            tag ??= "";
            List<Handler> list;
            if (!map.TryGetValue(tag, out list))
            {
                list = new List<Handler>();
                map[tag] = list;
            }
            return list;
        }

        // Called by Instance for a TagAdded / TagRemoved signal. The instance is null when it no longer exists.
        internal static void Dispatch(bool added, ulong id, string tag)
        {
            Dictionary<string, List<Handler>> map = added ? AddedHandlers : RemovedHandlers;
            List<Handler> list;
            if (!map.TryGetValue(tag, out list) || list.Count == 0) return;
            Instance inst = Instance.FromId(id);
            foreach (Handler h in Handlers.Live(list))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action<Instance>)h.Fn)(inst);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "CollectionService." + (added ? "InstanceAdded" : "InstanceRemoved") +
                                                " handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        internal static void ClearAll()
        {
            AddedHandlers.Clear();
            RemovedHandlers.Clear();
        }
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
        public static event Action<InputObject> InputBegan
        {
            add => Handlers.Add(BeganHandlers, value);
            remove => Handlers.Remove(BeganHandlers, value);
        }

        /// <summary>Fires when a key or mouse button goes up.</summary>
        public static event Action<InputObject> InputEnded
        {
            add => Handlers.Add(EndedHandlers, value);
            remove => Handlers.Remove(EndedHandlers, value);
        }

        private static readonly List<Handler> BeganHandlers = new List<Handler>();
        private static readonly List<Handler> EndedHandlers = new List<Handler>();

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
            List<Handler> handlers = Handlers.Live(began ? BeganHandlers : EndedHandlers);
            if (handlers.Count == 0) return;
            var io = new InputObject(text);
            foreach (Handler h in handlers)
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action<InputObject>)h.Fn)(io);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, (began ? "InputBegan" : "InputEnded") + " handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        internal static void Clear()
        {
            BeganHandlers.Clear();
            EndedHandlers.Clear();
        }
    }

    /// <summary>Per-frame events. Handlers run on the engine loop, after the scheduled tasks resume.</summary>
    public static class RunService
    {
        /// <summary>Fires once per engine step with the frame time in seconds.</summary>
        public static event Action<float> Heartbeat
        {
            add => Handlers.Add(HeartbeatHandlers, value);
            remove => Handlers.Remove(HeartbeatHandlers, value);
        }

        private static readonly List<Handler> HeartbeatHandlers = new List<Handler>();

        internal static void Fire(float dt)
        {
            if (HeartbeatHandlers.Count == 0) return;
            foreach (Handler h in Handlers.Live(HeartbeatHandlers))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action<float>)h.Fn)(dt);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "RunService.Heartbeat handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        internal static void Clear()
        {
            HeartbeatHandlers.Clear();
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
        private ScriptSlot _owner; // the script that awaited it: a retired script is never resumed

        internal Yield(float seconds)
        {
            _remaining = seconds;
        }

        public Yield GetAwaiter() => this;

        public bool IsCompleted => _remaining <= 0f;

        public void OnCompleted(Action continuation)
        {
            _continuation = continuation;
            _owner = ScriptEngine.Current;
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
                Yield y = ready[i];
                Action c = y._continuation;
                y._continuation = null;
                if (c == null || !ScriptEngine.IsLive(y._owner)) continue; // the script was destroyed meanwhile
                ScriptSlot previous = ScriptEngine.Enter(y._owner);
                try
                {
                    c.Invoke();
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "Task resumed with an exception: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        internal static void Clear()
        {
            Waiting.Clear();
        }
    }

    /// <summary>
    /// Destroys instances after a delay, as in Roblox: `Debris.AddItem(part, 5f);`. The delay counts game time, in
    /// the script loop; an item destroyed before its time is skipped.
    /// </summary>
    public static class Debris
    {
        private struct Entry
        {
            public ulong Id;
            public float Remaining;
        }

        private static readonly List<Entry> Items = new List<Entry>();

        /// <summary>Destroys item after lifetime seconds of game time.</summary>
        public static void AddItem(Instance item, float lifetime = 10f)
        {
            if (item == null) return;
            Items.Add(new Entry { Id = item.Id, Remaining = lifetime });
        }

        // Called by the script loop, once per step.
        internal static void Advance(float dt)
        {
            if (Items.Count == 0) return;
            var due = new List<ulong>();
            for (int i = Items.Count - 1; i >= 0; i--)
            {
                Entry e = Items[i];
                if (!Native.Exists(e.Id))
                {
                    Items.RemoveAt(i); // destroyed by someone else
                    continue;
                }
                e.Remaining -= dt;
                if (e.Remaining > 0f)
                {
                    Items[i] = e;
                    continue;
                }
                due.Add(e.Id);
                Items.RemoveAt(i);
            }
            // The loop above went newest first; destroy oldest first.
            for (int i = due.Count - 1; i >= 0; i--)
                Instance.FromId(due[i])?.Destroy();
        }

        internal static void Clear()
        {
            Items.Clear();
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
