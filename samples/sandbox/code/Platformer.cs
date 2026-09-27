using Devex;

// The 2D scene of the sandbox: a knight runs and jumps through a level of tiles, picks up coins,
// pushes crates and rides a platform, in front of a sunset that scrolls slower than the ground.
// Everything is a sprite or a tile seen through an orthographic camera, and moves with 2D physics:
// the tiles collide as their tileset says, the hero is a CharacterController2D, the coins are
// triggers.

// A coin the hero picks up by touching it: a trigger.
public class Coin2D : Component
{
}

// The hero: Move runs, Jump jumps. Its CharacterController2D moves it against the tiles, the crates
// and the platform, through ledges from below and onto them from above; water brings it back to
// where it started. It faces where it runs and plays idle, run, jump and fall on its SpriteAnimator.
public class Hero2D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 14.0f;
    // The tilemap it walks on, whose water sends it back.
    public Entity Level;
    // Moved where a coin is picked up, and burst.
    public Entity Sparkles;
    [AssetType("audio")]
    public AssetId PickupSound;
    // Tells how many coins are left.
    public Entity Counter;

    private int _collected;
    private Vec3 _start;

    public override void Start()
    {
        _start = Transform.Position;
    }

    public override void Update(float delta)
    {
        CharacterController2D controller = Entity.Get<CharacterController2D>();
        float run = Input.ActionVector("Move").X;
        Vec2 velocity = controller.Velocity;
        velocity.X = run * Speed;
        if (controller.Grounded && Input.WasActionPressed("Jump"))
        {
            velocity.Y = JumpSpeed;
        }
        controller.Velocity = velocity;

        // In the water, or fallen off the level, back to the start.
        Vec3 position = Transform.Position;
        (int cellX, int cellY) = Tilemaps.CellAt(Level, position + new Vec3(0.0f, 0.4f, 0.0f));
        if (Tilemaps.GetData(Level, cellX, cellY) == "water" || position.Y < -12.0f)
        {
            Transform.Position = _start;
            controller.Velocity = default;
        }

        SpriteAnimator animator = Entity.Get<SpriteAnimator>();
        if (!controller.Grounded)
        {
            animator.Play(controller.Velocity.Y > 0.0f ? "jump" : "fall");
        }
        else
        {
            animator.Play(MathF.Abs(run) > 0.1f ? "run" : "idle");
        }
        if (MathF.Abs(run) > 0.1f)
        {
            Entity.Get<SpriteRenderer>().FlipX = run < 0.0f;
        }
    }

    public override void OnTriggerEnter(Entity other)
    {
        if (other.IsAlive && other.Get<Coin2D>() is Coin2D coin)
        {
            Collect(coin);
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

// Goes back and forth along X, easing at both ends: the Transform of a kinematic RigidBody2D, which
// carries what stands on it.
public class Shuttle2D : Component
{
    public float Distance = 2.0f;
    // Seconds for a return trip.
    public float Period = 4.0f;

    private Vec3 _start;
    private float _time;

    public override void Start()
    {
        _start = Transform.Position;
    }

    public override void FixedUpdate(float delta)
    {
        _time += delta;
        float offset = (1.0f - MathF.Cos(_time * 2.0f * MathF.PI / Period)) * 0.5f * Distance;
        Transform.Position = _start + new Vec3(offset, 0.0f, 0.0f);
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
