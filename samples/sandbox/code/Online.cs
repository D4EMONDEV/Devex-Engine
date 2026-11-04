using Devex;

// The multiplayer example of the sandbox, in the scene arena_online: walk, jump and throw balls at the
// crates with others. Nothing here sends a position: what moves says so with a NetworkSync, what appears
// comes from under a NetworkSpawner, and only throwing a ball is a remote call. See docs/multiplayer.md.
//
// Play in the editor with "2 players" beside it: the games connect by themselves, and the menu never shows.

// Who plays: the host places one player for each person, and takes them away when they leave. The scene
// keeps this component on the NetworkSpawner the players appear under.
public class OnlineGame : Component
{
    public AssetId PlayerPrefab;
    public int Port = 7777;
    public Entity Menu;
    public Entity AddressField;
    public Entity StatusText;
    public Entity PlayersText;
    public Entity OverviewCamera;

    private readonly Dictionary<uint, Entity> _players = [];

    public override void Start()
    {
        Multiplayer.PeerJoined += AddPlayer;
        Multiplayer.PeerLeft += RemovePlayer;
        Multiplayer.ConnectionFailed += () => StatusText.Get<UiText>().Text = "Impossible de rejoindre cette adresse";
        Multiplayer.Disconnected += () => StatusText.Get<UiText>().Text = "La partie est terminée";
        // Connected by Play in the editor, this game hosts from the start.
        if (Multiplayer.IsHost)
        {
            BeginHosting();
        }
    }

    public override void Update(float delta)
    {
        bool offline = Multiplayer.State == MultiplayerState.Offline;
        Menu.Get<UiRect>().Visible = offline;
        if (offline && Ui.WasClicked("host"))
        {
            if (Multiplayer.Host(Port))
            {
                BeginHosting();
            }
            else
            {
                StatusText.Get<UiText>().Text = $"Impossible d'héberger sur le port {Port}";
            }
        }
        else if (offline && Ui.WasClicked("join"))
        {
            string address = AddressField.Get<UiText>().Text;
            StatusText.Get<UiText>().Text = Multiplayer.Join(address.Length != 0 ? address : "127.0.0.1", Port) ? "Connexion..." : "Adresse introuvable";
        }

        // Every game has every player, and what each has thrown follows by its NetworkSync: nobody counts
        // here but its own player. The camera over the arena is for those who do not play.
        string players = "";
        bool playing = false;
        foreach (Entity child in Entity.Children)
        {
            if (Scene.GetComponent<OnlinePlayer>(child) is { } player)
            {
                players += $"Joueur {Multiplayer.AuthorityOf(child)} : {player.Thrown} balles\n";
                playing = playing || player.IsAuthority;
            }
        }
        PlayersText.Get<UiText>().Text = players;
        OverviewCamera.Get<Camera>().Primary = !playing;
    }

    private void BeginHosting()
    {
        AddPlayer(Multiplayer.HostPeer);
        foreach (uint peer in Multiplayer.Peers)
        {
            AddPlayer(peer);
        }
    }

    // Under this entity, as for any other game: only the authority says it is theirs.
    private void AddPlayer(uint peer)
    {
        if (!Multiplayer.IsHost || _players.ContainsKey(peer))
        {
            return;
        }
        // Around the middle of the arena, looking at it, each further round than the one before.
        float angle = (peer - 1) * 2.4f;
        var place = new Vec3(MathF.Sin(angle) * 3.0f, 0.0f, 9.0f + MathF.Cos(angle) * 3.0f);
        Entity player = Prefabs.Instantiate(PlayerPrefab, place, Entity);
        player.Transform.Rotation = Quat.AngleAxis(angle, Vec3.Up);
        Multiplayer.SetAuthority(player, peer);
        _players[peer] = player;
    }

    private void RemovePlayer(uint peer)
    {
        if (Multiplayer.IsHost && _players.Remove(peer, out Entity player) && player.IsAlive)
        {
            player.Destroy();
        }
    }
}

