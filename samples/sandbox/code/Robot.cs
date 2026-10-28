using Devex;

// The robot of the arena: it walks between two posts along the navigation mesh, around what stands in
// its way, waits at each end, and waves at the player who comes close, without stopping. Its
// NavMeshAgent walks it; its Animator plays the state machine of assets/animators/robot.dvxanimator,
// where a blend tree of the base layer mixes idle and walk by the speed the agent really walks at, and
// Greeting crossfades the Gestures layer to the wave, which only its right arm and its head play: the
// legs walk on.
public class RobotGuide : Component
{
    // Where the robot walks to, from where it starts.
    public Vec3 Patrol = new(0.0f, 0.0f, -10.0f);

    // Seconds spent still at each end of the patrol.
    public float WaitTime = 1.5f;

    // The robot greets the player closer than this, in meters.
    public float GreetDistance = 4.5f;

    // How much of the wave shows over the walk, from 0 to 1.
    public float GestureWeight = 1.0f;

    // Played where it stands on the steps of its walk, which the import settings of the model mark
    // with events (assets/models/robot.glb.dvxmeta).
    [AssetType("audio")]
    public AssetId StepSound;

    private Vec3 _start;
    private Vec3 _target;
    private float _wait;

    public override void Start()
    {
        _start = Transform.Position;
        _target = _start + Patrol;
        Navigation.SetDestination(Entity, _target);
    }

    public override void Update(float delta)
    {
        // Greeting: the robot waves at the player close to it, while it walks or waits.
        Entity player = Entity.Find("Player");
        bool greeting = false;
        if (player.IsAlive && player.HasTransform)
        {
            Vec3 toPlayer = player.Transform.Position - Transform.Position;
            toPlayer.Y = 0.0f;
            greeting = toPlayer.Length < GreetDistance;
        }
        Animation.SetBool(Entity, "Greeting", greeting);
        Animation.SetLayerWeight(Entity, "Gestures", GestureWeight);
        Animation.SetFloat(Entity, "Speed", Entity.Get<NavMeshAgent>().Velocity.Length);

        if (!Navigation.HasDestination(Entity))
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
}
