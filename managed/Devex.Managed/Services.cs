using System.Runtime.InteropServices;

namespace Devex;

/// <summary>The game as a whole: quitting it and changing its scene.</summary>
public static unsafe class Game
{
    /// <summary>Ends the game: the player quits, and the editor stops playing.</summary>
    public static void Quit() => Bootstrap.Native.RequestQuit();

    /// <summary>Replaces the scene once the updates of the frame are done; the new scene starts over.</summary>
    public static void LoadScene(AssetId scene)
    {
        Uuid uuid = scene.Uuid;
        Bootstrap.Native.LoadScene(&uuid);
    }

    /// <summary>Replaces the scene by its path, such as "res://assets/scenes/arena.dvxscene".</summary>
    public static void LoadScene(string path) => LoadScene(Assets.Find(path) ?? throw new ArgumentException($"no scene at {path}"));
}

/// <summary>Time of the game.</summary>
public static class Time
{
    /// <summary>Seconds since the previous frame during Update, the fixed step during FixedUpdate.</summary>
    public static float Delta { get; internal set; }

    /// <summary>Seconds of game time since the scene started.</summary>
    public static double Elapsed { get; internal set; }

    /// <summary>Frames since the scene started.</summary>
    public static long Frame { get; internal set; }
}

/// <summary>The window the game draws in.</summary>
public static unsafe class Screen
{
    /// <summary>Its size in pixels.</summary>
    public static Vec2 Size
    {
        get
        {
            float* values = stackalloc float[2];
            Bootstrap.Native.WindowSize(values);
            return new Vec2(values[0], values[1]);
        }
    }
}

/// <summary>The assets of the game, which fields refer to by AssetId.</summary>
public static unsafe class Assets
{
    /// <summary>
    /// The asset of a source file, such as "res://assets/prefabs/crate.dvxscene"; null when there is none.
    /// An exported game only has the assets its scenes need and those of the folders the project always
    /// exports: assets found by path belong in those folders, or in a field of a component.
    /// </summary>
    public static AssetId? Find(string path)
    {
        Uuid uuid;
        using var text = new Utf8Buffer(path);
        return Bootstrap.Native.FindAsset(text.Pointer, &uuid) != 0 ? new AssetId(uuid) : null;
    }
}

/// <summary>Prefabs placed by the game while it plays.</summary>
public static unsafe class Prefabs
{
    /// <summary>A new instance of the prefab, under a parent or at the root; None when it cannot be loaded.</summary>
    public static Entity Instantiate(AssetId prefab, Entity parent = default)
    {
        Uuid uuid = prefab.Uuid;
        return Bootstrap.Native.InstantiatePrefab(Scene.Current.Pointer, &uuid, parent);
    }

    /// <summary>A new instance of the prefab, placed at a position of its parent (of the world at the root).</summary>
    public static Entity Instantiate(AssetId prefab, Vec3 position, Entity parent = default)
    {
        Entity instance = Instantiate(prefab, parent);
        if (instance.IsValid && instance.HasTransform)
        {
            instance.Transform.Position = position;
        }
        return instance;
    }
}

/// <summary>What a ray or a moving sphere hit.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct RayHit
{
    /// <summary>The entity of the body hit: the entity of its RigidBody, or of its collider.</summary>
    public Entity Entity;
    public Vec3 Point;
    public Vec3 Normal;
    public float Distance;
}

/// <summary>Whether two bodies started or stopped touching.</summary>
public enum ContactPhase : byte
{
    Begin = 0,
    End = 1,
}

/// <summary>Two bodies that started or stopped touching during the frame.</summary>
[StructLayout(LayoutKind.Explicit, Size = 24)]
public readonly struct Contact
{
    [FieldOffset(0)] public readonly ContactPhase Phase;
    [FieldOffset(4)] public readonly Entity First;
    [FieldOffset(12)] public readonly Entity Second;
    /// <summary>At least one of the bodies is a trigger: nothing was blocked, something entered or left.</summary>
    [FieldOffset(20)] public readonly bool Trigger;

    public bool Involves(Entity entity) => First == entity || Second == entity;

    /// <summary>The entity touching the given one.</summary>
    public Entity Other(Entity entity) => First == entity ? Second : First;
}

/// <summary>The simulation of the scene: queries, forces, and the contacts of the frame.</summary>
public static unsafe class Physics
{
    /// <summary>A mask of every collision layer.</summary>
    public const uint AllLayers = 0xFFFF;

