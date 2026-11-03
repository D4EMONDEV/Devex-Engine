using Devex;

namespace DevexSceneTools;

public static class SceneTools
{
    [EditorCommand("devex.scene-tools", "Create Spinning Cube")]
    public static void CreateCube(EditorContext context)
    {
        Entity cube = context.CreateEntity("Addon Cube");
        cube.Add<MeshRenderer>().Mesh = new AssetId(Uuid.Parse("00000000-0000-0000-0000-000000000001")!.Value);
        cube.Add<AddonSpinner>();
    }

    [EditorCommand("devex.scene-tools", "Center Selection")]
    public static void Center(EditorContext context)
    {
        foreach (Entity entity in context.Selection)
            if (entity.HasTransform)
                entity.Transform.Position = Vec3.Zero;
    }
}

public sealed class AddonSpinner : Component
{
    [Angle]
    public float Speed = 1;

    public override void Update(float delta)
    {
        Transform.Rotation = Quat.AngleAxis(Speed * delta, Vec3.Up) * Transform.Rotation;
    }
}
