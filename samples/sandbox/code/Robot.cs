using Devex;

// The robot of the arena: it walks between two posts, waits at each end, and waves when the player
// comes close. Its Animator plays the state machine of assets/animators/robot.dvxanimator: a blend
// tree mixes idle and walk by the Speed parameter, and Greeting crossfades to the wave.
public class RobotGuide : Component
{
    // Where the robot walks to, from where it starts.
    public Vec3 Patrol = new(0.0f, 0.0f, -10.0f);

    // In meters per second.
    public float Speed = 1.6f;

    // How fast the robot speeds up and slows down, in meters per second each second.
    public float Acceleration = 3.0f;

    // Seconds spent still at each end of the patrol.
    public float WaitTime = 1.5f;

    // The robot greets the player closer than this, in meters.
    public float GreetDistance = 4.5f;

    private Vec3 _start;
    private Vec3 _target;
    private float _wait;
    private float _speed;

    public override void Start()
    {
        _start = Transform.Position;
        _target = _start + Patrol;
        _wait = 0.0f;
    }

    public override void Update(float delta)
    {
        Entity player = Entity.Find("Player");
        Vec3 position = Transform.Position;
        bool greeting = false;
        if (player.IsAlive && player.HasTransform)
        {
            Vec3 toPlayer = player.Transform.Position - position;
            toPlayer.Y = 0.0f;
            greeting = toPlayer.Length < GreetDistance;
            if (greeting)
            {
                // Greeting: the robot stops, turns to the player and waves until it walks away.
                Face(toPlayer, delta);
            }
        }
        Animation.SetBool(Entity, "Greeting", greeting);

        Vec3 toTarget = _target - position;
        toTarget.Y = 0.0f;
        float wanted = Speed;
        if (greeting || _wait > 0.0f)
        {
            wanted = 0.0f;
            _wait = greeting ? _wait : _wait - delta;
        }
        else if (toTarget.Length < 0.15f)
        {
            // The end of the patrol: wait, then walk back.
            _target = (_target - _start).Length < 0.15f ? _start + Patrol : _start;
            _wait = WaitTime;
            wanted = 0.0f;
        }

        // The robot speeds up and slows down, and its walk follows its speed.
        _speed = _speed < wanted ? MathF.Min(_speed + Acceleration * delta, wanted) : MathF.Max(_speed - Acceleration * delta, wanted);
        Animation.SetFloat(Entity, "Speed", _speed);
        if (_speed > 0.0f && toTarget.Length > 0.001f)
        {
            if (!greeting)
            {
                Face(toTarget, delta);
            }
            Transform.Position = position + toTarget.Normalized * MathF.Min(_speed * delta, toTarget.Length);
        }
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
}
