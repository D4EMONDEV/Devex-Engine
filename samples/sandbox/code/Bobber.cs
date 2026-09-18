using Devex;

// A component written in C#, next to the C++ gameplay of the sandbox: it moves its entity up and
// down around where it started, and spins it. Its fields are edited in the inspector and saved in
// the scene, like those of a C++ component.
public class Bobber : Component
{
    // How far the entity goes above and below its starting height, in meters.
    public float Height = 0.5f;

    // Seconds for a full up and down.
    public float Period = 2.0f;

    // Turn speed around the vertical axis.
    [Angle]
    public float Spin = 1.0f;

    private Vec3 _origin;
    private float _time;

    public override void Start()
    {
        _origin = Transform.Position;
        Log.Info($"Bobber starts on {Scene.Name(Entity)}");
    }

    public override void Update(float delta)
    {
        _time += delta;
        float phase = Period > 0.01f ? _time / Period * MathF.Tau : 0.0f;
        ref Transform transform = ref Transform;
        transform.Position = _origin + Vec3.Up * (MathF.Sin(phase) * Height);
        transform.Rotation = Quat.AngleAxis(Spin * delta, Vec3.Up) * transform.Rotation;
    }
}

// A system, the other way to write gameplay in C#: a static method the engine runs over the whole
// scene at one phase, here once when the game starts.
public static class BobberReport
{
    [GameSystem(SystemPhase.Start)]
    public static void Report(Scene scene)
    {
        Log.Info($"C# system: {scene.Components<Bobber>().Count()} bobbing entities in the scene");
    }
}
