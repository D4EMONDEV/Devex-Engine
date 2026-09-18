using System.Runtime.InteropServices;
using System.Text;

namespace Devex;

/// <summary>
/// The engine functions the C# side calls, in the order of NativeApi in the engine's ManagedGame.cpp.
/// Filled by the engine when it starts the runtime.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeApi
{
    public delegate* unmanaged<int, byte*, void> Log;

    public delegate* unmanaged<void*, nuint, Entity**, int> ComponentEntities;
    public delegate* unmanaged<void*, nuint, Entity, void*> FindComponent;
    public delegate* unmanaged<void*, nuint, Entity, void*> AddComponent;
    public delegate* unmanaged<void*, nuint, Entity, void> RemoveComponent;
    public delegate* unmanaged<byte*, long> ComponentIndex;
    public delegate* unmanaged<ulong> RegistryGeneration;
    public delegate* unmanaged<nuint, ulong> ComponentLayoutHash;
    public delegate* unmanaged<void*, byte*> ReadString;
    public delegate* unmanaged<void*, byte*, void> WriteString;
    public delegate* unmanaged<nuint, int, void*, nuint> ListSize;
    public delegate* unmanaged<nuint, int, void*, nuint, void*> ListElement;
    public delegate* unmanaged<nuint, int, void*, nuint, void> ListResize;
    public delegate* unmanaged<nuint, int, void*, nuint, void> ListInsert;
    public delegate* unmanaged<nuint, int, void*, nuint, void> ListErase;
    public delegate* unmanaged<void*, Uuid*, Entity> ResolveEntity;
    public delegate* unmanaged<void*, Entity, Uuid*, void> EntityUuid;

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
    public delegate* unmanaged<Uuid*, void> LoadScene;
    public delegate* unmanaged<void*, Uuid*, Entity, Entity> InstantiatePrefab;
    public delegate* unmanaged<byte*, Uuid*, int> FindAsset;
    public delegate* unmanaged<float*, void> WindowSize;

    public delegate* unmanaged<Vec3*, Vec3*, float, uint, Entity, RayHit*, int> Raycast;
    public delegate* unmanaged<Vec3*, float, Vec3*, float, uint, Entity, RayHit*, int> SphereCast;
    public delegate* unmanaged<Vec3*, float, uint, Entity, Entity**, int> OverlapSphere;
    public delegate* unmanaged<Entity, Vec3*, void> AddForce;
    public delegate* unmanaged<Entity, Vec3*, void> AddTorque;
    public delegate* unmanaged<Entity, Vec3*, void> AddImpulse;
    public delegate* unmanaged<Entity, Vec3*, Vec3*, void> AddImpulseAt;
    public delegate* unmanaged<Contact**, int> Contacts;
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
    public delegate* unmanaged<int> IsDebuggerAttached;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BootstrapArguments
{
    // Changes with the function tables, so that an engine and a runtime of different builds refuse
    // each other.
    public int Version;
    public NativeApi* Native;
    public ManagedApi* Managed;
}

/// <summary>What the engine calls into: filling the function tables, then the game itself.</summary>
public static unsafe class Bootstrap
{
    internal const int Version = 2;

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
        if (bootstrap->Version != Version)
        {
            return -2;
        }
        Native = *bootstrap->Native;
        bootstrap->Managed->LoadGame = &LoadGame;
        bootstrap->Managed->UnloadGame = &UnloadGame;
        bootstrap->Managed->SetTypeLayout = &SetTypeLayout;
        bootstrap->Managed->RunPhase = &RunPhase;
        bootstrap->Managed->ApplyDefaults = &ApplyDefaults;
        bootstrap->Managed->IsDebuggerAttached = &IsDebuggerAttached;
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
            Log.Error($"Cannot load the game code: {exception}");
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
            Log.Error($"Cannot unload the game code: {exception}");
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
        try
        {
            var fieldOffsets = new int[count];
            for (int index = 0; index < count; ++index)
            {
                fieldOffsets[index] = (int)offsets[index];
            }
            GameRuntime.SetTypeLayout(Utf8.ToString(typeName) ?? string.Empty, typeIndex, fieldOffsets);
        }
        catch (Exception exception)
        {
            Log.Error($"Cannot bind the fields of a component: {exception}");
        }
    }

    [UnmanagedCallersOnly]
    private static void RunPhase(void* scene, int phase, float delta)
    {
        try
        {
            GameRuntime.RunPhase(scene, (SystemPhase)phase, delta);
        }
        catch (Exception exception)
        {
            // The runtime catches the errors of the game; this is one of the runtime itself.
            Log.Error($"The C# runtime failed: {exception}");
        }
    }

    [UnmanagedCallersOnly]
    private static void ApplyDefaults(byte* typeName, void* component)
    {
        try
        {
            GameRuntime.ApplyDefaults(Utf8.ToString(typeName) ?? string.Empty, component);
        }
        catch (Exception exception)
        {
            Log.Error($"Cannot give a component its default values: {exception}");
        }
    }

    [UnmanagedCallersOnly]
    private static int IsDebuggerAttached() => System.Diagnostics.Debugger.IsAttached ? 1 : 0;
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
