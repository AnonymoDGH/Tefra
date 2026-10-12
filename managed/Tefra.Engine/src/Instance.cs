using System;
using System.Collections.Generic;
using System.Text;

namespace Tefra
{
    // Lazily resolved global index of a reflected property (the same index is valid for every subclass).
    internal sealed class PropRef
    {
        private readonly string _cls;
        private readonly string _name;
        private int _index = -1;

        public PropRef(string cls, string name)
        {
            _cls = cls;
            _name = name;
        }

        public string Display => _cls + "." + _name;

        public int Index
        {
            get
            {
                if (_index < 0)
                {
                    _index = Native.PropIndex(_cls, _name);
                    if (_index < 0) throw new InvalidOperationException("Tefra: unknown property " + Display);
                }
                return _index;
            }
        }
    }

    // Base of every object in the DataModel. A managed wrapper is created on first access and cached per id; the
    // cache is the only owner of the wrapper, so a wrapper's events live as long as the script can reach it.
    public class Instance
    {
        private static readonly Dictionary<ulong, Instance> Cache = new Dictionary<ulong, Instance>();
        private static readonly Dictionary<string, int> GenericIndex = new Dictionary<string, int>();

        internal readonly ulong Id;

        internal Instance(ulong id)
        {
            Id = id;
        }

        // Fires with the property name after the property changed (Name included). Raised by the engine loop.
        public event Action<string> Changed
        {
            add => Subscribe(ref _changed, value);
            remove => Handlers.Remove(_changed ??= new List<Handler>(), value);
        }

        // Fires when the instance (or an ancestor) is removed from the DataModel.
        public event Action Destroying
        {
            add => Subscribe(ref _destroying, value);
            remove => Handlers.Remove(_destroying ??= new List<Handler>(), value);
        }

        // Fires with the attribute name after it was set or removed. Like Changed, it is delivered on the next engine
        // step, not inside SetAttribute.
        public event Action<string> AttributeChanged
        {
            add => AddSignal("AttributeChanged", value);
            remove => RemoveSignal("AttributeChanged", value);
        }

        private List<Handler> _changed;
        private List<Handler> _destroying;
        private Dictionary<string, AttributeChangedSignal> _attributeSignals;

        private static void Subscribe(ref List<Handler> list, Delegate handler)
        {
            list ??= new List<Handler>();
            Handlers.Add(list, handler);
        }

        private static readonly PropRef NameProp = new PropRef("Instance", "Name");

        public string ClassName => Native.ClassName(Id);

        public bool IsA(string className) => Native.IsA(Id, className);

        public bool IsA<T>() where T : Instance => this is T;

        public string Name
        {
            get => Native.Str(Id, NameProp.Index);
            set => SetString(NameProp, value);
        }

        public Instance Parent
        {
            get => FromId(Native.Parent(Id));
            set => Check(Native.SetParent(Id, value == null ? 0UL : value.Id), "Parent");
        }

        public Instance[] GetChildren()
        {
            int n = Native.ChildCount(Id);
            var list = new Instance[Math.Max(0, n)];
            for (int i = 0; i < list.Length; i++) list[i] = FromId(Native.ChildAt(Id, i));
            return list;
        }

        public Instance[] GetDescendants()
        {
            var output = new List<Instance>();
            CollectDescendants(this, output);
            return output.ToArray();
        }

        public Instance FindFirstChild(string name)
        {
            return FromId(Native.FindChild(Id, name ?? ""));
        }

        public Instance FindFirstChildOfClass(string className)
        {
            foreach (Instance child in GetChildren())
                if (child.IsA(className)) return child;
            return null;
        }

        public bool IsDescendantOf(Instance ancestor)
        {
            for (Instance p = Parent; p != null; p = p.Parent)
                if (ReferenceEquals(p, ancestor)) return true;
            return false;
        }

        // "Workspace.Model.Part" (the DataModel root is omitted, as in paths).
        public string FullName
        {
            get
            {
                string path = Name;
                for (Instance p = Parent; p != null && p.ClassName != "DataModel"; p = p.Parent)
                    path = p.Name + "." + path;
                return path;
            }
        }

        public Instance Clone(Instance parent = null)
        {
            return FromId(Native.Clone(Id, parent == null ? 0UL : parent.Id));
        }

