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
    public string Below = "";

    public override void OnCollisionEnter(Entity other) => ++Hits;

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