// A player: its own moves it at once, and the others only see it through its NetworkSync, which lists its
// Transform and Thrown. Every game has a copy, and only the one that has authority runs what follows.
public class OnlinePlayer : Component
{
    public float WalkSpeed = 4.5f;
    public float RunSpeed = 8.0f;
    public float JumpSpeed = 5.5f;
    public float Sensitivity = 0.00175f;
    public float ThrowSpeed = 16.0f;
    public AssetId BallPrefab;
    public List<AssetId> Colors = [];
    public int Thrown;

    private float _yaw;
    private float _pitch;
    private Entity _eyes;

    public override void Start()
    {
        foreach (Entity child in Entity.Children)
        {
            if (child.Name == "Eyes")
            {
                _eyes = child;
            }
            else if (child.Name == "Body" && Colors.Count != 0)
            {
                // The same color for the same player on every game, without sending anything.
                child.Get<MeshRenderer>().Material = Colors[(int)(Multiplayer.AuthorityOf(Entity) % Colors.Count)];
            }
        }
        Quat rotation = Transform.Rotation;
        _yaw = 2.0f * MathF.Atan2(rotation.Y, rotation.W);
        if (IsAuthority)
        {
            _eyes.Get<Camera>().Primary = true;
        }
    }

    public override void Update(float delta)
    {
        if (!IsAuthority)
        {
            return;
        }
        if (Input.WasMouseButtonPressed(MouseButton.Left) && !Input.MouseCaptured)
        {
            Input.MouseCaptured = true;
        }
        else if (Input.WasMouseButtonPressed(MouseButton.Left))
        {
            ThrowBall();
        }
        else if (Input.WasKeyPressed(Key.Escape))
        {
            Input.MouseCaptured = false;
        }

        Vec2 look = Input.MouseCaptured ? Input.MouseDelta * Sensitivity : default;
        _yaw -= look.X;
        _pitch = Math.Clamp(_pitch - look.Y, -1.48f, 1.48f);
        Transform.Rotation = Quat.AngleAxis(_yaw, Vec3.Up);
        _eyes.Transform.Rotation = Quat.AngleAxis(_pitch, Vec3.Right);

        CharacterController controller = Entity.Get<CharacterController>();
        Vec2 move = Input.ActionVector("Move");
        float speed = Input.IsActionDown("Run") ? RunSpeed : WalkSpeed;
        Vec3 walk = Transform.Rotation * new Vec3(move.X, 0.0f, -move.Y) * speed;
        float rise = controller.Grounded && Input.WasActionPressed("Jump") ? JumpSpeed : controller.Velocity.Y;
        controller.Velocity = new Vec3(walk.X, rise, walk.Z);
    }

    private void ThrowBall()
    {
        Thrown += 1;
        Quat aim = Quat.AngleAxis(_yaw, Vec3.Up) * Quat.AngleAxis(_pitch, Vec3.Right);
        Vec3 direction = aim * Vec3.Forward;
        Vec3 from = Transform.Position + new Vec3(0.0f, 1.6f, 0.0f) + direction * 0.7f;
        // The host simulates the balls, as what appears comes from it: this asks it to throw one.
        RpcId(Multiplayer.HostPeer, nameof(Throw), from, direction);
    }

    // Only the authority of this player may ask, which the host checks: another game asking for this
    // player gets nothing. CallLocal is for the host's own player, whose call stays here.
    [Rpc(CallLocal = true)]
    private void Throw(Vec3 from, Vec3 direction)
    {
        Entity ball = Prefabs.Instantiate(BallPrefab, from, Entity.Find("Online balls"));
        ball.Get<RigidBody>().LinearVelocity = direction * ThrowSpeed;
    }
}

// A ball that goes after a while. The host destroys it, and it goes from every game.
public class OnlineBall : Component
{
    public float Lifetime = 12.0f;

    private float _age;

    public override void Update(float delta)
    {
        _age += delta;
        if (IsAuthority && _age > Lifetime)
        {
            DestroyEntity();
        }
    }
}
