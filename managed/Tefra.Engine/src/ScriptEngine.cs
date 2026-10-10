// Script lifecycle: collect the enabled Script instances, compile them, run Start, then Update every frame.
// A script that throws in Start or Update is reported with its stack trace and disabled; the exception never
// leaves this file (it would abort the native host).
using System;
using System.Collections.Generic;

namespace Tefra
{
    /// <summary>
    /// Base class of every user script. Declare one public class per Script instance and override what you need:
    /// <code>
    /// public class Spinner : TefraScript
    /// {
    ///     public override void Update(float dt)
    ///     {
    ///         var p = (Part)Parent;
    ///         p.Orientation += new Vector3(0, 90 * dt, 0);
    ///     }
    /// }
    /// </code>
    /// </summary>
    public abstract class TefraScript
    {
        /// <summary>The Script instance this object runs for.</summary>
        public Script Script { get; private set; }

        /// <summary>The instance that contains the Script (script.Parent).</summary>
        public Instance Parent => Script == null ? null : Script.Parent;

        /// <summary>Called once, when Play starts. Use `async void` and `await Task.Wait(s)` to yield.</summary>
        public virtual void Start()
        {
        }

        /// <summary>Called every engine step with the frame time in seconds.</summary>
        public virtual void Update(float dt)
        {
        }

        internal void Bind(Script script)
        {
            Script = script;
        }

        protected static void print(params object[] args) => Debug.print(args);
    }

    internal sealed class ScriptSlot
    {
        public Script Instance;   // the Script instance in the DataModel
        public string DisplayName; // "Workspace.Spinner"
        public string Path;        // unique syntax tree path, used to attribute compiler diagnostics
        public string Source;
        public string TypeName;    // metadata name of the TefraScript-derived class
        public Type Type;
        public TefraScript Obj;
        public bool Failed;        // disabled by a compile or runtime error
    }

    internal static class ScriptEngine
    {
        private static readonly List<ScriptSlot> Slots = new List<ScriptSlot>();
        private static ScriptLoadContext _context;
        private static bool _compiled;
        private static bool _running;

        // Compiles every enabled Script under Workspace and ServerScriptService without running them.
        public static int Compile()
        {
            Teardown();
            foreach (ScriptSlot s in CollectScripts()) Slots.Add(s);

            ScriptLoadContext context;
            if (!ScriptCompiler.Compile(Slots, out context))
            {
                Teardown();
                return -1;
            }
            _context = context;
            _compiled = true;

            int ok = 0;
            foreach (ScriptSlot s in Slots)
            {
                if (s.Failed || s.Type == null) continue;
                try
                {
                    s.Obj = (TefraScript)Activator.CreateInstance(s.Type);
                    s.Obj.Bind(s.Instance);
                    ok++;
                }
                catch (Exception e)
                {
                    Fault(s, "constructor", e);
                }
            }
            return ok;
        }

        // Compiles if needed, then runs Start() of every compiled script. Returns the number started.
        public static int Start()
        {
            if (!_compiled && Compile() < 0) return -1;
            _running = true;
            int started = 0;
            foreach (ScriptSlot s in Slots.ToArray())
            {
                if (s.Obj == null || s.Failed) continue;
                try
                {
                    s.Obj.Start();
                    started++;
                }
                catch (Exception e)
                {
                    Fault(s, "Start", e);
                }
            }
            EngineSyncContext.Instance.Pump();
            return started;
        }

        public static void Step(float dt)
        {
            if (!_running) return;
            Yield.Advance(dt);
            RunService.Fire(dt);
            foreach (ScriptSlot s in Slots.ToArray())
            {
                if (s.Obj == null || s.Failed) continue;
                try
                {
                    s.Obj.Update(dt);
                }
                catch (Exception e)
                {
                    Fault(s, "Update", e);
                }
            }
            EngineSyncContext.Instance.Pump();
        }

        public static void Stop()
        {
            _running = false;
            Teardown();
        }

        private static void Teardown()
        {
            // Drop every reference into the script assembly before unloading it: wrapper events, heartbeat and
            // pending tasks all hold user delegates.
            RunService.Clear();
            Yield.Clear();
            Instance.ClearAll();
            foreach (ScriptSlot s in Slots)
            {
                s.Obj = null;
                s.Type = null;
            }
            Slots.Clear();
            _compiled = false;
            if (_context != null)
            {
                ScriptLoadContext ctx = _context;
                _context = null;
                ctx.Unload();
            }
        }

        private static List<ScriptSlot> CollectScripts()
        {
            var list = new List<ScriptSlot>();
            foreach (string service in new[] { "Workspace", "ServerScriptService" })
            {
                Instance root = Game.GetService(service);
                if (root == null) continue;
                foreach (Instance d in root.GetDescendants())
                {
                    Script script = d as Script;
                    if (script == null || !script.Enabled) continue;
                    list.Add(new ScriptSlot
                    {
                        Instance = script,
                        DisplayName = script.FullName,
                        Path = script.FullName + "#" + list.Count,
                        Source = script.Source ?? "",
                    });
                }
            }
            return list;
        }

        private static void Fault(ScriptSlot s, string phase, Exception e)
        {
            s.Failed = true;
            if (e is System.Reflection.TargetInvocationException tie && tie.InnerException != null) e = tie.InnerException;
            Native.Log(Native.LogError, s.DisplayName + ": exception in " + phase + "; the script was disabled.\n" + e);
        }
    }
}
