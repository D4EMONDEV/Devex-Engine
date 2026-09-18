using System.ComponentModel;

namespace Devex.Internal;

/// <summary>What generated views call. Not meant for game code.</summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public static unsafe class ViewSupport
{
    /// <summary>The index of the component type in the running engine, checked against the view's layout.</summary>
    public static nuint IndexOf<T>() where T : IComponentView<T>, allows ref struct => ViewIndex<T>.Get();

    public static string ReadString(void* address) => Utf8.ToString(Bootstrap.Native.ReadString(address)) ?? string.Empty;

    public static void WriteString(void* address, string? value)
    {
        using var text = new Utf8Buffer(value ?? string.Empty);
        Bootstrap.Native.WriteString(address, text.Pointer);
    }

    public static Entity ReadEntity(void* address) => Bootstrap.Native.ResolveEntity(Scene.Current.Pointer, (Uuid*)address);

    public static void WriteEntity(void* address, Entity entity)
        => Bootstrap.Native.EntityUuid(Scene.Current.Pointer, entity, (Uuid*)address);

    internal static void* CheckedElement(nuint type, int field, void* component, int index)
    {
        int count = (int)Bootstrap.Native.ListSize(type, field, component);
        if ((uint)index >= (uint)count)
        {
            throw new ArgumentOutOfRangeException(nameof(index), $"index {index} is outside a list of {count} elements");
        }
        return Bootstrap.Native.ListElement(type, field, component, (nuint)index);
    }

    internal static void CheckInsert(int index, int count)
    {
        if ((uint)index > (uint)count)
        {
            throw new ArgumentOutOfRangeException(nameof(index), $"cannot insert at {index} in a list of {count} elements");
        }
    }
}

// The index of a view's type, found by name and checked against the view's layout whenever the
// registry of the engine changes, as when the game's C++ code reloads.
internal static unsafe class ViewIndex<T> where T : IComponentView<T>, allows ref struct
{
    private static ulong _generation = ulong.MaxValue;
    private static nuint _index;

    public static nuint Get()
    {
        ulong generation = Bootstrap.Native.RegistryGeneration();
        if (generation == _generation)
        {
            return _index;
        }
        long index;
        using (var name = new Utf8Buffer(T.TypeName))
        {
            index = Bootstrap.Native.ComponentIndex(name.Pointer);
        }
        if (index < 0)
        {
            throw new InvalidOperationException($"{T.TypeName} is not a component of the engine or of the game's C++ code");
        }
        if (Bootstrap.Native.ComponentLayoutHash((nuint)index) != T.LayoutHash)
        {
            throw new InvalidOperationException(
                $"{T.TypeName} changed in C++ since the C# code was built: it is being built again");
        }
        _index = (nuint)index;
        _generation = generation;
        return _index;
    }
}
