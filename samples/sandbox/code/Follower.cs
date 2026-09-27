using Devex;

// A drone of the arena that follows its target along the navigation mesh: its NavMeshAgent walks to
// where the target stands, again every moment, and stops at its stopping distance; drones following
// the same target steer around each other.
public class Follower : Component
{
    public Entity Target;

    // Seconds between two looks at where the target stands.
    public float Interval = 0.3f;

    private float _wait;

    public override void Update(float delta)
    {
        _wait -= delta;
        if (_wait > 0.0f || !Target.IsAlive || !Target.HasTransform)
        {
            return;
        }
        _wait = Interval;
        Vec3 target = Target.WorldPosition;
        Vec3 toTarget = target - Entity.WorldPosition;
        toTarget.Y = 0.0f;
        // Close enough already: no need to walk.
        if (toTarget.Length > Entity.Get<NavMeshAgent>().StoppingDistance + 0.5f)
        {
            Navigation.SetDestination(Entity, target);
        }
    }
}