        // Removes the instance and its descendants from the DataModel. The wrapper stays usable but its properties
        // read as defaults and writes throw.
        public void Destroy()
        {
            if (!Native.Destroy(Id)) return;
            Forget(this);
            RaiseDestroying();
        }

        public static Instance New(string className, Instance parent = null)
        {
            ulong id = Native.Create(className ?? "", parent == null ? 0UL : parent.Id, null);
            if (id == 0) throw new ArgumentException("Instance.New: '" + className + "' is not a creatable class");
            return FromId(id);
        }

        // Generic access by property name. Enums come back as their integer index; Refs as Instance.
        public object this[string property]
        {
            get
            {
                int index = PropertyIndex(property);
                TefraValue v;
                if (!Native.TryGet(Id, index, out v)) return null;
                switch (v.type)
                {
                    case PropTypes.Bool: return v.n0 != 0;
                    case PropTypes.Int: return (int)v.n0;
                    case PropTypes.Float: return (float)v.n0;
                    case PropTypes.String: return Native.FromValue(v);
                    case PropTypes.Vector3: return new Vector3((float)v.n0, (float)v.n1, (float)v.n2);
                    case PropTypes.Color3: return new Color3((float)v.n0, (float)v.n1, (float)v.n2);
                    case PropTypes.Enum: return (int)v.n0;
                    case PropTypes.Ref: return FromId(v.reference);
                    default: return null;
                }
            }
            set
            {
                int index = PropertyIndex(property);
                int type = Native.PropType(ClassName, property);
                TefraValue v;
                switch (type)
                {
                    case PropTypes.Bool:
                        v = Native.NumValue(PropTypes.Bool, Convert.ToBoolean(value) ? 1 : 0);
                        break;
                    case PropTypes.Int:
                        v = Native.NumValue(PropTypes.Int, ToDouble(value));
                        break;
                    case PropTypes.Float:
                        v = Native.NumValue(PropTypes.Float, ToDouble(value));
                        break;
                    case PropTypes.Enum:
                        v = Native.NumValue(PropTypes.Enum, ToDouble(value));
                        break;
                    case PropTypes.String:
                        Check(Native.SetString(Id, index, Encoding.UTF8.GetBytes(value as string ?? "")), property);
                        return;
                    case PropTypes.Vector3:
                    {
                        var p = (Vector3)value;
                        v = Native.NumValue(PropTypes.Vector3, p.X, p.Y, p.Z);
                        break;
                    }
                    case PropTypes.Color3:
                    {
                        var c = (Color3)value;
                        v = Native.NumValue(PropTypes.Color3, c.R, c.G, c.B);
                        break;
                    }
                    case PropTypes.Ref:
                        v = Native.RefValue((value as Instance)?.Id ?? 0UL);
                        break;
                    default:
                        throw new ArgumentException("Tefra: unknown property '" + property + "' on " + ClassName);
                }
                Check(Native.Set(Id, index, v), property);
            }
        }

        public override string ToString() => Name;

        // ---- Attributes (free-form values stored on the instance; not reflected properties) -----------------------

        // Sets an attribute. Null removes it. Accepted values: bool, numbers (int and long are stored as integers,
        // float and double as floats), string, Vector3 and Color3. Any other type throws ArgumentException.
        public void SetAttribute(string name, object value)
        {
            if (string.IsNullOrEmpty(name)) throw new ArgumentException("SetAttribute: the name is empty");
            if (value == null)
            {
                RemoveAttribute(name);
                return;
            }
            byte[] text;
            TefraValue v = AttributeValue(value, out text);
            Check(Native.AttrSet(Id, name, v, text), "attribute '" + name + "'");
        }

        public void RemoveAttribute(string name)
        {
            if (!string.IsNullOrEmpty(name)) Native.AttrRemove(Id, name);
        }

        // The attribute's value, or null when it is not set. Every number comes back as a double (as in Roblox), so
        // use Convert.ToInt32 for integer attributes. Vector3 and Color3 come back as their structs.
        public object GetAttribute(string name)
        {
            TefraValue v;
            if (string.IsNullOrEmpty(name) || !Native.AttrGet(Id, name, out v)) return null;
            switch (v.type)
            {
                case PropTypes.Bool: return v.n0 != 0;
                case PropTypes.Int:
                case PropTypes.Float: return v.n0;
                case PropTypes.String: return Native.FromValue(v);
                case PropTypes.Vector3: return new Vector3((float)v.n0, (float)v.n1, (float)v.n2);
                case PropTypes.Color3: return new Color3((float)v.n0, (float)v.n1, (float)v.n2);
                default: return null;
            }
        }

