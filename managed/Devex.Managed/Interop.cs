using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Devex;

/// <summary>An entity of a scene: the same handle the engine uses.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity(uint index, uint generation) : IEquatable<Entity>
{
    public readonly uint Index = index;
    public readonly uint Generation = generation;

    public bool IsValid => Generation != 0;

    public bool Equals(Entity other) => Index == other.Index && Generation == other.Generation;

    public override bool Equals(object? other) => other is Entity entity && Equals(entity);

    public override int GetHashCode() => HashCode.Combine(Index, Generation);

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);

    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);

    internal ulong Key => Index | ((ulong)Generation << 32);
}

/// <summary>The engine functions the C# side calls. Filled by the engine when it starts the runtime.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeApi
{
    public delegate* unmanaged<int, byte*, void> Log;

    public delegate* unmanaged<void*, nuint, Entity**, int> ComponentEntities;
    public delegate* unmanaged<void*, nuint, Entity, void*> FindComponent;
    public delegate* unmanaged<void*, nuint, Entity, void*> AddComponent;
    public delegate* unmanaged<void*, nuint, Entity, void> RemoveComponent;
    public delegate* unmanaged<void*, nuint, byte*> ReadStringField;
    public delegate* unmanaged<void*, nuint, byte*, void> WriteStringField;

    public delegate* unmanaged<void*, byte*, Entity> CreateEntity;
    public delegate* unmanaged<void*, Entity, void> DestroyEntity;
    public delegate* unmanaged<void*, Entity, int> IsAlive;
    public delegate* unmanaged<void*, Entity, byte*> EntityName;
    public delegate* unmanaged<void*, Entity, byte*, void> SetEntityName;
    public delegate* unmanaged<void*, byte*, Entity> FindEntity;
    public delegate* unmanaged<void*, Entity, Entity> Parent;
    public delegate* unmanaged<void*, Entity, Entity> FirstChild;
    public delegate* unmanaged<void*, Entity, Entity> NextSibling;
    public delegate* unmanaged<void*, Entity, Entity, void> SetParent;
    public delegate* unmanaged<void*, Entity, void*> TransformOf;
    public delegate* unmanaged<void*, Entity, float*> WorldPositionOf;

    public delegate* unmanaged<int, int> IsKeyDown;
    public delegate* unmanaged<int, int> WasKeyPressed;
    public delegate* unmanaged<int, int> IsMouseButtonDown;
    public delegate* unmanaged<int, int> WasMouseButtonPressed;
    public delegate* unmanaged<float*, void> MouseDelta;
    public delegate* unmanaged<float*, void> MousePosition;
    public delegate* unmanaged<int> IsMouseCaptured;
    public delegate* unmanaged<int, void> SetMouseCaptured;
    public delegate* unmanaged<void> RequestQuit;
}

