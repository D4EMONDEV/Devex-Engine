using Devex;

// A component of the tests: it moves its entity and records that it started.
public class Mover : Component
{
    // In meters per second.
    public float Speed = 2.0f;

    public Vec3 Direction = new(1.0f, 0.0f, 0.0f);

    public string Label = "new";

    [Angle]
    public float Turn = 0.0f;

    public override void Start()
    {
        Label = "started";
    }

    public override void Update(float delta)
    {
        // A zone of the game's own, measured when the profiler records.
        using (Profiler.Scope("Move"))
        {
            Transform.Position += Direction * (Speed * delta);
        }
        Label = $"moved {Entity.Name}";
    }
}

// A second component, to check that every type of an assembly is registered.
public class Counter : Component
{
    public int Count;

    public override void FixedUpdate(float delta)
    {
        ++Count;
    }
}

// A system: it runs once per phase, over the whole scene, after the components.
public static class Renamer
{
    [GameSystem(SystemPhase.Update)]
    public static void Rename(Scene scene)
    {
        foreach (Mover mover in scene.Components<Mover>())
        {
            scene.SetName(mover.Entity, "renamed by the system");
        }
    }
}

// Lists, entity references, other components and the engine's components.
public class Patrol : Component
{
    public List<Vec3> Points = [new(0.0f, 0.0f, 0.0f), new(1.0f, 0.0f, 0.0f)];

    public List<string> Words = ["a"];

    public List<Entity> Friends = [];

    public Entity Leader;

    // Not saved, but kept while the component lives, reloads of the code included.
    private int _updates;
    private int _starts;

    public int Updates => _updates;

    public override void Start()
    {
        ++_starts;
    }

    public override void Update(float delta)
    {
        ++_updates;
        Points.Add(new Vec3(_updates, 0.0f, 0.0f));
        Words.Add($"update {_updates}, start {_starts}");
        if (Leader.IsAlive)
        {
            // A component of the engine, changed in place through its generated view.
            if (Leader.TryGet(out PointLight light))
            {
                light.Intensity += 100.0f;
            }
            // Another C# component, whose changes are kept at the end of the phase.
            Mover? mover = Leader.Get<Mover>();
            if (mover != null)
            {
                mover.Speed = 9.0f;
            }
            Friends.Add(Leader);
        }
    }
}

// Counts the bodies that hit it and pass through it, and looks down with a ray.
public class Bumper : Component
{
    public int Hits;
    public int Entered;
    public int Left;
    public int Breaks;
    public string Below = "";

    public override void OnCollisionEnter(Entity other) => ++Hits;

    public override void OnJointBreak(Entity joint) => ++Breaks;

    public override void OnTriggerEnter(Entity other) => ++Entered;

    public override void OnTriggerExit(Entity other) => ++Left;

    public override void Update(float delta)
    {
        Vec3 above = Transform.Position + new Vec3(0.0f, 5.0f, 0.0f);
        Below = Physics.Raycast(above, new Vec3(0.0f, -1.0f, 0.0f), 20.0f, out RayHit hit) ? hit.Entity.Name : "nothing";
    }
}

// Fails at every update, as a bug in game code would.
public class Faulty : Component
{
    public override void Update(float delta)
    {
        throw new InvalidOperationException("broken on purpose");
    }
}

// Plays its sound and a one-shot, and changes the volumes of the audio groups.
public class Jukebox : Component
{
    [AssetType("audio")]
    public AssetId Clip;

    [AudioGroup]
    public uint Group;

    public bool WasPlaying;
    public bool Stopped;
    public float MusicVolume;
    public float MasterVolume;
    public string Error = "";

    public override void Start()
    {
        Audio.Play(Entity);
        Audio.PlayOneShot(Clip, Transform.Position, 0.5f, Group);
        Audio.SetGroupVolume("Music", 0.25f);
        Audio.SetGroupVolume(Audio.Master, 0.5f);
        MusicVolume = Audio.GetGroupVolume("Music");
        MasterVolume = Audio.GetGroupVolume(Audio.Master);
        try
        {
            Audio.SetGroupVolume("Nothing", 1.0f);
        }
        catch (ArgumentException exception)
        {
            Error = exception.Message;
        }
    }

