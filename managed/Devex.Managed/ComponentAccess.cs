using System.ComponentModel;
using Devex.Internal;

namespace Devex;

/// <summary>
/// A view of a component written in C++, generated from its reflection: a ref struct over the
/// component's memory, whose properties read and write its fields in place. It is only valid during
/// the call that got it, until components are added to or removed from the scene.
/// </summary>
public unsafe interface IComponentView<TSelf> where TSelf : IComponentView<TSelf>, allows ref struct
{
    /// <summary>The name of the component type in the engine.</summary>
    static abstract string TypeName { get; }

    /// <summary>What the view was generated for, checked against the running engine before any access.</summary>
    static abstract ulong LayoutHash { get; }

    /// <summary>The view of the component at this address.</summary>
    static abstract TSelf At(void* component);
}

/// <summary>The C# components of an entity.</summary>
public static class ManagedComponentAccess
{
    /// <summary>The C# component of the entity, or null when it has none.</summary>
    public static T? Get<T>(this Entity entity) where T : Component => GameRuntime.GetInstance(typeof(T), entity) as T;

    public static bool Has<T>(this Entity entity) where T : Component => GameRuntime.GetInstance(typeof(T), entity) != null;

    /// <summary>Adds the C# component with its default values, or returns the one the entity has.</summary>
    public static T Add<T>(this Entity entity) where T : Component => (T)GameRuntime.AddComponent(typeof(T), entity);

    public static void Remove<T>(this Entity entity) where T : Component => GameRuntime.RemoveComponent(typeof(T), entity);
}

/// <summary>The components of an entity written in C++: those of the engine and of the game's C++ code.</summary>
public static unsafe class NativeComponentAccess
{
    /// <summary>The component of the entity, changed in place. Throws when the entity has none.</summary>
    public static T Get<T>(this Entity entity) where T : IComponentView<T>, allows ref struct
    {
        void* component = Bootstrap.Native.FindComponent(Scene.Current.Pointer, ViewSupport.IndexOf<T>(), entity);
        if (component == null)
        {
            throw new InvalidOperationException($"'{entity.Name}' has no {T.TypeName}");
        }
        return T.At(component);
    }

    /// <summary>The component of the entity, when it has one.</summary>
    public static bool TryGet<T>(this Entity entity, out T view) where T : IComponentView<T>, allows ref struct
    {
        void* component = Bootstrap.Native.FindComponent(Scene.Current.Pointer, ViewSupport.IndexOf<T>(), entity);
        view = component != null ? T.At(component) : default!;
        return component != null;
    }

    public static bool Has<T>(this Entity entity) where T : IComponentView<T>, allows ref struct
        => Bootstrap.Native.FindComponent(Scene.Current.Pointer, ViewSupport.IndexOf<T>(), entity) != null;

    /// <summary>Adds the component with its default values, or returns the one the entity has.</summary>
    public static T Add<T>(this Entity entity) where T : IComponentView<T>, allows ref struct
    {
        void* component = Bootstrap.Native.AddComponent(Scene.Current.Pointer, ViewSupport.IndexOf<T>(), entity);
        if (component == null)
        {
            throw new InvalidOperationException($"cannot add {T.TypeName} to '{entity.Name}'");
        }
        return T.At(component);
    }

    public static void Remove<T>(this Entity entity) where T : IComponentView<T>, allows ref struct
        => Bootstrap.Native.RemoveComponent(Scene.Current.Pointer, ViewSupport.IndexOf<T>(), entity);
}

