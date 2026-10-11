// Mirror of engine/scripting/include/tefra/scripting/abi.h. Keep the layouts identical; ABI version 4.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Tefra
{
    // Instance filter of a spatial query (C: TefraFilter). 16 bytes on LP64: pointer, uint32, int32.
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct TefraFilter
    {
        public ulong* ids;
        public uint count;
        public int include; // 1 = FilterType.Include, 0 = Exclude
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct TefraValue
    {
        public int type;      // PropType: 0 Bool, 1 Int, 2 Float, 3 String, 4 Vector3, 5 Color3, 6 Enum, 7 Ref
        public uint strLen;
        public double n0, n1, n2;
        public byte* str;
        public ulong reference;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct TefraEvent
    {
        public uint kind; // EventKind: 1 Changed, 2 Removing, 3 Added, 4 Reparented, 5 Signal
        public uint prop;
        public ulong id;
        public ulong other;
        public double number;
        public byte* name; // signals only, NUL-terminated UTF-8
        public byte* text;
    }

    internal static class EventKind
    {
        public const uint Changed = 1, Removing = 2, Added = 3, Reparented = 4, Signal = 5;
    }


    internal static class PropTypes
    {
        public const int Bool = 0, Int = 1, Float = 2, String = 3, Vector3 = 4, Color3 = 5, Enum = 6, Ref = 7;
    }

    // Native function table (C: TefraNativeApi).
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct NativeApi
    {
        public uint abi_version;
        public delegate* unmanaged<ulong, uint, TefraValue*, int> get_prop;
        public delegate* unmanaged<ulong, uint, TefraValue*, int> set_prop;
        public delegate* unmanaged<byte*, byte*, int> prop_index;
        public delegate* unmanaged<byte*, byte*, int> prop_type;
        public delegate* unmanaged<ulong, uint, byte*> prop_name;
        public delegate* unmanaged<ulong, byte*> class_name;
        public delegate* unmanaged<ulong, byte*, int> is_a;
        public delegate* unmanaged<byte*, ulong, byte*, ulong> create;
        public delegate* unmanaged<ulong, int> destroy;
        public delegate* unmanaged<ulong, ulong, int> set_parent;
        public delegate* unmanaged<ulong, ulong> parent;
        public delegate* unmanaged<ulong, int> child_count;
        public delegate* unmanaged<ulong, int, ulong> child_at;
        public delegate* unmanaged<ulong, byte*, ulong> find_child;
        public delegate* unmanaged<byte*, ulong> service;
        public delegate* unmanaged<ulong, ulong, ulong> clone;
        public delegate* unmanaged<byte*, ulong> find_path;
        public delegate* unmanaged<int, byte*, void> log;
        public delegate* unmanaged<double*, double*, double, TefraFilter*, double*, ulong> raycast;
        public delegate* unmanaged<int, int> input_key_down;
        public delegate* unmanaged<int, int> input_mouse_down;
        public delegate* unmanaged<double*, int> input_mouse_location;
        public delegate* unmanaged<ulong> root;
        public delegate* unmanaged<ulong, int> attr_count;
        public delegate* unmanaged<ulong, int, byte*> attr_name_at;
        public delegate* unmanaged<ulong, byte*, TefraValue*, int> attr_get;
        public delegate* unmanaged<ulong, byte*, TefraValue*, int> attr_set;
        public delegate* unmanaged<ulong, byte*, int> attr_remove;
        public delegate* unmanaged<ulong, byte*, int> tag_add;
        public delegate* unmanaged<ulong, byte*, int> tag_remove;
        public delegate* unmanaged<ulong, byte*, int> tag_has;
        public delegate* unmanaged<ulong, int> tag_count;
        public delegate* unmanaged<ulong, int, byte*> tag_at;
        public delegate* unmanaged<byte*, int> tagged_count;
        public delegate* unmanaged<byte*, int, ulong> tagged_at;
        public delegate* unmanaged<double*, double, double*, TefraFilter*, double*, ulong> spherecast;
        public delegate* unmanaged<double*, double*, double*, double*, TefraFilter*, double*, ulong> blockcast;
        public delegate* unmanaged<int, double*, ulong, TefraFilter*, uint, ulong*, uint, uint> overlap;
        public delegate* unmanaged<ulong, ulong*, uint, uint> touching_parts;
        public delegate* unmanaged<ulong, double*, int, int> apply_impulse;
        public delegate* unmanaged<ulong, double> mass;
    }

    // Managed function table (C: TefraManagedApi), filled by Bridge.Init.
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ManagedApi
    {
        public uint abi_version;
        public delegate* unmanaged<int> compile;
        public delegate* unmanaged<int> start;
        public delegate* unmanaged<float, int> step;
        public delegate* unmanaged<IntPtr, uint, void> dispatch;
        public delegate* unmanaged<void> stop;
    }

    // Thin, typed access to the native table. All strings are converted to NUL-terminated UTF-8 on the way in and
    // copied out on the way back, so no pointer outlives the call that produced it.
    internal static unsafe class Native
    {
        public const uint AbiVersion = 4;
        public static NativeApi* Api;

        public static bool IsReady => Api != null;

        public const int LogTrace = 0, LogDebug = 1, LogInfo = 2, LogWarn = 3, LogError = 4;

        // NUL-terminated UTF-8 for a `const char*` argument. Use with `fixed`.
        public static byte[] Z(string s)
        {
            if (s == null) s = "";
            byte[] utf8 = Encoding.UTF8.GetBytes(s);
            byte[] z = new byte[utf8.Length + 1];
            Buffer.BlockCopy(utf8, 0, z, 0, utf8.Length);
            return z;
        }

        public static string FromUtf8(byte* p)
        {
            return p == null ? null : Marshal.PtrToStringUTF8((IntPtr)p);
        }

        public static string FromValue(in TefraValue v)
        {
            if (v.str == null || v.strLen == 0) return "";
            return Encoding.UTF8.GetString(v.str, (int)v.strLen);
        }

        public static void Log(int level, string message)
        {
            if (Api == null) return;
            // Messages must not contain NUL: it would truncate the C string on the native side.
            message = message.Replace('\0', ' ');
            fixed (byte* p = Z(message)) Api->log(level, p);
        }

        public static int PropIndex(string cls, string prop)
        {
            fixed (byte* c = Z(cls)) fixed (byte* p = Z(prop)) return Api->prop_index(c, p);
        }

        public static int PropType(string cls, string prop)
        {
            fixed (byte* c = Z(cls)) fixed (byte* p = Z(prop)) return Api->prop_type(c, p);
        }

        public static string PropName(ulong id, uint index)
        {
            return FromUtf8(Api->prop_name(id, index));
        }

        public static string ClassName(ulong id)
        {
            return FromUtf8(Api->class_name(id)) ?? "";
        }

        public static bool Exists(ulong id)
        {
            return id != 0 && Api->class_name(id) != null;
        }

        public static bool IsA(ulong id, string cls)
        {
            fixed (byte* c = Z(cls)) return Api->is_a(id, c) != 0;
        }

        public static bool TryGet(ulong id, int index, out TefraValue value)
        {
            TefraValue v = default;
            int ok = Api->get_prop(id, (uint)index, &v);
            value = v;
            return ok != 0;
        }

        public static bool Set(ulong id, int index, TefraValue value)
        {
            return Api->set_prop(id, (uint)index, &value) != 0;
        }

        public static ulong Create(string cls, ulong parent, string name)
        {
            fixed (byte* c = Z(cls)) fixed (byte* n = Z(name ?? "")) return Api->create(c, parent, n);
        }

        public static bool Destroy(ulong id) => Api->destroy(id) != 0;

        public static bool SetParent(ulong id, ulong parent) => Api->set_parent(id, parent) != 0;

        public static ulong Parent(ulong id) => Api->parent(id);

        public static int ChildCount(ulong id) => Api->child_count(id);

        public static ulong ChildAt(ulong id, int index) => Api->child_at(id, index);

        public static ulong FindChild(ulong id, string name)
        {
            fixed (byte* n = Z(name)) return Api->find_child(id, n);
        }

        public static ulong Service(string name)
        {
            fixed (byte* n = Z(name)) return Api->service(n);
        }

        public static ulong Clone(ulong id, ulong parent) => Api->clone(id, parent);

        public static ulong FindPath(string path)
        {
            fixed (byte* p = Z(path)) return Api->find_path(p);
        }

        public static ulong Root() => Api->root();

        // Filter ids of a query (null = no filter) and its FilterType as the C flag. The ids stay pinned by the caller.
        internal static ulong[] FilterIds(QueryParams p, out int include)
        {
            include = p != null && p.FilterType == FilterType.Include ? 1 : 0;
            return p == null ? new ulong[0] : p.FilterIds();
        }

        // Returns the hit instance id (0 = miss) and fills position, normal and distance.
        public static ulong Raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxDistance,
            QueryParams p, out double[] hit)
        {
            hit = new double[7];
            double* o = stackalloc double[3];
            double* d = stackalloc double[3];
            o[0] = ox; o[1] = oy; o[2] = oz;
            d[0] = dx; d[1] = dy; d[2] = dz;
            int include;
            ulong[] ids = FilterIds(p, out include);
            fixed (ulong* idp = ids)
            fixed (double* h = hit)
            {
                TefraFilter f = new TefraFilter { ids = idp, count = (uint)ids.Length, include = include };
                return Api->raycast(o, d, maxDistance, &f, h);
            }
        }

        // ---- Attributes and tags (ABI 3). Strings returned by the native side are copied out at once. ----------------

        public static int AttrCount(ulong id) => Api->attr_count(id);

        public static string AttrNameAt(ulong id, int index) => FromUtf8(Api->attr_name_at(id, index));

        public static bool AttrGet(ulong id, string name, out TefraValue value)
        {
            TefraValue v = default;
            int ok;
            fixed (byte* n = Z(name)) ok = Api->attr_get(id, n, &v);
            value = v;
            return ok != 0;
        }

        // The value's string payload (if any) is `text`; both the name and the text are pinned for the call.
        public static bool AttrSet(ulong id, string name, TefraValue value, byte[] text)
        {
            text ??= new byte[0];
            fixed (byte* n = Z(name))
            fixed (byte* s = text)
            {
                value.str = s;
                value.strLen = (uint)text.Length;
                return Api->attr_set(id, n, &value) != 0;
            }
        }

        public static bool AttrRemove(ulong id, string name)
        {
            fixed (byte* n = Z(name)) return Api->attr_remove(id, n) != 0;
        }

        public static bool TagAdd(ulong id, string tag)
        {
            fixed (byte* t = Z(tag)) return Api->tag_add(id, t) != 0;
        }

        public static bool TagRemove(ulong id, string tag)
        {
            fixed (byte* t = Z(tag)) return Api->tag_remove(id, t) != 0;
        }

        public static bool TagHas(ulong id, string tag)
        {
            fixed (byte* t = Z(tag)) return Api->tag_has(id, t) != 0;
        }

        public static int TagCount(ulong id) => Api->tag_count(id);

        public static string TagAt(ulong id, int index) => FromUtf8(Api->tag_at(id, index));

        public static int TaggedCount(string tag)
        {
            fixed (byte* t = Z(tag)) return Api->tagged_count(t);
        }

        public static ulong TaggedAt(string tag, int index)
        {
            fixed (byte* t = Z(tag)) return Api->tagged_at(t, index);
        }
        public static ulong Spherecast(Vector3 origin, float radius, Vector3 direction, QueryParams p, out double[] hit)
        {
            hit = new double[7];
            double* o = stackalloc double[3];
            double* d = stackalloc double[3];
            o[0] = origin.X; o[1] = origin.Y; o[2] = origin.Z;
            d[0] = direction.X; d[1] = direction.Y; d[2] = direction.Z;
            int include;
            ulong[] ids = FilterIds(p, out include);
            fixed (ulong* idp = ids)
            fixed (double* h = hit)
            {
                TefraFilter f = new TefraFilter { ids = idp, count = (uint)ids.Length, include = include };
                return Api->spherecast(o, radius, d, &f, h);
            }
        }

        public static ulong Blockcast(Vector3 center, Vector3 orientation, Vector3 size, Vector3 direction, QueryParams p,
            out double[] hit)
        {
            hit = new double[7];
            double* c = stackalloc double[3];
            double* r = stackalloc double[3];
            double* s = stackalloc double[3];
            double* d = stackalloc double[3];
            c[0] = center.X; c[1] = center.Y; c[2] = center.Z;
            r[0] = orientation.X; r[1] = orientation.Y; r[2] = orientation.Z;
            s[0] = size.X; s[1] = size.Y; s[2] = size.Z;
            d[0] = direction.X; d[1] = direction.Y; d[2] = direction.Z;
            int include;
            ulong[] ids = FilterIds(p, out include);
            fixed (ulong* idp = ids)
            fixed (double* h = hit)
            {
                TefraFilter f = new TefraFilter { ids = idp, count = (uint)ids.Length, include = include };
                return Api->blockcast(c, r, s, d, &f, h);
            }
        }

        // kind: 0 Radius, 1 Box, 2 Part. Fills outIds with up to outIds.Length matches and returns how many matched.
        public static uint Overlap(int kind, Vector3 center, Vector3 orientation, Vector3 size, float radius, ulong partId,
            QueryParams p, int maxParts, ulong[] outIds)
        {
            double[] geom =
            {
                center.X, center.Y, center.Z, orientation.X, orientation.Y, orientation.Z, size.X, size.Y, size.Z, radius
            };
            int include;
            ulong[] ids = FilterIds(p, out include);
            fixed (double* g = geom)
            fixed (ulong* idp = ids)
            fixed (ulong* outp = outIds)
            {
                TefraFilter f = new TefraFilter { ids = idp, count = (uint)ids.Length, include = include };
                return Api->overlap(kind, g, partId, &f, (uint)Math.Max(maxParts, 0), outp, (uint)outIds.Length);
            }
        }

        public static uint TouchingParts(ulong partId, ulong[] outIds)
        {
            fixed (ulong* outp = outIds) return Api->touching_parts(partId, outp, (uint)outIds.Length);
        }

        public static bool ApplyImpulse(ulong partId, Vector3 value, bool angular)
        {
            double* v = stackalloc double[3];
            v[0] = value.X; v[1] = value.Y; v[2] = value.Z;
            return Api->apply_impulse(partId, v, angular ? 1 : 0) != 0;
        }

        public static double Mass(ulong partId) => Api->mass(partId);

        public static bool KeyDown(int keyCode) => Api->input_key_down(keyCode) != 0;

        public static bool MouseDown(int button) => Api->input_mouse_down(button) != 0;

        public static void MouseLocation(out double x, out double y)
        {
            double* xy = stackalloc double[2];
            if (Api->input_mouse_location(xy) == 0) { x = 0; y = 0; return; }
            x = xy[0];
            y = xy[1];
        }

        // Typed helpers over TefraValue. Each returns the default value when the instance or property is missing.
        public static double Num(ulong id, int index, int component = 0)
        {
            TefraValue v;
            if (!TryGet(id, index, out v)) return 0;
            switch (component)
            {
                case 0: return v.n0;
                case 1: return v.n1;
                default: return v.n2;
            }
        }

        public static string Str(ulong id, int index)
        {
            TefraValue v;
            return TryGet(id, index, out v) ? FromValue(v) : "";
        }

        public static ulong Ref(ulong id, int index)
        {
            TefraValue v;
            return TryGet(id, index, out v) ? v.reference : 0UL;
        }

        public static TefraValue NumValue(int type, double a, double b = 0, double c = 0)
        {
            TefraValue v = default;
            v.type = type;
            v.n0 = a;
            v.n1 = b;
            v.n2 = c;
            return v;
        }

        public static TefraValue RefValue(ulong id)
        {
            TefraValue v = default;
            v.type = PropTypes.Ref;
            v.reference = id;
            return v;
        }

        // String values point into a byte array that stays pinned only during the call, so the caller must keep
        // `utf8` alive across Set().
        public static bool SetString(ulong id, int index, byte[] utf8)
        {
            fixed (byte* p = utf8)
            {
                TefraValue v = default;
                v.type = PropTypes.String;
                v.str = p;
                v.strLen = (uint)utf8.Length;
                return Set(id, index, v);
            }
        }
    }
}
