using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Devex;

/// <summary>
/// A method of a component that other players can call with <see cref="Component.Rpc"/> and
/// <see cref="Component.RpcId"/>. By default only the authority of the entity may call it, and it runs on
/// the other players' games. Its parameters are booleans, numbers, enums, strings, vectors, quaternions,
/// entities, identifiers and arrays of bytes.
/// </summary>
[AttributeUsage(AttributeTargets.Method)]
public sealed class RpcAttribute : Attribute
{
    /// <summary>Any player may call it, rather than only the authority of the entity.</summary>
    public bool AnyPeer { get; set; }

    /// <summary>It also runs on the game that calls it, at once.</summary>
    public bool CallLocal { get; set; }

    /// <summary>It arrives, in order. Otherwise it may be lost, which suits what the next call replaces.</summary>
    public bool Reliable { get; set; } = true;
}

// A call on the network, as engine/src/net/Rpc.cpp writes it, and the same numbers for its types.
internal static class RpcCodec
{
    private const int MaxNameLength = 256;
    private const int MaxArguments = 32;
    private const byte BoolType = 0;
    private const byte WholeType = 1;
    private const byte DecimalType = 2;
    private const byte TextType = 3;
    private const byte Vec2Type = 4;
    private const byte Vec3Type = 5;
    private const byte Vec4Type = 6;
    private const byte QuatType = 7;
    private const byte UuidType = 8;
    private const byte BytesType = 9;

    public static bool CanCarry(Type type)
    {
        return type.IsEnum || type == typeof(bool) || type == typeof(sbyte) || type == typeof(byte) || type == typeof(short) ||
               type == typeof(ushort) || type == typeof(int) || type == typeof(uint) || type == typeof(long) || type == typeof(float) ||
               type == typeof(double) || type == typeof(string) || type == typeof(Vec2) || type == typeof(Vec3) || type == typeof(Vec4) ||
               type == typeof(Quat) || type == typeof(Uuid) || type == typeof(Entity) || type == typeof(byte[]);
    }

    /// <summary>A value of the game as a call carries it.</summary>
    public static object Carried(object? value)
    {
        return value switch
        {
            null => throw new ArgumentException("null cannot be sent"),
            bool or double or string or Vec2 or Vec3 or Vec4 or Quat or Uuid or byte[] => value,
            float number => (double)number,
            Entity entity => entity.IsValid ? entity.Uuid : Uuid.Nil,
            _ when CanCarry(value.GetType()) => Convert.ToInt64(value),
            _ => throw new ArgumentException($"a {value.GetType().Name} cannot be sent"),
        };
    }

    /// <summary>What a call carries as the type a method asks for; false when it is not one.</summary>
    public static bool TryConvert(object value, Type type, out object? converted)
    {
        converted = null;
        if (type.IsEnum)
        {
            if (!TryConvert(value, Enum.GetUnderlyingType(type), out object? number))
            {
                return false;
            }
            converted = Enum.ToObject(type, number!);
            return true;
        }
        switch (value)
        {
            case long whole when type == typeof(float):
                converted = (float)whole;
                return true;
            case long whole when type == typeof(double):
                converted = (double)whole;
                return true;
            case long whole when type != typeof(bool) && type.IsPrimitive:
                try
                {
                    converted = Convert.ChangeType(whole, type);
                    return true;
                }
                catch (OverflowException)
                {
                    return false;
                }
            case double number when type == typeof(float):
                converted = (float)number;
                return true;
            case Uuid uuid when type == typeof(Entity):
                converted = Scene.Current.Resolve(uuid);
                return true;
        }
        if (!type.IsInstanceOfType(value))
        {
            return false;
        }
        converted = value;
        return true;
    }

    /// <summary>Throws on what goes over the limits of a call.</summary>
    public static byte[] Encode(Uuid target, string name, object[] values)
    {
        byte[] text = Encoding.UTF8.GetBytes(name);
        if (text.Length is 0 or > MaxNameLength)
        {
            throw new ArgumentException($"a remote method has a name of 1 to {MaxNameLength} bytes");
        }
        if (values.Length > MaxArguments)
        {
            throw new ArgumentException($"a remote call has at most {MaxArguments} arguments");
        }
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        Span<byte> identifier = stackalloc byte[16];
        WriteUuid(writer, target, identifier);
        writer.Write((ushort)text.Length);
        writer.Write(text);
        writer.Write((byte)values.Length);
        foreach (object value in values)
        {
            WriteValue(writer, value, identifier);
        }
        writer.Flush();
        return stream.ToArray();
    }

