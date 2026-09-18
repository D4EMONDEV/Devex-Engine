using System.Runtime.InteropServices;

namespace Devex;

/// <summary>
/// An entity of the scene that plays: the handle the engine uses. It becomes invalid when the entity is
/// destroyed, which IsAlive tells. Its components are reached with Get, Has, Add and Remove:
/// <c>entity.Get&lt;RigidBody&gt;()</c> for the components of the engine and of the game's C++ code,
/// <c>entity.Get&lt;Door&gt;()</c> for a C# component.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity(uint index, uint generation) : IEquatable<Entity>
{
    public readonly uint Index = index;
    public readonly uint Generation = generation;

    /// <summary>No entity.</summary>
    public static Entity None => default;

    /// <summary>Whether the handle names an entity, alive or not. Live entities never have generation 0.</summary>
    public bool IsValid => Generation != 0 && Index != uint.MaxValue;

    /// <summary>Whether the entity still exists in the scene.</summary>
    public bool IsAlive => IsValid && Scene.Current.IsAlive(this);

    public string Name
    {
        get => Scene.Current.Name(this);
        set => Scene.Current.SetName(this, value);
    }

    /// <summary>The Transform of the entity, changed in place.</summary>
    public ref Transform Transform => ref Scene.Current.TransformOf(this);

    public bool HasTransform => Scene.Current.HasTransform(this);

    /// <summary>Where the entity is in the world, as of the last update of the transforms.</summary>
    public Vec3 WorldPosition => Scene.Current.WorldPosition(this);

    /// <summary>The parent of the entity; None for a root.</summary>
    public Entity Parent => Scene.Current.Parent(this);

    /// <summary>The children of the entity, in order.</summary>
    public IEnumerable<Entity> Children => Scene.Current.Children(this);

    /// <summary>The UUID of the entity, which scenes and entity fields refer to.</summary>
    public Uuid Uuid => Scene.Current.UuidOf(this);

    /// <summary>Moves the entity under a new parent, or to the roots with None, keeping its local transform.</summary>
    public void SetParent(Entity parent) => Scene.Current.SetParent(this, parent);

    /// <summary>Destroys the entity and its descendants.</summary>
    public void Destroy() => Scene.Current.Destroy(this);

    /// <summary>Creates an entity at the root of the scene.</summary>
    public static Entity Create(string name) => Scene.Current.CreateEntity(name);

    /// <summary>The first entity with this name, depth first; None when there is none.</summary>
    public static Entity Find(string name) => Scene.Current.Find(name);

    // The engine writes no entity with another index than C#'s None: both are no entity.
    public bool Equals(Entity other) => IsValid ? Index == other.Index && Generation == other.Generation : !other.IsValid;

    public override bool Equals(object? other) => other is Entity entity && Equals(entity);

    public override int GetHashCode() => IsValid ? HashCode.Combine(Index, Generation) : 0;

    public override string ToString() => IsValid ? $"Entity({Index}, {Generation})" : "Entity(None)";

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);

    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);

    internal ulong Key => Index | ((ulong)Generation << 32);
}
