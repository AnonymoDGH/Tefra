using System;
using System.Globalization;

namespace Tefra
{
    // Metres, +Y up, -Z forward (ADR-009). Orientation is expressed in degrees on parts.
    public readonly struct Vector3 : IEquatable<Vector3>
    {
        public readonly float X, Y, Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static readonly Vector3 Zero = new Vector3(0, 0, 0);
        public static readonly Vector3 One = new Vector3(1, 1, 1);
        public static readonly Vector3 Up = new Vector3(0, 1, 0);
        public static readonly Vector3 Forward = new Vector3(0, 0, -1);

        public float Magnitude => (float)Math.Sqrt(X * X + Y * Y + Z * Z);

        public Vector3 Unit
        {
            get
            {
                float m = Magnitude;
                return m > 0 ? this / m : Zero;
            }
        }

        public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

        public static Vector3 Cross(Vector3 a, Vector3 b) =>
            new Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);

        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * t;

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.X, -a.Y, -a.Z);
        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.X * s, a.Y * s, a.Z * s);
        public static Vector3 operator *(float s, Vector3 a) => new Vector3(a.X * s, a.Y * s, a.Z * s);
        public static Vector3 operator /(Vector3 a, float s) => new Vector3(a.X / s, a.Y / s, a.Z / s);
        public static bool operator ==(Vector3 a, Vector3 b) => a.Equals(b);
        public static bool operator !=(Vector3 a, Vector3 b) => !a.Equals(b);

        public bool Equals(Vector3 o) => X == o.X && Y == o.Y && Z == o.Z;
        public override bool Equals(object obj) => obj is Vector3 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);

        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "{0}, {1}, {2}", X, Y, Z);
    }

    // Colour in sRGB, components 0-1 (as stored in the .tefra file).
    public readonly struct Color3 : IEquatable<Color3>
    {
        public readonly float R, G, B;

        public Color3(float r, float g, float b)
        {
            R = r;
            G = g;
            B = b;
        }

        public static Color3 FromRGB(int r, int g, int b) =>
            new Color3(r / 255f, g / 255f, b / 255f);

        /// <summary>Parses "#RRGGBB" or "RRGGBB" (hex digits, sRGB).</summary>
        public static Color3 FromHex(string hex)
        {
            if (hex == null) throw new ArgumentNullException(nameof(hex));
            string digits = hex.StartsWith("#", StringComparison.Ordinal) ? hex.Substring(1) : hex;
            if (digits.Length != 6) throw new ArgumentException("Color3.FromHex expects 6 hex digits: " + hex, nameof(hex));
            int value = int.Parse(digits, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return FromRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
        }

        /// <summary>Hue, saturation and value, each 0-1 (hue wraps). Result is sRGB.</summary>
        public static Color3 FromHSV(float h, float s, float v)
        {
            float hh = (h - (float)Math.Floor(h)) * 6f;
            int i = (int)Math.Floor(hh);
            float f = hh - i;
            float p = v * (1 - s);
            float q = v * (1 - s * f);
            float t = v * (1 - s * (1 - f));
            switch (i % 6)
            {
                case 0: return new Color3(v, t, p);
                case 1: return new Color3(q, v, p);
                case 2: return new Color3(p, v, t);
                case 3: return new Color3(p, q, v);
                case 4: return new Color3(t, p, v);
                default: return new Color3(v, p, q);
            }
        }

        /// <summary>(hue, saturation, value) as a Vector3, each 0-1.</summary>
        public Vector3 ToHSV()
        {
            float max = Math.Max(R, Math.Max(G, B));
            float min = Math.Min(R, Math.Min(G, B));
            float d = max - min;
            float s = max > 0 ? d / max : 0f;
            float h = 0f;
            if (d > 0)
            {
                if (max == R) h = (G - B) / d;
                else if (max == G) h = (B - R) / d + 2f;
                else h = (R - G) / d + 4f;
                h /= 6f;
                if (h < 0) h += 1f;
            }
            return new Vector3(h, s, max);
        }

        public static Color3 Lerp(Color3 a, Color3 b, float t) =>
            new Color3(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t);

        public static readonly Color3 White = new Color3(1, 1, 1);
        public static readonly Color3 Black = new Color3(0, 0, 0);
        public static readonly Color3 Gray = new Color3(0.64f, 0.64f, 0.64f);

        public bool Equals(Color3 o) => R == o.R && G == o.G && B == o.B;
        public override bool Equals(object obj) => obj is Color3 c && Equals(c);
        public override int GetHashCode() => HashCode.Combine(R, G, B);
        public static bool operator ==(Color3 a, Color3 b) => a.Equals(b);
        public static bool operator !=(Color3 a, Color3 b) => !a.Equals(b);

        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "{0}, {1}, {2}", R, G, B);
    }
}