    /// <summary>The closest body along a ray, among the layers of the mask, ignoring the bodies of an entity.</summary>
    public static bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, out RayHit hit, uint layers = AllLayers,
                               Entity ignore = default)
    {
        RayHit result;
        bool found = Bootstrap.Native.Raycast(&origin, &direction, maxDistance, layers, ignore, &result) != 0;
        hit = found ? result : default;
        return found;
    }

    /// <summary>The first body a sphere moving along the direction touches.</summary>
    public static bool SphereCast(Vec3 origin, float radius, Vec3 direction, float maxDistance, out RayHit hit,
                                  uint layers = AllLayers, Entity ignore = default)
    {
        RayHit result;
        bool found = Bootstrap.Native.SphereCast(&origin, radius, &direction, maxDistance, layers, ignore, &result) != 0;
        hit = found ? result : default;
        return found;
    }

    /// <summary>The entities whose bodies intersect a sphere, each once.</summary>
    public static Entity[] OverlapSphere(Vec3 center, float radius, uint layers = AllLayers, Entity ignore = default)
    {
        Entity* entities;
        int count = Bootstrap.Native.OverlapSphere(&center, radius, layers, ignore, &entities);
        return count == 0 ? [] : new ReadOnlySpan<Entity>(entities, count).ToArray();
    }

    /// <summary>A force on the dynamic body of the entity during the next step, in world space.</summary>
    public static void AddForce(Entity entity, Vec3 force) => Bootstrap.Native.AddForce(entity, &force);

    public static void AddTorque(Entity entity, Vec3 torque) => Bootstrap.Native.AddTorque(entity, &torque);

    /// <summary>Changes the velocity of the dynamic body of the entity at once.</summary>
    public static void AddImpulse(Entity entity, Vec3 impulse) => Bootstrap.Native.AddImpulse(entity, &impulse);

    public static void AddImpulseAt(Entity entity, Vec3 impulse, Vec3 point)
        => Bootstrap.Native.AddImpulseAt(entity, &impulse, &point);

    /// <summary>The contacts that began or ended during the physics steps of the frame, seen during Update.</summary>
    public static ReadOnlySpan<Contact> Contacts
    {
        get
        {
            Contact* contacts;
            int count = Bootstrap.Native.Contacts(&contacts);
            return count == 0 ? [] : new ReadOnlySpan<Contact>(contacts, count);
        }
    }
}

/// <summary>
/// The sounds of the game: those of the AudioSource components of entities, sounds played once, and
/// the volumes of the audio groups of the project.
/// </summary>
public static unsafe class Audio
{
    /// <summary>The name of the volume of every group together.</summary>
    public const string Master = "Master";

    /// <summary>Plays the clip of the AudioSource of the entity from its start.</summary>
    public static void Play(Entity entity) => Bootstrap.Native.PlaySound(Scene.Current.Pointer, entity);

    public static void Stop(Entity entity) => Bootstrap.Native.StopSound(entity);

    public static void Pause(Entity entity) => Bootstrap.Native.PauseSound(entity);

    /// <summary>Goes on from where Pause stopped.</summary>
    public static void Resume(Entity entity) => Bootstrap.Native.ResumeSound(entity);

    public static bool IsPlaying(Entity entity) => Bootstrap.Native.IsSoundPlaying(entity) != 0;

    /// <summary>Plays a clip once at a position of the world, in an audio group.</summary>
    public static void PlayOneShot(AssetId clip, Vec3 position, float volume = 1.0f, uint group = 0)
    {
        Uuid uuid = clip.Uuid;
        Bootstrap.Native.PlayOneShot(&uuid, &position, volume, group, 1);
    }

    /// <summary>Plays a clip once, everywhere at the same volume, as music or the sounds of an interface.</summary>
    public static void PlayOneShot(AssetId clip, float volume = 1.0f, uint group = 0)
    {
        Uuid uuid = clip.Uuid;
        Vec3 position = default;
        Bootstrap.Native.PlayOneShot(&uuid, &position, volume, group, 0);
    }

    /// <summary>The volume of an audio group of the project, or of all of them with <see cref="Master"/>.</summary>
    public static float GetGroupVolume(string group)
    {
        using var text = new Utf8Buffer(group);
        float volume = Bootstrap.Native.GroupVolume(text.Pointer);
        return volume >= 0.0f ? volume : throw new ArgumentException($"no audio group {group}");
    }

    /// <summary>
    /// Changes the volume of an audio group until the game ends, as a settings menu does: 1 leaves the
    /// sounds as they are, 0 silences them.
    /// </summary>
    public static void SetGroupVolume(string group, float volume)
    {
        using var text = new Utf8Buffer(group);
        if (Bootstrap.Native.SetGroupVolume(text.Pointer, volume) == 0)
        {
            throw new ArgumentException($"no audio group {group}");
        }
    }
}
