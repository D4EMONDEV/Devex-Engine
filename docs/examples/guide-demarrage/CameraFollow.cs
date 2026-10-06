using Devex;

// À ajouter à une Camera à la racine de la scène, sans parent.
public class CameraFollow : Component
{
    public Entity Target;
    public Vec3 Offset = new(0.0f, 2.0f, 10.0f);

    public override void LateUpdate(float delta)
    {
        if (Target.IsAlive && Target.HasTransform)
            Transform.Position = Target.WorldPosition + Offset;
    }
}