/// <summary>A list field of a component written in C++, edited in place.</summary>
public readonly unsafe ref struct NativeList<T> where T : unmanaged
{
    private readonly void* _component;
    private readonly nuint _type;
    private readonly int _field;

    [EditorBrowsable(EditorBrowsableState.Never)]
    public NativeList(void* component, nuint type, int field)
    {
        _component = component;
        _type = type;
        _field = field;
    }

    public int Count => (int)Bootstrap.Native.ListSize(_type, _field, _component);

    public ref T this[int index] => ref *(T*)ViewSupport.CheckedElement(_type, _field, _component, index);

    public void Add(T value)
    {
        int count = Count;
        Bootstrap.Native.ListResize(_type, _field, _component, (nuint)(count + 1));
        this[count] = value;
    }

    public void Insert(int index, T value)
    {
        ViewSupport.CheckInsert(index, Count);
        Bootstrap.Native.ListInsert(_type, _field, _component, (nuint)index);
        this[index] = value;
    }

    public void RemoveAt(int index)
    {
        ViewSupport.CheckedElement(_type, _field, _component, index);
        Bootstrap.Native.ListErase(_type, _field, _component, (nuint)index);
    }

    public void Clear() => Bootstrap.Native.ListResize(_type, _field, _component, 0);

    /// <summary>The elements, valid until the list changes size.</summary>
    public Span<T> AsSpan()
    {
        int count = Count;
        return count == 0 ? Span<T>.Empty : new Span<T>(Bootstrap.Native.ListElement(_type, _field, _component, 0), count);
    }

    public Span<T>.Enumerator GetEnumerator() => AsSpan().GetEnumerator();

    public T[] ToArray() => AsSpan().ToArray();
}

/// <summary>A list of strings of a component written in C++, edited in place.</summary>
public readonly unsafe ref struct NativeStringList
{
    private readonly void* _component;
    private readonly nuint _type;
    private readonly int _field;

    [EditorBrowsable(EditorBrowsableState.Never)]
    public NativeStringList(void* component, nuint type, int field)
    {
        _component = component;
        _type = type;
        _field = field;
    }

    public int Count => (int)Bootstrap.Native.ListSize(_type, _field, _component);

    public string this[int index]
    {
        get => ViewSupport.ReadString(ViewSupport.CheckedElement(_type, _field, _component, index));
        set => ViewSupport.WriteString(ViewSupport.CheckedElement(_type, _field, _component, index), value);
    }

    public void Add(string value)
    {
        int count = Count;
        Bootstrap.Native.ListResize(_type, _field, _component, (nuint)(count + 1));
        this[count] = value;
    }

    public void Insert(int index, string value)
    {
        ViewSupport.CheckInsert(index, Count);
        Bootstrap.Native.ListInsert(_type, _field, _component, (nuint)index);
        this[index] = value;
    }

    public void RemoveAt(int index)
    {
        ViewSupport.CheckedElement(_type, _field, _component, index);
        Bootstrap.Native.ListErase(_type, _field, _component, (nuint)index);
    }

    public void Clear() => Bootstrap.Native.ListResize(_type, _field, _component, 0);

    public string[] ToArray()
    {
        var values = new string[Count];
        for (int index = 0; index < values.Length; ++index)
        {
            values[index] = this[index];
        }
        return values;
    }
}

/// <summary>A list of entities of a component written in C++, edited in place.</summary>
public readonly unsafe ref struct NativeEntityList
{
    private readonly void* _component;
    private readonly nuint _type;
    private readonly int _field;

    [EditorBrowsable(EditorBrowsableState.Never)]
    public NativeEntityList(void* component, nuint type, int field)
    {
        _component = component;
        _type = type;
        _field = field;
    }

    public int Count => (int)Bootstrap.Native.ListSize(_type, _field, _component);

    /// <summary>The entity of an element; None when it is empty or its entity is gone.</summary>
    public Entity this[int index]
    {
        get => ViewSupport.ReadEntity(ViewSupport.CheckedElement(_type, _field, _component, index));
        set => ViewSupport.WriteEntity(ViewSupport.CheckedElement(_type, _field, _component, index), value);
    }

    public void Add(Entity value)
    {
        int count = Count;
        Bootstrap.Native.ListResize(_type, _field, _component, (nuint)(count + 1));
        this[count] = value;
    }

    public void Insert(int index, Entity value)
    {
        ViewSupport.CheckInsert(index, Count);
        Bootstrap.Native.ListInsert(_type, _field, _component, (nuint)index);
        this[index] = value;
    }

    public void RemoveAt(int index)
    {
        ViewSupport.CheckedElement(_type, _field, _component, index);
        Bootstrap.Native.ListErase(_type, _field, _component, (nuint)index);
    }

    public void Clear() => Bootstrap.Native.ListResize(_type, _field, _component, 0);

    public Entity[] ToArray()
    {
        var values = new Entity[Count];
        for (int index = 0; index < values.Length; ++index)
        {
            values[index] = this[index];
        }
        return values;
    }
}
