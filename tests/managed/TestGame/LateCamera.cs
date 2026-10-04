using Devex;
using System.Numerics;

// Follows the rendered pose of a physics parent, and records which callbacks actually ran.
public class LateCamera : Component
{
    public Entity Target;
    public int Updates;
    public int FixedUpdates;
    public int LateUpdates;
    public int Systems;
    public float ObservedX;
    public float FrameDelta;
    public float Elapsed;

    public override void Update(float delta) => ++Updates;
    public override void FixedUpdate(float delta) => ++FixedUpdates;

    public override void LateUpdate(float delta)
    {
        ++LateUpdates;
        FrameDelta = Time.Delta;
        Elapsed = (float)Time.Elapsed;
        Vec3 target = Target.WorldPosition;
        ObservedX = target.X;
        Matrix4x4 parent = Entity.Parent.IsAlive ? Entity.Parent.WorldMatrix : Matrix4x4.Identity;
        if (Matrix4x4.Invert(parent, out Matrix4x4 inverse))
        {
            Vector3 local = Vector3.Transform(new Vector3(target.X, target.Y + 1.5f, 10.0f), inverse);
            Transform.Position = new Vec3(local.X, local.Y, local.Z);
        }
    }

    [GameSystem(SystemPhase.LateUpdate)]
    public static void AfterCameras(Scene scene)
    {
        foreach (LateCamera camera in scene.Components<LateCamera>())
        {
            ++camera.Systems;
        }
    }
}
