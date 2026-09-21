using Devex;

// A door that slides open while its zone says so, playing the sound of its AudioSource as it moves.
public class SlidingDoor : Component
{
    // Where the door goes when open, from where it stands closed.
    public Vec3 OpenOffset = new(0.0f, 2.8f, 0.0f);

    // In meters per second.
    public float Speed = 3.0f;

    public bool Open;

    private Vec3 _closed;
    private bool _wasOpen;

    public override void Start()
    {
        _closed = Transform.Position;
    }

    public override void Update(float delta)
    {
        if (Open != _wasOpen)
        {
            _wasOpen = Open;
            if (Entity.Has<AudioSource>())
            {
                Audio.Play(Entity);
            }
        }

        Vec3 target = Open ? _closed + OpenOffset : _closed;
        Vec3 position = Transform.Position;
        Vec3 remaining = target - position;
        float step = Speed * delta;
        Transform.Position = remaining.Length <= step ? target : position + remaining.Normalized * step;
    }
}

// A trigger zone that opens a door while the player stands in it.
public class DoorZone : Component
{
    // The entity of the SlidingDoor, chosen in the inspector.
    public Entity Door;

    private int _inside;

    public override void OnTriggerEnter(Entity other)
    {
        if (IsPlayer(other) && ++_inside == 1)
        {
            SetOpen(true);
        }
    }

    public override void OnTriggerExit(Entity other)
    {
        if (IsPlayer(other) && --_inside <= 0)
        {
            _inside = 0;
            SetOpen(false);
        }
    }

    // The character of the player, which the C++ code of the game drives.
    private static bool IsPlayer(Entity entity) => entity.Has<CharacterController>() && entity.Has<Player>();

    private void SetOpen(bool open)
    {
        if (Door.Get<SlidingDoor>() is { } door)
        {
            door.Open = open;
            Log.Info(open ? "The door opens" : "The door closes");
        }
    }
}
