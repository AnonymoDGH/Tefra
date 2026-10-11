// Messages between scripts: BindableEvent / BindableFunction (same script step) and RemoteEvent / RemoteFunction
// (server and client share this process, so a "remote" call is a loopback).
// Handler tables are static and keyed by instance id. Stop clears them with the rest of the script state
// (EventTables.Clear, called from ScriptEngine.Teardown). Bindables run their handlers at once. A RemoteEvent message
// is queued and delivered at the start of the next script step, as if it had crossed a network.
using System;
using System.Collections.Generic;

namespace Tefra
{
    // Handlers of one kind of message, keyed by instance id: a list (events, many handlers) or one callback (the
    // OnInvoke-style properties). Owners follow the rules of Handlers: a retired script's handlers never run.
    internal sealed class HandlerTable
    {
        private readonly Dictionary<ulong, List<Handler>> _lists = new Dictionary<ulong, List<Handler>>();
        private readonly Dictionary<ulong, Handler> _single = new Dictionary<ulong, Handler>();

        public void Add(ulong id, Delegate fn)
        {
            if (fn == null) return;
            List<Handler> list;
            if (!_lists.TryGetValue(id, out list)) _lists[id] = list = new List<Handler>();
            Handlers.Add(list, fn);
        }

        public void Remove(ulong id, Delegate fn)
        {
            List<Handler> list;
            if (fn != null && _lists.TryGetValue(id, out list)) Handlers.Remove(list, fn);
        }

        // Calls `call` for every live handler of `id`, in subscription order. A handler that throws is logged and the
        // others still run.
        public void Each(ulong id, string what, Action<Delegate> call)
        {
            List<Handler> list;
            if (!_lists.TryGetValue(id, out list) || list.Count == 0) return;
            foreach (Handler h in Handlers.Live(list))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    call(h.Fn);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, what + " handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        // Replaces the single callback of `id` (null clears it). The callback belongs to the script that set it.
        public void Set(ulong id, Delegate fn)
        {
            if (fn == null) _single.Remove(id);
            else _single[id] = new Handler { Fn = fn, Owner = ScriptEngine.Current };
        }

        // The single callback of `id`, or a Handler with a null Fn when it is unset or its script is gone.
        public Handler Get(ulong id)
        {
            Handler h;
            if (!_single.TryGetValue(id, out h) || h.Fn == null || !ScriptEngine.IsLive(h.Owner)) return default(Handler);
            return h;
        }

        // Runs the single callback of `id` through `call` in its script's context. Null when none may run.
        // Exceptions from the callback propagate to the caller.
        public object Call(ulong id, Func<Delegate, object> call)
        {
            Handler h = Get(id);
            if (h.Fn == null) return null;
            ScriptSlot previous = ScriptEngine.Enter(h.Owner);
            try
            {
                return call(h.Fn);
            }
            finally
            {
                ScriptEngine.Current = previous;
            }
        }

        public void Clear()
        {
            _lists.Clear();
            _single.Clear();
        }
    }

    internal static class EventTables
    {
        public static readonly HandlerTable BindableEvents = new HandlerTable();
        public static readonly HandlerTable BindableFunctions = new HandlerTable();
        public static readonly HandlerTable ServerRemotes = new HandlerTable();   // RemoteEvent.OnServerEvent
        public static readonly HandlerTable ClientRemotes = new HandlerTable();   // RemoteEvent.OnClientEvent
        public static readonly HandlerTable ServerInvokers = new HandlerTable();  // RemoteFunction.OnServerInvoke
        public static readonly HandlerTable ClientInvokers = new HandlerTable();  // RemoteFunction.OnClientInvoke

        public static void Clear()
        {
            BindableEvents.Clear();
            BindableFunctions.Clear();
            ServerRemotes.Clear();
            ClientRemotes.Clear();
            ServerInvokers.Clear();
            ClientInvokers.Clear();
            Remoting.Clear();
        }
    }

    // The queue of RemoteEvent messages. A message sent during a step is delivered at the start of the next one, and a
    // handler that fires another remote does not see it before then.
    internal static class Remoting
    {
        private struct Message
        {
            public ulong Remote;
            public bool ToServer;
            public Player Sender;   // the player that fired (FireServer); null for FireClient / FireAllClients
            public object[] Args;
        }

        private static readonly List<Message> Queue = new List<Message>();

        public static void Enqueue(ulong remote, bool toServer, Player sender, object[] args)
        {
            Queue.Add(new Message { Remote = remote, ToServer = toServer, Sender = sender, Args = args ?? new object[0] });
        }

        // Delivers the messages queued before this call. Messages queued by the handlers wait for the next step.
        public static void Deliver()
        {
            if (Queue.Count == 0) return;
            Message[] batch = Queue.ToArray();
            Queue.Clear();
            foreach (Message m in batch)
            {
                if (!Native.Exists(m.Remote)) continue; // the RemoteEvent was destroyed meanwhile
                if (m.ToServer)
                    EventTables.ServerRemotes.Each(m.Remote, "RemoteEvent.OnServerEvent",
                        fn => ((Action<Player, object[]>)fn)(m.Sender, m.Args));
                else
                    EventTables.ClientRemotes.Each(m.Remote, "RemoteEvent.OnClientEvent",
                        fn => ((Action<object[]>)fn)(m.Args));
            }
        }

        public static void Clear()
        {
            Queue.Clear();
        }
    }
}