    public override void Update(float delta)
    {
        WasPlaying = Audio.IsPlaying(Entity);
        Audio.Stop(Entity);
        Stopped = !Audio.IsPlaying(Entity);
    }
}

// Reads the input actions of the project, binds a key and turns a context off.
public class Pilot : Component
{
    public bool Jumping;
    public bool JumpPressed;
    public float Throttle;
    public float MoveX;
    public float MoveY;
    public string JumpLabel = "";
    public bool UnknownActionThrows;
    public bool Listen;
    public bool Listening;
    public bool StopGameplay;
    public bool GameplayActive;

    public override void Update(float delta)
    {
        Jumping = Input.IsActionDown("Jump");
        JumpPressed = Input.WasActionPressed("Jump");
        Throttle = Input.ActionAxis("Throttle");
        Vec2 move = Input.ActionVector("Move");
        MoveX = move.X;
        MoveY = move.Y;
        JumpLabel = Input.BindingLabel("Jump", 0);
        try
        {
            Input.IsActionDown("Fly");
        }
        catch (ArgumentException)
        {
            UnknownActionThrows = true;
        }
        if (Listen)
        {
            Input.ListenForBinding("Jump", 0);
            Listen = false;
        }
        Listening = Input.IsListeningForBinding;
        if (StopGameplay)
        {
            Input.SetContextActive("Gameplay", false);
        }
        GameplayActive = Input.IsContextActive("Gameplay");
    }
}

public enum Rank
{
    Bronze,
    Silver,
    Gold,
}

// What the test game keeps between sessions.
public class ProgressData
{
    public int Level;
    public string Name = "";
    public List<string> Items = [];
    public Vec3 Checkpoint;
    public Rank Rank;
    public float Speed = 2.5f;
}

// Saves and loads its progress, lists the saves, and keeps settings.
public class Archivist : Component
{
    public bool Saved;
    public bool Loaded;
    public int LoadedLevel;
    public string LoadedName = "";
    public int LoadedItems;
    public float LoadedCheckpointY;
    public string LoadedRank = "";
    public int SlotCount;
    public string FirstLabel = "";
    public int FirstVersion;
    public bool MissingIsNull;
    public float MasterVolume;
    public string Language = "";
    public int Difficulty;
    public bool Deleted;

    public override void Update(float delta)
    {
        var progress = new ProgressData
        {
            Level = 5,
            Name = "Léa",
            Items = ["key", "map"],
            Checkpoint = new Vec3(1.0f, 2.0f, 3.0f),
            Rank = Rank.Gold,
        };
        Saves.Save("1", progress, label: "Cave", scene: false, thumbnail: false, version: 2);
        Saved = Saves.Exists("1");
        ProgressData? loaded = Saves.Load<ProgressData>("1");
        Loaded = loaded != null;
        LoadedLevel = loaded?.Level ?? 0;
        LoadedName = loaded?.Name ?? "";
        LoadedItems = loaded?.Items.Count ?? 0;
        LoadedCheckpointY = loaded?.Checkpoint.Y ?? 0.0f;
        LoadedRank = loaded?.Rank.ToString() ?? "";
        SaveSlot[] slots = Saves.List();
        SlotCount = slots.Length;
        FirstLabel = slots.Length > 0 ? slots[0].Label : "";
        FirstVersion = slots.Length > 0 ? slots[0].Version : -1;
        MissingIsNull = Saves.Load<ProgressData>("nothing") == null;

        PlayerSettings.SetVolume(Audio.Master, 0.3f);
        MasterVolume = PlayerSettings.GetVolume(Audio.Master);
        PlayerSettings.SetString("language", "fr");
        Language = PlayerSettings.GetString("language");
        PlayerSettings.SetInt("difficulty", 3);
        Difficulty = PlayerSettings.GetInt("difficulty");

        Saves.Delete("1");
        Deleted = !Saves.Exists("1");
    }
}

// Reads the translations of the game, and changes its language.
public class Translator : Component
{
    public string Before = "";
    public string Language = "";
    public string Greeting = "";
    public string Score = "";
    public string Missing = "";
    public string Languages = "";
    public string Native = "";

