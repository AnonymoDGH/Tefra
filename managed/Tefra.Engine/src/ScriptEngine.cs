// Script lifecycle. A script runs where it lives in the DataModel (ADR-013, M3b):
//   Script      under Workspace or ServerScriptService;
//   LocalScript under a Player (PlayerGui, PlayerScripts, ...) or under a Player's Character.
// Templates in StarterGui / StarterPlayer never run; the runtime clones them into the player at spawn and the clones
// start as soon as they appear. Destroying a script (or moving it out of a run location) stops it at the next step.
// A script that throws in Start or Update is reported with its stack trace and disabled; the exception never leaves
// this file (it would abort the native host).
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Microsoft.CodeAnalysis;

namespace Tefra
{
    /// <summary>
    /// Base class of every user script. Declare one public class per Script or LocalScript and override what you need:
    /// <code>
    /// public class Spinner : TefraScript
    /// {
    ///     public override void Update(float dt)
    ///     {
    ///         var p = (BasePart)Parent;
    ///         p.Orientation += new Vector3(0, 90 * dt, 0);
    ///     }
    /// }
    /// </code>
    /// Every Script instance gets its own TefraScript object; identical sources share one compiled class.
    /// </summary>
    public abstract class TefraScript
    {
        /// <summary>The Script or LocalScript instance this object runs for.</summary>
        public BaseScript Script { get; private set; }

        /// <summary>The instance that contains the script (script.Parent).</summary>
        public Instance Parent => Script == null ? null : Script.Parent;

        /// <summary>The Workspace service, as in Roblox scripts: `Workspace.Raycast(origin, direction)`.</summary>
        protected static Workspace Workspace => Game.Workspace;

        /// <summary>Called once, when the script starts. Use `async void` and `await Task.Wait(s)` to yield.</summary>
        public virtual void Start()
        {
        }

        /// <summary>Called every engine step with the frame time in seconds.</summary>
        public virtual void Update(float dt)
        {
        }

        internal void Bind(BaseScript script)
        {
            Script = script;
        }

        protected static void print(params object[] args) => Debug.print(args);
    }

    // One running script: the Script/LocalScript instance and the object that runs it.
    internal sealed class ScriptSlot
    {
        public ulong Id;           // the BaseScript instance
        public string DisplayName; // "Workspace.Spinner"
        public string Source;      // the text the slot was compiled from (a change restarts the script)
        public ScriptUnit Unit;    // the compiled type (shared by identical sources)
        public TefraScript Obj;
        public bool Started;
        public bool Failed;        // disabled by a compile or runtime error
        public bool Retired;       // destroyed, disabled, moved or edited: its handlers and resumptions no longer run
    }

    // A handler subscribed by user code, with the script that subscribed it (null: engine code). Handlers of a
    // retired script are skipped, so a destroyed Script stops responding to Touched, Heartbeat, InputBegan and so on.
    internal struct Handler
    {
        public Delegate Fn;
        public ScriptSlot Owner;
    }

    internal static class Handlers
    {
        public static void Add(List<Handler> list, Delegate fn)
        {
            if (fn == null) return;
            list.Add(new Handler { Fn = fn, Owner = ScriptEngine.Current });
        }

        // Removes the first subscription equal to fn (same rule as Delegate.Remove).
        public static void Remove(List<Handler> list, Delegate fn)
        {
            if (fn == null) return;
            for (int i = list.Count - 1; i >= 0; i--)
            {
                if (!list[i].Fn.Equals(fn)) continue;
                list.RemoveAt(i);
                return;
            }
        }

        // The handlers that may run now, in subscription order. A copy: a handler may subscribe or unsubscribe.
        public static List<Handler> Live(List<Handler> list)
        {
            var live = new List<Handler>(list.Count);
            foreach (Handler h in list)
                if (ScriptEngine.IsLive(h.Owner)) live.Add(h);
            return live;
        }
    }

    internal static class ScriptEngine
    {
        // Service roots scanned for running scripts, and for ModuleScripts (all of them: modules can live anywhere).
        private static readonly string[] RunRoots = { "Workspace", "ServerScriptService", "Players" };
        private static readonly string[] ModuleRoots =
        {
            "Workspace", "Lighting", "ReplicatedStorage", "ServerScriptService", "StarterPlayer", "StarterGui",
            "Players", "SoundService"
        };

        private static readonly List<ScriptSlot> Slots = new List<ScriptSlot>();
        private static readonly Dictionary<ulong, ScriptSlot> SlotById = new Dictionary<ulong, ScriptSlot>();
        private static readonly Dictionary<string, ScriptUnit> Units = new Dictionary<string, ScriptUnit>();
        private static ScriptLoadContext _context;
        private static MetadataReference _library; // the batch that holds the ModuleScripts, referenced by later batches
        private static bool _compiled;
        private static bool _running;
        private static bool _dirty;
        private static int _unitCounter;

