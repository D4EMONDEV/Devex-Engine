using Devex;

// A component of the tests: it moves its entity and records that it started.
public class Mover : Component
{
    // In meters per second.
    public float Speed = 2.0f;

    public Vec3 Direction = new(1.0f, 0.0f, 0.0f);

    public string Label = "new";

    [Angle]
    public float Turn = 0.0f;

    public override void Start()
    {
        Label = "started";
    }

    public override void Update(float delta)
    {
        Transform.Position += Direction * (Speed * delta);
        Label = $"moved {Scene.Name(Entity)}";
    }
}

// A second component, to check that every type of an assembly is registered.
public class Counter : Component
{
    public int Count;

    public override void FixedUpdate(float delta)
    {
        ++Count;
    }
}

// A system: it runs once per phase, over the whole scene, after the components.
public static class Renamer
{
    [GameSystem(SystemPhase.Update)]
    public static void Rename(Scene scene)
    {
        foreach (Mover mover in scene.Components<Mover>())
        {
            scene.SetName(mover.Entity, "renamed by the system");
        }
    }
}
