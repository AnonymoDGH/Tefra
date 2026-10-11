// Local persistence (DataStoreService) and JSON helpers (HttpService). Nothing here uses the network.
//
// Storage: one JSON file per store. A store is read from its file on every call and rewritten on every change
// (through a temporary file, then moved over the old one), so nothing is cached between calls and a write is on disk
// when the call returns. The folder is TEFRA_DATASTORE_DIR when that variable is set (tests use it), otherwise
// <ApplicationData>/Tefra/datastores. Ordered stores live in "ordered-<name>.json".
//
// Limitation: a store is identified by its name only. The engine does not pass the place's identity to managed code
// yet, so two games that use the same store name on one machine share the file.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace Tefra
{
    /// <summary>
    /// Values a store or HttpService.JSONEncode accepts: null, booleans, numbers, strings, and lists and dictionaries
    /// (string keys) of those. Numbers read back as double.
    /// </summary>
    internal static class DataValues
    {
        public static void Check(object value, string what)
        {
            switch (value)
            {
                case null:
                case bool _:
                case string _:
                case int _:
                case long _:
                case short _:
                case byte _:
                case sbyte _:
                case uint _:
                case ulong _:
                case ushort _:
                    return;
                case double d:
                    if (double.IsNaN(d) || double.IsInfinity(d)) throw new ArgumentException(what + ": NaN and infinity cannot be stored");
                    return;
                case float f:
                    if (float.IsNaN(f) || float.IsInfinity(f)) throw new ArgumentException(what + ": NaN and infinity cannot be stored");
                    return;
                case IDictionary<string, object> dict:
                    foreach (KeyValuePair<string, object> kv in dict) Check(kv.Value, what);
                    return;
                case IEnumerable list:
                    foreach (object item in list) Check(item, what);
                    return;
            }
            throw new ArgumentException(what + ": a value of type " + value.GetType().Name + " cannot be stored");
        }

        // The number held by a value, when it is one.
        public static bool Number(object value, out double number)
        {
            number = 0;
            if (value is double d) { number = d; return true; }
            if (value is int || value is long || value is float || value is short || value is byte || value is sbyte ||
                value is uint || value is ulong || value is ushort)
            {
                number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
                return true;
            }
            return false;
        }

        // Turns parsed JSON into the values scripts see: Dictionary<string, object>, List<object>, double, string, bool.
        public static object FromJson(JsonElement e)
        {
            switch (e.ValueKind)
            {
                case JsonValueKind.Object:
                {
                    var dict = new Dictionary<string, object>();
                    foreach (JsonProperty p in e.EnumerateObject()) dict[p.Name] = FromJson(p.Value);
                    return dict;
                }
                case JsonValueKind.Array:
                {
                    var list = new List<object>();
                    foreach (JsonElement item in e.EnumerateArray()) list.Add(FromJson(item));
                    return list;
                }
                case JsonValueKind.String: return e.GetString();
                case JsonValueKind.Number: return e.GetDouble();
                case JsonValueKind.True: return true;
                case JsonValueKind.False: return false;
                default: return null;
            }
        }
    }

    /// <summary>Global key-value storage that survives Play sessions (Roblox DataStoreService). Calls are synchronous.</summary>
    public static class DataStoreService
    {
        /// <summary>Environment variable that replaces the folder of the stores (used by the tests).</summary>
        public const string FolderVariable = "TEFRA_DATASTORE_DIR";

        /// <summary>The store with this name (created on first write).</summary>
        public static DataStore GetDataStore(string name) => new DataStore(StorePath(RequireName(name), ""));

        /// <summary>A store of numbers that can be read in order (leaderboards).</summary>
        public static OrderedDataStore GetOrderedDataStore(string name) => new OrderedDataStore(StorePath(RequireName(name), "ordered-"));

        internal static string Folder
        {
            get
            {
                string dir = Environment.GetEnvironmentVariable(FolderVariable);
                if (!string.IsNullOrEmpty(dir)) return dir;
                string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
                if (string.IsNullOrEmpty(appData)) appData = AppContext.BaseDirectory;
                return Path.Combine(appData, "Tefra", "datastores");
            }
        }

        private static string StorePath(string name, string prefix)
        {
            var safe = new StringBuilder(name.Length);
            char[] invalid = Path.GetInvalidFileNameChars();
            foreach (char c in name) safe.Append(Array.IndexOf(invalid, c) >= 0 || c == '/' || c == '\\' ? '_' : c);
            return Path.Combine(Folder, prefix + safe.ToString() + ".json");
        }

        private static string RequireName(string name)
        {
            if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("DataStore name must not be empty");
            return name;
        }

        internal static string RequireKey(string key)
        {
            if (string.IsNullOrEmpty(key)) throw new ArgumentException("DataStore key must not be empty");
            return key;
        }

        // ---- File access (used by DataStore and OrderedDataStore) ------------------------------------------------

        private static readonly JsonSerializerOptions WriteOptions = new JsonSerializerOptions
        {
            WriteIndented = true,
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping, // accents stay readable in the file
        };

        internal static Dictionary<string, object> Read(string path)
        {
            var data = new Dictionary<string, object>();
            if (!File.Exists(path)) return data;
            string text = File.ReadAllText(path, Encoding.UTF8);
            if (text.Trim().Length == 0) return data;
            try
            {
                using (JsonDocument doc = JsonDocument.Parse(text))
                {
                    if (doc.RootElement.ValueKind != JsonValueKind.Object)
                        throw new InvalidOperationException("DataStore file is not a JSON object (left unchanged): " + path);
                    foreach (JsonProperty p in doc.RootElement.EnumerateObject())
                        data[p.Name] = DataValues.FromJson(p.Value);
                }
            }
            catch (JsonException e)
            {
                // Never overwrite a file that cannot be read: the data would be lost.
                throw new InvalidOperationException("DataStore file is corrupt (left unchanged): " + path + ": " + e.Message, e);
            }
            return data;
        }

        internal static void Write(string path, Dictionary<string, object> data)
        {
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            string tmp = path + ".tmp";
            File.WriteAllText(tmp, JsonSerializer.Serialize(data, WriteOptions), new UTF8Encoding(false));
            File.Move(tmp, path, true);
        }
    }

    /// <summary>One named store of values, keyed by string.</summary>
    public sealed class DataStore
    {
        private readonly string _path;

        internal DataStore(string path)
        {
            _path = path;
        }

        /// <summary>The value stored under key, or null.</summary>
        public object GetAsync(string key)
        {
            object value;
            return DataStoreService.Read(_path).TryGetValue(DataStoreService.RequireKey(key), out value) ? value : null;
        }

        /// <summary>Stores value under key (null removes the key). Written to disk before the call returns.</summary>
        public void SetAsync(string key, object value)
        {
            DataStoreService.RequireKey(key);
            DataValues.Check(value, "DataStore.SetAsync('" + key + "')");
            var data = DataStoreService.Read(_path);
            if (value == null) data.Remove(key);
            else data[key] = value;
            DataStoreService.Write(_path, data);
        }

        /// <summary>
        /// Passes the current value (null when absent) to transform and stores the result. A transform that returns
        /// null cancels the update. Returns the value after the call.
        /// </summary>
        public object UpdateAsync(string key, Func<object, object> transform)
        {
            DataStoreService.RequireKey(key);
            if (transform == null) throw new ArgumentNullException(nameof(transform));
            var data = DataStoreService.Read(_path);
            object current;
            data.TryGetValue(key, out current);
            object next = transform(current);
            if (next == null) return current;
            DataValues.Check(next, "DataStore.UpdateAsync('" + key + "')");
            data[key] = next;
            DataStoreService.Write(_path, data);
            return next;
        }

        /// <summary>Removes key and returns the value it had (null when absent).</summary>
        public object RemoveAsync(string key)
        {
            DataStoreService.RequireKey(key);
            var data = DataStoreService.Read(_path);
            object old;
            if (!data.TryGetValue(key, out old)) return null;
            data.Remove(key);
            DataStoreService.Write(_path, data);
            return old;
        }

        /// <summary>Adds delta to the number under key (0 when absent) and returns the new value.</summary>
        public double IncrementAsync(string key, double delta = 1)
        {
            DataStoreService.RequireKey(key);
            var data = DataStoreService.Read(_path);
            object current;
            double value = 0;
            if (data.TryGetValue(key, out current) && current != null && !DataValues.Number(current, out value))
                throw new InvalidOperationException("DataStore.IncrementAsync('" + key + "'): the stored value is not a number");
            value += delta;
            data[key] = value;
            DataStoreService.Write(_path, data);
            return value;
        }

        // Every key and value of the store, as of now (for OrderedDataStore).
        internal Dictionary<string, object> Snapshot() => DataStoreService.Read(_path);
    }

    /// <summary>A store of numbers that can be listed in order of value (Roblox OrderedDataStore).</summary>
    public sealed class OrderedDataStore
    {
        private readonly DataStore _store;

        internal OrderedDataStore(string path)
        {
            _store = new DataStore(path);
        }

        /// <summary>The number under key, or null.</summary>
        public double? GetAsync(string key)
        {
            object value = _store.GetAsync(key);
            double number;
            return value != null && DataValues.Number(value, out number) ? number : (double?)null;
        }

        public void SetAsync(string key, double value) => _store.SetAsync(key, value);

        public double IncrementAsync(string key, double delta = 1) => _store.IncrementAsync(key, delta);

        public void RemoveAsync(string key) => _store.RemoveAsync(key);

        /// <summary>
        /// Up to pageSize entries in order of value (ascending or descending). Entries that are not numbers are left out.
        /// </summary>
        public List<(string Key, double Value)> GetSortedAsync(bool ascending, int pageSize)
        {
            if (pageSize < 1) throw new ArgumentOutOfRangeException(nameof(pageSize));
            var entries = new List<(string Key, double Value)>();
            foreach (KeyValuePair<string, object> kv in _store.Snapshot())
            {
                double number;
                if (DataValues.Number(kv.Value, out number)) entries.Add((kv.Key, number));
            }
            entries.Sort((a, b) => ascending ? a.Value.CompareTo(b.Value) : b.Value.CompareTo(a.Value));
            if (entries.Count > pageSize) entries.RemoveRange(pageSize, entries.Count - pageSize);
            return entries;
        }
    }

    /// <summary>JSON encoding and decoding (Roblox HttpService, without the network).</summary>
    public static class HttpService
    {
        private static readonly JsonSerializerOptions CompactOptions = new JsonSerializerOptions
        {
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };

        /// <summary>Encodes numbers, strings, booleans, lists and dictionaries as JSON text.</summary>
        public static string JSONEncode(object value)
        {
            DataValues.Check(value, "HttpService.JSONEncode");
            return JsonSerializer.Serialize(value, CompactOptions);
        }

        /// <summary>Decodes JSON text. Objects become Dictionary&lt;string, object&gt;, arrays List&lt;object&gt;, numbers double.</summary>
        public static object JSONDecode(string json)
        {
            if (json == null) throw new ArgumentNullException(nameof(json));
            try
            {
                using (JsonDocument doc = JsonDocument.Parse(json))
                    return DataValues.FromJson(doc.RootElement);
            }
            catch (JsonException e)
            {
                throw new ArgumentException("HttpService.JSONDecode: " + e.Message, e);
            }
        }

        /// <summary>A new random identifier, in braces when wrap is true ("{XXXXXXXX-...}").</summary>
        public static string GenerateGUID(bool wrap = true)
        {
            string guid = Guid.NewGuid().ToString("D").ToUpperInvariant();
            return wrap ? "{" + guid + "}" : guid;
        }
    }
}
