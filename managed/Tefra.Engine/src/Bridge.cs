// Entry points called by the native host (engine/scripting). Every method here is an [UnmanagedCallersOnly] export
// and must never let an exception escape: a managed exception that reaches native code aborts the process.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace Tefra
{
    public static unsafe class Bridge
    {
        private static bool _initialized;

        // Called once by the host, via hostfxr load_assembly_and_get_function_pointer. Returns 0 on success.
        [UnmanagedCallersOnly]
        public static int Init(IntPtr nativePtr, IntPtr managedPtr)
        {
            try
            {
                var native = (NativeApi*)nativePtr;
                var managed = (ManagedApi*)managedPtr;
                if (native == null || managed == null) return -1;
                if (native->abi_version != Native.AbiVersion) return -2;
                if (!_initialized)
                {
                    Native.Api = native;
                    // Async void methods post their exceptions to the current SynchronizationContext. Installing an
                    // engine context keeps them on the main thread and lets the loop report them (no thread pool).
                    SynchronizationContext.SetSynchronizationContext(EngineSyncContext.Instance);
                    _initialized = true;
                }
                managed->abi_version = Native.AbiVersion;
                managed->compile = (delegate* unmanaged<int>)&Compile;
                managed->start = (delegate* unmanaged<int>)&Start;
                managed->step = (delegate* unmanaged<float, int>)&Step;
                managed->dispatch = (delegate* unmanaged<IntPtr, uint, void>)&Dispatch;
                managed->stop = (delegate* unmanaged<void>)&Stop;
                return 0;
            }
            catch (Exception e)
            {
                Debug.Report("Tefra.Engine init failed: " + e);
                return -3;
            }
        }

        [UnmanagedCallersOnly]
        public static int Compile()
        {
            try
            {
                return ScriptEngine.Compile();
            }
            catch (Exception e)
            {
                Native.Log(Native.LogError, "CompileScripts failed: " + e);
                return -1;
            }
        }

        [UnmanagedCallersOnly]
        public static int Start()
        {
            try
            {
                return ScriptEngine.Start();
            }
            catch (Exception e)
            {
                Native.Log(Native.LogError, "Start failed: " + e);
                return -1;
            }
        }

        [UnmanagedCallersOnly]
        public static int Step(float dt)
        {
            try
            {
                ScriptEngine.Step(dt);
                return 0;
            }
            catch (Exception e)
            {
                Native.Log(Native.LogError, "Step failed: " + e);
                return -1;
            }
        }

        [UnmanagedCallersOnly]
        public static void Dispatch(IntPtr events, uint count)
        {
            try
            {
                Instance.DispatchEvents((TefraEvent*)events, count);
            }
            catch (Exception e)
            {
                Native.Log(Native.LogError, "Dispatch failed: " + e);
            }
        }

        [UnmanagedCallersOnly]
        public static void Stop()
        {
            try
            {
                ScriptEngine.Stop();
            }
            catch (Exception e)
            {
                Native.Log(Native.LogError, "Stop failed: " + e);
            }
        }
    }

    // Single-threaded SynchronizationContext: Post queues work that the engine loop runs in Step (no threads).
    internal sealed class EngineSyncContext : SynchronizationContext
    {
        public static readonly EngineSyncContext Instance = new EngineSyncContext();
        private readonly List<(SendOrPostCallback cb, object state)> _queue = new List<(SendOrPostCallback, object)>();

        public override void Post(SendOrPostCallback d, object state)
        {
            _queue.Add((d, state));
        }

        public override void Send(SendOrPostCallback d, object state)
        {
            d(state);
        }

        // Runs queued callbacks. An exception here comes from async code that has no caller left to receive it.
        public void Pump()
        {
            while (_queue.Count > 0)
            {
                var batch = _queue.ToArray();
                _queue.Clear();
                foreach (var item in batch)
                {
                    try
                    {
                        item.cb(item.state);
                    }
                    catch (Exception e)
                    {
                        Native.Log(Native.LogError, "Unhandled exception in async script code: " + e);
                    }
                }
            }
        }
    }
}