        // Every attribute of the instance, by name (values as GetAttribute returns them).
        public Dictionary<string, object> GetAttributes()
        {
            var result = new Dictionary<string, object>();
            int n = Native.AttrCount(Id);
            for (int i = 0; i < n; i++)
            {
                string name = Native.AttrNameAt(Id, i);
                if (name == null) continue;
                result[name] = GetAttribute(name);
            }
            return result;
        }

        // Connect(handler) runs the handler when this attribute is set or removed (see AttributeChangedSignal).
        public AttributeChangedSignal GetAttributeChangedSignal(string name)
        {
            name ??= "";
            _attributeSignals ??= new Dictionary<string, AttributeChangedSignal>();
            AttributeChangedSignal signal;
            if (!_attributeSignals.TryGetValue(name, out signal))
            {
                signal = new AttributeChangedSignal(this, name);
                _attributeSignals[name] = signal;
            }
            return signal;
        }

        // ---- Tags (CollectionService) ----------------------------------------------------------------------------

        public void AddTag(string tag)
        {
            if (string.IsNullOrEmpty(tag)) throw new ArgumentException("AddTag: the tag is empty");
            Native.TagAdd(Id, tag);
        }

        public void RemoveTag(string tag)
        {
            if (!string.IsNullOrEmpty(tag)) Native.TagRemove(Id, tag);
        }

        public bool HasTag(string tag) => !string.IsNullOrEmpty(tag) && Native.TagHas(Id, tag);

        // The instance's tags, sorted.
        public string[] GetTags()
        {
            int n = Native.TagCount(Id);
            var list = new string[Math.Max(0, n)];
            for (int i = 0; i < list.Length; i++) list[i] = Native.TagAt(Id, i) ?? "";
            return list;
        }

        // Converts a supported managed value to the flat form. Strings are returned with their UTF-8 bytes in `text`.
        private static TefraValue AttributeValue(object value, out byte[] text)
        {
            text = null;
            switch (value)
            {
                case bool b: return Native.NumValue(PropTypes.Bool, b ? 1 : 0);
                case int i: return Native.NumValue(PropTypes.Int, i);
                case long l: return Native.NumValue(PropTypes.Int, l);
                case short sh: return Native.NumValue(PropTypes.Int, sh);
                case byte by: return Native.NumValue(PropTypes.Int, by);
                case float f: return Native.NumValue(PropTypes.Float, f);
                case double d: return Native.NumValue(PropTypes.Float, d);
                case string s:
                {
                    text = Encoding.UTF8.GetBytes(s);
                    TefraValue sv = default;
                    sv.type = PropTypes.String;
                    sv.strLen = (uint)text.Length;
                    return sv;
                }
                case Vector3 p: return Native.NumValue(PropTypes.Vector3, p.X, p.Y, p.Z);
                case Color3 c: return Native.NumValue(PropTypes.Color3, c.R, c.G, c.B);
                default:
                    throw new ArgumentException("Tefra: attribute values must be bool, number, string, Vector3 or Color3, got " +
                                                value.GetType().Name);
            }
        }

        // ---- Wrapper cache ---------------------------------------------------------------------------------------

        // Returns the typed wrapper for an id (Part, Script, ...), or null for 0 and for ids that no longer exist.
        internal static Instance FromId(ulong id)
        {
            if (id == 0) return null;
            Instance cached;
            if (Cache.TryGetValue(id, out cached)) return cached;
            if (!Native.Exists(id)) return null;
            Func<ulong, Instance> make;
            Instance created = ClassMap.Map.TryGetValue(Native.ClassName(id), out make) ? make(id) : new Instance(id);
            Cache[id] = created;
            return created;
        }

        internal static void Forget(Instance inst)
        {
            Cache.Remove(inst.Id);
        }