/// <summary>The C# functions the engine calls. Filled by the runtime when it starts.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ManagedApi
{
    public delegate* unmanaged<byte*, byte**, int> LoadGame;
    public delegate* unmanaged<void> UnloadGame;
    public delegate* unmanaged<byte*, nuint, nuint*, int, void> SetTypeLayout;
    public delegate* unmanaged<void*, int, float, void> RunPhase;
    public delegate* unmanaged<byte*, void*, void> ApplyDefaults;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BootstrapArguments
{
    public NativeApi* Native;
    public ManagedApi* Managed;
}

/// <summary>What the engine calls into: filling the function tables, then the game itself.</summary>
public static unsafe class Bootstrap
{
    internal static NativeApi Native;
    private static byte[]? _description;
    private static GCHandle _descriptionHandle;

    /// <summary>Entry point of the runtime, called once by the engine with the function tables.</summary>
    [UnmanagedCallersOnly]
    public static int Initialize(nint arguments, int size)
    {
        if (arguments == 0 || size < sizeof(BootstrapArguments))
        {
            return -1;
        }
        var bootstrap = (BootstrapArguments*)arguments;
        Native = *bootstrap->Native;
        bootstrap->Managed->LoadGame = &LoadGame;
        bootstrap->Managed->UnloadGame = &UnloadGame;
        bootstrap->Managed->SetTypeLayout = &SetTypeLayout;
        bootstrap->Managed->RunPhase = &RunPhase;
        bootstrap->Managed->ApplyDefaults = &ApplyDefaults;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int LoadGame(byte* assemblyPath, byte** description)
    {
        try
        {
            string path = Utf8.ToString(assemblyPath) ?? string.Empty;
            string text = GameRuntime.Load(path);
            // Pinned until the next load, so that the engine can read it after this call.
            ReleaseDescription();
            _description = Utf8.ToBytes(text);
            _descriptionHandle = GCHandle.Alloc(_description, GCHandleType.Pinned);
            *description = (byte*)_descriptionHandle.AddrOfPinnedObject();
            return 0;
        }
        catch (Exception exception)
        {
            Log.Error($"Cannot load the game code: {exception.Message}");
            return -1;
        }
    }

    [UnmanagedCallersOnly]
    private static void UnloadGame()
    {
        try
        {
            GameRuntime.Unload();
            ReleaseDescription();
        }
        catch (Exception exception)
        {
            Log.Error($"Cannot unload the game code: {exception.Message}");
        }
    }

    private static void ReleaseDescription()
    {
        if (_descriptionHandle.IsAllocated)
        {
            _descriptionHandle.Free();
        }
        _description = null;
    }

    [UnmanagedCallersOnly]
    private static void SetTypeLayout(byte* typeName, nuint typeIndex, nuint* offsets, int count)
    {
        var fieldOffsets = new int[count];
        for (int index = 0; index < count; ++index)
        {
            fieldOffsets[index] = (int)offsets[index];
        }
        GameRuntime.SetTypeLayout(Utf8.ToString(typeName) ?? string.Empty, typeIndex, fieldOffsets);
    }

    [UnmanagedCallersOnly]
    private static void RunPhase(void* scene, int phase, float delta)
    {
        GameRuntime.RunPhase(scene, (SystemPhase)phase, delta);
    }

    [UnmanagedCallersOnly]
    private static void ApplyDefaults(byte* typeName, void* component)
    {
        GameRuntime.ApplyDefaults(Utf8.ToString(typeName) ?? string.Empty, component);
    }
}

/// <summary>UTF-8 strings, as the engine passes them.</summary>
internal static unsafe class Utf8
{
    public static string? ToString(byte* text)
    {
        return text == null ? null : Marshal.PtrToStringUTF8((nint)text);
    }

    public static byte[] ToBytes(string text)
    {
        int count = Encoding.UTF8.GetByteCount(text);
        var bytes = new byte[count + 1];
        Encoding.UTF8.GetBytes(text, bytes);
        bytes[count] = 0;
        return bytes;
    }

}

/// <summary>A string as the engine reads it: zero-terminated UTF-8, freed at the end of the call.</summary>
internal readonly unsafe ref struct Utf8Buffer(string? text) : IDisposable
{
    private readonly nint _pointer = text == null ? 0 : Marshal.StringToCoTaskMemUTF8(text);

    public byte* Pointer => (byte*)_pointer;

    public void Dispose()
    {
        if (_pointer != 0)
        {
            Marshal.FreeCoTaskMem(_pointer);
        }
    }
}

/// <summary>The engine's log, which the editor shows in its output panel.</summary>
public static unsafe class Log
{
    public static void Debug(string message) => Write(1, message);

    public static void Info(string message) => Write(2, message);

    public static void Warning(string message) => Write(3, message);

    public static void Error(string message) => Write(4, message);

    private static void Write(int level, string message)
    {
        if (Bootstrap.Native.Log == null)
        {
            Console.WriteLine(message);
            return;
        }
        byte[] bytes = Utf8.ToBytes(message);
        fixed (byte* pointer = bytes)
        {
            Bootstrap.Native.Log(level, pointer);
        }
    }
}

/// <summary>When the systems and behaviours of a game run.</summary>
public enum SystemPhase
{
    /// <summary>Once when the game starts, and when a component appears.</summary>
    Start = 0,
    /// <summary>At the fixed update rate, before each physics step.</summary>
    FixedUpdate = 1,
    /// <summary>Once per frame.</summary>
    Update = 2,
}
