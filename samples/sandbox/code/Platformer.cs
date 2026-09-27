using Devex;

// The 2D scene of the sandbox: a knight runs and jumps through a level of tiles, picks up coins,
// in front of a sunset that scrolls slower than the ground. Everything is a sprite or a tile seen
// through an orthographic camera; the moves are written here against the collisions the tileset
// gives its tiles, rather than simulated.

// A coin the hero picks up by touching it.
public class Coin2D : Component
{
}

// The hero: Move runs, Jump jumps. Full tiles stop it on every side, Top tiles hold it up when it
// lands on them from above; water brings it back to where it started. It faces where it runs and
// plays idle, run, jump and fall on its SpriteAnimator.
public class Hero2D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 11.0f;
    public float Gravity = 30.0f;
    // The tilemap it walks on.
    public Entity Level;
    // Moved where a coin is picked up, and burst.
    public Entity Sparkles;
    [AssetType("audio")]
    public AssetId PickupSound;
    // Tells how many coins are left.
    public Entity Counter;

    // The box of the hero around its feet, in meters. The cells of the level are a meter wide, from
    // the origin, which the moves below round to.
    private const float HalfWidth = 0.3f;
    private const float Height = 0.85f;

    private Vec2 _velocity;
    private bool _grounded;
    private int _collected;
    private Vec3 _start;

    public override void Start()
    {
        _start = Transform.Position;
    }

    private TileCollision CollisionAt(float x, float y)
    {
        (int cellX, int cellY) = Tilemaps.CellAt(Level, new Vec3(x, y, 0.0f));
        return Tilemaps.GetCollision(Level, cellX, cellY);
    }

    // Whether a full tile stands anywhere along a vertical or horizontal stretch of the box.
    private bool FullAlongY(float x, float bottom, float top)
    {
        for (float y = bottom; ; y = MathF.Min(y + 0.5f, top))
        {
            if (CollisionAt(x, y) == TileCollision.Full)
            {
                return true;
            }
            if (y >= top)
            {
                return false;
            }
        }
    }

    private bool HoldsAlongX(float left, float right, float y, bool top)
    {
        for (float x = left; ; x = MathF.Min(x + 0.5f, right))
        {
            TileCollision collision = CollisionAt(x, y);
            if (collision == TileCollision.Full || (top && collision == TileCollision.Top))
            {
                return true;
            }
            if (x >= right)
            {
                return false;
            }
        }
    }

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
        Vec3 position = transform.Position;
        if (Level.IsAlive)
        {
            // Across first: a full tile at the side stops the hero against it.
            float x = position.X + _velocity.X * delta;
            if (_velocity.X != 0.0f)
            {
                float side = x + MathF.Sign(_velocity.X) * HalfWidth;
                if (FullAlongY(side, position.Y + 0.05f, position.Y + Height - 0.05f))
                {
                    x = _velocity.X > 0.0f ? MathF.Floor(side) - HalfWidth - 0.001f : MathF.Floor(side) + 1.0f + HalfWidth + 0.001f;
                }
            }
            // Then up or down: the feet land on the first top of a tile they reach from above this
            // frame, and pass through ledges from below; the head bumps into full tiles.
            float y = position.Y + _velocity.Y * delta;
            _grounded = false;
            if (_velocity.Y <= 0.0f)
            {
                for (float surface = MathF.Floor(position.Y + 0.001f); surface >= y; surface -= 1.0f)
                {
                    if (HoldsAlongX(x - HalfWidth + 0.02f, x + HalfWidth - 0.02f, surface - 0.5f, true))
                    {
                        y = surface;
                        _velocity.Y = 0.0f;
                        _grounded = true;
                        break;
                    }
                }
            }
            else if (HoldsAlongX(x - HalfWidth + 0.02f, x + HalfWidth - 0.02f, y + Height, false))
            {
                y = MathF.Floor(y + Height) - Height - 0.001f;
                _velocity.Y = 0.0f;
            }
            position = new Vec3(x, y, position.Z);
            // In the water, or fallen off the level, back to the start.
            (int cellX, int cellY) = Tilemaps.CellAt(Level, position + new Vec3(0.0f, 0.4f, 0.0f));
            if (Tilemaps.GetData(Level, cellX, cellY) == "water" || y < -12.0f)
            {
                position = _start;
                _velocity = default;
            }
        }
        transform.Position = position;

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

        Vec3 middle = position + new Vec3(0.0f, 0.5f, 0.0f);
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