        // Called by the engine loop: notifications queued by the DataModel since the last step (changes, structure,
        // gameplay signals). Structure changes only mark the script set dirty; the next step re-scans it.
        internal static unsafe void DispatchEvents(TefraEvent* events, uint count)
        {
            for (uint i = 0; i < count; i++)
            {
                TefraEvent e = events[i];
                if (e.kind == EventKind.Added || e.kind == EventKind.Reparented)
                {
                    ScriptEngine.MarkDirty();
                    continue;
                }
                if (e.kind == EventKind.Signal)
                {
                    DispatchSignal(e);
                    continue;
                }
                Instance inst;
                if (e.kind == EventKind.Removing)
                {
                    // Also for instances already forgotten by Destroy() (called from managed code): the scripts must
                    // be re-scanned, or a destroyed script keeps running until the next structural change.
                    ScriptEngine.MarkDirty();
                    if (!Cache.TryGetValue(e.id, out inst)) continue;
                    Forget(inst);
                    inst.RaiseDestroying();
                    continue;
                }
                if (!Cache.TryGetValue(e.id, out inst)) continue;
                if (e.kind == EventKind.Changed)
                {
                    string name = Native.PropName(e.id, e.prop);
                    if (name == null) continue;
                    if (inst is BaseScript && (name == "Source" || name == "Enabled")) ScriptEngine.MarkDirty();
                    inst.RaiseChanged(name);
                }
            }
        }

        // Routes a gameplay signal. Input signals come from the DataModel root and go to UserInputService; everything
        // else goes to the wrapper of its source, when a script has one (instances nobody listens to have none).
        private static unsafe void DispatchSignal(TefraEvent e)
        {
            string name = e.name == null ? "" : Native.FromUtf8((byte*)e.name) ?? "";
            string text = e.text == null ? "" : Native.FromUtf8((byte*)e.text) ?? "";
            if (name == "InputBegan" || name == "InputEnded")
            {
                UserInputService.Dispatch(name == "InputBegan", text);
                return;
            }
            if (name == "TagAdded" || name == "TagRemoved")
            {
                // Per-tag signals reach scripts that never touched the instance, so they bypass the wrapper cache.
                CollectionService.Dispatch(name == "TagAdded", e.id, text);
                return;
            }
            Instance source;
            if (!Cache.TryGetValue(e.id, out source)) return;
            Instance other = e.other == 0 ? null : FromId(e.other);
            source.RaiseSignal(name, other, e.number, text);
        }

        // ---- Gameplay signals (Touched, Activated, Died, ...) -----------------------------------------------------

        private Dictionary<string, List<Handler>> _signals;

        // Subscribes a handler to a signal. The wrapper keeps its handlers until Stop, so the handlers stay alive.
        internal void AddSignal(string name, Delegate handler)
        {
            if (handler == null) return;
            if (_signals == null) _signals = new Dictionary<string, List<Handler>>();
            List<Handler> list;
            if (!_signals.TryGetValue(name, out list)) _signals[name] = list = new List<Handler>();
            Handlers.Add(list, handler);
        }

        internal void RemoveSignal(string name, Delegate handler)
        {
            List<Handler> list;
            if (_signals == null || handler == null || !_signals.TryGetValue(name, out list)) return;
            Handlers.Remove(list, handler);
        }

