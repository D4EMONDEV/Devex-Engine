using Devex;

// The robot of the arena: it walks between two posts, waits at each end, and waves when the player
// comes close. The clips come from its model, and the Animator crossfades between them.
public class RobotGuide : Component
{
    [AssetType("animation")]
    public AssetId Idle;

    [AssetType("animation")]
    public AssetId Walk;

    [AssetType("animation")]
    public AssetId Wave;

    // Where the robot walks to, from where it starts.
    public Vec3 Patrol = new(0.0f, 0.0f, -10.0f);

    // In meters per second.
    public float Speed = 1.6f;

    // Seconds spent still at each end of the patrol.
    public float WaitTime = 1.5f;

    // The robot greets the player closer than this, in meters.
    public float GreetDistance = 4.5f;

    private Vec3 _start;
    private Vec3 _target;
    private float _wait;
    private AssetId _clip;

    public override void Start()
    {
        _start = Transform.Position;
        _target = _start + Patrol;
        _wait = 0.0f;
        PlayClip(Idle);
    }

    public override void Update(float delta)
    {
        Entity player = Entity.Find("Player");
        Vec3 position = Transform.Position;
        if (player.IsAlive && player.HasTransform)
        {
            Vec3 toPlayer = player.Transform.Position - position;
            toPlayer.Y = 0.0f;
            if (toPlayer.Length < GreetDistance)
            {
                // Greeting: the robot turns to the player and waves until it walks away.
                Face(toPlayer, delta);
                PlayClip(Wave);
                return;
            }
        }

        if (_wait > 0.0f)
        {
            _wait -= delta;
            PlayClip(Idle);
            return;
        }

        Vec3 toTarget = _target - position;
        toTarget.Y = 0.0f;
        if (toTarget.Length < 0.15f)
        {
            // The end of the patrol: wait, then walk back.
            _target = (_target - _start).Length < 0.15f ? _start + Patrol : _start;
            _wait = WaitTime;
            PlayClip(Idle);
            return;
        }

        PlayClip(Walk);
        Face(toTarget, delta);
        Transform.Position = position + toTarget.Normalized * (Speed * delta);
    }

    // Turns towards a direction, a little every frame.
    private void Face(Vec3 direction, float delta)
    {
        if (direction.Length < 0.001f)
        {
            return;
        }
        float wanted = MathF.Atan2(direction.X, direction.Z);
        ref Transform transform = ref Transform;
        Vec3 forward = transform.Rotation * Vec3.Forward;
        float current = MathF.Atan2(-forward.X, -forward.Z);
        float difference = wanted - current;
        while (difference > MathF.PI)
        {
            difference -= MathF.Tau;
        }
        while (difference < -MathF.PI)
        {
            difference += MathF.Tau;
        }
        float step = MathF.Min(MathF.Abs(difference), 4.0f * delta) * MathF.Sign(difference);
        transform.Rotation = Quat.AngleAxis(step, Vec3.Up) * transform.Rotation;
    }

    // Starts a clip only when it is not the one already playing, so that the crossfade happens once.
    private void PlayClip(AssetId clip)
    {
        if (clip.IsValid && clip != _clip)
        {
            _clip = clip;
            Animation.Play(Entity, clip);
        }
    }
}
