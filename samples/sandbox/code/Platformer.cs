using Devex;

// The 2D scene of the sandbox: a knight runs and jumps over ledges, picks up coins, in front of a
// sunset that scrolls slower than the ground. Everything is a sprite seen through an orthographic
// camera; the moves are written here rather than simulated.

// A ledge the hero lands on from above: its top runs through the entity, this wide.
public class Platform2D : Component
{
    public float Width = 1.0f;
}

// A coin the hero picks up by touching it.
public class Coin2D : Component
{
}

// The hero: Move runs, Jump jumps, the ledges hold it up. It faces where it runs and plays idle,
// run, jump and fall on its SpriteAnimator.
public class Hero2D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 11.0f;
    public float Gravity = 30.0f;
    // Moved where a coin is picked up, and burst.
    public Entity Sparkles;
    [AssetType("audio")]
    public AssetId PickupSound;
    // Tells how many coins are left.
    public Entity Counter;

    private Vec2 _velocity;
    private bool _grounded;
    private int _collected;

    public override void Update(float delta)
    {
        delta = MathF.Min(delta, 0.05f);
        float run = Input.ActionVector("Move").X;
        _velocity.X = run * Speed;
        if (_grounded && Input.WasActionPressed("Jump"))
        {
            _velocity.Y = JumpSpeed;
        }
        _velocity.Y -= Gravity * delta;

        ref Transform transform = ref Transform;
        Vec3 from = transform.Position;
        Vec3 to = from + new Vec3(_velocity.X, _velocity.Y, 0.0f) * delta;
        _grounded = false;
        // Falling through the top of a ledge stops there.
        if (_velocity.Y <= 0.0f)
        {
            foreach (Platform2D ledge in Scene.Components<Platform2D>())
            {
                Vec3 at = ledge.Entity.WorldPosition;
                if (MathF.Abs(to.X - at.X) <= ledge.Width * 0.5f && from.Y >= at.Y - 0.01f && to.Y <= at.Y)
                {
                    to.Y = at.Y;
                    _velocity.Y = 0.0f;
                    _grounded = true;
                }
            }
        }
        transform.Position = to;

        SpriteAnimator animator = Entity.Get<SpriteAnimator>();
        if (!_grounded)
        {
            animator.Play(_velocity.Y > 0.0f ? "jump" : "fall");
        }
        else
        {
            animator.Play(MathF.Abs(run) > 0.1f ? "run" : "idle");
        }
        if (MathF.Abs(run) > 0.1f)
        {
            Entity.Get<SpriteRenderer>().FlipX = run < 0.0f;
        }

        Vec3 middle = to + new Vec3(0.0f, 0.5f, 0.0f);
        foreach (Coin2D coin in Scene.Components<Coin2D>().ToList())
        {
            Vec3 offset = coin.Entity.WorldPosition - middle;
            if (offset.X * offset.X + offset.Y * offset.Y < 0.5f)
            {
                Collect(coin);
            }
        }
    }

    private void Collect(Coin2D coin)
    {
        Vec3 at = coin.Entity.WorldPosition;
        coin.Entity.Destroy();
        ++_collected;
        if (Sparkles.IsAlive)
        {
            Sparkles.Transform.Position = at;
            Particles.Emit(Sparkles, 24);
        }
        if (PickupSound.IsValid)
        {
            Audio.PlayOneShot(PickupSound, at, 0.6f);
        }
        if (Counter.IsAlive)
        {
            // The coin just picked up is gone from the scene, not yet from the components.
            int left = Scene.Components<Coin2D>().Count(other => other.Entity.IsAlive);
            Counter.Get<UiText>().Text = left > 0 ? $"Coins {_collected}   ({left} left)" : $"All {_collected} coins!";
        }
    }
}

// Keeps the camera on its target, a little ahead of where it runs, between two limits.
public class Follow2D : Component
{
    public Entity Target;
    // How fast the camera catches up, per second.
    public float Smoothing = 4.0f;
    public float MinX = -10.0f;
    public float MaxX = 10.0f;
    // The lowest the camera goes, and how far above its target it looks.
    public float MinY = 1.0f;
    public float Above = 1.5f;

    public override void Update(float delta)
    {
        if (!Target.IsAlive)
        {
            return;
        }
        Vec3 target = Target.WorldPosition;
        ref Transform transform = ref Transform;
        float blend = 1.0f - MathF.Exp(-Smoothing * delta);
        float x = Math.Clamp(target.X, MinX, MaxX);
        float y = MathF.Max(MinY, target.Y + Above);
        transform.Position = new Vec3(transform.Position.X + (x - transform.Position.X) * blend,
                                      transform.Position.Y + (y - transform.Position.Y) * blend,
                                      transform.Position.Z);
    }
}

// Scrolls with the camera, slower than the world: the farther, the smaller the factor.
public class Parallax2D : Component
{
    public Entity Camera;
    // 1 follows the camera, as if infinitely far; 0 stays in the world.
    public float Factor = 0.8f;

    private Vec3 _origin;

    public override void Start()
    {
        _origin = Transform.Position;
    }

    public override void Update(float delta)
    {
        if (Camera.IsAlive)
        {
            Vec3 camera = Camera.WorldPosition;
            Transform.Position = new Vec3(_origin.X + camera.X * Factor, _origin.Y + camera.Y * Factor, _origin.Z);
        }
    }
}