    public override void Update(float delta)
    {
        Before = Localization.Tr("HELLO");
        Localization.Language = "fr_CA";
        Language = Localization.Language;
        Greeting = Localization.Tr("HELLO");
        Score = Localization.Tr("SCORE", ("points", 12));
        Missing = Localization.Tr("Not a key");
        Languages = string.Join(",", Localization.Languages);
        Native = Localization.LanguageName("fr");
    }
}

// Asks for a scene in the background, and reads how far it is.
public class Loader : Component
{
    public AssetId Target;
    public bool Loading;
    public float Progress = -1.0f;
    public bool Ready;

    public override void Update(float delta)
    {
        Loading = Game.IsLoadingScene;
        Progress = Game.LoadingProgress;
        if (!Loading)
        {
            Game.LoadSceneInBackground(Target);
        }
        Assets.Preload(Target);
        Ready = Assets.IsReady(Target);
    }
}

// Tweens and coroutines: a coroutine that waits for frames, time, tweens, another coroutine, a Task
// and a condition, and one that counts frames for as long as the component lives.
public class Choreographer : Component
{
    public int Step;
    public bool NestedFailed;
    public bool TweenFailed;
    public bool OnGameThread;
    public bool Release;

    internal static int Ticks;

    public override void Start()
    {
        _ = Run();
        _ = Tick();
    }

    private async Coroutine Run()
    {
        Step = 1;
        await Wait.NextFrame();
        Step = 2;
        await Wait.Seconds(0.5f);
        Step = 3;
        await Tween.MoveBy(Entity, new Vec3(0.0f, 2.0f, 0.0f), 1.0f, Ease.Linear);
        Step = 4;
        try
        {
            await Fail();
        }
        catch (InvalidOperationException)
        {
            NestedFailed = true;
        }
        try
        {
            // The entity has no UiRect.
            _ = Tween.Fade(Entity, 0.0f, 1.0f);
        }
        catch (InvalidOperationException)
        {
            TweenFailed = true;
        }
        int thread = System.Environment.CurrentManagedThreadId;
        await Task.Yield();
        OnGameThread = System.Environment.CurrentManagedThreadId == thread;
        Step = 5;
        await new TweenSequence()
            .Append(new TweenSpec(Entity, "Transform.scale", new Vec3(2.0f, 2.0f, 2.0f), 0.5f) { Ease = Ease.Linear })
            .AppendInterval(0.5f)
            .Play();
        Step = 6;
        await Wait.Until(() => Release);
        Step = 7;
    }

    private static async Coroutine Fail()
    {
        await Wait.NextFrame();
        throw new InvalidOperationException("nested");
    }

    private static async Coroutine Tick()
    {
        while (true)
        {
            await Wait.NextFrame();
            ++Ticks;
        }
    }
}

// Reads the frames the coroutine of the Choreographer counted.
public class TickReader : Component
{
    public int Ticks;

    public override void Update(float delta) => Ticks = Choreographer.Ticks;
}

// Bursts the particles of its entity, reads how many there are, then stops them.
public class Pyrotechnician : Component
{
    public int Emitted;
    public bool Playing;
    public bool Stopped;

    private int _frame;

    public override void Update(float delta)
    {
        ++_frame;
        if (_frame == 1)
        {
            Particles.Emit(Entity, 5);
        }
        else if (_frame == 2)
        {
            Emitted = Particles.Count(Entity);
            Playing = Particles.IsPlaying(Entity);
            Particles.Stop(Entity, clear: true);
        }
        else if (_frame == 3)
        {
            Stopped = Particles.Count(Entity) == 0 && !Particles.IsPlaying(Entity);
        }
    }
}

// Plays the animations of the sprite of its entity, and flips it, as a character would.
public class Animator2D : Component
{
    public string Shown = "";
    public bool Restarted;

    private int _frame;

