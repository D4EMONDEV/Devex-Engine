using Devex;

/// <summary>Reads a path, moves along it, and changes how a follower behaves, in its first update.</summary>
public sealed class PathProbe : Component
{
    public float Length;
    public float MiddleX;
    public float DirectionX;
    public float Closest;
    public float NoPath;

    public override void Update(float delta)
    {
        Length = Paths.Length(Entity);
        MiddleX = Paths.PositionAt(Entity, Length * 0.5f).X;
        DirectionX = Paths.DirectionAt(Entity, 1.0f).X;
        Closest = Paths.ClosestDistance(Entity, new Vec3(3.0f, 4.0f, 0.0f));
        NoPath = Paths.Length(Entity.None);
        Entity.Get<PathFollower>().Speed = 7.0f;
    }
}
