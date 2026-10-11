// TweenService: Roblox-style property tweens, in managed code. A tween interpolates reflected properties of one
// instance (Position, Transparency, Color, ...) over time. The engine loop steps the active tweens once per step
// (ScriptEngine.Step); Stop drops them all (ScriptEngine.Teardown). Properties are read and written through the
// generic indexer of Instance, so every reflected property with a supported value type can be tweened.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace Tefra
{
    /// <summary>The curve of a tween. Matches the standard Penner curves used by Roblox.</summary>
    public enum EasingStyle
    {
        Linear, Sine, Quad, Cubic, Quart, Quint, Exponential, Circular, Back, Elastic, Bounce
    }

    /// <summary>Which end of the curve the easing applies to.</summary>
    public enum EasingDirection
    {
        In, Out, InOut
    }

    /// <summary>
    /// How a tween runs. Defaults as in Roblox: 1 s, Quad, Out, no repeat, no reverse, no delay.
    /// RepeatCount: extra cycles after the first (-1 repeats forever). Reverses: each cycle goes to the goal and back,
    /// so a cycle takes twice Time and ends at the start values.
    /// </summary>
    public class TweenInfo
    {
        public float Time { get; }
        public EasingStyle EasingStyle { get; }
        public EasingDirection EasingDirection { get; }
        public int RepeatCount { get; }
        public bool Reverses { get; }
        public float DelayTime { get; }

        public TweenInfo(float time = 1f, EasingStyle easingStyle = EasingStyle.Quad,
            EasingDirection easingDirection = EasingDirection.Out, int repeatCount = 0, bool reverses = false,
            float delayTime = 0f)
        {
            Time = time;
            EasingStyle = easingStyle;
            EasingDirection = easingDirection;
            RepeatCount = repeatCount;
            Reverses = reverses;
            DelayTime = delayTime;
        }
    }

    /// <summary>Where a tween is in its life. Completed and Cancelled are final until Play starts it again.</summary>
    public enum PlaybackState
    {
        Begin, Playing, Paused, Completed, Cancelled
    }

    // The shape of a tweened value: how many components it has and which type to rebuild.
    internal enum ValueKind
    {
        Number, Integer, Vector2, Vector3, Color3
    }

    /// <summary>
    /// One running tween, created by TweenService.Create. Play starts it (reading the start values then), Pause holds
    /// it, Play again resumes it, Cancel stops it where it is. Completed fires with the final state (Completed or
    /// Cancelled) from the engine loop. A tween whose instance is destroyed stops silently: no event, no error.
    /// </summary>
    public sealed class Tween
    {
        private readonly Instance _target;
        private readonly TweenInfo _info;
        private readonly List<KeyValuePair<string, object>> _goals;
        private readonly List<string> _names = new List<string>();
        private readonly List<ValueKind> _kinds = new List<ValueKind>();
        private readonly List<double[]> _start = new List<double[]>();
        private readonly List<double[]> _goal = new List<double[]>();
        private PlaybackState _state = PlaybackState.Begin;
        private float _elapsed;
        private List<Handler> _completed;

        internal Tween(Instance target, TweenInfo info, List<KeyValuePair<string, object>> goals)
        {
            _target = target;
            _info = info;
            _goals = goals;
        }

        /// <summary>The instance whose properties this tween sets.</summary>
        public Instance Instance => _target;

        public TweenInfo TweenInfo => _info;

        public PlaybackState PlaybackState => _state;

        /// <summary>Fires when the tween finishes (PlaybackState.Completed) or is cancelled (PlaybackState.Cancelled).</summary>
        public event Action<PlaybackState> Completed
        {
            add => Handlers.Add(_completed ??= new List<Handler>(), value);
            remove => Handlers.Remove(_completed ??= new List<Handler>(), value);
        }

        /// <summary>Starts the tween, or resumes it after Pause. Restarts it when it had completed or was cancelled.</summary>
        public void Play()
        {
            if (_state == PlaybackState.Playing) return;
            if (_state == PlaybackState.Paused)
            {
                _state = PlaybackState.Playing;
                TweenService.Register(this);
                return;
            }
            if (!Native.Exists(_target.Id))
            {
                _state = PlaybackState.Cancelled;
                return;
            }
            ReadStartValues();
            _elapsed = 0f;
            _state = PlaybackState.Playing;
            TweenService.Register(this);
        }

        /// <summary>Holds the tween where it is. Play resumes it.</summary>
        public void Pause()
        {
            if (_state == PlaybackState.Playing) _state = PlaybackState.Paused;
        }

        /// <summary>Stops the tween where it is (the properties keep their current values) and fires Completed.</summary>
        public void Cancel()
        {
            if (_state != PlaybackState.Playing && _state != PlaybackState.Paused) return;
            _state = PlaybackState.Cancelled;
            Raise(PlaybackState.Cancelled);
        }

        // Advances the tween by dt seconds. Called by TweenService.Step.
        internal void Tick(float dt)
        {
            if (_state != PlaybackState.Playing) return;
            if (!Native.Exists(_target.Id))
            {
                _state = PlaybackState.Cancelled; // destroyed: silently
                return;
            }
            _elapsed += dt;
            float t = _elapsed - _info.DelayTime;
            if (t < 0f) return;

            float time = _info.Time;
            float cycle = _info.Reverses ? 2f * time : time;
            bool finished;
            double alpha;
            if (cycle <= 0f)
            {
                finished = true;
                alpha = 1.0;
            }
            else if (_info.RepeatCount >= 0 && t >= (_info.RepeatCount + 1) * cycle)
            {
                finished = true;
                alpha = _info.Reverses ? 0.0 : 1.0; // a reversed tween ends back at its start values
            }
            else
            {
                finished = false;
                float within = t - (float)Math.Floor(t / cycle) * cycle;
                double p = within < time ? within / time : 1.0 - (within - time) / time;
                alpha = TweenService.Ease(p, _info.EasingStyle, _info.EasingDirection);
            }

            try
            {
                for (int i = 0; i < _names.Count; i++)
                    _target[_names[i]] = TweenService.Build(_kinds[i], Lerp(_start[i], _goal[i], alpha));
            }
            catch (Exception e)
            {
                _state = PlaybackState.Cancelled;
                Native.Log(Native.LogError, "Tween on " + _target.FullName + " failed: " + e.Message);
                return;
            }

            if (!finished) return;
            _state = PlaybackState.Completed;
            Raise(PlaybackState.Completed);
        }

        // Stops the tween at Clear (Stop): no event, and no handler kept alive.
        internal void Abandon()
        {
            if (_state == PlaybackState.Playing || _state == PlaybackState.Paused) _state = PlaybackState.Cancelled;
            _completed = null;
        }

        // Reads the start values from the instance and converts the goals to the same shape.
        private void ReadStartValues()
        {
            _names.Clear();
            _kinds.Clear();
            _start.Clear();
            _goal.Clear();
            foreach (KeyValuePair<string, object> goal in _goals)
            {
                object current = _target[goal.Key];
                ValueKind kind;
                double[] startComponents = TweenService.Components(current, out kind);
                if (startComponents == null)
                    throw new ArgumentException("TweenService: cannot tween '" + goal.Key + "' on " +
                                                _target.ClassName + " (type " + (current == null ? "null" : current.GetType().Name) + ")");
                double[] goalComponents = TweenService.Components(goal.Value, out _);
                if (goalComponents == null)
                    throw new ArgumentException("TweenService: unsupported goal value for '" + goal.Key + "'");
                if (current is Vector3 && goal.Value is Vector2)
                    goalComponents = new[] { goalComponents[0], goalComponents[1], 0.0 }; // as GuiObject.FromV2
                if (goalComponents.Length != startComponents.Length)
                    throw new ArgumentException("TweenService: goal for '" + goal.Key + "' does not match its type " +
                                                (current == null ? "null" : current.GetType().Name));
                _names.Add(goal.Key);
                _kinds.Add(kind);
                _start.Add(startComponents);
                _goal.Add(goalComponents);
            }
        }

        private static double[] Lerp(double[] a, double[] b, double alpha)
        {
            var r = new double[a.Length];
            for (int i = 0; i < a.Length; i++) r[i] = a[i] * (1.0 - alpha) + b[i] * alpha;
            return r;
        }

        private void Raise(PlaybackState final)
        {
            if (_completed == null || _completed.Count == 0) return;
            foreach (Handler h in Handlers.Live(_completed))
            {
                ScriptSlot previous = ScriptEngine.Enter(h.Owner);
                try
                {
                    ((Action<PlaybackState>)h.Fn)(final);
                }
                catch (Exception e)
                {
                    Native.Log(Native.LogError, "Tween.Completed handler failed: " + e);
                }
                finally
                {
                    ScriptEngine.Current = previous;
                }
            }
        }
    }

    /// <summary>
    /// Creates tweens and steps the active ones. Goals are an anonymous object (`new { Position = v, Transparency = 1f }`)
    /// or a Dictionary&lt;string, object&gt;, mapping property names to target values. Tweenable values: float, int,
    /// Vector2, Vector3, Color3.
    /// </summary>
    public static class TweenService
    {
        private static readonly List<Tween> Active = new List<Tween>();

        /// <summary>Makes a tween of `target` towards `goals`. Nothing moves until Play().</summary>
        public static Tween Create(Instance target, TweenInfo info, object goals)
        {
            if (target == null) throw new ArgumentNullException(nameof(target));
            if (info == null) throw new ArgumentNullException(nameof(info));
            if (goals == null) throw new ArgumentNullException(nameof(goals));
            return new Tween(target, info, ReadGoals(goals));
        }

        /// <summary>The eased value of a curve at alpha in [0, 1] (alpha is clamped).</summary>
        public static float GetValue(float alpha, EasingStyle style, EasingDirection direction)
        {
            return (float)Ease(alpha, style, direction);
        }

        internal static void Register(Tween tween)
        {
            if (!Active.Contains(tween)) Active.Add(tween);
        }

        // Called once per engine step, after Heartbeat. Tweens started by a completed handler run from the next step.
        internal static void Step(float dt)
        {
            if (Active.Count == 0) return;
            foreach (Tween t in Active.ToArray())
            {
                t.Tick(dt);
                if (t.PlaybackState != PlaybackState.Playing && t.PlaybackState != PlaybackState.Paused)
                    Active.Remove(t);
            }
        }

        // On Stop: no tween keeps running, and none keeps a user delegate alive.
        internal static void Clear()
        {
            foreach (Tween t in Active) t.Abandon();
            Active.Clear();
        }

        private static List<KeyValuePair<string, object>> ReadGoals(object goals)
        {
            var list = new List<KeyValuePair<string, object>>();
            var dictionary = goals as IDictionary<string, object>;
            if (dictionary != null)
            {
                foreach (KeyValuePair<string, object> kv in dictionary) list.Add(kv);
            }
            else
            {
                foreach (PropertyInfo p in goals.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
                {
                    if (!p.CanRead || p.GetIndexParameters().Length > 0) continue;
                    list.Add(new KeyValuePair<string, object>(p.Name, p.GetValue(goals)));
                }
            }
            if (list.Count == 0) throw new ArgumentException("TweenService.Create: the goals name no property");
            foreach (KeyValuePair<string, object> kv in list)
            {
                ValueKind kind;
                if (string.IsNullOrEmpty(kv.Key) || Components(kv.Value, out kind) == null)
                    throw new ArgumentException("TweenService.Create: '" + kv.Key + "' cannot be tweened (use float, int, Vector2, Vector3 or Color3)");
            }
            return list;
        }

        // The numeric components of a tweenable value, or null when the type cannot be tweened.
        internal static double[] Components(object value, out ValueKind kind)
        {
            switch (value)
            {
                case float f: kind = ValueKind.Number; return new[] { (double)f };
                case double d: kind = ValueKind.Number; return new[] { d };
                case int i: kind = ValueKind.Integer; return new[] { (double)i };
                case Vector2 v2: kind = ValueKind.Vector2; return new[] { (double)v2.X, v2.Y };
                case Vector3 v3: kind = ValueKind.Vector3; return new[] { (double)v3.X, v3.Y, v3.Z };
                case Color3 c: kind = ValueKind.Color3; return new[] { (double)c.R, c.G, c.B };
                default: kind = ValueKind.Number; return null;
            }
        }

        internal static object Build(ValueKind kind, double[] v)
        {
            switch (kind)
            {
                case ValueKind.Integer: return (int)Math.Round(v[0]);
                case ValueKind.Vector2: return new Vector2((float)v[0], (float)v[1]);
                case ValueKind.Vector3: return new Vector3((float)v[0], (float)v[1], (float)v[2]);
                case ValueKind.Color3: return new Color3((float)v[0], (float)v[1], (float)v[2]);
                default: return (float)v[0];
            }
        }

        // ---- Penner curves ---------------------------------------------------------------------------------------

        internal static double Ease(double alpha, EasingStyle style, EasingDirection direction)
        {
            double t = alpha < 0.0 ? 0.0 : alpha > 1.0 ? 1.0 : alpha;
            switch (style)
            {
                case EasingStyle.Linear: return t;
                case EasingStyle.Sine:
                    switch (direction)
                    {
                        case EasingDirection.In: return 1.0 - Math.Cos(t * Math.PI / 2.0);
                        case EasingDirection.Out: return Math.Sin(t * Math.PI / 2.0);
                        default: return -(Math.Cos(Math.PI * t) - 1.0) / 2.0;
                    }
                case EasingStyle.Quad: return Power(t, 2, direction);
                case EasingStyle.Cubic: return Power(t, 3, direction);
                case EasingStyle.Quart: return Power(t, 4, direction);
                case EasingStyle.Quint: return Power(t, 5, direction);
                case EasingStyle.Exponential: return Exponential(t, direction);
                case EasingStyle.Circular: return Circular(t, direction);
                case EasingStyle.Back: return Back(t, direction);
                case EasingStyle.Elastic: return Elastic(t, direction);
                case EasingStyle.Bounce:
                    switch (direction)
                    {
                        case EasingDirection.In: return 1.0 - BounceOut(1.0 - t);
                        case EasingDirection.Out: return BounceOut(t);
                        default:
                            return t < 0.5 ? (1.0 - BounceOut(1.0 - 2.0 * t)) / 2.0 : (1.0 + BounceOut(2.0 * t - 1.0)) / 2.0;
                    }
                default: return t;
            }
        }

        private static double Power(double t, int n, EasingDirection direction)
        {
            switch (direction)
            {
                case EasingDirection.In: return Math.Pow(t, n);
                case EasingDirection.Out: return 1.0 - Math.Pow(1.0 - t, n);
                default:
                    return t < 0.5 ? Math.Pow(2.0, n - 1) * Math.Pow(t, n) : 1.0 - Math.Pow(-2.0 * t + 2.0, n) / 2.0;
            }
        }

        private static double Exponential(double t, EasingDirection direction)
        {
            switch (direction)
            {
                case EasingDirection.In: return t == 0.0 ? 0.0 : Math.Pow(2.0, 10.0 * t - 10.0);
                case EasingDirection.Out: return t == 1.0 ? 1.0 : 1.0 - Math.Pow(2.0, -10.0 * t);
                default:
                    if (t == 0.0) return 0.0;
                    if (t == 1.0) return 1.0;
                    return t < 0.5 ? Math.Pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - Math.Pow(2.0, -20.0 * t + 10.0)) / 2.0;
            }
        }

        private static double Circular(double t, EasingDirection direction)
        {
            switch (direction)
            {
                case EasingDirection.In: return 1.0 - Math.Sqrt(1.0 - t * t);
                case EasingDirection.Out: return Math.Sqrt(1.0 - (t - 1.0) * (t - 1.0));
                default:
                    return t < 0.5
                        ? (1.0 - Math.Sqrt(1.0 - 4.0 * t * t)) / 2.0
                        : (Math.Sqrt(1.0 - (-2.0 * t + 2.0) * (-2.0 * t + 2.0)) + 1.0) / 2.0;
            }
        }

        private const double BackC1 = 1.70158;
        private const double BackC2 = BackC1 * 1.525;
        private const double BackC3 = BackC1 + 1.0;

        private static double Back(double t, EasingDirection direction)
        {
            switch (direction)
            {
                case EasingDirection.In: return BackC3 * t * t * t - BackC1 * t * t;
                case EasingDirection.Out: return 1.0 + BackC3 * Math.Pow(t - 1.0, 3) + BackC1 * Math.Pow(t - 1.0, 2);
                default:
                    return t < 0.5
                        ? (2.0 * t * 2.0 * t * ((BackC2 + 1.0) * 2.0 * t - BackC2)) / 2.0
                        : (Math.Pow(2.0 * t - 2.0, 2) * ((BackC2 + 1.0) * (2.0 * t - 2.0) + BackC2) + 2.0) / 2.0;
            }
        }

        private const double ElasticC4 = 2.0 * Math.PI / 3.0;
        private const double ElasticC5 = 2.0 * Math.PI / 4.5;

        private static double Elastic(double t, EasingDirection direction)
        {
            switch (direction)
            {
                case EasingDirection.In:
                    if (t == 0.0) return 0.0;
                    if (t == 1.0) return 1.0;
                    return -Math.Pow(2.0, 10.0 * t - 10.0) * Math.Sin((t * 10.0 - 10.75) * ElasticC4);
                case EasingDirection.Out:
                    if (t == 0.0) return 0.0;
                    if (t == 1.0) return 1.0;
                    return Math.Pow(2.0, -10.0 * t) * Math.Sin((t * 10.0 - 0.75) * ElasticC4) + 1.0;
                default:
                    if (t == 0.0) return 0.0;
                    if (t == 1.0) return 1.0;
                    return t < 0.5
                        ? -(Math.Pow(2.0, 20.0 * t - 10.0) * Math.Sin((20.0 * t - 11.125) * ElasticC5)) / 2.0
                        : Math.Pow(2.0, -20.0 * t + 10.0) * Math.Sin((20.0 * t - 11.125) * ElasticC5) / 2.0 + 1.0;
            }
        }

        private static double BounceOut(double t)
        {
            const double n1 = 7.5625;
            const double d1 = 2.75;
            if (t < 1.0 / d1) return n1 * t * t;
            if (t < 2.0 / d1)
            {
                t -= 1.5 / d1;
                return n1 * t * t + 0.75;
            }
            if (t < 2.5 / d1)
            {
                t -= 2.25 / d1;
                return n1 * t * t + 0.9375;
            }
            t -= 2.625 / d1;
            return n1 * t * t + 0.984375;
        }
    }
}