    public override void Update(float delta)
    {
        ++_frame;
        SpriteAnimator animator = Entity.Get<SpriteAnimator>();
        if (_frame == 1)
        {
            animator.Play("run");
            Entity.Get<SpriteRenderer>().FlipX = true;
        }
        else if (_frame == 2)
        {
            // Called again, the same animation goes on.
            animator.Frame = 2;
            animator.Play("run");
            Shown = $"{animator.Animation} {animator.Frame}";
        }
        else if (_frame == 3)
        {
            // An animation that ended starts again.
            animator.Playing = false;
            animator.Play("run");
            Restarted = animator.Playing && animator.Frame == 0;
        }
    }
}

// Paints and reads the cells of the tilemap of its entity.
public class Tiler : Component
{
    public int Read;
    public int CellX;
    public int CellY;
    public Vec3 Center;

    public override void Update(float delta)
    {
        Tilemaps.SetTile(Entity, 2, 3, 5, flipX: true);
        Tilemaps.SetTile(Entity, 4, 4, 6);
        Tilemaps.SetTile(Entity, 4, 4, 0);
        Read = Tilemaps.GetTile(Entity, 2, 3) * 10 + Tilemaps.GetTile(Entity, 4, 4);
        (CellX, CellY) = Tilemaps.CellAt(Entity, new Vec3(12.5f, -0.5f, 0.0f));
        Center = Tilemaps.CellCenter(Entity, 1, 1);
    }
}

// Paints terrains on the tilemap of its entity, and reads them back.
public class TerrainPainter : Component
{
    public int GroundSet = -2;
    public int Ground = -2;
    public int MissingSet = -2;
    public int ReadSet = -2;
    public int ReadTerrain = -2;
    public int EmptySet = -2;
    public int Middle;

    public override void Update(float delta)
    {
        (GroundSet, Ground) = Tilemaps.FindTerrain(Entity, "Ground");
        (MissingSet, _) = Tilemaps.FindTerrain(Entity, "Lava");
        Tilemaps.SetTerrain(Entity, [(0, 0), (1, 0), (2, 0)], GroundSet, Ground);
        Tilemaps.SetTerrainPath(Entity, [(0, 2), (0, 3)], GroundSet, Ground);
        Tilemaps.SetTerrain(Entity, 2, 0, GroundSet, -1);
        (ReadSet, ReadTerrain) = Tilemaps.GetTerrain(Entity, 1, 0);
        (EmptySet, _) = Tilemaps.GetTerrain(Entity, 2, 0);
        Middle = Tilemaps.GetTile(Entity, 1, 0);
    }
}

// Counts the 2D bodies that hit it and pass through it, and looks around with 2D queries.
public class Bumper2D : Component
{
    public int Hits;
    public int Entered;
    public int Left;
    public string Below = "";
    public int Near;

    public override void OnCollisionEnter(Entity other) => ++Hits;

    public override void OnTriggerEnter(Entity other) => ++Entered;

    public override void OnTriggerExit(Entity other) => ++Left;

    public override void Update(float delta)
    {
        Vec3 position = Transform.Position;
        Below = Physics2D.Raycast(new Vec2(position.X, position.Y + 5.0f), new Vec2(0.0f, -1.0f), 20.0f, out RayHit2D hit)
            ? hit.Entity.Name
            : "nothing";
        Near = Physics2D.OverlapCircle(new Vec2(position.X, position.Y), 2.5f).Length;
    }
}

// Walks right, jumps once from the ground, and kicks a crate up.
public class Walker2D : Component
{
    public Entity Crate;
    public bool Jumped;
    public bool Landed;

    public override void Start() => Physics2D.AddImpulse(Crate, new Vec2(0.0f, 4.0f));

    public override void FixedUpdate(float delta)
    {
        CharacterController2D controller = Entity.Get<CharacterController2D>();
        if (!Jumped && controller.Grounded)
        {
            controller.Velocity = new Vec2(2.0f, 5.0f);
            Jumped = true;
        }
        else
        {
            controller.Velocity = new Vec2(2.0f, controller.Velocity.Y);
            Landed = Landed || (Jumped && controller.Grounded);
        }
    }
}

// Drives a state machine from C#: its speed, then a trigger, reading back the state it is in.
public class Conductor : Component
{
    public string State = "";
    public float Speed;
    public bool Waved;
    private int _frames;

