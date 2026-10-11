// Roblox-style value types: CFrame, UDim/UDim2, NumberRange, NumberSequence, ColorSequence and TefraRandom.
// Plain managed code, no native calls. Conventions match Math.cs (metres, +Y up, -Z forward, right-handed).
//
// Rotations: a CFrame's rotation is a 3x3 matrix R, applied to column vectors (R * v). Its columns are the part's
// right (X), up (Y) and back (Z) axes, so LookVector = -Z column. Part.Orientation is stored as degrees in Y-X-Z
// order, R = Ry * Rx * Rz; the conversions below mirror orientation_to_quat / quat_to_orientation in
// engine/world/src/datamodel.cpp exactly.
using System;
using System.Collections.Generic;
using System.Globalization;

namespace Tefra
{
    // Row-major 3x3 matrix. Internal: CFrame is the public face of it.
    internal readonly struct Mat3
    {
        public readonly float M00, M01, M02, M10, M11, M12, M20, M21, M22;

        public Mat3(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
        {
            M00 = m00; M01 = m01; M02 = m02;
            M10 = m10; M11 = m11; M12 = m12;
            M20 = m20; M21 = m21; M22 = m22;
        }

        public static readonly Mat3 Identity = new Mat3(1, 0, 0, 0, 1, 0, 0, 0, 1);

        public static Mat3 RotX(float a)
        {
            float c = (float)Math.Cos(a), s = (float)Math.Sin(a);
            return new Mat3(1, 0, 0, 0, c, -s, 0, s, c);
        }

        public static Mat3 RotY(float a)
        {
            float c = (float)Math.Cos(a), s = (float)Math.Sin(a);
            return new Mat3(c, 0, s, 0, 1, 0, -s, 0, c);
        }

        public static Mat3 RotZ(float a)
        {
            float c = (float)Math.Cos(a), s = (float)Math.Sin(a);
            return new Mat3(c, -s, 0, s, c, 0, 0, 0, 1);
        }

        public static Mat3 operator *(Mat3 a, Mat3 b) => new Mat3(
            a.M00 * b.M00 + a.M01 * b.M10 + a.M02 * b.M20, a.M00 * b.M01 + a.M01 * b.M11 + a.M02 * b.M21, a.M00 * b.M02 + a.M01 * b.M12 + a.M02 * b.M22,
            a.M10 * b.M00 + a.M11 * b.M10 + a.M12 * b.M20, a.M10 * b.M01 + a.M11 * b.M11 + a.M12 * b.M21, a.M10 * b.M02 + a.M11 * b.M12 + a.M12 * b.M22,
            a.M20 * b.M00 + a.M21 * b.M10 + a.M22 * b.M20, a.M20 * b.M01 + a.M21 * b.M11 + a.M22 * b.M21, a.M20 * b.M02 + a.M21 * b.M12 + a.M22 * b.M22);

        public Vector3 Mul(Vector3 v) => new Vector3(
            M00 * v.X + M01 * v.Y + M02 * v.Z,
            M10 * v.X + M11 * v.Y + M12 * v.Z,
            M20 * v.X + M21 * v.Y + M22 * v.Z);

        // For a rotation, the transpose is the inverse.
        public Mat3 Transpose() => new Mat3(M00, M10, M20, M01, M11, M21, M02, M12, M22);
    }

    /// <summary>Position and rotation. Immutable; every operation returns a new value.</summary>
    public readonly struct CFrame : IEquatable<CFrame>
    {
        /// <summary>Origin of the frame (metres).</summary>
        public readonly Vector3 Position;
        private readonly Mat3 _r;

        private CFrame(Vector3 position, Mat3 rotation)
        {
            Position = position;
            _r = rotation;
        }

        /// <summary>CFrame.new(x, y, z): translation only.</summary>
        public CFrame(float x, float y, float z) : this(new Vector3(x, y, z), Mat3.Identity) { }

        /// <summary>CFrame.new(position): translation only.</summary>
        public CFrame(Vector3 position) : this(position, Mat3.Identity) { }

        /// <summary>CFrame.new(position, lookAt): the look vector points at lookAt, Y stays up.</summary>
        public CFrame(Vector3 position, Vector3 lookAt) : this(position, lookAtRotation(position, lookAt, Vector3.Up)) { }

        /// <summary>CFrame.new(x, y, z, R00, R01, R02, R10, R11, R12, R20, R21, R22): full matrix, row by row.</summary>
        public CFrame(float x, float y, float z, float r00, float r01, float r02, float r10, float r11, float r12,
                      float r20, float r21, float r22)
            : this(new Vector3(x, y, z), new Mat3(r00, r01, r02, r10, r11, r12, r20, r21, r22)) { }

        public static CFrame Identity => new CFrame(new Vector3(0, 0, 0), Mat3.Identity);

        /// <summary>Right axis (+X of the part), in world space.</summary>
        public Vector3 RightVector => new Vector3(_r.M00, _r.M10, _r.M20);
        /// <summary>Up axis (+Y of the part), in world space.</summary>
        public Vector3 UpVector => new Vector3(_r.M01, _r.M11, _r.M21);
        /// <summary>Forward direction (-Z of the part), in world space.</summary>
        public Vector3 LookVector => new Vector3(-_r.M02, -_r.M12, -_r.M22);

        public float X => Position.X;
        public float Y => Position.Y;
        public float Z => Position.Z;

        /// <summary>The same rotation at the origin.</summary>
        public CFrame Rotation => new CFrame(new Vector3(0, 0, 0), _r);

        /// <summary>CFrame.Angles(rx, ry, rz): radians, rotations applied X, then Y, then Z (Rx * Ry * Rz).</summary>
        public static CFrame Angles(float rx, float ry, float rz) =>
            new CFrame(new Vector3(0, 0, 0), Mat3.RotX(rx) * Mat3.RotY(ry) * Mat3.RotZ(rz));

        /// <summary>Radians in the engine's Y, X, Z order (Ry * Rx * Rz), the same as Part.Orientation.</summary>
        public static CFrame fromEulerAnglesYXZ(float rx, float ry, float rz) =>
            new CFrame(new Vector3(0, 0, 0), Mat3.RotY(ry) * Mat3.RotX(rx) * Mat3.RotZ(rz));

        /// <summary>Rotation of angle radians about axis (need not be unit length; a zero axis gives identity).</summary>
        public static CFrame fromAxisAngle(Vector3 axis, float angle)
        {
            float len = axis.Magnitude;
            if (len == 0) return Identity;
            float x = axis.X / len, y = axis.Y / len, z = axis.Z / len;
            float c = (float)Math.Cos(angle), s = (float)Math.Sin(angle), t = 1 - c;
            return new CFrame(new Vector3(0, 0, 0), new Mat3(
                t * x * x + c, t * x * y - s * z, t * x * z + s * y,
                t * x * y + s * z, t * y * y + c, t * y * z - s * x,
                t * x * z - s * y, t * y * z + s * x, t * z * z + c));
        }

        /// <summary>CFrame at position at, facing target, with the given up direction.</summary>
        public static CFrame lookAt(Vector3 at, Vector3 target, Vector3 up) => new CFrame(at, lookAtRotation(at, target, up));

        /// <summary>CFrame at position at, facing target, with Y up.</summary>
        public static CFrame lookAt(Vector3 at, Vector3 target) => lookAt(at, target, Vector3.Up);

        /// <summary>Rotation with the back axis (+Z) opposite the look direction; falls back to identity rotation when
        /// the look direction is zero.</summary>
        private static Mat3 lookAtRotation(Vector3 position, Vector3 target, Vector3 up)
        {
            Vector3 back = position - target;
            if (back.Magnitude == 0) return Mat3.Identity;
            back = back.Unit;
            Vector3 right = Vector3.Cross(up, back);
            if (right.Magnitude < 1e-6f)
            {
                // Looking straight along up: pick any axis perpendicular to back.
                right = Vector3.Cross(new Vector3(0, 0, 1), back);
                if (right.Magnitude < 1e-6f) right = Vector3.Cross(new Vector3(1, 0, 0), back);
            }
            right = right.Unit;
            Vector3 upAxis = Vector3.Cross(back, right);
            return new Mat3(right.X, upAxis.X, back.X, right.Y, upAxis.Y, back.Y, right.Z, upAxis.Z, back.Z);
        }

        /// <summary>Position and Part.Orientation (degrees, Y-X-Z) as one CFrame. Internal: used by BasePart.</summary>
        internal static CFrame FromOrientation(Vector3 position, Vector3 degrees)
        {
            const float toRad = (float)(Math.PI / 180.0);
            Mat3 r = Mat3.RotY(degrees.Y * toRad) * Mat3.RotX(degrees.X * toRad) * Mat3.RotZ(degrees.Z * toRad);
            return new CFrame(position, r);
        }

        /// <summary>The rotation as Part.Orientation degrees (Y-X-Z). Inverse of FromOrientation. Internal.</summary>
        internal Vector3 ToOrientation()
        {
            const float toDeg = (float)(180.0 / Math.PI);
            Vector3 radians = ToEulerAnglesYXZ();
            return new Vector3(radians.X * toDeg, radians.Y * toDeg, radians.Z * toDeg);
        }

        /// <summary>Decomposes the rotation into radians (x, y, z) such that it equals fromEulerAnglesYXZ(x, y, z).
        /// At gimbal lock (x = +-90 degrees) z is set to 0.</summary>
        public Vector3 ToEulerAnglesYXZ()
        {
            float sx = Clamp(-_r.M12, -1f, 1f);
            float x = (float)Math.Asin(sx);
            float y, z;
            if (Math.Abs(sx) > 0.999999f)
            {
                z = 0;
                y = sx > 0 ? (float)Math.Atan2(_r.M01, _r.M00) : (float)Math.Atan2(-_r.M01, _r.M00);
            }
            else
            {
                y = (float)Math.Atan2(_r.M02, _r.M22);
                z = (float)Math.Atan2(_r.M10, _r.M11);
            }
            return new Vector3(x, y, z);
        }

        /// <summary>The inverse transform: rotation transposed, position -R^T * p.</summary>
        public CFrame Inverse()
        {
            Mat3 rt = _r.Transpose();
            return new CFrame(-rt.Mul(Position), rt);
        }

        /// <summary>Spherical interpolation of the rotation and linear interpolation of the position.</summary>
        public CFrame Lerp(CFrame goal, float alpha)
        {
            Vector3 p = Vector3.Lerp(Position, goal.Position, alpha);
            float[] a = ToQuaternion(_r);
            float[] b = ToQuaternion(goal._r);
            float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
            if (dot < 0)
            {
                dot = -dot;
                for (int i = 0; i < 4; i++) b[i] = -b[i];
            }
            float w0, w1;
            if (dot > 0.9995f)
            {
                w0 = 1 - alpha;
                w1 = alpha;
            }
            else
            {
                float theta = (float)Math.Acos(dot);
                float sin = (float)Math.Sin(theta);
                w0 = (float)Math.Sin((1 - alpha) * theta) / sin;
                w1 = (float)Math.Sin(alpha * theta) / sin;
            }
            var q = new float[4];
            for (int i = 0; i < 4; i++) q[i] = a[i] * w0 + b[i] * w1;
            return new CFrame(p, FromQuaternion(q));
        }

        /// <summary>CFrame to be applied in this frame's parent space: this * other.</summary>
        public CFrame ToWorldSpace(CFrame other) => this * other;

        /// <summary>other expressed relative to this frame: Inverse() * other.</summary>
        public CFrame ToObjectSpace(CFrame other) => Inverse() * other;

        public Vector3 PointToWorldSpace(Vector3 v) => this * v;

        public Vector3 PointToObjectSpace(Vector3 v) => Inverse() * v;

        public Vector3 VectorToWorldSpace(Vector3 v) => _r.Mul(v);

        public Vector3 VectorToObjectSpace(Vector3 v) => _r.Transpose().Mul(v);

        public static CFrame operator *(CFrame a, CFrame b) =>
            new CFrame(a.Position + a._r.Mul(b.Position), a._r * b._r);

        public static Vector3 operator *(CFrame a, Vector3 v) => a.Position + a._r.Mul(v);

        public static CFrame operator +(CFrame a, Vector3 offset) => new CFrame(a.Position + offset, a._r);

        public static CFrame operator -(CFrame a, Vector3 offset) => new CFrame(a.Position - offset, a._r);

        public bool Equals(CFrame o) =>
            Position == o.Position && _r.M00 == o._r.M00 && _r.M01 == o._r.M01 && _r.M02 == o._r.M02 &&
            _r.M10 == o._r.M10 && _r.M11 == o._r.M11 && _r.M12 == o._r.M12 &&
            _r.M20 == o._r.M20 && _r.M21 == o._r.M21 && _r.M22 == o._r.M22;

        public override bool Equals(object obj) => obj is CFrame c && Equals(c);

        public override int GetHashCode() => HashCode.Combine(Position, _r.M00, _r.M11, _r.M22);

        public static bool operator ==(CFrame a, CFrame b) => a.Equals(b);

        public static bool operator !=(CFrame a, CFrame b) => !a.Equals(b);

        public override string ToString() => string.Format(CultureInfo.InvariantCulture,
            "{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}, {11}",
            Position.X, Position.Y, Position.Z, _r.M00, _r.M01, _r.M02, _r.M10, _r.M11, _r.M12, _r.M20, _r.M21, _r.M22);

        private static float Clamp(float v, float lo, float hi) => v < lo ? lo : (v > hi ? hi : v);

        // Quaternion (x, y, z, w) of a rotation matrix (Shepperd's method).
        private static float[] ToQuaternion(Mat3 m)
        {
            float trace = m.M00 + m.M11 + m.M22;
            float x, y, z, w;
            if (trace > 0)
            {
                float s = (float)Math.Sqrt(trace + 1) * 2;
                w = 0.25f * s;
                x = (m.M21 - m.M12) / s;
                y = (m.M02 - m.M20) / s;
                z = (m.M10 - m.M01) / s;
            }
            else if (m.M00 > m.M11 && m.M00 > m.M22)
            {
                float s = (float)Math.Sqrt(1 + m.M00 - m.M11 - m.M22) * 2;
                w = (m.M21 - m.M12) / s;
                x = 0.25f * s;
                y = (m.M01 + m.M10) / s;
                z = (m.M02 + m.M20) / s;
            }
            else if (m.M11 > m.M22)
            {
                float s = (float)Math.Sqrt(1 + m.M11 - m.M00 - m.M22) * 2;
                w = (m.M02 - m.M20) / s;
                x = (m.M01 + m.M10) / s;
                y = 0.25f * s;
                z = (m.M12 + m.M21) / s;
            }
            else
            {
                float s = (float)Math.Sqrt(1 + m.M22 - m.M00 - m.M11) * 2;
                w = (m.M10 - m.M01) / s;
                x = (m.M02 + m.M20) / s;
                y = (m.M12 + m.M21) / s;
                z = 0.25f * s;
            }
            return new[] { x, y, z, w };
        }

        private static Mat3 FromQuaternion(float[] q)
        {
            float x = q[0], y = q[1], z = q[2], w = q[3];
            float len = (float)Math.Sqrt(x * x + y * y + z * z + w * w);
            if (len > 0) { x /= len; y /= len; z /= len; w /= len; }
            return new Mat3(
                1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y));
        }
    }

