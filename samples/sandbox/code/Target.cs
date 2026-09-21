using Devex;

// A board that counts the balls the player throws at it. Its lamp, another entity, flashes on each hit.
public class Target : Component
{
    // The entity whose PointLight flashes.
    public Entity Lamp;

    // In lumens, at the start of a flash.
    public float FlashIntensity = 6000.0f;

    // Seconds of the flash.
    public float FlashTime = 0.5f;

    // Played once where the target is, at each hit.
    [AssetType("audio")]
    public AssetId HitSound;

    public int Hits;

    private float _flash;

    public override void OnCollisionEnter(Entity other)
    {
        // Only the thrown balls: the C++ code of the game gives them its Ball component.
        if (!other.Has<Ball>())
        {
            return;
        }
        ++Hits;
        _flash = FlashTime;
        if (HitSound.IsValid)
        {
            Audio.PlayOneShot(HitSound, Entity.WorldPosition);
        }
        Log.Info($"{Entity.Name} hit ({Hits})");
    }

    public override void Update(float delta)
    {
        _flash = MathF.Max(0.0f, _flash - delta);
        // The PointLight of the engine, changed in place.
        if (Lamp.IsAlive && Lamp.TryGet(out PointLight light))
        {
            light.Intensity = FlashIntensity * (_flash / FlashTime);
        }
    }
}

// The score: the hits of every target, written when it changes.
public static class Score
{
    private static int _shown;

    [GameSystem(SystemPhase.Start)]
    public static void Reset() => _shown = 0;

    [GameSystem(SystemPhase.Update)]
    public static void Show(Scene scene)
    {
        int score = scene.Components<Target>().Sum(target => target.Hits);
        if (score != _shown)
        {
            _shown = score;
            Log.Info($"Score: {score}");
        }
    }
}