    public override void Update(float delta)
    {
        ++_frames;
        if (_frames == 1)
        {
            Animation.SetFloat(Entity, "Speed", 2.0f);
        }
        if (_frames == 3)
        {
            Animation.SetTrigger(Entity, "Wave");
        }
        State = Animation.GetState(Entity);
        Speed = Animation.GetFloat(Entity, "Speed");
        Waved = Waved || Animation.IsInState(Entity, "Waving");
    }
}

// Sends its agent across the level, and asks for a path, the ground under a point and a ray.
public class Navigator : Component
{
    public Vec3 Target;
    public bool Sent;
    public int Corners;
    public bool Sampled;
    public float SampledHeight;
    public bool Blocked;
    public bool Arrived;

    public override void Update(float delta)
    {
        if (!Sent)
        {
            Sent = Navigation.SetDestination(Entity, Target);
            Corners = Navigation.FindPath(Transform.Position, Target).Length;
            Sampled = Navigation.SamplePosition(new Vec3(2.0f, 1.5f, 7.0f), 3.0f, out Vec3 ground);
            SampledHeight = ground.Y;
            Blocked = Navigation.Raycast(new Vec3(-5.0f, 0.0f, -5.0f), new Vec3(5.0f, 0.0f, -5.0f), out _, out _);
            return;
        }
        Arrived = Arrived || !Navigation.HasDestination(Entity);
    }
}

// Opens a popup, reads the menu a right click opened and the double clicks of a row, and the
// options of a dropdown.
public class Menus : Component
{
    public Entity Popup;
    public Entity Row;
    public Entity Choice;
    public int Step;
    public bool Opened;
    public bool TargetSeen;
    public bool DoubleClicked;
    public bool Closed;
    public int Options;
    public string SecondOption = "";

    public override void Update(float delta)
    {
        if (Step == 0)
        {
            Ui.OpenPopup(Popup, new Vec2(300.0f, 120.0f));
            Opened = Ui.IsPopupOpen(Popup);
            var dropdown = Choice.Get<UiDropdown>();
            Options = dropdown.Options.Count;
            SecondOption = dropdown.Options[1];
        }
        TargetSeen = TargetSeen || Ui.ContextTarget == Row;
        DoubleClicked = DoubleClicked || Ui.WasDoubleClicked(Row);
        if (Step == 6)
        {
            Ui.ClosePopup(Popup);
            Closed = !Ui.IsPopupOpen(Popup);
        }
        ++Step;
    }
}

// Colours a word of an area of text from its Start, before the interface has laid it out, finds
// something behind its first letter, marks its second line and reads the lines in view. The letters
// before the word are one character of C# and two bytes, then two characters and four bytes.
public class TextColors : Component
{
    public Entity Area;
    public int First = -1;
    public int Count = -1;

    public override void Start()
    {
        int word = Area.Get<UiText>().Text.IndexOf("red");
        Ui.SetTextColors(Area, [new UiTextSpan(word, word + 3, new Vec4(1.0f, 0.0f, 0.0f, 1.0f))]);
        Ui.SetTextHighlights(Area, [new UiTextSpan(0, 1, new Vec4(1.0f, 1.0f, 0.0f, 0.5f))]);
        Ui.SetTextMarks(Area, [new UiTextLineMark(1, new Vec4(0.0f, 1.0f, 0.0f, 1.0f))]);
    }

    public override void Update(float delta)
    {
        (First, Count) = Ui.VisibleTextLines(Area);
    }
}

// Reads what the pointer carries and what a slot takes.
public class Drops : Component
{
    public Entity Gem;
    public Entity Slot;
    public bool Carrying;
    public bool Dropped;
    public bool FromGem;
    public string Data = "";
    public float AtX;
    public float AtY;

    public override void Update(float delta)
    {
        Carrying = Carrying || Ui.Carried == Gem;
        if (Ui.WasDropped("slot") && Ui.WasDropped(Slot) && Ui.Dropped is UiDrop drop)
        {
            Dropped = true;
            FromGem = drop.Source == Gem && drop.Target == Slot && drop.Type == "item";
            Data = drop.Data;
            AtX = drop.At.X;
            AtY = drop.At.Y;
        }
    }
}
