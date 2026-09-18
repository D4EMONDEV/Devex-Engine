using System.Runtime.InteropServices;

namespace Devex;

/// <summary>Two floats, laid out like the engine's math::Vec2.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec2(float x, float y)
{
    public float X = x;
    public float Y = y;

    public override readonly string ToString() => $"({X}, {Y})";
}

/// <summary>Three floats, laid out like the engine's math::Vec3. Y is up, -Z is forward.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec3(float x, float y, float z)
{
    public float X = x;
    public float Y = y;
    public float Z = z;

    public static readonly Vec3 Zero = new(0.0f, 0.0f, 0.0f);
    public static readonly Vec3 One = new(1.0f, 1.0f, 1.0f);
    public static readonly Vec3 Up = new(0.0f, 1.0f, 0.0f);
    public static readonly Vec3 Right = new(1.0f, 0.0f, 0.0f);
    public static readonly Vec3 Forward = new(0.0f, 0.0f, -1.0f);

    public static Vec3 operator +(Vec3 left, Vec3 right) => new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vec3 operator -(Vec3 left, Vec3 right) => new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vec3 operator -(Vec3 value) => new(-value.X, -value.Y, -value.Z);
    public static Vec3 operator *(Vec3 value, float scale) => new(value.X * scale, value.Y * scale, value.Z * scale);
    public static Vec3 operator *(float scale, Vec3 value) => value * scale;

    public readonly float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);

    public readonly Vec3 Normalized
    {
        get
        {
            float length = Length;
            return length > 1e-6f ? this * (1.0f / length) : Zero;
        }
    }

    public static float Dot(Vec3 left, Vec3 right) => left.X * right.X + left.Y * right.Y + left.Z * right.Z;

    public static Vec3 Cross(Vec3 left, Vec3 right) => new(
        left.Y * right.Z - left.Z * right.Y,
        left.Z * right.X - left.X * right.Z,
        left.X * right.Y - left.Y * right.X);

    public override readonly string ToString() => $"({X}, {Y}, {Z})";
}

/// <summary>Four floats, laid out like the engine's math::Vec4.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec4(float x, float y, float z, float w)
{
    public float X = x;
    public float Y = y;
    public float Z = z;
    public float W = w;

    public override readonly string ToString() => $"({X}, {Y}, {Z}, {W})";
}

/// <summary>A rotation, stored like the engine's math::Quat: x, y, z then w.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Quat(float x, float y, float z, float w)
{
    public float X = x;
    public float Y = y;
    public float Z = z;
    public float W = w;

    public static readonly Quat Identity = new(0.0f, 0.0f, 0.0f, 1.0f);

    /// <summary>A rotation of an angle in radians around an axis, which must be normalized.</summary>
    public static Quat AngleAxis(float radians, Vec3 axis)
    {
        float half = radians * 0.5f;
        float sin = MathF.Sin(half);
        return new Quat(axis.X * sin, axis.Y * sin, axis.Z * sin, MathF.Cos(half));
    }

    public static Quat operator *(Quat left, Quat right) => new(
        left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
        left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
        left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
        left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z);

    /// <summary>Turns a direction by this rotation.</summary>
    public static Vec3 operator *(Quat rotation, Vec3 direction)
    {
        var axis = new Vec3(rotation.X, rotation.Y, rotation.Z);
        Vec3 uv = Vec3.Cross(axis, direction);
        Vec3 uuv = Vec3.Cross(axis, uv);
        return direction + ((uv * rotation.W) + uuv) * 2.0f;
    }

    public override readonly string ToString() => $"({X}, {Y}, {Z}, {W})";
}

/// <summary>A 128-bit identifier, written as "6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23", as the engine names entities and assets.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Uuid(ulong first, ulong second) : IEquatable<Uuid>
{
    // The 16 bytes of the identifier, in the order the engine stores them.
    private readonly ulong _first = first;
    private readonly ulong _second = second;

    public static Uuid Nil => default;

    public bool IsNil => _first == 0 && _second == 0;

    /// <summary>Reads the canonical 36-character form; null when the text is not one.</summary>
    public static Uuid? Parse(string text)
    {
        if (text.Length != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        {
            return null;
        }
        Span<byte> bytes = stackalloc byte[16];
        int written = 0;
        for (int index = 0; index < text.Length; index += 2)
        {
            if (text[index] == '-')
            {
                --index;
                continue;
            }
            if (!byte.TryParse(text.AsSpan(index, 2), System.Globalization.NumberStyles.HexNumber, null, out bytes[written++]))
            {
                return null;
            }
        }
        return MemoryMarshal.Read<Uuid>(bytes);
    }

    public override string ToString()
    {
        Uuid copy = this;
        ReadOnlySpan<byte> bytes = MemoryMarshal.AsBytes(new ReadOnlySpan<Uuid>(in copy));
        string hex = Convert.ToHexStringLower(bytes);
        return $"{hex[..8]}-{hex[8..12]}-{hex[12..16]}-{hex[16..20]}-{hex[20..]}";
    }

    public bool Equals(Uuid other) => _first == other._first && _second == other._second;

    public override bool Equals(object? other) => other is Uuid uuid && Equals(uuid);

    public override int GetHashCode() => HashCode.Combine(_first, _second);

    public static bool operator ==(Uuid left, Uuid right) => left.Equals(right);

    public static bool operator !=(Uuid left, Uuid right) => !left.Equals(right);
}

/// <summary>Refers to an asset of the project, such as a mesh, a prefab or a scene.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct AssetId(Uuid uuid) : IEquatable<AssetId>
{
    public Uuid Uuid { get; } = uuid;

    public bool IsValid => !Uuid.IsNil;

    public override string ToString() => $"asset({Uuid})";

    public bool Equals(AssetId other) => Uuid == other.Uuid;

    public override bool Equals(object? other) => other is AssetId id && Equals(id);

    public override int GetHashCode() => Uuid.GetHashCode();

    public static bool operator ==(AssetId left, AssetId right) => left.Equals(right);

    public static bool operator !=(AssetId left, AssetId right) => !left.Equals(right);
}