        // The script whose user code is running (Start, Update, an event handler, a resumed Task.Wait), or null in
        // engine code. Subscriptions and waits made now are attributed to it.
        public static ScriptSlot Current;

        // False once the script was retired, or its instance is gone (destroyed from managed code, not yet re-scanned):
        // its handlers and resumptions must not run. Null (engine code) is always live.
        public static bool IsLive(ScriptSlot owner)
        {
            return owner == null || (!owner.Retired && Native.Exists(owner.Id));
        }

        // Makes `owner` the current script and returns the previous one (restore it with Current = previous).
        internal static ScriptSlot Enter(ScriptSlot owner)
        {
            ScriptSlot previous = Current;
            Current = owner;
            return previous;
        }

        // Set by the DataModel listeners (added, removed, reparented, Source/Enabled changed): the next step re-scans.
        public static void MarkDirty()
        {
            _dirty = true;
        }

        // Compiles every script that should run, plus the ModuleScripts, without running them. Returns the number of
        // scripts that have an object, -1 on fatal error.
        public static int Compile()
        {
            Teardown();
            _context = new ScriptLoadContext();
            _compiled = true;
            Sync(CollectModules());
            return Slots.Count(s => s.Obj != null);
        }

        // Compiles if needed, then runs Start() of every script. Returns the number started.
        public static int Start()
        {
            if (!_compiled && Compile() < 0) return -1;
            _running = true;
            Sync(null); // catches up with changes since Compile()
            StartPending();
            EngineSyncContext.Instance.Pump();
            return Slots.Count(s => s.Started);
        }

        public static void Step(float dt)
        {
            if (!_running) return;
            if (_dirty) Sync(null);
            Remoting.Deliver(); // RemoteEvent messages sent during the previous step
            Yield.Advance(dt);
            Debris.Advance(dt);
            RunService.Fire(dt);
            foreach (ScriptSlot s in Slots.ToArray())
            {
                if (s.Obj == null || s.Failed || !s.Started) continue;
                if (!Native.Exists(s.Id))
                {
                    _dirty = true; // destroyed during this step: retired at the next sync
                    continue;
                }
                ScriptSlot previous = Enter(s);
                try
                {
                    s.Obj.Update(dt);
                }
                catch (Exception e)
                {
                    Fault(s, "Update", e);
                }
                finally
                {
                    Current = previous;
                }
            }
            EngineSyncContext.Instance.Pump();
        }

        public static void Stop()
        {
            _running = false;
            Teardown();
        }

        // Brings the slots in line with the DataModel: retires scripts that are gone, disabled, moved out of a run
        // location or edited; creates slots for new ones, compiles their sources and starts them while running.
        // `extra` are units compiled in the same batch (the ModuleScripts, at Compile time only).
        private static void Sync(List<ScriptUnit> extra)
        {
            _dirty = false;

            var desired = new Dictionary<ulong, BaseScript>();
            var order = new List<BaseScript>();
            foreach (BaseScript b in RunnableScripts())
            {
                if (desired.ContainsKey(b.Id)) continue;
                desired[b.Id] = b;
                order.Add(b);
            }

            for (int i = Slots.Count - 1; i >= 0; i--)
            {
                ScriptSlot s = Slots[i];
                BaseScript b;
                bool keep = desired.TryGetValue(s.Id, out b) && (b.Source ?? "") == s.Source;
                if (!keep) Retire(s);
            }

            var fresh = new List<ScriptSlot>();
            var batch = new List<ScriptUnit>();
            if (extra != null) batch.AddRange(extra);
            foreach (BaseScript b in order)
            {
                if (SlotById.ContainsKey(b.Id)) continue;
                string source = b.Source ?? "";
                ScriptUnit unit;
                if (!Units.TryGetValue(source, out unit))
                {
                    int n = ++_unitCounter;
                    unit = new ScriptUnit
                    {
                        Source = source,
                        Namespace = "TefraS" + n,
                        Path = "tefra-script#" + n + ".cs",
                    };
                    Units[source] = unit;
                    batch.Add(unit);
                }
                var slot = new ScriptSlot { Id = b.Id, DisplayName = b.FullName, Source = source, Unit = unit };
                unit.Slots.Add(slot);
                Slots.Add(slot);
                SlotById[slot.Id] = slot;
                fresh.Add(slot);
            }

            if (batch.Count > 0)
            {
                foreach (ScriptUnit u in batch) u.Label = LabelOf(u);
                MetadataReference image;
                bool ok = ScriptCompiler.Build(batch, _context, _library, out image);
                if (!ok)
                    foreach (ScriptUnit u in batch) u.Failed = true;
                if (ok && extra != null && _library == null && extra.Any(u => !u.Failed)) _library = image;
            }
            foreach (ScriptSlot s in fresh)
                if (s.Unit.Failed || s.Unit.Type == null) s.Failed = true;

            foreach (ScriptSlot s in fresh)
            {
                if (s.Failed) continue;
                try
                {
                    s.Obj = (TefraScript)Activator.CreateInstance(s.Unit.Type);
                    s.Obj.Bind(desired[s.Id]);
                }
                catch (Exception e)
                {
                    Fault(s, "constructor", e);
                }
            }
            if (_running) StartPending();
        }

