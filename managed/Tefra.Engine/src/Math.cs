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