        // Calls the handlers of a signal with the argument its signature takes (see the event declarations).
        // Handlers of scripts that were destroyed are skipped (see Handlers.Live).
        internal void RaiseSignal(string name, Instance other, double number, string text)
        {
            if (_signals == null) return;
            List<Handler> list;
            if (!_signals.TryGetValue(name, out list) || list.Count == 0) return;
            foreach (Handler h in Handlers.Live(list))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                Delegate d = h.Fn;
                try
                {
                    switch (name)
                    {
                        case "HealthChanged":
                            ((Action<float>)d)((float)number);
                            break;
                        case "StateChanged":
                        {
                            HumanoidState next;
                            if (!Enum.TryParse(text, out next)) next = HumanoidState.Idle;
                            ((Action<HumanoidState, HumanoidState>)d)((HumanoidState)(int)number, next);
                            break;
                        }
                        case "Touched":
                        case "TouchEnded":
                        case "CharacterAdded":
                        case "CharacterRemoving":
                            ((Action<Instance>)d)(other);
                            break;
                        case "InputBegan":
                        case "InputEnded":
                            ((Action<InputObject>)d)(new InputObject(text));
                            break;
                        case "AttributeChanged":
                            ((Action<string>)d)(text);
                            break;
                        default:
                            ((Action)d)();
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Native.Log(Native.LogError, FullName + "." + name + " handler failed: " + ex);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        // On Stop: drop every handler so user delegates do not keep the script assembly alive.
        internal static void ClearAll()
        {
            foreach (Instance inst in Cache.Values)
            {
                inst._changed = null;
                inst._destroying = null;
                inst._signals = null;
                inst._attributeSignals = null;
            }
            Cache.Clear();
            CollectionService.ClearAll();
        }

        private void RaiseChanged(string name)
        {
            if (_changed == null || _changed.Count == 0) return;
            foreach (Handler h in Handlers.Live(_changed))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action<string>)h.Fn)(name);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, FullName + ".Changed handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        private void RaiseDestroying()
        {
            if (_destroying == null || _destroying.Count == 0) return;
            foreach (Handler h in Handlers.Live(_destroying))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action)h.Fn)();
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "Destroying handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }

        // ---- Typed property helpers (used by the generated-by-hand classes in Classes.cs) -----------------------

        internal float GetFloat(PropRef p) => (float)Native.Num(Id, p.Index);
        internal int GetInt(PropRef p) => (int)Native.Num(Id, p.Index);
        internal bool GetBool(PropRef p) => Native.Num(Id, p.Index) != 0;
        internal string GetString(PropRef p) => Native.Str(Id, p.Index);
        internal Instance GetRef(PropRef p) => FromId(Native.Ref(Id, p.Index));

        internal Vector3 GetVector3(PropRef p)
        {
            int i = p.Index;
            return new Vector3((float)Native.Num(Id, i, 0), (float)Native.Num(Id, i, 1), (float)Native.Num(Id, i, 2));
        }

        internal Color3 GetColor3(PropRef p)
        {
            int i = p.Index;
            return new Color3((float)Native.Num(Id, i, 0), (float)Native.Num(Id, i, 1), (float)Native.Num(Id, i, 2));
        }

        internal void SetFloat(PropRef p, float value) => Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Float, value)), p);
        internal void SetInt(PropRef p, int value) => Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Int, value)), p);
        internal void SetEnum(PropRef p, int value) => Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Enum, value)), p);
        internal void SetBool(PropRef p, bool value) => Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Bool, value ? 1 : 0)), p);
        internal void SetRef(PropRef p, Instance value) => Check(Native.Set(Id, p.Index, Native.RefValue(value == null ? 0UL : value.Id)), p);

        internal void SetVector3(PropRef p, Vector3 value) =>
            Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Vector3, value.X, value.Y, value.Z)), p);

        internal void SetColor3(PropRef p, Color3 value) =>
            Check(Native.Set(Id, p.Index, Native.NumValue(PropTypes.Color3, value.R, value.G, value.B)), p);

        internal void SetString(PropRef p, string value)
        {
            Check(Native.SetString(Id, p.Index, Encoding.UTF8.GetBytes(value ?? "")), p);
        }

        private static void CollectDescendants(Instance root, List<Instance> output)
        {
            foreach (Instance child in root.GetChildren())
            {
                if (child == null) continue;
                output.Add(child);
                CollectDescendants(child, output);
            }
        }

        private int PropertyIndex(string property)
        {
            string key = ClassName + "." + property;
            int index;
            if (!GenericIndex.TryGetValue(key, out index))
            {
                index = Native.PropIndex(ClassName, property);
                if (index < 0) throw new ArgumentException("Tefra: unknown property '" + property + "' on " + ClassName);
                GenericIndex[key] = index;
            }
            return index;
        }

        private void Check(bool ok, PropRef p)
        {
            if (!ok) throw new InvalidOperationException("Tefra: cannot set " + p.Display + " (read-only, detached or wrong type)");
        }

        private void Check(bool ok, string what)
        {
            if (!ok) throw new InvalidOperationException("Tefra: cannot set " + what + " on " + ClassName + " '" + Name + "' (read-only, detached or wrong type)");
        }

        private static double ToDouble(object value)
        {
            switch (value)
            {
                case int i: return i;
                case long l: return l;
                case float f: return f;
                case double d: return d;
                case short s: return s;
                case byte b: return b;
                default: throw new ArgumentException("Tefra: expected a number, got " + (value == null ? "null" : value.GetType().Name));
            }
        }
    }
}
