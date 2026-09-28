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

    /// <summary>
    /// Loads a scene in the background: what it shows loads while this scene goes on, then it replaces
    /// this one. <see cref="LoadingProgress"/> says how far it is, for a loading screen.
    /// </summary>
    public static void LoadSceneInBackground(AssetId scene)
    {
        Uuid uuid = scene.Uuid;
        Bootstrap.Native.LoadSceneInBackground(&uuid);
    }

    /// <summary>Loads a scene in the background by its path.</summary>
    public static void LoadSceneInBackground(string path) =>
        LoadSceneInBackground(Assets.Find(path) ?? throw new ArgumentException($"no scene at {path}"));

    /// <summary>Whether a scene is loading in the background.</summary>
    public static bool IsLoadingScene => Bootstrap.Native.SceneLoadingProgress() >= 0.0f;

    /// <summary>How much of the scene loading in the background is ready, from 0 to 1; 0 when none is.</summary>
    public static float LoadingProgress => MathF.Max(Bootstrap.Native.SceneLoadingProgress(), 0.0f);
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

    /// <summary>
    /// Starts loading an asset without using it, so that it is ready when it is shown: meshes, textures
    /// and materials load in the background.
    /// </summary>
    public static void Preload(AssetId asset)
    {
        Uuid uuid = asset.Uuid;
        Bootstrap.Native.PreloadAsset(&uuid);
    }

    /// <summary>Whether the asset is loaded and, for what is drawn, on the GPU.</summary>
    public static bool IsReady(AssetId asset)
    {
        Uuid uuid = asset.Uuid;
        return Bootstrap.Native.IsAssetReady(&uuid) != 0;
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

    /// <summary>
    /// The contacts that began or ended during the physics steps of the frame, seen during Update: those
    /// of 3D bodies, then those of 2D bodies.
    /// </summary>
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
/// The navigation of the scene: NavMeshAgent components walk the navigation mesh of its
/// NavMeshSurface to the destinations given here, and paths along it are found on demand.
/// </summary>
public static unsafe class Navigation
{
    /// <summary>
    /// Sends an agent to the point of the navigation mesh closest to a destination; false when the
    /// destination is far from the mesh.
    /// </summary>
    public static bool SetDestination(Entity agent, Vec3 destination) => Bootstrap.Native.NavSetDestination(agent, &destination) != 0;

    /// <summary>Stops the agent where it is.</summary>
    public static void Stop(Entity agent) => Bootstrap.Native.NavStop(agent);

    /// <summary>Whether the agent walks to a destination; false once it arrived.</summary>
    public static bool HasDestination(Entity agent) => Bootstrap.Native.NavHasDestination(agent) != 0;

    /// <summary>How far the agent still has to walk along its path.</summary>
    public static float RemainingDistance(Entity agent) => Bootstrap.Native.NavRemainingDistance(agent);

    /// <summary>The corners of the shortest path along the navigation mesh; empty when there is none.</summary>
    public static Vec3[] FindPath(Vec3 from, Vec3 to)
    {
        Vec3* corners;
        int count = Bootstrap.Native.NavFindPath(&from, &to, &corners);
        return count == 0 ? [] : new ReadOnlySpan<Vec3>(corners, count).ToArray();
    }

    /// <summary>The point of the navigation mesh closest to a point, within a distance.</summary>
    public static bool SamplePosition(Vec3 point, float maxDistance, out Vec3 position)
    {
        Vec3 found;
        bool hit = Bootstrap.Native.NavSamplePosition(&point, maxDistance, &found) != 0;
        position = found;
        return hit;
    }

    /// <summary>Walks a straight line along the navigation mesh; true when it leaves the mesh, at the position given.</summary>
    public static bool Raycast(Vec3 from, Vec3 to, out Vec3 position, out Vec3 normal)
    {
        Vec3 hit;
        Vec3 away;
        bool blocked = Bootstrap.Native.NavRaycast(&from, &to, &hit, &away) != 0;
        position = hit;
        normal = away;
        return blocked;
    }
}

/// <summary>What a 2D ray hit.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct RayHit2D
{
    /// <summary>The entity of the body hit: the entity of its RigidBody2D, or of its collider.</summary>
    public Entity Entity;
    public Vec2 Point;
    public Vec2 Normal;
    public float Distance;
}

/// <summary>
/// The 2D simulation of the scene, in the XY plane: queries and forces. Its contacts come with the
/// others in <see cref="Physics.Contacts"/>, and reach OnCollisionEnter and OnTriggerEnter alike.
/// </summary>
public static unsafe class Physics2D
{
    /// <summary>
    /// The closest 2D body along a ray, among the layers of the mask, through triggers, ignoring the
    /// bodies of an entity.
    /// </summary>
    public static bool Raycast(Vec2 origin, Vec2 direction, float maxDistance, out RayHit2D hit,
                               uint layers = Physics.AllLayers, Entity ignore = default)
    {
        RayHit2D result;
        bool found = Bootstrap.Native.Raycast2D(&origin, &direction, maxDistance, layers, ignore, &result) != 0;
        hit = found ? result : default;
        return found;
    }

    /// <summary>The entities whose 2D bodies, triggers included, overlap a circle, each once.</summary>
    public static Entity[] OverlapCircle(Vec2 center, float radius, uint layers = Physics.AllLayers, Entity ignore = default)
    {
        Entity* entities;
        int count = Bootstrap.Native.OverlapCircle2D(&center, radius, layers, ignore, &entities);
        return count == 0 ? [] : new ReadOnlySpan<Entity>(entities, count).ToArray();
    }

    /// <summary>A force on the dynamic 2D body of the entity during the next step.</summary>
    public static void AddForce(Entity entity, Vec2 force) => Bootstrap.Native.AddForce2D(entity, &force);

    /// <summary>A torque around Z on the dynamic 2D body of the entity during the next step.</summary>
    public static void AddTorque(Entity entity, float torque) => Bootstrap.Native.AddTorque2D(entity, torque);

    /// <summary>Changes the velocity of the dynamic 2D body of the entity at once.</summary>
    public static void AddImpulse(Entity entity, Vec2 impulse) => Bootstrap.Native.AddImpulse2D(entity, &impulse);
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

/// <summary>
/// The animations of the game: the clips the Animator components play on the bones of their entity.
/// </summary>
public static unsafe class Animation
{
    /// <summary>
    /// Plays a clip on the animator of the entity, crossfading over fade seconds; a negative fade
    /// takes the blend time of the Animator. The clip becomes the one the Animator holds.
    /// </summary>
    public static void Play(Entity entity, AssetId clip, float fade = -1.0f)
    {
        Uuid uuid = clip.Uuid;
        Bootstrap.Native.PlayAnimation(Scene.Current.Pointer, entity, &uuid, fade);
    }

    /// <summary>Plays the clip the Animator already holds, from its start.</summary>
    public static void Play(Entity entity)
    {
        if (entity.TryGet(out Animator animator))
        {
            Uuid uuid = animator.Clip.Uuid;
            Bootstrap.Native.PlayAnimation(Scene.Current.Pointer, entity, &uuid, 0.0f);
        }
    }

    public static void Stop(Entity entity) => Bootstrap.Native.StopAnimation(entity);

    public static void Pause(Entity entity) => Bootstrap.Native.PauseAnimation(entity);

    /// <summary>Goes on from where Pause stopped.</summary>
    public static void Resume(Entity entity) => Bootstrap.Native.ResumeAnimation(entity);

    public static bool IsPlaying(Entity entity) => Bootstrap.Native.IsAnimationPlaying(entity) != 0;

    /// <summary>Seconds into the clip that plays.</summary>
    public static float GetTime(Entity entity) => Bootstrap.Native.AnimationTime(entity);

    /// <summary>Jumps to a time of the clip and poses the bones there.</summary>
    public static void SetTime(Entity entity, float seconds)
        => Bootstrap.Native.SetAnimationTime(Scene.Current.Pointer, entity, seconds);

    /// <summary>Sets a float parameter of the state machine of the Animator of the entity.</summary>
    public static void SetFloat(Entity entity, string parameter, float value) => SetParameter(entity, parameter, 0, value);

    public static void SetInteger(Entity entity, string parameter, int value) => SetParameter(entity, parameter, 1, value);

    public static void SetBool(Entity entity, string parameter, bool value) => SetParameter(entity, parameter, 2, value ? 1.0f : 0.0f);

    /// <summary>Sets a trigger, which the transition it lets through resets.</summary>
    public static void SetTrigger(Entity entity, string parameter) => SetParameter(entity, parameter, 3, 1.0f);

    public static void ResetTrigger(Entity entity, string parameter) => SetParameter(entity, parameter, 4, 0.0f);

    /// <summary>A parameter of the state machine; bools and triggers read as 0 or 1.</summary>
    public static float GetFloat(Entity entity, string parameter)
    {
        using var text = new Utf8Buffer(parameter);
        float value;
        Bootstrap.Native.AnimatorParameter(entity, text.Pointer, &value);
        return value;
    }

    public static int GetInteger(Entity entity, string parameter) => (int)MathF.Round(GetFloat(entity, parameter));

    public static bool GetBool(Entity entity, string parameter) => GetFloat(entity, parameter) != 0.0f;

    /// <summary>The state the state machine of the entity is in; empty without a controller.</summary>
    public static string GetState(Entity entity) => ReadState(entity, out _);

    /// <summary>Whether the state machine of the entity is in a state.</summary>
    public static bool IsInState(Entity entity, string state) => GetState(entity) == state;

    /// <summary>How far into its state the state machine is: 1 at its end, more for a state that loops.</summary>
    public static float GetStateTime(Entity entity)
    {
        ReadState(entity, out float time);
        return time;
    }

    private static void SetParameter(Entity entity, string parameter, int kind, float value)
    {
        using var text = new Utf8Buffer(parameter);
        Bootstrap.Native.SetAnimatorParameter(entity, text.Pointer, kind, value);
    }

    private static string ReadState(Entity entity, out float time)
    {
        float read;
        byte* state = Bootstrap.Native.AnimatorState(entity, &read);
        time = read;
        return Utf8.ToString(state) ?? "";
    }
}

/// <summary>
/// Frame by frame animation of sprites: the SpriteAnimator components, which show the frames of an
/// animation of their sprite frames on the SpriteRenderer of their entity.
/// </summary>
public static class SpriteAnimations
{
    /// <summary>
    /// Plays an animation by name. Another animation starts from its first frame; the one already
    /// playing goes on, so that this can be called every frame; one that ended starts again.
    /// </summary>
    public static void Play(this SpriteAnimator animator, string animation)
    {
        if (animator.Animation != animation)
        {
            animator.Animation = animation;
        }
        else if (!animator.Playing)
        {
            // Backwards, the animation starts from its last frame, where the frame is clamped to.
            animator.Frame = animator.Speed < 0.0f ? int.MaxValue : 0;
        }
        animator.Playing = true;
    }
}

/// <summary>What a tile is to the game, as its tileset says.</summary>
public enum TileCollision : byte
{
    /// <summary>Walked through.</summary>
    None,
    /// <summary>Solid on every side: ground, walls.</summary>
    Full,
    /// <summary>Holds up what lands on it from above, lets through what comes from below.</summary>
    Top,
}

/// <summary>
/// The Tilemap components of the scene: grids of cells painted with the tiles of a tileset. Cell
/// (x, y) covers [x, x + 1) by [y, y + 1) cells from the origin of its entity, y up.
/// </summary>
public static unsafe class Tilemaps
{
    /// <summary>The flags of a cell that mirror its tile, above its identifier.</summary>
    public const int FlipX = 0x4000;
    public const int FlipY = 0x8000;

    /// <summary>The tile of a cell, its identifier in the tileset; 0 for none.</summary>
    public static int GetTile(Entity tilemap, int x, int y)
        => Bootstrap.Native.TileAt(Scene.Current.Pointer, tilemap, x, y) & 0x3FFF;

    /// <summary>Paints a cell with a tile of the tileset, mirrored or not; 0 empties it.</summary>
    public static void SetTile(Entity tilemap, int x, int y, int tile, bool flipX = false, bool flipY = false)
        => Bootstrap.Native.SetTile(Scene.Current.Pointer, tilemap, x, y,
                                    tile == 0 ? 0 : (tile & 0x3FFF) | (flipX ? FlipX : 0) | (flipY ? FlipY : 0));

    /// <summary>The cell under a point of the world.</summary>
    public static (int X, int Y) CellAt(Entity tilemap, Vec3 point)
    {
        int x = 0;
        int y = 0;
        Bootstrap.Native.CellAt(Scene.Current.Pointer, tilemap, &point, &x, &y);
        return (x, y);
    }

    /// <summary>The middle of a cell, in the world.</summary>
    public static Vec3 CellCenter(Entity tilemap, int x, int y)
    {
        Vec3 center = default;
        Bootstrap.Native.CellCenter(Scene.Current.Pointer, tilemap, x, y, &center);
        return center;
    }

    /// <summary>How the tile of a cell collides, None for an empty cell.</summary>
    public static TileCollision GetCollision(Entity tilemap, int x, int y)
        => (TileCollision)Bootstrap.Native.TileCollision(Scene.Current.Pointer, tilemap, x, y);

    /// <summary>What the tileset tells the game of the tile of a cell, such as "water"; empty for none.</summary>
    public static string GetData(Entity tilemap, int x, int y)
        => Utf8.ToString(Bootstrap.Native.TileData(Scene.Current.Pointer, tilemap, x, y)) ?? string.Empty;
}

/// <summary>
/// The particles of the game: the ParticleEmitter components of the scene, which emit by themselves
/// when they play on start, and which code plays, stops and bursts.
/// </summary>
public static unsafe class Particles
{
    /// <summary>Emits again from the start of a cycle, with its burst.</summary>
    public static void Play(Entity emitter) => Bootstrap.Native.PlayParticles(Scene.Current.Pointer, emitter);

    /// <summary>Stops emitting; the particles alive finish their life, or vanish with clear.</summary>
    public static void Stop(Entity emitter, bool clear = false) => Bootstrap.Native.StopParticles(emitter, clear ? 1 : 0);

    /// <summary>Holds the emitter and its particles where they are.</summary>
    public static void Pause(Entity emitter) => Bootstrap.Native.PauseParticles(emitter);

    public static void Resume(Entity emitter) => Bootstrap.Native.ResumeParticles(emitter);

    /// <summary>Emits particles at once, whether the emitter plays or not: sparks where a ball lands.</summary>
    public static void Emit(Entity emitter, int count) => Bootstrap.Native.EmitParticles(Scene.Current.Pointer, emitter, count);

    /// <summary>While it emits, or particles it emitted are alive.</summary>
    public static bool IsPlaying(Entity emitter) => Bootstrap.Native.AreParticlesPlaying(emitter) != 0;

    /// <summary>The particles alive.</summary>
    public static int Count(Entity emitter) => Bootstrap.Native.ParticleCount(emitter);
}

/// <summary>
/// The interface of the game: the canvases of the scene, what the player clicked this frame, and
/// where the focus sits for the keyboard and the pad.
/// </summary>
public static unsafe class Ui
{
    /// <summary>
    /// Whether a button carrying this action was clicked during this frame. As many buttons as
    /// needed may share an action: any of them answers.
    /// </summary>
    public static bool WasClicked(string action)
    {
        using var text = new Utf8Buffer(action);
        return Bootstrap.Native.UiClickedAction(text.Pointer) != 0;
    }

    /// <summary>Whether that very button was clicked during this frame.</summary>
    public static bool WasClicked(Entity entity) => Bootstrap.Native.UiClickedEntity(entity) != 0;

    /// <summary>
    /// Whether a slider carrying this action was moved, or a toggle carrying it turned over,
    /// during this frame. The value itself is read from the component of the element.
    /// </summary>
    public static bool WasChanged(string action)
    {
        using var text = new Utf8Buffer(action);
        return Bootstrap.Native.UiChangedAction(text.Pointer) != 0;
    }

    /// <summary>
    /// Whether a field carrying this action was ended with Enter during this frame. What was
    /// typed is the text of the UiText of the field.
    /// </summary>
    public static bool WasSubmitted(string action)
    {
        using var text = new Utf8Buffer(action);
        return Bootstrap.Native.UiSubmittedAction(text.Pointer) != 0;
    }

    /// <summary>The field being typed into, or an invalid entity.</summary>
    public static Entity EditedField => Bootstrap.Native.UiEditedField();

    /// <summary>Whether the player asked to go back this frame: Escape, or the east button.</summary>
    public static bool WasCancelled() => Bootstrap.Native.UiCancelled() != 0;

    /// <summary>The button under the pointer, or an invalid entity.</summary>
    public static Entity Hovered => Bootstrap.Native.UiHovered();

    /// <summary>The button the keyboard and the pad act on, or an invalid entity.</summary>
    public static Entity Focused
    {
        get => Bootstrap.Native.UiFocused();
        set => Bootstrap.Native.UiSetFocus(Scene.Current.Pointer, value);
    }

    /// <summary>
    /// Whether the pointer rests on the interface, which a game reads before acting on a click of
    /// its own.
    /// </summary>
    public static bool PointerOverInterface => Bootstrap.Native.UiPointerOverInterface() != 0;

    /// <summary>
    /// Whether a button carrying this action was clicked twice in a row, quickly, during this
    /// frame. The second click also counts as a click.
    /// </summary>
    public static bool WasDoubleClicked(string action)
    {
        using var text = new Utf8Buffer(action);
        return Bootstrap.Native.UiDoubleClickedAction(text.Pointer) != 0;
    }

    /// <summary>Whether that very button was clicked twice in a row, quickly, during this frame.</summary>
    public static bool WasDoubleClicked(Entity entity) => Bootstrap.Native.UiDoubleClickedEntity(entity) != 0;

    /// <summary>Opens a popup (UiPopup) where its anchors put it.</summary>
    public static void OpenPopup(Entity popup) => Bootstrap.Native.UiOpenPopup(Scene.Current.Pointer, popup, null);

    /// <summary>Opens a popup with its top left corner at a point of the screen, in pixels.</summary>
    public static void OpenPopup(Entity popup, Vec2 at) => Bootstrap.Native.UiOpenPopup(Scene.Current.Pointer, popup, &at);

    /// <summary>Closes a popup. Menus close by themselves; a modal stays until it is closed.</summary>
    public static void ClosePopup(Entity popup) => Bootstrap.Native.UiClosePopup(Scene.Current.Pointer, popup);

    /// <summary>Whether a popup is open.</summary>
    public static bool IsPopupOpen(Entity popup) => Bootstrap.Native.UiPopupOpen(Scene.Current.Pointer, popup) != 0;

    /// <summary>
    /// The element whose UiContextMenu opened the last context menu, such as the row of a list,
    /// which the entries of the menu act on.
    /// </summary>
    public static Entity ContextTarget => Bootstrap.Native.UiContextTarget();
}
