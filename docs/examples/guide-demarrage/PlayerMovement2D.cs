using System;
using Devex;

// À ajouter à Player, avec un CharacterController2D.
public class PlayerMovement2D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 7.0f;
    public Entity Visual;
    public bool Animate = false;

    public override void Update(float delta)
    {
        var controller = Entity.Get<CharacterController2D>();
        float horizontal = Input.ActionVector("Move").X;
        Vec2 velocity = controller.Velocity;
        velocity.X = horizontal * Speed;

        if (controller.Grounded && Input.WasActionPressed("Jump"))
            velocity.Y = JumpSpeed;

        // La simulation applique déjà la gravité et le pas de temps.
        controller.Velocity = velocity;

        if (!Visual.IsAlive)
            return;

        if (Visual.TryGet<SpriteRenderer>(out var sprite) && MathF.Abs(horizontal) > 0.01f)
            sprite.FlipX = horizontal < 0.0f; // Image d'origine tournée à droite.

        if (Animate && Visual.TryGet<SpriteAnimator>(out var animator))
        {
            string animation = !controller.Grounded || velocity.Y > 0.1f
                ? "jump"
                : MathF.Abs(horizontal) > 0.01f ? "run" : "idle";
            animator.Play(animation);
        }
    }
}