    /// <summary>Scale plus pixel offset along one axis.</summary>
    public readonly struct UDim : IEquatable<UDim>
    {
        public readonly float Scale;
        public readonly float Offset;

        public UDim(float scale, float offset)
        {
            Scale = scale;
            Offset = offset;
        }

        public static UDim Lerp(UDim a, UDim b, float t) =>
            new UDim(a.Scale + (b.Scale - a.Scale) * t, a.Offset + (b.Offset - a.Offset) * t);

        public static UDim operator +(UDim a, UDim b) => new UDim(a.Scale + b.Scale, a.Offset + b.Offset);
        public static UDim operator -(UDim a, UDim b) => new UDim(a.Scale - b.Scale, a.Offset - b.Offset);

        public bool Equals(UDim o) => Scale == o.Scale && Offset == o.Offset;
        public override bool Equals(object obj) => obj is UDim u && Equals(u);
        public override int GetHashCode() => HashCode.Combine(Scale, Offset);
        public static bool operator ==(UDim a, UDim b) => a.Equals(b);
        public static bool operator !=(UDim a, UDim b) => !a.Equals(b);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "{0}, {1}", Scale, Offset);
    }

    /// <summary>Two UDims, one per axis. Construct with new UDim2(xScale, xOffset, yScale, yOffset).</summary>
    public readonly struct UDim2 : IEquatable<UDim2>
    {
        public readonly UDim X;
        public readonly UDim Y;

        public UDim2(float xScale, float xOffset, float yScale, float yOffset)
        {
            X = new UDim(xScale, xOffset);
            Y = new UDim(yScale, yOffset);
        }

        public UDim2(UDim x, UDim y)
        {
            X = x;
            Y = y;
        }

        /// <summary>UDim2.fromScale: fractions of the parent size, no offset.</summary>
        public static UDim2 fromScale(float xScale, float yScale) => new UDim2(xScale, 0, yScale, 0);

        /// <summary>UDim2.fromOffset: pixels, no scale.</summary>
        public static UDim2 fromOffset(float xOffset, float yOffset) => new UDim2(0, xOffset, 0, yOffset);

        public UDim2 Lerp(UDim2 goal, float alpha) => new UDim2(UDim.Lerp(X, goal.X, alpha), UDim.Lerp(Y, goal.Y, alpha));

        public static UDim2 operator +(UDim2 a, UDim2 b) => new UDim2(a.X + b.X, a.Y + b.Y);
        public static UDim2 operator -(UDim2 a, UDim2 b) => new UDim2(a.X - b.X, a.Y - b.Y);

        public bool Equals(UDim2 o) => X.Equals(o.X) && Y.Equals(o.Y);
        public override bool Equals(object obj) => obj is UDim2 u && Equals(u);
        public override int GetHashCode() => HashCode.Combine(X, Y);
        public static bool operator ==(UDim2 a, UDim2 b) => a.Equals(b);
        public static bool operator !=(UDim2 a, UDim2 b) => !a.Equals(b);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "{0}, {1}", X, Y);
    }

    /// <summary>Closed interval of floats.</summary>
    public readonly struct NumberRange : IEquatable<NumberRange>
    {
        public readonly float Min;
        public readonly float Max;

        /// <summary>Range from min to max (min is clamped to max if given in reverse order).</summary>
        public NumberRange(float min, float max)
        {
            Min = Math.Min(min, max);
            Max = Math.Max(min, max);
        }

        /// <summary>Single value: Min = Max = value.</summary>
        public NumberRange(float value) : this(value, value) { }

        public bool Equals(NumberRange o) => Min == o.Min && Max == o.Max;
        public override bool Equals(object obj) => obj is NumberRange r && Equals(r);
        public override int GetHashCode() => HashCode.Combine(Min, Max);
        public static bool operator ==(NumberRange a, NumberRange b) => a.Equals(b);
        public static bool operator !=(NumberRange a, NumberRange b) => !a.Equals(b);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "{0}, {1}", Min, Max);
    }

    /// <summary>One point of a NumberSequence. Time is 0-1.</summary>
    public readonly struct NumberSequenceKeypoint : IEquatable<NumberSequenceKeypoint>
    {
        public readonly float Time;
        public readonly float Value;
        public readonly float Envelope;

        public NumberSequenceKeypoint(float time, float value, float envelope = 0)
        {
            if (time < 0 || time > 1) throw new ArgumentOutOfRangeException(nameof(time), "keypoint time must be in [0, 1]");
            Time = time;
            Value = value;
            Envelope = envelope;
        }

        public bool Equals(NumberSequenceKeypoint o) => Time == o.Time && Value == o.Value && Envelope == o.Envelope;
        public override bool Equals(object obj) => obj is NumberSequenceKeypoint k && Equals(k);
        public override int GetHashCode() => HashCode.Combine(Time, Value, Envelope);
        public static bool operator ==(NumberSequenceKeypoint a, NumberSequenceKeypoint b) => a.Equals(b);
        public static bool operator !=(NumberSequenceKeypoint a, NumberSequenceKeypoint b) => !a.Equals(b);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "{0}, {1}, {2}", Time, Value, Envelope);
    }

    /// <summary>Piecewise-linear curve over t in [0, 1], given by at least two keypoints.</summary>
    public sealed class NumberSequence
    {
        private readonly NumberSequenceKeypoint[] _keys;

        /// <summary>Keypoints in any order; they are sorted by time. Needs at least two.</summary>
        public NumberSequence(params NumberSequenceKeypoint[] keypoints)
        {
            if (keypoints == null || keypoints.Length < 2)
                throw new ArgumentException("NumberSequence needs at least two keypoints");
            _keys = (NumberSequenceKeypoint[])keypoints.Clone();
            Array.Sort(_keys, (a, b) => a.Time.CompareTo(b.Time));
        }

        /// <summary>Constant-ramp shorthand: value start at t = 0, value end at t = 1.</summary>
        public NumberSequence(float start, float end)
            : this(new NumberSequenceKeypoint(0, start), new NumberSequenceKeypoint(1, end)) { }

        public NumberSequenceKeypoint[] Keypoints => (NumberSequenceKeypoint[])_keys.Clone();

        /// <summary>Value at t; t outside [0, 1] holds the first or last value.</summary>
        public float Evaluate(float t)
        {
            NumberSequenceKeypoint first = _keys[0], last = _keys[_keys.Length - 1];
            if (t <= first.Time) return first.Value;
            if (t >= last.Time) return last.Value;
            for (int i = 1; i < _keys.Length; i++)
            {
                NumberSequenceKeypoint b = _keys[i];
                if (t > b.Time) continue;
                NumberSequenceKeypoint a = _keys[i - 1];
                float span = b.Time - a.Time;
                if (span <= 0) return b.Value;
                return a.Value + (b.Value - a.Value) * ((t - a.Time) / span);
            }
            return last.Value;
        }
    }

    /// <summary>One point of a ColorSequence. Time is 0-1.</summary>
    public readonly struct ColorSequenceKeypoint : IEquatable<ColorSequenceKeypoint>
    {
        public readonly float Time;
        public readonly Color3 Value;

        public ColorSequenceKeypoint(float time, Color3 value)
        {
            if (time < 0 || time > 1) throw new ArgumentOutOfRangeException(nameof(time), "keypoint time must be in [0, 1]");
            Time = time;
            Value = value;
        }

        public bool Equals(ColorSequenceKeypoint o) => Time == o.Time && Value == o.Value;
        public override bool Equals(object obj) => obj is ColorSequenceKeypoint k && Equals(k);
        public override int GetHashCode() => HashCode.Combine(Time, Value);
        public static bool operator ==(ColorSequenceKeypoint a, ColorSequenceKeypoint b) => a.Equals(b);
        public static bool operator !=(ColorSequenceKeypoint a, ColorSequenceKeypoint b) => !a.Equals(b);
        public override string ToString() => Time.ToString(CultureInfo.InvariantCulture) + ", " + Value;
    }

    /// <summary>Colour gradient over t in [0, 1]. Interpolates the sRGB components linearly.</summary>
    public sealed class ColorSequence
    {
        private readonly ColorSequenceKeypoint[] _keys;

        /// <summary>Keypoints in any order; they are sorted by time. Needs at least two.</summary>
        public ColorSequence(params ColorSequenceKeypoint[] keypoints)
        {
            if (keypoints == null || keypoints.Length < 2)
                throw new ArgumentException("ColorSequence needs at least two keypoints");
            _keys = (ColorSequenceKeypoint[])keypoints.Clone();
            Array.Sort(_keys, (a, b) => a.Time.CompareTo(b.Time));
        }

        /// <summary>Two-colour shorthand: start at t = 0, end at t = 1.</summary>
        public ColorSequence(Color3 start, Color3 end)
            : this(new ColorSequenceKeypoint(0, start), new ColorSequenceKeypoint(1, end)) { }

        public ColorSequenceKeypoint[] Keypoints => (ColorSequenceKeypoint[])_keys.Clone();

        /// <summary>Colour at t; t outside [0, 1] holds the first or last colour.</summary>
        public Color3 Evaluate(float t)
        {
            ColorSequenceKeypoint first = _keys[0], last = _keys[_keys.Length - 1];
            if (t <= first.Time) return first.Value;
            if (t >= last.Time) return last.Value;
            for (int i = 1; i < _keys.Length; i++)
            {
                ColorSequenceKeypoint b = _keys[i];
                if (t > b.Time) continue;
                ColorSequenceKeypoint a = _keys[i - 1];
                float span = b.Time - a.Time;
                if (span <= 0) return b.Value;
                return Color3.Lerp(a.Value, b.Value, (t - a.Time) / span);
            }
            return last.Value;
        }
    }

    /// <summary>Seeded pseudo-random numbers. Named TefraRandom because scripts import System, where "Random" already
    /// names System.Random, and an unqualified Random would be ambiguous there.</summary>
    public sealed class TefraRandom
    {
        private readonly System.Random _rng;

        /// <summary>Seeded from the clock.</summary>
        public TefraRandom() : this(Environment.TickCount) { }

        /// <summary>The same seed always gives the same sequence.</summary>
        public TefraRandom(int seed)
        {
            _rng = new System.Random(seed);
        }

        /// <summary>Uniform in [0, 1).</summary>
        public float NextNumber() => (float)_rng.NextDouble();

        /// <summary>Uniform in [min, max).</summary>
        public float NextNumber(float min, float max)
        {
            if (max < min) throw new ArgumentException("NextNumber: max < min");
            return min + (float)_rng.NextDouble() * (max - min);
        }

        /// <summary>Uniform integer in [min, max], both ends included.</summary>
        public int NextInteger(int min, int max)
        {
            if (max < min) throw new ArgumentException("NextInteger: max < min");
            return _rng.Next(min, max + 1);
        }

        /// <summary>Uniformly distributed direction on the unit sphere.</summary>
        public Vector3 NextUnitVector()
        {
            float z = 2f * (float)_rng.NextDouble() - 1f;
            float phi = 2f * (float)Math.PI * (float)_rng.NextDouble();
            float r = (float)Math.Sqrt(Math.Max(0f, 1f - z * z));
            return new Vector3(r * (float)Math.Cos(phi), z, r * (float)Math.Sin(phi));
        }
    }
}
