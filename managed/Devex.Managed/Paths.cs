namespace Devex;

/// <summary>
/// What the Path3D component of an entity tells, in the coordinates of the world, to place things along it or to
/// follow it by hand. A <c>PathFollower</c> component does that without code. These walk the whole curve at
/// each call, which is not meant for many times a frame.
/// </summary>
public static unsafe class Paths
{
    /// <summary>The length of the curve in meters, 0 for an entity without a path.</summary>
    public static float Length(Entity path) => Bootstrap.Native.PathLength(Scene.Current.Pointer, path);

    /// <summary>The point of the curve at a distance along it in the meters of the path, which stays at its ends and goes around a closed one.</summary>
    public static Vec3 PositionAt(Entity path, float distance)
    {
        Vec3 position;
        Bootstrap.Native.PathPositionAt(Scene.Current.Pointer, path, distance, &position);
        return position;
    }

    /// <summary>Where the curve goes at a distance along it, normalized.</summary>
    public static Vec3 DirectionAt(Entity path, float distance)
    {
        Vec3 direction;
        Bootstrap.Native.PathDirectionAt(Scene.Current.Pointer, path, distance, &direction);
        return direction;
    }

    /// <summary>How far along the curve is the point of it closest to a position, for a camera that follows a rail.</summary>
    public static float ClosestDistance(Entity path, Vec3 position) => Bootstrap.Native.PathClosestDistance(Scene.Current.Pointer, path, &position);
}