        private static void StartPending()
        {
            foreach (ScriptSlot s in Slots.ToArray())
            {
                if (s.Started || s.Failed || s.Obj == null) continue;
                s.Started = true;
                ScriptSlot previous = Enter(s);
                try
                {
                    s.Obj.Start();
                }
                catch (Exception e)
                {
                    Fault(s, "Start", e);
                }
                finally
                {
                    Current = previous;
                }
            }
        }

        private static void Retire(ScriptSlot s)
        {
            s.Retired = true;
            Slots.Remove(s);
            SlotById.Remove(s.Id);
            s.Unit.Slots.Remove(s);
            s.Obj = null;
            if (s.Unit.Slots.Count > 0) s.Unit.Label = LabelOf(s.Unit);
        }

        // Scripts that should run now: enabled, and inside a run location for their class.
        private static IEnumerable<BaseScript> RunnableScripts()
        {
            foreach (string service in RunRoots)
            {
                Instance root = Game.TryService(service);
                if (root == null) continue;
                foreach (Instance d in root.GetDescendants())
                {
                    BaseScript b = d as BaseScript;
                    if (b == null || !b.Enabled || !ShouldRun(b)) continue;
                    yield return b;
                }
            }
        }

        private static bool ShouldRun(BaseScript b)
        {
            if (b is Script)
                return IsUnder(b, "Workspace") || IsUnder(b, "ServerScriptService");
            if (b is LocalScript)
                return InsidePlayer(b);
            return false;
        }

        private static bool IsUnder(Instance x, string service)
        {
            Instance root = Game.TryService(service);
            return root != null && x.IsDescendantOf(root);
        }

        // True under a Player instance, or under the Model that is some Player's Character.
        private static bool InsidePlayer(Instance x)
        {
            for (Instance p = x.Parent; p != null; p = p.Parent)
            {
                if (p.IsA("Player")) return true;
                if (p.IsA("Model") && IsCharacterOfAnyPlayer(p)) return true;
            }
            return false;
        }

        private static bool IsCharacterOfAnyPlayer(Instance model)
        {
            Instance players = Game.TryService("Players");
            if (players == null) return false;
            foreach (Instance child in players.GetChildren())
            {
                Player player = child as Player;
                if (player != null && player.Character != null && player.Character.Id == model.Id) return true;
            }
            return false;
        }

        // ModuleScripts are compiled once per Play, with the first batch. Edits to them during Play take effect at the
        // next Play; modules added during Play are not visible to scripts (documented in the M3b report).
        private static List<ScriptUnit> CollectModules()
        {
            var list = new List<ScriptUnit>();
            foreach (string service in ModuleRoots)
            {
                Instance root = Game.TryService(service);
                if (root == null) continue;
                foreach (Instance d in root.GetDescendants())
                {
                    ModuleScript m = d as ModuleScript;
                    if (m == null) continue;
                    int n = ++_unitCounter;
                    var unit = new ScriptUnit
                    {
                        Source = m.Source ?? "",
                        Path = "tefra-module#" + n + ".cs",
                        IsModule = true,
                        Label = "ModuleScript " + m.FullName,
                    };
                    list.Add(unit);
                }
            }
            return list;
        }

        private static string LabelOf(ScriptUnit u)
        {
            return string.Join(", ", u.Slots.Select(s => s.DisplayName));
        }

        private static void Fault(ScriptSlot s, string phase, Exception e)
        {
            s.Failed = true;
            s.Retired = true; // a disabled script keeps no handler running either
            if (e is TargetInvocationException tie && tie.InnerException != null) e = tie.InnerException;
            Native.Log(Native.LogError, s.DisplayName + ": exception in " + phase + "; the script was disabled.\n" + e);
        }

        private static void Teardown()
        {
            // Drop every reference into the script assembly before unloading it: wrapper events, heartbeat and
            // pending tasks all hold user delegates.
            RunService.Clear();
            UserInputService.Clear();
            Yield.Clear();
            EventTables.Clear();
            Debris.Clear();
            Instance.ClearAll();
            foreach (ScriptSlot s in Slots)
            {
                s.Obj = null;
                s.Retired = true;
            }
            Current = null;
            Slots.Clear();
            SlotById.Clear();
            Units.Clear();
            _library = null;
            _compiled = false;
            _dirty = false;
            if (_context != null)
            {
                ScriptLoadContext ctx = _context;
                _context = null;
                ctx.Unload();
            }
        }
    }
}
