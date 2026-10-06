using Devex;

// À ajouter à Player, avec RigidBody (Dynamic) et CapsuleCollider.
// Le Transform est aux pieds ; la capsule mesure 1.8 et son Center.Y vaut 0.9.
public class PlayerMovement3D : Component
{
    public float Speed = 5.0f;
    public float JumpSpeed = 6.0f;

    public override void Update(float delta)
    {
        var body = Entity.Get<RigidBody>();
        Vec2 move = Input.ActionVector("Move");
        Vec3 velocity = body.LinearVelocity;
        velocity.X = move.X * Speed;
        velocity.Z = -move.Y * Speed; // Avancer = -Z, avec la caméra derrière le joueur.

        Vec3 foot = Entity.WorldPosition;
        bool grounded = velocity.Y <= 0.1f
            && Physics.Raycast(foot + new Vec3(0.0f, 0.1f, 0.0f),
                               new Vec3(0.0f, -1.0f, 0.0f), 0.2f,
                               out RayHit hit, ignore: Entity)
            && hit.Normal.Y > 0.6f;

        if (grounded && Input.WasActionPressed("Jump"))
            velocity.Y = JumpSpeed;

        body.LinearVelocity = velocity;
    }
}
