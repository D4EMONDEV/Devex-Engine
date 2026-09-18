using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Devex;

/// <summary>Position, rotation and scale of an entity, relative to its parent.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Transform
{
    public Vec3 Position;
    public Quat Rotation;
    public Vec3 Scale;
}

/// <summary>
/// A component of an entity, written in C#: its public fields are saved in scenes and edited in the
/// inspector, and Start, Update and FixedUpdate run for every entity that has it.
/// </summary>
public abstract class Component
{
    /// <summary>The entity this component belongs to.</summary>
    public Entity Entity { get; internal set; }

    /// <summary>The scene the entity lives in.</summary>
    public Scene Scene { get; internal set; } = null!;

    /// <summary>Called once, before the first update of the component.</summary>
    public virtual void Start()
    {
    }

    /// <summary>Called once per frame with the seconds since the previous frame.</summary>
    public virtual void Update(float delta)
    {
    }

    /// <summary>Called at the fixed update rate, before each physics step.</summary>
    public virtual void FixedUpdate(float delta)
    {
    }

    /// <summary>The Transform of the entity, changed in place.</summary>
    public ref Transform Transform => ref Scene.TransformOf(Entity);

    /// <summary>The C# component of the entity, or null when it has none.</summary>
    public T? Get<T>() where T : Component => Scene.GetComponent<T>(Entity);

    /// <summary>Destroys the entity and its children at the end of the frame's updates.</summary>
    public void DestroyEntity() => Scene.Destroy(Entity);
}

/// <summary>A float field shown in degrees, whose value is in radians.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class AngleAttribute : Attribute;

/// <summary>A Vec3 or Vec4 field holding a linear color, edited with a color picker.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class ColorAttribute : Attribute;

/// <summary>A uint field that chooses one of the collision layers of the project.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class PhysicsLayerAttribute : Attribute;

/// <summary>An AssetId field limited to one kind of asset: "mesh", "material", "texture", "model" or "scene".</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetTypeAttribute(string type) : Attribute
{
    public string Type { get; } = type;
}

/// <summary>A public field that is not saved and not shown in the inspector.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class HiddenAttribute : Attribute;

/// <summary>
/// A static method the engine runs for the whole scene at each phase, after the components of that
/// phase. It takes the scene, or nothing: <c>static void Spawn(Scene scene)</c>.
/// </summary>
[AttributeUsage(AttributeTargets.Method)]
public sealed class GameSystemAttribute(SystemPhase phase = SystemPhase.Update) : Attribute
{
    public SystemPhase Phase { get; } = phase;

    /// <summary>Systems run by increasing order, then in the order they were found.</summary>
    public int Order { get; init; }
}

/// <summary>The scene the game plays: its entities, their hierarchy and their components.</summary>
public sealed unsafe class Scene
{
    private void* _scene;

    internal Scene(void* scene)
    {
        _scene = scene;
    }

    internal void Bind(void* scene) => _scene = scene;

    /// <summary>Creates an entity without a parent.</summary>
    public Entity CreateEntity(string name)
    {
        using var text = new Utf8Buffer(name);
        return Bootstrap.Native.CreateEntity(_scene, text.Pointer);
    }

    /// <summary>Destroys an entity and its descendants.</summary>
    public void Destroy(Entity entity) => Bootstrap.Native.DestroyEntity(_scene, entity);

    public bool IsAlive(Entity entity) => Bootstrap.Native.IsAlive(_scene, entity) != 0;

    public string Name(Entity entity) => Utf8.ToString(Bootstrap.Native.EntityName(_scene, entity)) ?? string.Empty;

    public void SetName(Entity entity, string name)
    {
        using var text = new Utf8Buffer(name);
        Bootstrap.Native.SetEntityName(_scene, entity, text.Pointer);
    }

    /// <summary>The first entity with this name, or an invalid entity.</summary>
    public Entity Find(string name)
    {
        using var text = new Utf8Buffer(name);
        return Bootstrap.Native.FindEntity(_scene, text.Pointer);
    }

    public Entity Parent(Entity entity) => Bootstrap.Native.Parent(_scene, entity);

    public Entity FirstChild(Entity entity) => Bootstrap.Native.FirstChild(_scene, entity);

    public Entity NextSibling(Entity entity) => Bootstrap.Native.NextSibling(_scene, entity);

    /// <summary>The children of an entity, in order.</summary>
    public IEnumerable<Entity> Children(Entity entity)
    {
        for (Entity child = FirstChild(entity); child.IsValid; child = NextSibling(child))
        {
            yield return child;
        }
    }

    /// <summary>Moves an entity under a new parent, or to the roots when the parent is invalid.</summary>
    public void SetParent(Entity child, Entity parent) => Bootstrap.Native.SetParent(_scene, child, parent);

    /// <summary>The Transform of an entity, changed in place. The entity must have one.</summary>
    public ref Transform TransformOf(Entity entity)
    {
        void* transform = Bootstrap.Native.TransformOf(_scene, entity);
        if (transform == null)
        {
            throw new InvalidOperationException($"the entity '{Name(entity)}' has no Transform");
        }
        return ref Unsafe.AsRef<Transform>(transform);
    }

    public bool HasTransform(Entity entity) => Bootstrap.Native.TransformOf(_scene, entity) != null;

    /// <summary>Where the entity is in the world, once the engine has updated the transforms.</summary>
    public Vec3 WorldPosition(Entity entity)
    {
        float* position = Bootstrap.Native.WorldPositionOf(_scene, entity);
        return position == null ? Vec3.Zero : new Vec3(position[0], position[1], position[2]);
    }

    /// <summary>The C# component of an entity, or null.</summary>
    public T? GetComponent<T>(Entity entity) where T : Component => GameRuntime.FindInstance(typeof(T), entity) as T;

    /// <summary>Adds a C# component to an entity, or returns the one it already has.</summary>
    public T AddComponent<T>(Entity entity) where T : Component
    {
        if (GameRuntime.FindInstance(typeof(T), entity) is T existing)
        {
            return existing;
        }
        return (T)GameRuntime.AddComponent(this, _scene, typeof(T), entity);
    }

    public void RemoveComponent<T>(Entity entity) where T : Component
        => GameRuntime.RemoveComponent(_scene, typeof(T), entity);

    /// <summary>Every entity of the scene that has this C# component.</summary>
    public IEnumerable<T> Components<T>() where T : Component => GameRuntime.Instances<T>();

    internal void* Pointer => _scene;
}
