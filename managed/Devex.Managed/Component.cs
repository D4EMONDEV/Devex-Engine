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
/// inspector, and Start, Update and FixedUpdate run for every entity that has it. Its other fields
/// last as long as the component, reloads of the code included.
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

    /// <summary>Called, before Update, when the body of the entity started touching another one.</summary>
    public virtual void OnCollisionEnter(Entity other)
    {
    }

    /// <summary>Called, before Update, when the body of the entity stopped touching another one.</summary>
    public virtual void OnCollisionExit(Entity other)
    {
    }

    /// <summary>Called, before Update, when another body entered a trigger of the entity, or the entity entered a trigger.</summary>
    public virtual void OnTriggerEnter(Entity other)
    {
    }

    /// <summary>Called, before Update, when another body left a trigger of the entity, or the entity left a trigger.</summary>
    public virtual void OnTriggerExit(Entity other)
    {
    }

    /// <summary>The Transform of the entity, changed in place.</summary>
    public ref Transform Transform => ref Entity.Transform;

    /// <summary>Destroys the entity and its children.</summary>
    public void DestroyEntity() => Entity.Destroy();

    // Whether Start ran, or the component came back from a reload of the code.
    internal bool Started;
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

/// <summary>A uint field that chooses one of the audio groups of the project.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class AudioGroupAttribute : Attribute;

/// <summary>
/// An AssetId field limited to one kind of asset: "mesh", "material", "texture", "model", "scene" or "audio".
/// </summary>
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

    /// <summary>The scene that plays, which entities and components refer to.</summary>
    public static Scene Current => GameRuntime.CurrentScene;

    internal void Bind(void* scene) => _scene = scene;

    internal void* Pointer => _scene;

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

    /// <summary>The first entity with this name, or None.</summary>
    public Entity Find(string name)
    {
        using var text = new Utf8Buffer(name);
        return Bootstrap.Native.FindEntity(_scene, text.Pointer);
    }

    /// <summary>The entity with this UUID, or None.</summary>
    public Entity Resolve(Uuid uuid) => Bootstrap.Native.ResolveEntity(_scene, &uuid);

    /// <summary>The UUID of an entity; nil when it is not alive.</summary>
    public Uuid UuidOf(Entity entity)
    {
        Uuid uuid;
        Bootstrap.Native.EntityUuid(_scene, entity, &uuid);
        return uuid;
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

    /// <summary>Moves an entity under a new parent, or to the roots when the parent is None.</summary>
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
    public T? GetComponent<T>(Entity entity) where T : Component => GameRuntime.GetInstance(typeof(T), entity) as T;

    /// <summary>Adds a C# component to an entity, or returns the one it already has.</summary>
    public T AddComponent<T>(Entity entity) where T : Component => (T)GameRuntime.AddComponent(typeof(T), entity);

    public void RemoveComponent<T>(Entity entity) where T : Component => GameRuntime.RemoveComponent(typeof(T), entity);

    /// <summary>Every C# component of this type in the scene.</summary>
    public IEnumerable<T> Components<T>() where T : Component => GameRuntime.Instances<T>();
}
