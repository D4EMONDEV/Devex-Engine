using Devex;

// The robot of the arena: it walks between two posts along the navigation mesh, around what stands in
// its way, waits at each end, and waves when the player comes close. Its NavMeshAgent walks it; its
// Animator plays the state machine of assets/animators/robot.dvxanimator, where a blend tree mixes
// idle and walk by the speed the agent really walks at, and Greeting crossfades to the wave.
public class RobotGuide : Component
{
    // Where the robot walks to, from where it starts.
    public Vec3 Patrol = new(0.0f, 0.0f, -10.0f);

    // Seconds spent still at each end of the patrol.
    public float WaitTime = 1.5f;

    // The robot greets the player closer than this, in meters.
    public float GreetDistance = 4.5f;

    // Played where it stands on the steps of its walk, which the import settings of the model mark
    // with events (assets/models/robot.glb.dvxmeta).
    [AssetType("audio")]
    public AssetId StepSound;

    private Vec3 _start;
    private Vec3 _target;
    private float _wait;
    private bool _greeting;

    public override void Start()
    {
        _start = Transform.Position;
        _target = _start + Patrol;
        Navigation.SetDestination(Entity, _target);
    }

    public override void Update(float delta)
    {
        Entity player = Entity.Find("Player");
        bool greeting = false;
        if (player.IsAlive && player.HasTransform)
        {
            Vec3 toPlayer = player.Transform.Position - Transform.Position;
            toPlayer.Y = 0.0f;
            greeting = toPlayer.Length < GreetDistance;
            if (greeting)
            {
                // Greeting: the robot stops, turns to the player and waves until it walks away.
                Face(toPlayer, delta);
            }
        }
        if (greeting != _greeting)
        {
            _greeting = greeting;
            if (greeting)
            {
                Navigation.Stop(Entity);
            }
            else
            {
                Navigation.SetDestination(Entity, _target);
            }
        }
        Animation.SetBool(Entity, "Greeting", greeting);
        Animation.SetFloat(Entity, "Speed", Entity.Get<NavMeshAgent>().Velocity.Length);

        if (!greeting && !Navigation.HasDestination(Entity))
        {
            // At an end of the patrol: wait, then walk to the other one.
            _wait += delta;
            if (_wait >= WaitTime)
            {
                _wait = 0.0f;
                _target = (_target - _start).Length < 0.5f ? _start + Patrol : _start;
                Navigation.SetDestination(Entity, _target);
            }
        }
    }

    public override void OnAnimationEvent(string name)
    {
        if (name == "step" && StepSound.IsValid)
        {
            Audio.PlayOneShot(StepSound, Transform.Position, 0.6f);
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
