using Devex;

public enum RpcKind
{
    First = 0,
    Second = 2,
}

/// <summary>Hosts a session, has its remote methods called, and calls others as the test commands.</summary>
public sealed class RpcProbe : Component
{
    public float Hosted;
    public float Command;
    public float Hits;
    public float Said;
    public float SaidBy;
    public float LocalRuns;
    public float LocalSender;
    public float SenderAfter;
    public float Carried;
    public float AuthorityHere;

    private bool _started;

    public override void Update(float delta)
    {
        if (!_started)
        {
            _started = true;
            Hosted = Multiplayer.Host(47396, 4) ? 1 : 0;
        }
        int command = (int)Command;
        Command = 0;
        switch (command)
        {
            case 1:
                Rpc(nameof(Hit), 7);
                break;
            case 2:
                Rpc(nameof(Local));
                SenderAfter = Multiplayer.RemoteSender + 1;
                break;
            case 3:
                Rpc(nameof(Hit), "not a number");
                Rpc(nameof(Hit));
                Rpc(nameof(Hit), 1L << 40);
                Rpc(nameof(Hit), new object());
                Rpc("NotRemote");
                break;
            case 4:
                Rpc(nameof(Everything), true, 12, 1.5f, "été", new Vec2(1, 2), new Vec3(3, 4, 5), new Vec4(6, 7, 8, 9), new Quat(0, 0, 0, 1), Entity,
                    new byte[] { 1, 2, 3 }, RpcKind.Second);
                break;
            case 5:
                Rpc("Native", 3);
                break;
            case 6:
                Multiplayer.SetAuthority(Entity, 5);
                Rpc(nameof(Hit), 1);
                break;
        }
        AuthorityHere = IsAuthority ? 1 : 0;
    }

    [Rpc]
    public void Hit(int damage)
    {
        Hits += damage;
    }

    [Rpc(CallLocal = true)]
    public void Local()
    {
        ++LocalRuns;
        LocalSender = Multiplayer.RemoteSender;
    }

    [Rpc(AnyPeer = true)]
    private void Say(string text)
    {
        Said = text.Length;
        SaidBy = Multiplayer.RemoteSender;
    }

    [Rpc(AnyPeer = true)]
    public void Everything(bool flag, int whole, float number, string text, Vec2 two, Vec3 three, Vec4 four, Quat turn, Entity who, byte[] raw, RpcKind kind)
    {
        bool right = flag && whole == 12 && number == 1.5f && text == "été" && two.Y == 2 && three.Z == 5 && four.W == 9 && turn.W == 1 && who == Entity &&
                     raw is [1, 2, 3] && kind == RpcKind.Second;
        Carried = right ? 1 : -1;
    }
}