    /// <summary>What comes from the network is not trusted: false on anything malformed.</summary>
    public static bool TryDecode(byte[] bytes, out Uuid target, out string name, out object[] values)
    {
        target = default;
        name = "";
        values = [];
        using var stream = new MemoryStream(bytes, false);
        using var reader = new BinaryReader(stream);
        try
        {
            target = ReadUuid(reader);
            int length = reader.ReadUInt16();
            if (length is 0 or > MaxNameLength)
            {
                return false;
            }
            name = Encoding.UTF8.GetString(ReadExactly(reader, length));
            int count = reader.ReadByte();
            if (count > MaxArguments)
            {
                return false;
            }
            values = new object[count];
            for (int index = 0; index < count; ++index)
            {
                values[index] = ReadValue(reader);
            }
            return true;
        }
        catch (Exception exception) when (exception is EndOfStreamException or FormatException)
        {
            return false;
        }
    }

    private static void WriteUuid(BinaryWriter writer, Uuid uuid, Span<byte> identifier)
    {
        MemoryMarshal.Write(identifier, in uuid);
        writer.Write(identifier);
    }

    private static void WriteValue(BinaryWriter writer, object value, Span<byte> identifier)
    {
        switch (value)
        {
            case bool flag:
                writer.Write(BoolType);
                writer.Write((byte)(flag ? 1 : 0));
                break;
            case long whole:
                writer.Write(WholeType);
                writer.Write(whole);
                break;
            case double number:
                writer.Write(DecimalType);
                writer.Write(number);
                break;
            case string text:
                writer.Write(TextType);
                WriteSized(writer, Encoding.UTF8.GetBytes(text));
                break;
            case Vec2 vec2:
                writer.Write(Vec2Type);
                writer.Write(vec2.X);
                writer.Write(vec2.Y);
                break;
            case Vec3 vec3:
                writer.Write(Vec3Type);
                writer.Write(vec3.X);
                writer.Write(vec3.Y);
                writer.Write(vec3.Z);
                break;
            case Vec4 vec4:
                writer.Write(Vec4Type);
                writer.Write(vec4.X);
                writer.Write(vec4.Y);
                writer.Write(vec4.Z);
                writer.Write(vec4.W);
                break;
            case Quat quat:
                writer.Write(QuatType);
                writer.Write(quat.X);
                writer.Write(quat.Y);
                writer.Write(quat.Z);
                writer.Write(quat.W);
                break;
            case Uuid uuid:
                writer.Write(UuidType);
                WriteUuid(writer, uuid, identifier);
                break;
            case byte[] raw:
                writer.Write(BytesType);
                WriteSized(writer, raw);
                break;
        }
    }

    private static void WriteSized(BinaryWriter writer, byte[] bytes)
    {
        writer.Write((uint)bytes.Length);
        writer.Write(bytes);
    }

    private static byte[] ReadExactly(BinaryReader reader, long length)
    {
        if (length > reader.BaseStream.Length - reader.BaseStream.Position)
        {
            throw new EndOfStreamException();
        }
        return reader.ReadBytes((int)length);
    }

    private static Uuid ReadUuid(BinaryReader reader)
    {
        return MemoryMarshal.Read<Uuid>(ReadExactly(reader, 16));
    }

    private static object ReadValue(BinaryReader reader)
    {
        byte type = reader.ReadByte();
        return type switch
        {
            BoolType => reader.ReadByte() != 0,
            WholeType => reader.ReadInt64(),
            DecimalType => reader.ReadDouble(),
            TextType => Encoding.UTF8.GetString(ReadExactly(reader, reader.ReadUInt32())),
            Vec2Type => new Vec2(reader.ReadSingle(), reader.ReadSingle()),
            Vec3Type => new Vec3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
            Vec4Type => new Vec4(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
            QuatType => new Quat(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
            UuidType => ReadUuid(reader),
            BytesType => ReadExactly(reader, reader.ReadUInt32()),
            _ => throw new FormatException(),
        };
    }
}

internal static unsafe partial class GameRuntime
{
    private sealed class RemoteMethod(ComponentTypeInfo info, MethodInfo method, RpcAttribute policy)
    {
        private readonly ParameterInfo[] _parameters = method.GetParameters();

        public ComponentTypeInfo Info { get; } = info;
        public MethodInfo Method { get; } = method;
        public RpcAttribute Policy { get; } = policy;

        /// <summary>The arguments of the method from what a call carries, null when they are not those it takes.</summary>
        public object?[]? Convert(object[] values)
        {
            if (values.Length != _parameters.Length)
            {
                return null;
            }
            var arguments = new object?[values.Length];
            for (int index = 0; index < values.Length; ++index)
            {
                if (!RpcCodec.TryConvert(values[index], _parameters[index].ParameterType, out arguments[index]))
                {
                    return null;
                }
            }
            return arguments;
        }

        public string Signature => $"{Method.Name}({string.Join(", ", _parameters.Select(parameter => parameter.ParameterType.Name))})";
    }

    private static readonly Dictionary<string, RemoteMethod> Rpcs = [];
    private static bool _rpcsDeclared;

    private static void AddRpcs(ComponentTypeInfo info)
    {
        const BindingFlags methods = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        foreach (MethodInfo method in info.Type.GetMethods(methods))
        {
            if (method.GetCustomAttribute<RpcAttribute>() is not { } policy)
            {
                continue;
            }
            string name = $"{info.Name}.{method.Name}";
            ParameterInfo? refused = method.GetParameters().FirstOrDefault(parameter => !RpcCodec.CanCarry(parameter.ParameterType));
            if (refused != null)
            {
                Log.Warning($"{name} is skipped: a remote call cannot carry a {refused.ParameterType.Name}");
            }
            else if (method.IsGenericMethod)
            {
                Log.Warning($"{name} is skipped: a remote method cannot be generic");
            }
            else if (!Rpcs.TryAdd(name, new RemoteMethod(info, method, policy)))
            {
                Log.Warning($"Two remote methods are named {name}: only the first one is used");
            }
        }
    }

    // Once for each network of a game and each load of its code, when a phase gives the network.
    private static void DeclareRpcs()
    {
        if (_rpcsDeclared)
        {
            return;
        }
        _rpcsDeclared = true;
        foreach ((string name, RemoteMethod rpc) in Rpcs)
        {
            nint text = Marshal.StringToCoTaskMemUTF8(name);
            try
            {
                int flags = (rpc.Policy.AnyPeer ? 1 : 0) | (rpc.Policy.CallLocal ? 2 : 0) | (rpc.Policy.Reliable ? 4 : 0);
                Bootstrap.Native.MultiplayerDeclareRpc((byte*)text, flags);
            }
            finally
            {
                Marshal.FreeCoTaskMem(text);
            }
        }
    }

    internal static void SendRpc(Component component, uint to, string method, object?[]? arguments)
    {
        string name = $"{(TypesByType.TryGetValue(component.GetType(), out ComponentTypeInfo? info) ? info.Name : component.GetType().Name)}.{method}";
        RemoteMethod? rpc = Rpcs.GetValueOrDefault(name);
        object?[]? converted = null;
        byte[] bytes;
        try
        {
            object[] values = (arguments ?? [null]).Select(RpcCodec.Carried).ToArray();
            if (rpc != null)
            {
                converted = rpc.Convert(values);
                if (converted == null)
                {
                    Log.Error($"Cannot call {name} with ({string.Join(", ", values.Select(value => value.GetType().Name))}): it takes {rpc.Signature}");
                    return;
                }
            }
            bytes = RpcCodec.Encode(component.Entity.Uuid, name, values);
        }
        catch (Exception exception) when (exception is ArgumentException or OverflowException)
        {
            Log.Error($"Cannot call {name}: {exception.Message}");
            return;
        }
        fixed (byte* first = bytes)
        {
            if (Bootstrap.Native.MultiplayerSendRpc(to, first, bytes.Length) == 0)
            {
                return;
            }
            if (rpc == null)
            {
                // A method of C++, which runs when its systems are in charge of the scene.
                Bootstrap.Native.MultiplayerLoopbackRpc(first, bytes.Length);
                return;
            }
        }
        RunRpc(rpc, component, converted!, Multiplayer.Caller);
    }

    // A call that arrived, which the engine has accepted. Those it does not know here are methods of C++.
    internal static void ReceiveRpc(int index, uint sender)
    {
        int size = Bootstrap.Native.MultiplayerEventBytes(index, null, 0);
        var bytes = new byte[size];
        fixed (byte* first = bytes)
        {
            Bootstrap.Native.MultiplayerEventBytes(index, first, size);
        }
        if (!RpcCodec.TryDecode(bytes, out Uuid target, out string name, out object[] values))
        {
            Log.Warning($"Refused a malformed remote call from player {sender}");
            return;
        }
        if (!Rpcs.TryGetValue(name, out RemoteMethod? rpc))
        {
            return;
        }
        Entity entity = SceneView.Resolve(target);
        Component? instance = entity.IsValid ? GetInstance(rpc.Info.Type, entity) : null;
        if (instance == null)
        {
            Log.Warning($"The remote call {name} from player {sender} is for an entity this game does not have, or without {rpc.Info.Name}");
            return;
        }
        object?[]? arguments = rpc.Convert(values);
        if (arguments == null)
        {
            Log.Error($"The remote call {name} from player {sender} does not carry what {rpc.Signature} takes");
            return;
        }
        RunRpc(rpc, instance, arguments, sender);
    }

    private static void RunRpc(RemoteMethod rpc, Component instance, object?[] arguments, uint sender)
    {
        // Back to what they were after: a method called at once runs inside the Update that calls it.
        uint previousSender = Multiplayer.RemoteSender;
        Component? previousOwner = Coroutines.CurrentOwner;
        Multiplayer.RemoteSender = sender;
        Call(rpc.Info, instance, rpc.Method.Name, (target, _) => rpc.Method.Invoke(target, arguments), 0.0f);
        Multiplayer.RemoteSender = previousSender;
        Coroutines.CurrentOwner = previousOwner;
    }
}
